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

#include "common/typedef/proto_type_helpers.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "google/protobuf/descriptor.pb.h"
#include "absl/base/nullability.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "common/ast.h"
#include "common/constant.h"
#include "common/minimal_descriptor_database.h"
#include "common/typedef/typedef.h"
#include "internal/status_macros.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor_database.h"
#include "google/protobuf/message.h"

namespace cel::typedef_internal {
namespace {

inline constexpr absl::string_view kProtoAspect = "proto";

struct ScalarProtoTypeEntry {
  google::protobuf::FieldDescriptor::Type desc_type;
  google::protobuf::FieldDescriptorProto::Type proto_type;
  absl::string_view name;
};

constexpr ScalarProtoTypeEntry kScalarProtoTypes[] = {
    {google::protobuf::FieldDescriptor::TYPE_BOOL,
     google::protobuf::FieldDescriptorProto::TYPE_BOOL, "bool"},
    {google::protobuf::FieldDescriptor::TYPE_INT32,
     google::protobuf::FieldDescriptorProto::TYPE_INT32, "int32"},
    {google::protobuf::FieldDescriptor::TYPE_INT64,
     google::protobuf::FieldDescriptorProto::TYPE_INT64, "int64"},
    {google::protobuf::FieldDescriptor::TYPE_UINT32,
     google::protobuf::FieldDescriptorProto::TYPE_UINT32, "uint32"},
    {google::protobuf::FieldDescriptor::TYPE_UINT64,
     google::protobuf::FieldDescriptorProto::TYPE_UINT64, "uint64"},
    {google::protobuf::FieldDescriptor::TYPE_SINT32,
     google::protobuf::FieldDescriptorProto::TYPE_SINT32, "sint32"},
    {google::protobuf::FieldDescriptor::TYPE_SINT64,
     google::protobuf::FieldDescriptorProto::TYPE_SINT64, "sint64"},
    {google::protobuf::FieldDescriptor::TYPE_FIXED32,
     google::protobuf::FieldDescriptorProto::TYPE_FIXED32, "fixed32"},
    {google::protobuf::FieldDescriptor::TYPE_FIXED64,
     google::protobuf::FieldDescriptorProto::TYPE_FIXED64, "fixed64"},
    {google::protobuf::FieldDescriptor::TYPE_SFIXED32,
     google::protobuf::FieldDescriptorProto::TYPE_SFIXED32, "sfixed32"},
    {google::protobuf::FieldDescriptor::TYPE_SFIXED64,
     google::protobuf::FieldDescriptorProto::TYPE_SFIXED64, "sfixed64"},
    {google::protobuf::FieldDescriptor::TYPE_FLOAT,
     google::protobuf::FieldDescriptorProto::TYPE_FLOAT, "float"},
    {google::protobuf::FieldDescriptor::TYPE_DOUBLE,
     google::protobuf::FieldDescriptorProto::TYPE_DOUBLE, "double"},
    {google::protobuf::FieldDescriptor::TYPE_STRING,
     google::protobuf::FieldDescriptorProto::TYPE_STRING, "string"},
    {google::protobuf::FieldDescriptor::TYPE_BYTES,
     google::protobuf::FieldDescriptorProto::TYPE_BYTES, "bytes"},
};

struct WellKnownProtoFileEntry {
  absl::string_view full_name;
  absl::string_view file_name;
};

constexpr WellKnownProtoFileEntry kWellKnownProtoFiles[] = {
    {"google.protobuf.Any", "google/protobuf/any.proto"},
    {"google.protobuf.Duration", "google/protobuf/duration.proto"},
    {"google.protobuf.Timestamp", "google/protobuf/timestamp.proto"},
    {"google.protobuf.Struct", "google/protobuf/struct.proto"},
    {"google.protobuf.Value", "google/protobuf/struct.proto"},
    {"google.protobuf.ListValue", "google/protobuf/struct.proto"},
    {"google.protobuf.NullValue", "google/protobuf/struct.proto"},
    {"google.protobuf.BoolValue", "google/protobuf/wrappers.proto"},
    {"google.protobuf.Int32Value", "google/protobuf/wrappers.proto"},
    {"google.protobuf.Int64Value", "google/protobuf/wrappers.proto"},
    {"google.protobuf.UInt32Value", "google/protobuf/wrappers.proto"},
    {"google.protobuf.UInt64Value", "google/protobuf/wrappers.proto"},
    {"google.protobuf.FloatValue", "google/protobuf/wrappers.proto"},
    {"google.protobuf.DoubleValue", "google/protobuf/wrappers.proto"},
    {"google.protobuf.StringValue", "google/protobuf/wrappers.proto"},
    {"google.protobuf.BytesValue", "google/protobuf/wrappers.proto"},
};

}  // namespace

absl::StatusOr<std::optional<absl::string_view>> GetOptionalStringProperty(
    const Aspect* absl_nullable aspect, absl::string_view key) {
  if (aspect == nullptr) {
    return std::nullopt;
  }
  const Constant* prop = aspect->GetProperty(key);
  if (prop == nullptr) {
    return std::nullopt;
  }
  if (!prop->has_string_value()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Aspect property 'proto.", key, "' must be a string."));
  }
  if (prop->string_value().empty()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Aspect property 'proto.", key, "' cannot be empty."));
  }
  return absl::string_view(prop->string_value());
}

absl::StatusOr<std::optional<bool>> GetOptionalBoolProperty(
    const Aspect* absl_nullable aspect, absl::string_view key) {
  if (aspect == nullptr) {
    return std::nullopt;
  }
  const Constant* prop = aspect->GetProperty(key);
  if (prop == nullptr) {
    return std::nullopt;
  }
  if (!prop->has_bool_value()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Aspect property 'proto.", key, "' must be a bool."));
  }
  return prop->bool_value();
}

absl::StatusOr<std::optional<int32_t>> GetOptionalFieldIdProperty(
    const Aspect* absl_nullable aspect, absl::string_view key) {
  if (aspect == nullptr) {
    return std::nullopt;
  }
  const Constant* prop = aspect->GetProperty(key);
  if (prop == nullptr) {
    return std::nullopt;
  }
  if (!prop->has_int_value()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Aspect property 'proto.", key, "' must be an int."));
  }
  int64_t val = prop->int_value();
  if (val <= 0 || val > std::numeric_limits<int32_t>::max()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Aspect property 'proto.", key,
                     "' must be a positive 32-bit integer, got ", val, "."));
  }
  return static_cast<int32_t>(val);
}

