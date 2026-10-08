// Copyright 2026 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "common/typedef/proto_to_typedef.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "google/protobuf/descriptor.pb.h"
#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/types/span.h"
#include "common/ast.h"
#include "common/constant.h"
#include "common/minimal_descriptor_database.h"
#include "common/typedef/proto_schema.h"
#include "common/typedef/schema.h"
#include "common/typedef/typedef.h"
#include "internal/status_macros.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor_database.h"
#include "google/protobuf/message.h"

namespace cel {
namespace {

class ErrorCollector final : public google::protobuf::DescriptorPool::ErrorCollector {
 public:
  void RecordError(absl::string_view filename, absl::string_view element_name,
                   const google::protobuf::Message* /*descriptor*/,
                   ErrorLocation /*location*/,
                   absl::string_view message) override {
    if (!errors_.empty()) {
      absl::StrAppend(&errors_, "; ");
    }
    absl::StrAppend(&errors_, filename, ": ", element_name, ": ", message);
  }

  absl::string_view errors() const { return errors_; }

 private:
  std::string errors_;
};

absl::StatusOr<absl::string_view> GetSchemaName(
    const google::protobuf::FileDescriptorProto& file_proto) {
  if (file_proto.syntax() == "proto3") {
    return kProto3SchemaName;
  }
  if (file_proto.syntax().empty() || file_proto.syntax() == "proto2") {
    return kProto2SchemaName;
  }
  return absl::InvalidArgumentError(
      absl::StrCat("Unsupported proto syntax '", file_proto.syntax(),
                   "' in file '", file_proto.name(), "'."));
}

bool IsWellKnownMessageType(const google::protobuf::Descriptor& desc) {
  switch (desc.well_known_type()) {
    case google::protobuf::Descriptor::WELLKNOWNTYPE_BOOLVALUE:
    case google::protobuf::Descriptor::WELLKNOWNTYPE_INT32VALUE:
    case google::protobuf::Descriptor::WELLKNOWNTYPE_INT64VALUE:
    case google::protobuf::Descriptor::WELLKNOWNTYPE_UINT32VALUE:
    case google::protobuf::Descriptor::WELLKNOWNTYPE_UINT64VALUE:
    case google::protobuf::Descriptor::WELLKNOWNTYPE_FLOATVALUE:
    case google::protobuf::Descriptor::WELLKNOWNTYPE_DOUBLEVALUE:
    case google::protobuf::Descriptor::WELLKNOWNTYPE_BYTESVALUE:
    case google::protobuf::Descriptor::WELLKNOWNTYPE_STRINGVALUE:
    case google::protobuf::Descriptor::WELLKNOWNTYPE_ANY:
    case google::protobuf::Descriptor::WELLKNOWNTYPE_DURATION:
    case google::protobuf::Descriptor::WELLKNOWNTYPE_TIMESTAMP:
    case google::protobuf::Descriptor::WELLKNOWNTYPE_VALUE:
    case google::protobuf::Descriptor::WELLKNOWNTYPE_LISTVALUE:
    case google::protobuf::Descriptor::WELLKNOWNTYPE_STRUCT:
      return true;
    default:
      return false;
  }
}

bool IsWellKnownEnumType(const google::protobuf::EnumDescriptor& enum_desc) {
  return enum_desc.full_name() == "google.protobuf.NullValue";
}

TypeSpec MessageDescriptorToTypeSpec(const google::protobuf::Descriptor& desc) {
  switch (desc.well_known_type()) {
    case google::protobuf::Descriptor::WELLKNOWNTYPE_BOOLVALUE:
      return TypeSpec(PrimitiveTypeWrapper(PrimitiveType::kBool));
    case google::protobuf::Descriptor::WELLKNOWNTYPE_INT32VALUE:
    case google::protobuf::Descriptor::WELLKNOWNTYPE_INT64VALUE:
      return TypeSpec(PrimitiveTypeWrapper(PrimitiveType::kInt64));
    case google::protobuf::Descriptor::WELLKNOWNTYPE_UINT32VALUE:
    case google::protobuf::Descriptor::WELLKNOWNTYPE_UINT64VALUE:
      return TypeSpec(PrimitiveTypeWrapper(PrimitiveType::kUint64));
    case google::protobuf::Descriptor::WELLKNOWNTYPE_FLOATVALUE:
    case google::protobuf::Descriptor::WELLKNOWNTYPE_DOUBLEVALUE:
      return TypeSpec(PrimitiveTypeWrapper(PrimitiveType::kDouble));
    case google::protobuf::Descriptor::WELLKNOWNTYPE_BYTESVALUE:
      return TypeSpec(PrimitiveTypeWrapper(PrimitiveType::kBytes));
    case google::protobuf::Descriptor::WELLKNOWNTYPE_STRINGVALUE:
      return TypeSpec(PrimitiveTypeWrapper(PrimitiveType::kString));
    case google::protobuf::Descriptor::WELLKNOWNTYPE_ANY:
      return TypeSpec(WellKnownTypeSpec::kAny);
    case google::protobuf::Descriptor::WELLKNOWNTYPE_DURATION:
      return TypeSpec(WellKnownTypeSpec::kDuration);
    case google::protobuf::Descriptor::WELLKNOWNTYPE_TIMESTAMP:
      return TypeSpec(WellKnownTypeSpec::kTimestamp);
    case google::protobuf::Descriptor::WELLKNOWNTYPE_VALUE:
      return TypeSpec(DynTypeSpec());
    case google::protobuf::Descriptor::WELLKNOWNTYPE_LISTVALUE:
      return TypeSpec(ListTypeSpec(std::make_unique<TypeSpec>(DynTypeSpec())));
    case google::protobuf::Descriptor::WELLKNOWNTYPE_STRUCT:
      return TypeSpec(
          MapTypeSpec(std::make_unique<TypeSpec>(PrimitiveType::kString),
                      std::make_unique<TypeSpec>(DynTypeSpec())));
    default:
      return TypeSpec(MessageTypeSpec(std::string(desc.full_name())));
  }
}

TypeSpec EnumDescriptorToTypeSpec(const google::protobuf::EnumDescriptor& enum_desc) {
  if (IsWellKnownEnumType(enum_desc)) {
    return TypeSpec(NullTypeSpec());
  }
  return TypeSpec(MessageTypeSpec(std::string(enum_desc.full_name())));
}

absl::StatusOr<TypeSpec> SingularFieldDescriptorToTypeSpec(
    const google::protobuf::FieldDescriptor& field) {
  switch (field.type()) {
    case google::protobuf::FieldDescriptor::TYPE_BOOL:
      return TypeSpec(PrimitiveType::kBool);
    case google::protobuf::FieldDescriptor::TYPE_INT32:
    case google::protobuf::FieldDescriptor::TYPE_SINT32:
    case google::protobuf::FieldDescriptor::TYPE_SFIXED32:
    case google::protobuf::FieldDescriptor::TYPE_INT64:
    case google::protobuf::FieldDescriptor::TYPE_SINT64:
    case google::protobuf::FieldDescriptor::TYPE_SFIXED64:
      return TypeSpec(PrimitiveType::kInt64);
    case google::protobuf::FieldDescriptor::TYPE_UINT32:
    case google::protobuf::FieldDescriptor::TYPE_FIXED32:
    case google::protobuf::FieldDescriptor::TYPE_UINT64:
    case google::protobuf::FieldDescriptor::TYPE_FIXED64:
      return TypeSpec(PrimitiveType::kUint64);
    case google::protobuf::FieldDescriptor::TYPE_FLOAT:
    case google::protobuf::FieldDescriptor::TYPE_DOUBLE:
      return TypeSpec(PrimitiveType::kDouble);
    case google::protobuf::FieldDescriptor::TYPE_STRING:
      return TypeSpec(PrimitiveType::kString);
    case google::protobuf::FieldDescriptor::TYPE_BYTES:
      return TypeSpec(PrimitiveType::kBytes);
    case google::protobuf::FieldDescriptor::TYPE_ENUM:
      return EnumDescriptorToTypeSpec(*field.enum_type());
    case google::protobuf::FieldDescriptor::TYPE_MESSAGE:
    case google::protobuf::FieldDescriptor::TYPE_GROUP:
      return MessageDescriptorToTypeSpec(*field.message_type());
  }
  return absl::InvalidArgumentError(
      absl::StrCat("Unsupported proto field type: ", field.type_name(),
                   " on field '", field.full_name(), "'."));
}

absl::StatusOr<TypeSpec> FieldDescriptorToTypeSpec(
    const google::protobuf::FieldDescriptor& field) {
  if (field.is_map()) {
    const google::protobuf::Descriptor* entry = field.message_type();
    const google::protobuf::FieldDescriptor* key_field = entry->map_key();
    const google::protobuf::FieldDescriptor* value_field = entry->map_value();
    if (key_field == nullptr || value_field == nullptr) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Invalid map entry for field '", field.full_name(), "'."));
    }
    CEL_ASSIGN_OR_RETURN(TypeSpec key_type,
                         SingularFieldDescriptorToTypeSpec(*key_field));
    CEL_ASSIGN_OR_RETURN(TypeSpec value_type,
                         SingularFieldDescriptorToTypeSpec(*value_field));
    return TypeSpec(
        MapTypeSpec(std::make_unique<TypeSpec>(std::move(key_type)),
                    std::make_unique<TypeSpec>(std::move(value_type))));
  }
  CEL_ASSIGN_OR_RETURN(TypeSpec elem_type,
                       SingularFieldDescriptorToTypeSpec(field));
  if (field.is_repeated()) {
    return TypeSpec(
        ListTypeSpec(std::make_unique<TypeSpec>(std::move(elem_type))));
  }
  return elem_type;
}

std::string GetSingularProtoTypeName(const google::protobuf::FieldDescriptor& field) {
  switch (field.type()) {
    case google::protobuf::FieldDescriptor::TYPE_BOOL:
      return "bool";
    case google::protobuf::FieldDescriptor::TYPE_INT32:
      return "int32";
    case google::protobuf::FieldDescriptor::TYPE_INT64:
      return "int64";
    case google::protobuf::FieldDescriptor::TYPE_UINT32:
      return "uint32";
    case google::protobuf::FieldDescriptor::TYPE_UINT64:
      return "uint64";
    case google::protobuf::FieldDescriptor::TYPE_SINT32:
      return "sint32";
    case google::protobuf::FieldDescriptor::TYPE_SINT64:
      return "sint64";
    case google::protobuf::FieldDescriptor::TYPE_FIXED32:
      return "fixed32";
    case google::protobuf::FieldDescriptor::TYPE_FIXED64:
      return "fixed64";
    case google::protobuf::FieldDescriptor::TYPE_SFIXED32:
      return "sfixed32";
    case google::protobuf::FieldDescriptor::TYPE_SFIXED64:
      return "sfixed64";
    case google::protobuf::FieldDescriptor::TYPE_FLOAT:
      return "float";
    case google::protobuf::FieldDescriptor::TYPE_DOUBLE:
      return "double";
    case google::protobuf::FieldDescriptor::TYPE_STRING:
      return "string";
    case google::protobuf::FieldDescriptor::TYPE_BYTES:
      return "bytes";
    case google::protobuf::FieldDescriptor::TYPE_ENUM:
      return std::string(field.enum_type()->full_name());
    case google::protobuf::FieldDescriptor::TYPE_GROUP:
      return "group";
    case google::protobuf::FieldDescriptor::TYPE_MESSAGE:
      return std::string(field.message_type()->full_name());
  }
  return "";
}

absl::StatusOr<std::string> DefaultSingularProtoTypeName(
    const TypeSpec& type_spec) {
  if (type_spec.has_primitive()) {
    switch (type_spec.primitive()) {
      case PrimitiveType::kBool:
        return "bool";
      case PrimitiveType::kInt64:
        return "int64";
      case PrimitiveType::kUint64:
        return "uint64";
      case PrimitiveType::kDouble:
        return "double";
      case PrimitiveType::kString:
        return "string";
      case PrimitiveType::kBytes:
        return "bytes";
      case PrimitiveType::kPrimitiveTypeUnspecified:
        break;
    }
    return absl::InvalidArgumentError("Unspecified primitive type.");
  }
  if (type_spec.has_wrapper()) {
    switch (type_spec.wrapper()) {
      case PrimitiveType::kBool:
        return "google.protobuf.BoolValue";
      case PrimitiveType::kInt64:
        return "google.protobuf.Int64Value";
      case PrimitiveType::kUint64:
        return "google.protobuf.UInt64Value";
      case PrimitiveType::kDouble:
        return "google.protobuf.DoubleValue";
      case PrimitiveType::kString:
        return "google.protobuf.StringValue";
      case PrimitiveType::kBytes:
        return "google.protobuf.BytesValue";
      case PrimitiveType::kPrimitiveTypeUnspecified:
        break;
    }
    return absl::InvalidArgumentError("Unspecified primitive wrapper type.");
  }
  if (type_spec.has_well_known()) {
    switch (type_spec.well_known()) {
      case WellKnownTypeSpec::kAny:
        return "google.protobuf.Any";
      case WellKnownTypeSpec::kDuration:
        return "google.protobuf.Duration";
      case WellKnownTypeSpec::kTimestamp:
        return "google.protobuf.Timestamp";
      case WellKnownTypeSpec::kWellKnownTypeUnspecified:
        break;
    }
    return absl::InvalidArgumentError("Unspecified well-known type.");
  }
  if (type_spec.has_dyn()) {
    return "google.protobuf.Value";
  }
  if (type_spec.has_null()) {
    return "google.protobuf.NullValue";
  }
  if (type_spec.has_message_type()) {
    return type_spec.message_type().type();
  }
  if (type_spec.has_abstract_type() &&
      type_spec.abstract_type().parameter_types().empty()) {
    return type_spec.abstract_type().name();
  }
  if (type_spec.has_list_type()) {
    if (type_spec.list_type().elem_type().has_dyn()) {
      return "google.protobuf.ListValue";
    }
    return absl::InvalidArgumentError(
        "Nested list type other than list<dyn> is not representable as a "
        "singular protobuf field.");
  }
  if (type_spec.has_map_type()) {
    if (type_spec.map_type().key_type().has_primitive() &&
        type_spec.map_type().key_type().primitive() == PrimitiveType::kString &&
        type_spec.map_type().value_type().has_dyn()) {
      return "google.protobuf.Struct";
    }
    return absl::InvalidArgumentError(
        "Nested map type other than map<string, dyn> is not representable as a "
        "singular protobuf field.");
  }
  return absl::InvalidArgumentError(
      "Unsupported TypeSpec for protobuf field conversion.");
}

absl::StatusOr<std::optional<std::string>> ExtractNonDefaultProtoFieldType(
    const google::protobuf::FieldDescriptor& field, const TypeSpec& cel_type) {
  if (field.is_map()) {
    const google::protobuf::Descriptor* entry = field.message_type();
    const google::protobuf::FieldDescriptor* key_field = entry->map_key();
    const google::protobuf::FieldDescriptor* value_field = entry->map_value();
    std::string actual_key = GetSingularProtoTypeName(*key_field);
    std::string actual_val = GetSingularProtoTypeName(*value_field);
    CEL_ASSIGN_OR_RETURN(
        std::string default_key,
        DefaultSingularProtoTypeName(cel_type.map_type().key_type()));
    CEL_ASSIGN_OR_RETURN(
        std::string default_val,
        DefaultSingularProtoTypeName(cel_type.map_type().value_type()));
    if (actual_key != default_key || actual_val != default_val) {
      return absl::StrCat("map<", actual_key, ", ", actual_val, ">");
    }
    return std::nullopt;
  }
  if (field.is_repeated()) {
    std::string actual_elem = GetSingularProtoTypeName(field);
    CEL_ASSIGN_OR_RETURN(
        std::string default_elem,
        DefaultSingularProtoTypeName(cel_type.list_type().elem_type()));
    if (actual_elem != default_elem) {
      return actual_elem;
    }
    return std::nullopt;
  }
  // Singular field: top-level list<dyn> defaults to repeated Value, and
  // top-level map<string, dyn> defaults to map<string, Value>, so singular
  // ListValue and Struct fields must record their explicit message type.
  std::string actual = GetSingularProtoTypeName(field);
  if (cel_type.has_list_type() || cel_type.has_map_type()) {
    return actual;
  }
  CEL_ASSIGN_OR_RETURN(std::string default_singular,
                       DefaultSingularProtoTypeName(cel_type));
  if (actual != default_singular) {
    return actual;
  }
  return std::nullopt;
}

template <typename DescriptorT>
std::string ExtractLeadingComment(const DescriptorT& desc,
                                  const ProtoToTypeDefOptions& options) {
  if (!options.include_comments) {
    return "";
  }
  proto2::SourceLocation location;
  if (!desc.GetSourceLocation(&location)) {
    return "";
  }
  return std::string(absl::StripAsciiWhitespace(location.leading_comments));
}

Constant ExtractFieldDefaultValue(const google::protobuf::FieldDescriptor& field,
                                  const ProtoToTypeDefOptions& options) {
  if (!options.include_defaults || !field.has_default_value() ||
      field.is_repeated()) {
    return Constant();
  }
  switch (field.cpp_type()) {
    case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
      return Constant(field.default_value_bool());
    case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
      return Constant(static_cast<int64_t>(field.default_value_int32()));
    case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
      return Constant(field.default_value_int64());
    case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
      return Constant(static_cast<uint64_t>(field.default_value_uint32()));
    case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
      return Constant(field.default_value_uint64());
    case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
      return Constant(static_cast<double>(field.default_value_float()));
    case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
      return Constant(field.default_value_double());
    case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
      if (field.type() == google::protobuf::FieldDescriptor::TYPE_BYTES) {
        return Constant(BytesConstant(field.default_value_string()));
      }
      return Constant(StringConstant(field.default_value_string()));
    case google::protobuf::FieldDescriptor::CPPTYPE_ENUM:
    case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
      // CEL Constant only represents scalar literals; enum defaults are stored
      // in ProtoFieldProperties::default_value.
      return Constant();
  }
  return Constant();
}

std::string DefaultJsonName(absl::string_view name) {
  std::string result;
  result.reserve(name.size());
  bool next_upper = false;
  for (char c : name) {
    if (c == '_') {
      next_upper = true;
    } else if (next_upper) {
      result.push_back(absl::ascii_toupper(static_cast<unsigned char>(c)));
      next_upper = false;
    } else {
      result.push_back(c);
    }
  }
  return result;
}

absl::Status ConvertEnumDescriptor(const google::protobuf::EnumDescriptor& enum_desc,
                                   absl::string_view schema_name,
                                   const ProtoToTypeDefOptions& options,
                                   TypeDefSet& type_def_set) {
  if (IsWellKnownEnumType(enum_desc)) {
    return absl::OkStatus();
  }
  EnumTypeDef enum_def;
  enum_def.name = std::string(enum_desc.full_name());
  enum_def.doc = ExtractLeadingComment(enum_desc, options);

  if (enum_desc.containing_type() == nullptr ||
      enum_desc.options().has_allow_alias()) {
    auto props = std::make_unique<ProtoEnumProperties>();
    if (enum_desc.containing_type() == nullptr) {
      props->file_name = std::string(enum_desc.file()->name());
    }
    if (enum_desc.options().has_allow_alias()) {
      props->allow_alias = enum_desc.options().allow_alias();
    }
    enum_def.schema_specific_properties[std::string(schema_name)] =
        std::move(props);
  }

  for (int i = 0; i < enum_desc.value_count(); ++i) {
    const google::protobuf::EnumValueDescriptor* val = enum_desc.value(i);
    EnumTypeDef::EnumConstant constant;
    constant.name = std::string(val->name());
    constant.id = val->number();
    constant.doc = ExtractLeadingComment(*val, options);
    CEL_RETURN_IF_ERROR(enum_def.AddConstant(std::move(constant)));
  }

  return type_def_set.AddTypeDef(TypeDef(std::move(enum_def)));
}

absl::Status ConvertMessageDescriptor(const google::protobuf::Descriptor& desc,
                                      absl::string_view schema_name,
                                      const ProtoToTypeDefOptions& options,
                                      TypeDefSet& type_def_set) {
  if (desc.options().map_entry() || IsWellKnownMessageType(desc)) {
    return absl::OkStatus();
  }

  ObjectTypeDef object_def;
  object_def.name = std::string(desc.full_name());
  object_def.doc = ExtractLeadingComment(desc, options);

  if (desc.containing_type() == nullptr || desc.extension_range_count() > 0) {
    auto obj_props = std::make_unique<ProtoObjectProperties>();
    if (desc.containing_type() == nullptr) {
      obj_props->file_name = std::string(desc.file()->name());
    }
    for (int i = 0; i < desc.extension_range_count(); ++i) {
      const google::protobuf::Descriptor::ExtensionRange* range = desc.extension_range(i);
      obj_props->extension_ranges.push_back(ProtoExtensionRange{
          .start = range->start_number(),
          .end = range->end_number(),
      });
    }
    object_def.schema_specific_properties[std::string(schema_name)] =
        std::move(obj_props);
  }

  for (int i = 0; i < desc.field_count(); ++i) {
    const google::protobuf::FieldDescriptor* field = desc.field(i);
    ObjectTypeDef::Field field_def;
    field_def.name = std::string(field->name());
    CEL_ASSIGN_OR_RETURN(field_def.type, FieldDescriptorToTypeSpec(*field));
    field_def.doc = ExtractLeadingComment(*field, options);
    field_def.default_value = ExtractFieldDefaultValue(*field, options);

    auto field_props = std::make_unique<ProtoFieldProperties>();
    field_props->id = field->number();
    if (field->has_json_name() &&
        field->json_name() != DefaultJsonName(field->name())) {
      field_props->json_name = std::string(field->json_name());
    }
    CEL_ASSIGN_OR_RETURN(field_props->type, ExtractNonDefaultProtoFieldType(
                                                *field, field_def.type));
    if (schema_name == kProto2SchemaName && field->is_required()) {
      field_props->label = "required";
    } else if (schema_name == kProto3SchemaName &&
               field->containing_oneof() != nullptr &&
               field->real_containing_oneof() == nullptr) {
      field_props->label = "optional";
    }
    if (field->real_containing_oneof() != nullptr) {
      field_props->oneof = std::string(field->real_containing_oneof()->name());
    }
    if (options.include_defaults && field->has_default_value() &&
        !field->is_repeated() &&
        field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_ENUM) {
      field_props->default_value =
          std::string(field->default_value_enum()->name());
    }
    field_def.schema_specific_properties[std::string(schema_name)] =
        std::move(field_props);

    CEL_RETURN_IF_ERROR(object_def.AddField(std::move(field_def)));
  }

  CEL_RETURN_IF_ERROR(type_def_set.AddTypeDef(TypeDef(std::move(object_def))));

  for (int i = 0; i < desc.enum_type_count(); ++i) {
    CEL_RETURN_IF_ERROR(ConvertEnumDescriptor(*desc.enum_type(i), schema_name,
                                              options, type_def_set));
  }

  for (int i = 0; i < desc.nested_type_count(); ++i) {
    CEL_RETURN_IF_ERROR(ConvertMessageDescriptor(
        *desc.nested_type(i), schema_name, options, type_def_set));
  }

  return absl::OkStatus();
}

// =============================================================================
// TypeDefSetToFileDescriptorSet helpers
// =============================================================================

const ProtoObjectProperties* GetProtoObjectProperties(
    const ObjectTypeDef& obj, absl::string_view schema_name) {
  auto it = obj.schema_specific_properties.find(schema_name);
  if (it == obj.schema_specific_properties.end()) {
    return nullptr;
  }
  return dynamic_cast<const ProtoObjectProperties*>(it->second.get());
}

const ProtoFieldProperties* GetProtoFieldProperties(
    const ObjectTypeDef::Field& field, absl::string_view schema_name) {
  auto it = field.schema_specific_properties.find(schema_name);
  if (it == field.schema_specific_properties.end()) {
    return nullptr;
  }
  return dynamic_cast<const ProtoFieldProperties*>(it->second.get());
}

const ProtoEnumProperties* GetProtoEnumProperties(
    const EnumTypeDef& e, absl::string_view schema_name) {
  auto it = e.schema_specific_properties.find(schema_name);
  if (it == e.schema_specific_properties.end()) {
    return nullptr;
  }
  return dynamic_cast<const ProtoEnumProperties*>(it->second.get());
}

const ProtoEnumConstantProperties* GetProtoEnumConstantProperties(
    const EnumTypeDef::EnumConstant& c, absl::string_view schema_name) {
  auto it = c.schema_specific_properties.find(schema_name);
  if (it == c.schema_specific_properties.end()) {
    return nullptr;
  }
  return dynamic_cast<const ProtoEnumConstantProperties*>(it->second.get());
}

std::string MapEntryName(absl::string_view field_name) {
  std::string result;
  result.reserve(field_name.size() + 5);
  bool cap_next = true;
  for (char c : field_name) {
    if (c == '_') {
      cap_next = true;
    } else if (cap_next) {
      result.push_back(absl::ascii_toupper(static_cast<unsigned char>(c)));
      cap_next = false;
    } else {
      result.push_back(c);
    }
  }
  absl::StrAppend(&result, "Entry");
  return result;
}

template <typename MapT>
absl::Status CheckSchemaMapForProtoSyntax(const MapT& props_map,
                                          bool* has_proto2, bool* has_proto3) {
  if (props_map.contains(kProto2SchemaName)) {
    *has_proto2 = true;
  }
  if (props_map.contains(kProto3SchemaName)) {
    *has_proto3 = true;
  }
  if (*has_proto2 && *has_proto3) {
    return absl::InvalidArgumentError(
        "TypeDef mixes 'proto2' and 'proto3' schema properties.");
  }
  return absl::OkStatus();
}

absl::Status CollectTreeProtoSyntax(
    const TypeDef& td,
    const absl::flat_hash_map<std::string, std::vector<const TypeDef*>>&
        children_map,
    bool* has_proto2, bool* has_proto3) {
  if (td.is_object()) {
    CEL_RETURN_IF_ERROR(CheckSchemaMapForProtoSyntax(
        td.object_type().schema_specific_properties, has_proto2, has_proto3));
    for (const ObjectTypeDef::Field& field : td.object_type().fields) {
      CEL_RETURN_IF_ERROR(CheckSchemaMapForProtoSyntax(
          field.schema_specific_properties, has_proto2, has_proto3));
    }
  } else if (td.is_enum()) {
    CEL_RETURN_IF_ERROR(CheckSchemaMapForProtoSyntax(
        td.enum_type().schema_specific_properties, has_proto2, has_proto3));
    for (const EnumTypeDef::EnumConstant& c : td.enum_type().constants) {
      CEL_RETURN_IF_ERROR(CheckSchemaMapForProtoSyntax(
          c.schema_specific_properties, has_proto2, has_proto3));
    }
  }
  auto it = children_map.find(td.name());
  if (it != children_map.end()) {
    for (const TypeDef* child : it->second) {
      CEL_RETURN_IF_ERROR(
          CollectTreeProtoSyntax(*child, children_map, has_proto2, has_proto3));
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<std::string> DetermineSchemaNameForTree(
    const TypeDef& root_td,
    const absl::flat_hash_map<std::string, std::vector<const TypeDef*>>&
        children_map) {
  bool has_proto2 = false;
  bool has_proto3 = false;
  CEL_RETURN_IF_ERROR(
      CollectTreeProtoSyntax(root_td, children_map, &has_proto2, &has_proto3));
  return has_proto2 ? std::string(kProto2SchemaName)
                    : std::string(kProto3SchemaName);
}

std::optional<std::string> GetWellKnownTypeFileName(
    absl::string_view full_type_name) {
  if (full_type_name == "google.protobuf.Any") {
    return "google/protobuf/any.proto";
  }
  if (full_type_name == "google.protobuf.Duration") {
    return "google/protobuf/duration.proto";
  }
  if (full_type_name == "google.protobuf.Timestamp") {
    return "google/protobuf/timestamp.proto";
  }
  if (full_type_name == "google.protobuf.Struct" ||
      full_type_name == "google.protobuf.Value" ||
      full_type_name == "google.protobuf.ListValue" ||
      full_type_name == "google.protobuf.NullValue") {
    return "google/protobuf/struct.proto";
  }
  if (full_type_name == "google.protobuf.BoolValue" ||
      full_type_name == "google.protobuf.Int32Value" ||
      full_type_name == "google.protobuf.Int64Value" ||
      full_type_name == "google.protobuf.UInt32Value" ||
      full_type_name == "google.protobuf.UInt64Value" ||
      full_type_name == "google.protobuf.FloatValue" ||
      full_type_name == "google.protobuf.DoubleValue" ||
      full_type_name == "google.protobuf.StringValue" ||
      full_type_name == "google.protobuf.BytesValue") {
    return "google/protobuf/wrappers.proto";
  }
  return std::nullopt;
}

void AddCommentLocation(absl::Span<const int> path, absl::string_view doc,
                        const TypeDefToProtoOptions& options,
                        proto2::SourceCodeInfo* source_code_info) {
  if (!options.include_comments || doc.empty()) {
    return;
  }
  proto2::SourceCodeInfo::Location* loc = source_code_info->add_location();
  for (int p : path) {
    loc->add_path(p);
  }
  loc->add_span(0);
  loc->add_span(0);
  loc->add_span(0);
  loc->set_leading_comments(absl::StrCat(" ", doc, "\n"));
}

absl::Status ParseMapProtoType(absl::string_view map_type_str,
                               std::string* key_type, std::string* val_type) {
  absl::string_view inner = map_type_str;
  if (!absl::ConsumePrefix(&inner, "map<") ||
      !absl::ConsumeSuffix(&inner, ">")) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Invalid protobuf map type override: '", map_type_str, "'."));
  }
  size_t comma_pos = inner.find(',');
  if (comma_pos == absl::string_view::npos ||
      inner.find(',', comma_pos + 1) != absl::string_view::npos) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Invalid protobuf map type override: '", map_type_str, "'."));
  }
  *key_type =
      std::string(absl::StripAsciiWhitespace(inner.substr(0, comma_pos)));
  *val_type =
      std::string(absl::StripAsciiWhitespace(inner.substr(comma_pos + 1)));
  if (key_type->empty() || val_type->empty()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Invalid protobuf map type override: '", map_type_str, "'."));
  }
  return absl::OkStatus();
}

absl::Status PopulateSingularFieldProtoType(
    const TypeSpec& type_spec, absl::string_view proto_type_str,
    const TypeDefSet& type_def_set, google::protobuf::FieldDescriptorProto* field_proto,
    absl::flat_hash_set<std::string>* referenced_types) {
  if (proto_type_str == "bool") {
    field_proto->set_type(google::protobuf::FieldDescriptorProto::TYPE_BOOL);
    return absl::OkStatus();
  }
  if (proto_type_str == "int32") {
    field_proto->set_type(google::protobuf::FieldDescriptorProto::TYPE_INT32);
    return absl::OkStatus();
  }
  if (proto_type_str == "int64") {
    field_proto->set_type(google::protobuf::FieldDescriptorProto::TYPE_INT64);
    return absl::OkStatus();
  }
  if (proto_type_str == "uint32") {
    field_proto->set_type(google::protobuf::FieldDescriptorProto::TYPE_UINT32);
    return absl::OkStatus();
  }
  if (proto_type_str == "uint64") {
    field_proto->set_type(google::protobuf::FieldDescriptorProto::TYPE_UINT64);
    return absl::OkStatus();
  }
  if (proto_type_str == "sint32") {
    field_proto->set_type(google::protobuf::FieldDescriptorProto::TYPE_SINT32);
    return absl::OkStatus();
  }
  if (proto_type_str == "sint64") {
    field_proto->set_type(google::protobuf::FieldDescriptorProto::TYPE_SINT64);
    return absl::OkStatus();
  }
  if (proto_type_str == "fixed32") {
    field_proto->set_type(google::protobuf::FieldDescriptorProto::TYPE_FIXED32);
    return absl::OkStatus();
  }
  if (proto_type_str == "fixed64") {
    field_proto->set_type(google::protobuf::FieldDescriptorProto::TYPE_FIXED64);
    return absl::OkStatus();
  }
  if (proto_type_str == "sfixed32") {
    field_proto->set_type(google::protobuf::FieldDescriptorProto::TYPE_SFIXED32);
    return absl::OkStatus();
  }
  if (proto_type_str == "sfixed64") {
    field_proto->set_type(google::protobuf::FieldDescriptorProto::TYPE_SFIXED64);
    return absl::OkStatus();
  }
  if (proto_type_str == "float") {
    field_proto->set_type(google::protobuf::FieldDescriptorProto::TYPE_FLOAT);
    return absl::OkStatus();
  }
  if (proto_type_str == "double") {
    field_proto->set_type(google::protobuf::FieldDescriptorProto::TYPE_DOUBLE);
    return absl::OkStatus();
  }
  if (proto_type_str == "string") {
    field_proto->set_type(google::protobuf::FieldDescriptorProto::TYPE_STRING);
    return absl::OkStatus();
  }
  if (proto_type_str == "bytes") {
    field_proto->set_type(google::protobuf::FieldDescriptorProto::TYPE_BYTES);
    return absl::OkStatus();
  }
  if (proto_type_str == "group") {
    std::string group_msg_name;
    if (type_spec.has_message_type()) {
      group_msg_name = type_spec.message_type().type();
    } else if (type_spec.has_abstract_type() &&
               type_spec.abstract_type().parameter_types().empty()) {
      group_msg_name = type_spec.abstract_type().name();
    } else {
      return absl::InvalidArgumentError(
          "Proto 'group' field must have a message TypeSpec.");
    }
    field_proto->set_type(google::protobuf::FieldDescriptorProto::TYPE_GROUP);
    field_proto->set_type_name(absl::StrCat(".", group_msg_name));
    referenced_types->insert(group_msg_name);
    return absl::OkStatus();
  }
  if (proto_type_str == "google.protobuf.NullValue") {
    field_proto->set_type(google::protobuf::FieldDescriptorProto::TYPE_ENUM);
    field_proto->set_type_name(".google.protobuf.NullValue");
    referenced_types->insert("google.protobuf.NullValue");
    return absl::OkStatus();
  }

  const TypeDef* target_td = type_def_set.FindTypeDef(proto_type_str);
  if (target_td != nullptr) {
    if (target_td->is_enum()) {
      field_proto->set_type(google::protobuf::FieldDescriptorProto::TYPE_ENUM);
    } else {
      field_proto->set_type(google::protobuf::FieldDescriptorProto::TYPE_MESSAGE);
    }
    field_proto->set_type_name(absl::StrCat(".", proto_type_str));
    referenced_types->insert(std::string(proto_type_str));
    return absl::OkStatus();
  }

  if (GetWellKnownTypeFileName(proto_type_str).has_value()) {
    field_proto->set_type(google::protobuf::FieldDescriptorProto::TYPE_MESSAGE);
    field_proto->set_type_name(absl::StrCat(".", proto_type_str));
    referenced_types->insert(std::string(proto_type_str));
    return absl::OkStatus();
  }

  return absl::InvalidArgumentError(
      absl::StrCat("Unknown referenced protobuf type '", proto_type_str, "'."));
}

absl::StatusOr<std::string> FormatConstantDefaultValue(
    const Constant& constant, google::protobuf::FieldDescriptorProto::Type proto_type) {
  switch (proto_type) {
    case google::protobuf::FieldDescriptorProto::TYPE_BOOL:
      if (!constant.has_bool_value()) {
        return absl::InvalidArgumentError(
            "Expected bool default value for bool field.");
      }
      return constant.bool_value() ? "true" : "false";
    case google::protobuf::FieldDescriptorProto::TYPE_INT32:
    case google::protobuf::FieldDescriptorProto::TYPE_SINT32:
    case google::protobuf::FieldDescriptorProto::TYPE_SFIXED32:
    case google::protobuf::FieldDescriptorProto::TYPE_INT64:
    case google::protobuf::FieldDescriptorProto::TYPE_SINT64:
    case google::protobuf::FieldDescriptorProto::TYPE_SFIXED64:
      if (!constant.has_int64_value()) {
        return absl::InvalidArgumentError(
            "Expected int default value for integer field.");
      }
      return absl::StrCat(constant.int64_value());
    case google::protobuf::FieldDescriptorProto::TYPE_UINT32:
    case google::protobuf::FieldDescriptorProto::TYPE_FIXED32:
    case google::protobuf::FieldDescriptorProto::TYPE_UINT64:
    case google::protobuf::FieldDescriptorProto::TYPE_FIXED64:
      if (!constant.has_uint64_value()) {
        return absl::InvalidArgumentError(
            "Expected uint default value for unsigned integer field.");
      }
      return absl::StrCat(constant.uint64_value());
    case google::protobuf::FieldDescriptorProto::TYPE_FLOAT:
      if (!constant.has_double_value()) {
        return absl::InvalidArgumentError(
            "Expected double default value for float field.");
      }
      return absl::StrCat(static_cast<float>(constant.double_value()));
    case google::protobuf::FieldDescriptorProto::TYPE_DOUBLE:
      if (!constant.has_double_value()) {
        return absl::InvalidArgumentError(
            "Expected double default value for double field.");
      }
      return absl::StrCat(constant.double_value());
    case google::protobuf::FieldDescriptorProto::TYPE_STRING:
      if (!constant.has_string_value()) {
        return absl::InvalidArgumentError(
            "Expected string default value for string field.");
      }
      return constant.string_value();
    case google::protobuf::FieldDescriptorProto::TYPE_BYTES:
      if (!constant.has_bytes_value()) {
        return absl::InvalidArgumentError(
            "Expected bytes default value for bytes field.");
      }
      return absl::CEscape(constant.bytes_value());
    default:
      return absl::InvalidArgumentError(
          "Scalar Constant default value is only supported on primitive "
          "fields.");
  }
}

std::string ShortTypeName(absl::string_view full_name) {
  size_t pos = full_name.rfind('.');
  if (pos == absl::string_view::npos) {
    return std::string(full_name);
  }
  return std::string(full_name.substr(pos + 1));
}

void BuildEnumProto(const EnumTypeDef& enum_def, absl::string_view schema_name,
                    const TypeDefToProtoOptions& options,
                    std::vector<int> base_path,
                    google::protobuf::EnumDescriptorProto* enum_proto,
                    proto2::SourceCodeInfo* source_code_info) {
  enum_proto->set_name(ShortTypeName(enum_def.name));
  AddCommentLocation(base_path, enum_def.doc, options, source_code_info);

  const ProtoEnumProperties* enum_props =
      GetProtoEnumProperties(enum_def, schema_name);
  if (enum_props != nullptr && enum_props->allow_alias.has_value()) {
    enum_proto->mutable_options()->set_allow_alias(*enum_props->allow_alias);
  }

  for (size_t i = 0; i < enum_def.constants.size(); ++i) {
    const EnumTypeDef::EnumConstant& constant = enum_def.constants[i];
    google::protobuf::EnumValueDescriptorProto* val_proto = enum_proto->add_value();
    const ProtoEnumConstantProperties* const_props =
        GetProtoEnumConstantProperties(constant, schema_name);
    if (const_props != nullptr && const_props->name.has_value()) {
      val_proto->set_name(*const_props->name);
    } else {
      val_proto->set_name(constant.name);
    }
    val_proto->set_number(constant.id);

    std::vector<int> val_path = base_path;
    val_path.push_back(google::protobuf::EnumDescriptorProto::kValueFieldNumber);
    val_path.push_back(static_cast<int>(i));
    AddCommentLocation(val_path, constant.doc, options, source_code_info);
  }
}

absl::Status BuildMessageProto(
    const ObjectTypeDef& obj_def, absl::string_view schema_name,
    const TypeDefSet& type_def_set,
    const absl::flat_hash_map<std::string, std::vector<const TypeDef*>>&
        children_map,
    const TypeDefToProtoOptions& options, std::vector<int> base_path,
    google::protobuf::DescriptorProto* msg_proto,
    proto2::SourceCodeInfo* source_code_info,
    absl::flat_hash_set<std::string>* referenced_types) {
  msg_proto->set_name(ShortTypeName(obj_def.name));
  AddCommentLocation(base_path, obj_def.doc, options, source_code_info);

  const ProtoObjectProperties* obj_props =
      GetProtoObjectProperties(obj_def, schema_name);
  if (obj_props != nullptr) {
    for (const ProtoExtensionRange& range : obj_props->extension_ranges) {
      google::protobuf::DescriptorProto::ExtensionRange* ext_range =
          msg_proto->add_extension_range();
      ext_range->set_start(range.start);
      ext_range->set_end(range.end);
    }
  }

  // First pass: register real oneof declarations so all real oneofs precede any
  // synthetic proto3_optional oneofs (as required by descriptor.proto).
  absl::flat_hash_map<std::string, int> real_oneof_indices;
  absl::flat_hash_set<std::string> used_oneof_names;
  absl::flat_hash_set<int32_t> used_field_numbers;
  for (const ObjectTypeDef::Field& field : obj_def.fields) {
    const ProtoFieldProperties* field_props =
        GetProtoFieldProperties(field, schema_name);
    if (field_props != nullptr) {
      if (field_props->id.has_value()) {
        used_field_numbers.insert(*field_props->id);
      }
      if (field_props->oneof.has_value()) {
        auto [it, inserted] = real_oneof_indices.try_emplace(
            *field_props->oneof, msg_proto->oneof_decl_size());
        if (inserted) {
          msg_proto->add_oneof_decl()->set_name(*field_props->oneof);
          used_oneof_names.insert(*field_props->oneof);
        }
      }
    }
  }

  // Build nested enums and explicit nested messages first so we know their
  // names and can avoid collisions when synthesizing map-entry messages.
  absl::flat_hash_set<std::string> used_nested_type_names;
  auto children_it = children_map.find(obj_def.name);
  if (children_it != children_map.end()) {
    for (const TypeDef* child : children_it->second) {
      if (child->is_enum()) {
        int enum_idx = msg_proto->enum_type_size();
        std::vector<int> enum_path = base_path;
        enum_path.push_back(google::protobuf::DescriptorProto::kEnumTypeFieldNumber);
        enum_path.push_back(enum_idx);
        BuildEnumProto(child->enum_type(), schema_name, options,
                       std::move(enum_path), msg_proto->add_enum_type(),
                       source_code_info);
      } else if (child->is_object()) {
        used_nested_type_names.insert(ShortTypeName(child->name()));
        int nested_idx = msg_proto->nested_type_size();
        std::vector<int> nested_path = base_path;
        nested_path.push_back(google::protobuf::DescriptorProto::kNestedTypeFieldNumber);
        nested_path.push_back(nested_idx);
        CEL_RETURN_IF_ERROR(BuildMessageProto(
            child->object_type(), schema_name, type_def_set, children_map,
            options, std::move(nested_path), msg_proto->add_nested_type(),
            source_code_info, referenced_types));
      }
    }
  }

  int32_t next_auto_field_number = 1;
  auto allocate_field_number = [&]() -> int32_t {
    while (
        used_field_numbers.contains(next_auto_field_number) ||
        (next_auto_field_number >= 19000 && next_auto_field_number <= 19999)) {
      ++next_auto_field_number;
    }
    used_field_numbers.insert(next_auto_field_number);
    return next_auto_field_number++;
  };

  for (size_t i = 0; i < obj_def.fields.size(); ++i) {
    const ObjectTypeDef::Field& field_def = obj_def.fields[i];
    const ProtoFieldProperties* field_props =
        GetProtoFieldProperties(field_def, schema_name);

    google::protobuf::FieldDescriptorProto* field_proto = msg_proto->add_field();
    field_proto->set_name(field_def.name);
    if (field_props != nullptr && field_props->id.has_value()) {
      field_proto->set_number(*field_props->id);
    } else {
      field_proto->set_number(allocate_field_number());
    }
    if (field_props != nullptr && field_props->json_name.has_value()) {
      field_proto->set_json_name(*field_props->json_name);
    }

    std::vector<int> field_path = base_path;
    field_path.push_back(google::protobuf::DescriptorProto::kFieldFieldNumber);
    field_path.push_back(static_cast<int>(i));
    AddCommentLocation(field_path, field_def.doc, options, source_code_info);

    const bool is_singular_struct_override =
        field_props != nullptr && field_props->type.has_value() &&
        *field_props->type == "google.protobuf.Struct";
    const bool is_singular_list_value_override =
        field_props != nullptr && field_props->type.has_value() &&
        *field_props->type == "google.protobuf.ListValue";

    if (field_def.type.has_map_type() && !is_singular_struct_override) {
      // Protobuf map<K, V> field.
      CEL_ASSIGN_OR_RETURN(
          std::string key_proto_type,
          DefaultSingularProtoTypeName(field_def.type.map_type().key_type()));
      CEL_ASSIGN_OR_RETURN(
          std::string val_proto_type,
          DefaultSingularProtoTypeName(field_def.type.map_type().value_type()));
      if (field_props != nullptr && field_props->type.has_value()) {
        CEL_RETURN_IF_ERROR(ParseMapProtoType(
            *field_props->type, &key_proto_type, &val_proto_type));
      }

      std::string entry_name = MapEntryName(field_def.name);
      while (used_nested_type_names.contains(entry_name)) {
        absl::StrAppend(&entry_name, "_");
      }
      used_nested_type_names.insert(entry_name);

      google::protobuf::DescriptorProto* entry_proto = msg_proto->add_nested_type();
      entry_proto->set_name(entry_name);
      entry_proto->mutable_options()->set_map_entry(true);

      google::protobuf::FieldDescriptorProto* key_field_proto = entry_proto->add_field();
      key_field_proto->set_name("key");
      key_field_proto->set_number(1);
      key_field_proto->set_label(google::protobuf::FieldDescriptorProto::LABEL_OPTIONAL);
      CEL_RETURN_IF_ERROR(PopulateSingularFieldProtoType(
          field_def.type.map_type().key_type(), key_proto_type, type_def_set,
          key_field_proto, referenced_types));

      google::protobuf::FieldDescriptorProto* val_field_proto = entry_proto->add_field();
      val_field_proto->set_name("value");
      val_field_proto->set_number(2);
      val_field_proto->set_label(google::protobuf::FieldDescriptorProto::LABEL_OPTIONAL);
      CEL_RETURN_IF_ERROR(PopulateSingularFieldProtoType(
          field_def.type.map_type().value_type(), val_proto_type, type_def_set,
          val_field_proto, referenced_types));

      field_proto->set_label(google::protobuf::FieldDescriptorProto::LABEL_REPEATED);
      field_proto->set_type(google::protobuf::FieldDescriptorProto::TYPE_MESSAGE);
      field_proto->set_type_name(
          absl::StrCat(".", obj_def.name, ".", entry_name));
    } else if (field_def.type.has_list_type() &&
               !is_singular_list_value_override) {
      // Repeated field.
      field_proto->set_label(google::protobuf::FieldDescriptorProto::LABEL_REPEATED);
      const TypeSpec& elem_spec = field_def.type.list_type().elem_type();
      std::string elem_proto_type;
      if (field_props != nullptr && field_props->type.has_value()) {
        elem_proto_type = *field_props->type;
      } else {
        CEL_ASSIGN_OR_RETURN(elem_proto_type,
                             DefaultSingularProtoTypeName(elem_spec));
      }
      CEL_RETURN_IF_ERROR(PopulateSingularFieldProtoType(
          elem_spec, elem_proto_type, type_def_set, field_proto,
          referenced_types));
    } else {
      // Singular field.
      if (schema_name == kProto2SchemaName && field_props != nullptr &&
          field_props->label.has_value() && *field_props->label == "required") {
        field_proto->set_label(google::protobuf::FieldDescriptorProto::LABEL_REQUIRED);
      } else {
        field_proto->set_label(google::protobuf::FieldDescriptorProto::LABEL_OPTIONAL);
      }

      std::string singular_proto_type;
      if (field_props != nullptr && field_props->type.has_value()) {
        singular_proto_type = *field_props->type;
      } else {
        CEL_ASSIGN_OR_RETURN(singular_proto_type,
                             DefaultSingularProtoTypeName(field_def.type));
      }
      CEL_RETURN_IF_ERROR(PopulateSingularFieldProtoType(
          field_def.type, singular_proto_type, type_def_set, field_proto,
          referenced_types));

      if (field_props != nullptr && field_props->oneof.has_value()) {
        field_proto->set_oneof_index(
            real_oneof_indices.at(*field_props->oneof));
      } else if (schema_name == kProto3SchemaName && field_props != nullptr &&
                 field_props->label.has_value() &&
                 *field_props->label == "optional") {
        field_proto->set_proto3_optional(true);
        std::string synth_oneof_name = absl::StrCat("_", field_def.name);
        while (used_oneof_names.contains(synth_oneof_name)) {
          synth_oneof_name = absl::StrCat("_", synth_oneof_name);
        }
        used_oneof_names.insert(synth_oneof_name);
        int synth_idx = msg_proto->oneof_decl_size();
        msg_proto->add_oneof_decl()->set_name(synth_oneof_name);
        field_proto->set_oneof_index(synth_idx);
      }

      if (schema_name == kProto2SchemaName) {
        if (field_def.default_value.has_value()) {
          CEL_ASSIGN_OR_RETURN(
              std::string default_str,
              FormatConstantDefaultValue(field_def.default_value,
                                         field_proto->type()));
          field_proto->set_default_value(default_str);
        } else if (field_props != nullptr &&
                   field_props->default_value.has_value()) {
          field_proto->set_default_value(*field_props->default_value);
        }
      }
    }
  }

  return absl::OkStatus();
}

enum class TopoVisitState { kUnvisited, kVisiting, kVisited };

absl::Status TopoSortFile(
    const std::string& file_name,
    const absl::btree_map<std::string, google::protobuf::FileDescriptorProto>&
        files_by_name,
    absl::flat_hash_map<std::string, TopoVisitState>* visit_state,
    google::protobuf::FileDescriptorSet* output_set) {
  TopoVisitState state = (*visit_state)[file_name];
  if (state == TopoVisitState::kVisited) {
    return absl::OkStatus();
  }
  if (state == TopoVisitState::kVisiting) {
    return absl::InvalidArgumentError(
        absl::StrCat("Cyclic file dependency involving '", file_name, "'."));
  }
  (*visit_state)[file_name] = TopoVisitState::kVisiting;
  auto it = files_by_name.find(file_name);
  if (it != files_by_name.end()) {
    for (const std::string& dep : it->second.dependency()) {
      if (files_by_name.contains(dep)) {
        CEL_RETURN_IF_ERROR(
            TopoSortFile(dep, files_by_name, visit_state, output_set));
      }
    }
    *output_set->add_file() = it->second;
  }
  (*visit_state)[file_name] = TopoVisitState::kVisited;
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<TypeDefSet> FileDescriptorSetToTypeDefSet(
    const google::protobuf::FileDescriptorSet& file_descriptor_set,
    const ProtoToTypeDefOptions& options) {
  google::protobuf::SimpleDescriptorDatabase ext_db;
  for (const google::protobuf::FileDescriptorProto& file_proto :
       file_descriptor_set.file()) {
    if (file_proto.name().empty()) {
      return absl::InvalidArgumentError(
          "File descriptor name cannot be empty.");
    }
    if (!ext_db.Add(file_proto)) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Duplicate file descriptor: '", file_proto.name(), "'."));
    }
  }

  // Place ext_db first so user-provided descriptors (including SourceCodeInfo)
  // take precedence over the minimal well-known types fallback database.
  google::protobuf::MergedDescriptorDatabase merged_db(
      &ext_db, cel::GetMinimalDescriptorDatabase());
  ErrorCollector error_collector;
  google::protobuf::DescriptorPool pool(&merged_db, &error_collector);

  TypeDefSet type_def_set;
  for (const google::protobuf::FileDescriptorProto& file_proto :
       file_descriptor_set.file()) {
    CEL_ASSIGN_OR_RETURN(absl::string_view schema_name,
                         GetSchemaName(file_proto));
    const google::protobuf::FileDescriptor* file_desc =
        pool.FindFileByName(file_proto.name());
    if (file_desc == nullptr) {
      if (!error_collector.errors().empty()) {
        return absl::InvalidArgumentError(
            absl::StrCat("Failed to build file descriptor '", file_proto.name(),
                         "': ", error_collector.errors()));
      }
      return absl::InvalidArgumentError(absl::StrCat(
          "Failed to build file descriptor '", file_proto.name(), "'."));
    }

    for (int i = 0; i < file_desc->enum_type_count(); ++i) {
      CEL_RETURN_IF_ERROR(ConvertEnumDescriptor(
          *file_desc->enum_type(i), schema_name, options, type_def_set));
    }

    for (int i = 0; i < file_desc->message_type_count(); ++i) {
      CEL_RETURN_IF_ERROR(ConvertMessageDescriptor(
          *file_desc->message_type(i), schema_name, options, type_def_set));
    }
  }

  return type_def_set;
}

absl::StatusOr<google::protobuf::FileDescriptorSet> TypeDefSetToFileDescriptorSet(
    const TypeDefSet& type_def_set, const TypeDefToProtoOptions& options) {
  if (type_def_set.types().empty()) {
    return google::protobuf::FileDescriptorSet{};
  }

  // Partition TypeDefs into top-level types and nested children of enclosing
  // ObjectTypeDefs.
  std::vector<const TypeDef*> top_level_types;
  absl::flat_hash_map<std::string, std::vector<const TypeDef*>> children_map;
  absl::flat_hash_map<std::string, const TypeDef*> parent_map;

  for (const TypeDef& td : type_def_set.types()) {
    absl::string_view full_name = td.name();
    size_t dot_pos = full_name.rfind('.');
    const TypeDef* parent_td = nullptr;
    if (dot_pos != absl::string_view::npos) {
      const TypeDef* candidate =
          type_def_set.FindTypeDef(full_name.substr(0, dot_pos));
      if (candidate != nullptr && candidate->is_object()) {
        parent_td = candidate;
      }
    }
    if (parent_td != nullptr) {
      children_map[std::string(parent_td->name())].push_back(&td);
      parent_map[std::string(full_name)] = parent_td;
    } else {
      top_level_types.push_back(&td);
    }
  }

  struct FileSpec {
    std::string file_name;
    std::string package;
    std::string schema_name;
    std::vector<const TypeDef*> top_types;
  };
  absl::btree_map<std::string, FileSpec> file_specs;
  absl::flat_hash_map<std::string, std::string> top_type_to_file;

  for (const TypeDef* root_td : top_level_types) {
    CEL_ASSIGN_OR_RETURN(std::string schema_name,
                         DetermineSchemaNameForTree(*root_td, children_map));
    absl::string_view full_name = root_td->name();
    size_t dot_pos = full_name.rfind('.');
    std::string package = (dot_pos == absl::string_view::npos)
                              ? ""
                              : std::string(full_name.substr(0, dot_pos));

    std::optional<std::string> explicit_file_name;
    if (root_td->is_object()) {
      const ProtoObjectProperties* props =
          GetProtoObjectProperties(root_td->object_type(), schema_name);
      if (props != nullptr && props->file_name.has_value() &&
          !props->file_name->empty()) {
        explicit_file_name = *props->file_name;
      }
    } else if (root_td->is_enum()) {
      const ProtoEnumProperties* props =
          GetProtoEnumProperties(root_td->enum_type(), schema_name);
      if (props != nullptr && props->file_name.has_value() &&
          !props->file_name->empty()) {
        explicit_file_name = *props->file_name;
      }
    }

    std::string file_name;
    if (explicit_file_name.has_value()) {
      file_name = *std::move(explicit_file_name);
    } else if (package.empty()) {
      file_name = absl::StrCat(schema_name, "_types.proto");
    } else {
      file_name = absl::StrCat(absl::StrReplaceAll(package, {{".", "/"}}), "/",
                               schema_name, "_types.proto");
    }

    auto [it, inserted] =
        file_specs.try_emplace(file_name, FileSpec{.file_name = file_name,
                                                   .package = package,
                                                   .schema_name = schema_name});
    if (!inserted) {
      if (it->second.package != package) {
        return absl::InvalidArgumentError(
            absl::StrCat("Conflicting packages '", it->second.package,
                         "' and '", package, "' in file '", file_name, "'."));
      }
      if (it->second.schema_name != schema_name) {
        return absl::InvalidArgumentError(absl::StrCat(
            "Conflicting proto syntaxes '", it->second.schema_name, "' and '",
            schema_name, "' in file '", file_name, "'."));
      }
    }
    it->second.top_types.push_back(root_td);
    top_type_to_file[std::string(root_td->name())] = file_name;
  }

  auto resolve_type_file =
      [&](absl::string_view full_type_name) -> absl::StatusOr<std::string> {
    const TypeDef* td = type_def_set.FindTypeDef(full_type_name);
    if (td != nullptr) {
      const TypeDef* curr = td;
      while (true) {
        auto pit = parent_map.find(curr->name());
        if (pit == parent_map.end()) {
          break;
        }
        curr = pit->second;
      }
      return top_type_to_file.at(curr->name());
    }
    std::optional<std::string> wkt_file =
        GetWellKnownTypeFileName(full_type_name);
    if (wkt_file.has_value()) {
      return *std::move(wkt_file);
    }
    return absl::InvalidArgumentError(absl::StrCat(
        "Unknown referenced protobuf type '", full_type_name, "'."));
  };

  absl::btree_map<std::string, google::protobuf::FileDescriptorProto> files_by_name;
  absl::btree_set<std::string> required_wkt_files;

  for (const auto& [file_name, spec] : file_specs) {
    google::protobuf::FileDescriptorProto file_proto;
    file_proto.set_name(spec.file_name);
    if (!spec.package.empty()) {
      file_proto.set_package(spec.package);
    }
    file_proto.set_syntax(spec.schema_name);

    proto2::SourceCodeInfo source_code_info;
    absl::flat_hash_set<std::string> referenced_types;

    for (const TypeDef* top_td : spec.top_types) {
      if (top_td->is_enum()) {
        int enum_idx = file_proto.enum_type_size();
        std::vector<int> path = {
            google::protobuf::FileDescriptorProto::kEnumTypeFieldNumber, enum_idx};
        BuildEnumProto(top_td->enum_type(), spec.schema_name, options,
                       std::move(path), file_proto.add_enum_type(),
                       &source_code_info);
      } else if (top_td->is_object()) {
        int msg_idx = file_proto.message_type_size();
        std::vector<int> path = {
            google::protobuf::FileDescriptorProto::kMessageTypeFieldNumber, msg_idx};
        CEL_RETURN_IF_ERROR(BuildMessageProto(
            top_td->object_type(), spec.schema_name, type_def_set, children_map,
            options, std::move(path), file_proto.add_message_type(),
            &source_code_info, &referenced_types));
      }
    }

    if (source_code_info.location_size() > 0) {
      *file_proto.mutable_source_code_info() = std::move(source_code_info);
    }

    absl::btree_set<std::string> deps;
    for (const std::string& ref_type : referenced_types) {
      CEL_ASSIGN_OR_RETURN(std::string dep_file, resolve_type_file(ref_type));
      if (dep_file != spec.file_name) {
        deps.insert(dep_file);
        if (!file_specs.contains(dep_file)) {
          required_wkt_files.insert(dep_file);
        }
      }
    }
    for (const std::string& dep : deps) {
      file_proto.add_dependency(dep);
    }

    files_by_name[spec.file_name] = std::move(file_proto);
  }

  if (options.include_well_known_type_files && !required_wkt_files.empty()) {
    google::protobuf::DescriptorDatabase* wkt_db = cel::GetMinimalDescriptorDatabase();
    std::vector<std::string> worklist(required_wkt_files.begin(),
                                      required_wkt_files.end());
    while (!worklist.empty()) {
      std::string wkt_file = std::move(worklist.back());
      worklist.pop_back();
      if (files_by_name.contains(wkt_file)) {
        continue;
      }
      google::protobuf::FileDescriptorProto wkt_proto;
      if (!wkt_db->FindFileByName(wkt_file, &wkt_proto)) {
        return absl::InvalidArgumentError(
            absl::StrCat("Well-known protobuf file '", wkt_file,
                         "' not found in minimal descriptor database."));
      }
      for (const std::string& dep : wkt_proto.dependency()) {
        if (!files_by_name.contains(dep)) {
          worklist.push_back(dep);
        }
      }
      files_by_name[wkt_file] = std::move(wkt_proto);
    }
  }

  google::protobuf::FileDescriptorSet result;
  absl::flat_hash_map<std::string, TopoVisitState> visit_state;
  for (const auto& [file_name, _] : files_by_name) {
    CEL_RETURN_IF_ERROR(
        TopoSortFile(file_name, files_by_name, &visit_state, &result));
  }

  // Validate that the produced FileDescriptorSet builds cleanly in
  // DescriptorPool.
  google::protobuf::SimpleDescriptorDatabase ext_db;
  for (const google::protobuf::FileDescriptorProto& f : result.file()) {
    ext_db.Add(f);
  }
  google::protobuf::MergedDescriptorDatabase merged_db(
      &ext_db, cel::GetMinimalDescriptorDatabase());
  ErrorCollector error_collector;
  google::protobuf::DescriptorPool pool(&merged_db, &error_collector);
  for (const google::protobuf::FileDescriptorProto& f : result.file()) {
    if (pool.FindFileByName(f.name()) == nullptr) {
      return absl::InvalidArgumentError(
          absl::StrCat("Generated invalid FileDescriptorProto '", f.name(),
                       "': ", error_collector.errors()));
    }
  }

  return result;
}

}  // namespace cel
