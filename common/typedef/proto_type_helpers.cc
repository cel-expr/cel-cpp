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
#include "common/minimal_descriptor_database.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor_database.h"
#include "google/protobuf/message.h"

namespace cel::typedef_internal {

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

std::string GetSingularProtoTypeName(const google::protobuf::FieldDescriptor& field) {
  switch (field.type()) {
    case google::protobuf::FieldDescriptor::TYPE_ENUM:
      return std::string(field.enum_type()->full_name());
    case google::protobuf::FieldDescriptor::TYPE_GROUP:
    case google::protobuf::FieldDescriptor::TYPE_MESSAGE:
      return std::string(field.message_type()->full_name());
    default:
      return std::string(field.type_name());
  }
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
    return absl::InvalidArgumentError("unspecified primitive type");
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
    return absl::InvalidArgumentError("unspecified primitive wrapper type");
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
    return absl::InvalidArgumentError("unspecified well-known type");
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
  if (type_spec.has_list_type()) {
    return "google.protobuf.ListValue";
  }
  if (type_spec.has_map_type()) {
    return "google.protobuf.Struct";
  }
  return absl::InvalidArgumentError("unsupported type spec kind");
}

std::string ProtoDefaultJsonName(absl::string_view name) {
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
      return absl::InvalidArgumentError("file descriptor name cannot be empty");
    }
    if (!ext_db->Add(file_proto)) {
      return absl::InvalidArgumentError(
          absl::StrCat("duplicate or conflicting file descriptor: '",
                       file_proto.name(), "'"));
    }
  }
  return absl::WrapUnique(new TypeDefDescriptorPool(std::move(ext_db)));
}

absl::StatusOr<const google::protobuf::FileDescriptor* absl_nonnull>
TypeDefDescriptorPool::FindFile(absl::string_view file_name) {
  error_collector_.Clear();
  const google::protobuf::FileDescriptor* file_desc = pool_.FindFileByName(file_name);
  if (file_desc == nullptr) {
    return absl::InvalidArgumentError(
        absl::StrCat("failed to build file descriptor '", file_name,
                     "': ", error_collector_.errors()));
  }
  return file_desc;
}

}  // namespace cel::typedef_internal