absl::StatusOr<std::optional<absl::string_view>> GetProtoNameOverride(
    const TypeDef& td) {
  return GetOptionalStringProperty(FindAspect(td, kProtoAspect), "name");
}

absl::StatusOr<std::optional<absl::string_view>> GetProtoFileNameOverride(
    const TypeDef& td) {
  return GetOptionalStringProperty(FindAspect(td, kProtoAspect), "file_name");
}

std::optional<TypeSpec> WellKnownMessageToTypeSpec(
    const google::protobuf::Descriptor& desc) {
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
      return std::nullopt;
  }
}

bool IsWellKnownMessageType(const google::protobuf::Descriptor& desc) {
  return WellKnownMessageToTypeSpec(desc).has_value();
}

bool IsWellKnownEnumType(const google::protobuf::EnumDescriptor& enum_desc) {
  return enum_desc.full_name() == "google.protobuf.NullValue";
}

std::optional<absl::string_view> GetWellKnownTypeFileName(
    absl::string_view full_type_name) {
  for (const WellKnownProtoFileEntry& entry : kWellKnownProtoFiles) {
    if (entry.full_name == full_type_name) {
      return entry.file_name;
    }
  }
  return std::nullopt;
}

std::string GetSingularProtoTypeName(const google::protobuf::FieldDescriptor& field) {
  for (const ScalarProtoTypeEntry& entry : kScalarProtoTypes) {
    if (entry.desc_type == field.type()) {
      return std::string(entry.name);
    }
  }
  switch (field.type()) {
    case google::protobuf::FieldDescriptor::TYPE_ENUM:
      return std::string(field.enum_type()->full_name());
    case google::protobuf::FieldDescriptor::TYPE_GROUP:
    case google::protobuf::FieldDescriptor::TYPE_MESSAGE:
      return std::string(field.message_type()->full_name());
    default:
      return "";
  }
}

