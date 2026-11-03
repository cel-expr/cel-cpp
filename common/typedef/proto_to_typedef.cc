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

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "google/protobuf/descriptor.pb.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "common/ast.h"
#include "common/constant.h"
#include "common/typedef/proto_type_helpers.h"
#include "common/typedef/typedef.h"
#include "internal/status_macros.h"
#include "google/protobuf/descriptor.h"

namespace cel {
namespace {

using ::cel::typedef_internal::DefaultSingularProtoTypeName;
using ::cel::typedef_internal::GetSingularProtoTypeName;
using ::cel::typedef_internal::IsWellKnownEnumType;
using ::cel::typedef_internal::IsWellKnownMessageType;
using ::cel::typedef_internal::SnakeToCamelCase;
using ::cel::typedef_internal::TypeDefDescriptorPool;
using ::cel::typedef_internal::WellKnownMessageToTypeSpec;

TypeSpec MessageDescriptorToTypeSpec(const google::protobuf::Descriptor& desc) {
  if (std::optional<TypeSpec> wkt = WellKnownMessageToTypeSpec(desc);
      wkt.has_value()) {
    return *std::move(wkt);
  }
  return TypeSpec(MessageTypeSpec(std::string(desc.full_name())));
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
    CEL_ASSIGN_OR_RETURN(TypeSpec key_type,
                         SingularFieldDescriptorToTypeSpec(*entry->map_key()));
    CEL_ASSIGN_OR_RETURN(TypeSpec value_type, SingularFieldDescriptorToTypeSpec(
                                                  *entry->map_value()));
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
  google::protobuf::SourceLocation location;
  if (!desc.GetSourceLocation(&location)) {
    return "";
  }
  return std::string(absl::StripAsciiWhitespace(location.leading_comments));
}

Constant ExtractFieldDefaultValue(const google::protobuf::FieldDescriptor& field,
                                  const ProtoToTypeDefOptions& options) {
  if (options.include_defaults && field.has_default_value()) {
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
        return Constant(StringConstant(field.default_value_enum()->name()));
      case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
        break;
    }
  }
  return Constant();
}

absl::Status ConvertEnumDescriptor(const google::protobuf::EnumDescriptor& enum_desc,
                                   const ProtoToTypeDefOptions& options,
                                   TypeDefSet& type_def_set) {
  if (IsWellKnownEnumType(enum_desc)) {
    return absl::OkStatus();
  }
  absl::flat_hash_map<std::string, Aspect> enum_aspects;
  absl::flat_hash_map<std::string, Constant> proto_props;
  if (enum_desc.containing_type() == nullptr) {
    proto_props.emplace(
        "file_name",
        Constant(StringConstant(std::string(enum_desc.file()->name()))));
  }
  if (enum_desc.options().has_allow_alias()) {
    proto_props.emplace("allow_alias",
                        Constant(enum_desc.options().allow_alias()));
  }
  if (!proto_props.empty()) {
    enum_aspects.emplace(kProtoAspectName, Aspect(std::move(proto_props)));
  }

  EnumTypeDef enum_def(std::string(enum_desc.full_name()),
                       ExtractLeadingComment(enum_desc, options),
                       std::move(enum_aspects));

  for (int i = 0; i < enum_desc.value_count(); ++i) {
    const google::protobuf::EnumValueDescriptor* val = enum_desc.value(i);
    EnumTypeDef::EnumConstant constant(std::string(val->name()), val->number(),
                                       ExtractLeadingComment(*val, options));
    CEL_RETURN_IF_ERROR(enum_def.AddConstant(std::move(constant)));
  }

  return type_def_set.AddTypeDef(TypeDef(std::move(enum_def)));
}

absl::StatusOr<StructTypeDef::Field> ConvertFieldDescriptor(
    const google::protobuf::FieldDescriptor& field,
    const ProtoToTypeDefOptions& options) {
  CEL_ASSIGN_OR_RETURN(TypeSpec field_type, FieldDescriptorToTypeSpec(field));
  std::string field_doc = ExtractLeadingComment(field, options);
  Constant field_default_value = ExtractFieldDefaultValue(field, options);

  absl::flat_hash_map<std::string, Constant> proto_props;
  proto_props.emplace("id", Constant(static_cast<int64_t>(field.number())));
  if (field.has_json_name() &&
      field.json_name() !=
          SnakeToCamelCase(field.name(), /*upper_first=*/false)) {
    proto_props.emplace(
        "json_name", Constant(StringConstant(std::string(field.json_name()))));
  }
  CEL_ASSIGN_OR_RETURN(std::optional<std::string> type,
                       ExtractNonDefaultProtoFieldType(field, field_type));
  if (type.has_value()) {
    proto_props.emplace("type", Constant(StringConstant(*std::move(type))));
  }
  absl::flat_hash_map<std::string, Aspect> field_aspects;
  field_aspects.emplace(kProtoAspectName, Aspect(std::move(proto_props)));

  return StructTypeDef::Field(
      std::string(field.name()), std::move(field_type), std::move(field_doc),
      std::move(field_default_value), std::move(field_aspects));
}

absl::Status ConvertMessageDescriptor(const google::protobuf::Descriptor& desc,
                                      const ProtoToTypeDefOptions& options,
                                      TypeDefSet& type_def_set) {
  if (desc.options().map_entry() || IsWellKnownMessageType(desc)) {
    return absl::OkStatus();
  }

  absl::flat_hash_map<std::string, Aspect> struct_aspects;
  if (desc.containing_type() == nullptr) {
    absl::flat_hash_map<std::string, Constant> proto_props;
    proto_props.emplace(
        "file_name",
        Constant(StringConstant(std::string(desc.file()->name()))));
    struct_aspects.emplace(kProtoAspectName, Aspect(std::move(proto_props)));
  }

  StructTypeDef struct_def(std::string(desc.full_name()),
                           ExtractLeadingComment(desc, options),
                           std::move(struct_aspects));

  absl::flat_hash_set<const google::protobuf::OneofDescriptor*> emitted_oneofs;
  for (int i = 0; i < desc.field_count(); ++i) {
    const google::protobuf::FieldDescriptor* field = desc.field(i);
    const google::protobuf::OneofDescriptor* real_oneof = field->real_containing_oneof();
    if (real_oneof != nullptr) {
      if (!emitted_oneofs.insert(real_oneof).second) {
        continue;
      }
      absl::flat_hash_map<std::string, Constant> oneof_proto_props;
      oneof_proto_props.emplace(
          "name", Constant(StringConstant(std::string(real_oneof->name()))));
      absl::flat_hash_map<std::string, Aspect> oneof_aspects;
      oneof_aspects.emplace(kProtoAspectName,
                            Aspect(std::move(oneof_proto_props)));
      StructTypeDef::Oneof oneof_def(std::move(oneof_aspects));
      for (int j = 0; j < real_oneof->field_count(); ++j) {
        CEL_ASSIGN_OR_RETURN(
            StructTypeDef::Field oneof_field,
            ConvertFieldDescriptor(*real_oneof->field(j), options));
        CEL_RETURN_IF_ERROR(oneof_def.AddField(std::move(oneof_field)));
      }
      CEL_RETURN_IF_ERROR(struct_def.AddOneof(std::move(oneof_def)));
    } else {
      CEL_ASSIGN_OR_RETURN(StructTypeDef::Field field_def,
                           ConvertFieldDescriptor(*field, options));
      CEL_RETURN_IF_ERROR(struct_def.AddField(std::move(field_def)));
    }
  }

  CEL_RETURN_IF_ERROR(type_def_set.AddTypeDef(TypeDef(std::move(struct_def))));

  for (int i = 0; i < desc.enum_type_count(); ++i) {
    CEL_RETURN_IF_ERROR(
        ConvertEnumDescriptor(*desc.enum_type(i), options, type_def_set));
  }

  for (int i = 0; i < desc.nested_type_count(); ++i) {
    CEL_RETURN_IF_ERROR(
        ConvertMessageDescriptor(*desc.nested_type(i), options, type_def_set));
  }

  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<TypeDefSet> FileDescriptorSetToTypeDefSet(
    const google::protobuf::FileDescriptorSet& file_descriptor_set,
    const ProtoToTypeDefOptions& options) {
  CEL_ASSIGN_OR_RETURN(std::unique_ptr<TypeDefDescriptorPool> pool,
                       TypeDefDescriptorPool::Create(file_descriptor_set));

  TypeDefSet type_def_set;
  for (const google::protobuf::FileDescriptorProto& file_proto :
       file_descriptor_set.file()) {
    CEL_ASSIGN_OR_RETURN(const google::protobuf::FileDescriptor* file_desc,
                         pool->FindFile(file_proto.name()));

    for (int i = 0; i < file_desc->enum_type_count(); ++i) {
      CEL_RETURN_IF_ERROR(ConvertEnumDescriptor(*file_desc->enum_type(i),
                                                options, type_def_set));
    }

    for (int i = 0; i < file_desc->message_type_count(); ++i) {
      CEL_RETURN_IF_ERROR(ConvertMessageDescriptor(*file_desc->message_type(i),
                                                   options, type_def_set));
    }
  }

  return type_def_set;
}

}  // namespace cel