std::optional<google::protobuf::FieldDescriptorProto::Type> ParseScalarProtoFieldType(
    absl::string_view proto_type_str) {
  for (const ScalarProtoTypeEntry& entry : kScalarProtoTypes) {
    if (entry.name == proto_type_str) {
      return entry.proto_type;
    }
  }
  return std::nullopt;
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

std::string SnakeToCamelCase(absl::string_view name, bool upper_first) {
  std::string result;
  result.reserve(name.size());
  bool next_upper = upper_first;
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

void TypeDefDescriptorPool::ErrorCollector::RecordError(
    absl::string_view filename, absl::string_view element_name,
    const google::protobuf::Message* /*descriptor*/, ErrorLocation /*location*/,
    absl::string_view message) {
  if (!errors_.empty()) {
    absl::StrAppend(&errors_, "; ");
  }
  absl::StrAppend(&errors_, filename, ": ", element_name, ": ", message);
}

TypeDefDescriptorPool::TypeDefDescriptorPool(
    std::unique_ptr<google::protobuf::SimpleDescriptorDatabase> ext_db)
    // Place ext_db first so user-provided descriptors (including
    // SourceCodeInfo) take precedence over the minimal well-known types
    // fallback database.
    : ext_db_(std::move(ext_db)),
      merged_db_(ext_db_.get(), cel::GetMinimalDescriptorDatabase()),
      pool_(&merged_db_, &error_collector_) {}

absl::StatusOr<std::unique_ptr<TypeDefDescriptorPool>>
TypeDefDescriptorPool::Create(
    const google::protobuf::FileDescriptorSet& file_descriptor_set) {
  auto ext_db = std::make_unique<google::protobuf::SimpleDescriptorDatabase>();
  for (const google::protobuf::FileDescriptorProto& file_proto :
       file_descriptor_set.file()) {
    if (file_proto.name().empty()) {
      return absl::InvalidArgumentError(
          "File descriptor name cannot be empty.");
    }
    if (!ext_db->Add(file_proto)) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Duplicate file descriptor: '", file_proto.name(), "'."));
    }
  }
  return absl::WrapUnique(new TypeDefDescriptorPool(std::move(ext_db)));
}

absl::StatusOr<const google::protobuf::FileDescriptor* absl_nonnull>
TypeDefDescriptorPool::FindFile(absl::string_view file_name) const {
  const google::protobuf::FileDescriptor* file_desc = pool_.FindFileByName(file_name);
  if (file_desc == nullptr) {
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to build file descriptor '", file_name,
                     "': ", error_collector_.errors()));
  }
  return file_desc;
}

absl::Status TypeDefDescriptorPool::BuildAllFiles(
    const google::protobuf::FileDescriptorSet& file_descriptor_set) const {
  for (const google::protobuf::FileDescriptorProto& file_proto :
       file_descriptor_set.file()) {
    CEL_RETURN_IF_ERROR(FindFile(file_proto.name()).status());
  }
  return absl::OkStatus();
}

absl::Status ValidateFileDescriptorSet(
    const google::protobuf::FileDescriptorSet& file_descriptor_set) {
  CEL_ASSIGN_OR_RETURN(std::unique_ptr<TypeDefDescriptorPool> pool,
                       TypeDefDescriptorPool::Create(file_descriptor_set));
  return pool->BuildAllFiles(file_descriptor_set);
}

}  // namespace cel::typedef_internal
