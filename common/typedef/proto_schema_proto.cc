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

#include "common/typedef/proto_schema_proto.h"

#include <any>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "common/typedef/proto_schema.h"
#include "common/typedef/proto_schema.pb.h"
#include "common/typedef/typedef.pb.h"

namespace cel {

// =============================================================================
// ProtoSchemaProto ("proto")
// =============================================================================

absl::StatusOr<std::any> ProtoSchemaProto::DecodeStructProperties(
    const types::Struct& proto) const {
  if (!proto.HasExtension(types::proto_struct_aspect)) {
    return std::any();
  }
  const types::ProtoStructProperties& ext =
      proto.GetExtension(types::proto_struct_aspect);
  std::optional<std::string> name;
  if (ext.has_name()) {
    name = ext.name();
  }
  return ProtoStructProperties(std::move(name));
}

absl::StatusOr<std::any> ProtoSchemaProto::DecodeFieldProperties(
    const types::Struct::Field& proto) const {
  if (!proto.HasExtension(types::proto_field_aspect)) {
    return std::any();
  }
  const types::ProtoFieldProperties& ext =
      proto.GetExtension(types::proto_field_aspect);
  std::optional<std::string> name;
  std::optional<int32_t> id;
  std::optional<std::string> json_name;
  if (ext.has_name()) {
    name = ext.name();
  }
  if (ext.has_id()) {
    id = ext.id();
  }
  if (ext.has_json_name()) {
    json_name = ext.json_name();
  }
  return ProtoFieldProperties(std::move(name), id, std::move(json_name));
}

absl::StatusOr<std::any> ProtoSchemaProto::DecodeEnumProperties(
    const types::Enum& proto) const {
  if (!proto.HasExtension(types::proto_enum_aspect)) {
    return std::any();
  }
  const types::ProtoEnumProperties& ext =
      proto.GetExtension(types::proto_enum_aspect);
  std::optional<std::string> name;
  std::optional<bool> allow_alias;
  if (ext.has_name()) {
    name = ext.name();
  }
  if (ext.has_allow_alias()) {
    allow_alias = ext.allow_alias();
  }
  return ProtoEnumProperties(std::move(name), allow_alias);
}

absl::StatusOr<std::any> ProtoSchemaProto::DecodeEnumConstantProperties(
    const types::Enum::EnumConstant& proto) const {
  if (!proto.HasExtension(types::proto_enum_constant_aspect)) {
    return std::any();
  }
  const types::ProtoEnumConstantProperties& ext =
      proto.GetExtension(types::proto_enum_constant_aspect);
  std::optional<std::string> name;
  if (ext.has_name()) {
    name = ext.name();
  }
  return ProtoEnumConstantProperties(std::move(name));
}

absl::Status ProtoSchemaProto::EncodeStructProperties(
    const std::any& properties, types::Struct* absl_nonnull proto) const {
  const auto* props = std::any_cast<ProtoStructProperties>(&properties);
  if (props == nullptr) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Invalid struct properties type for '", name(), "' aspect"));
  }
  types::ProtoStructProperties* ext =
      proto->MutableExtension(types::proto_struct_aspect);
  ext->Clear();
  if (props->name().has_value()) {
    ext->set_name(*props->name());
  }
  return absl::OkStatus();
}

absl::Status ProtoSchemaProto::EncodeFieldProperties(
    const std::any& properties,
    types::Struct::Field* absl_nonnull proto) const {
  const auto* props = std::any_cast<ProtoFieldProperties>(&properties);
  if (props == nullptr) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Invalid field properties type for '", name(), "' aspect"));
  }
  types::ProtoFieldProperties* ext =
      proto->MutableExtension(types::proto_field_aspect);
  ext->Clear();
  if (props->name().has_value()) {
    ext->set_name(*props->name());
  }
  if (props->id().has_value()) {
    ext->set_id(*props->id());
  }
  if (props->json_name().has_value()) {
    ext->set_json_name(*props->json_name());
  }
  return absl::OkStatus();
}

absl::Status ProtoSchemaProto::EncodeEnumProperties(
    const std::any& properties, types::Enum* absl_nonnull proto) const {
  const auto* props = std::any_cast<ProtoEnumProperties>(&properties);
  if (props == nullptr) {
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid enum properties type for '", name(), "' aspect"));
  }
  types::ProtoEnumProperties* ext =
      proto->MutableExtension(types::proto_enum_aspect);
  ext->Clear();
  if (props->name().has_value()) {
    ext->set_name(*props->name());
  }
  if (props->allow_alias().has_value()) {
    ext->set_allow_alias(*props->allow_alias());
  }
  return absl::OkStatus();
}

absl::Status ProtoSchemaProto::EncodeEnumConstantProperties(
    const std::any& properties,
    types::Enum::EnumConstant* absl_nonnull proto) const {
  const auto* props = std::any_cast<ProtoEnumConstantProperties>(&properties);
  if (props == nullptr) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Invalid enum constant properties type for '", name(), "' aspect"));
  }
  types::ProtoEnumConstantProperties* ext =
      proto->MutableExtension(types::proto_enum_constant_aspect);
  ext->Clear();
  if (props->name().has_value()) {
    ext->set_name(*props->name());
  }
  return absl::OkStatus();
}

// =============================================================================
// Proto2SchemaProto ("proto2")
// =============================================================================

absl::StatusOr<std::any> Proto2SchemaProto::DecodeStructProperties(
    const types::Struct& proto) const {
  if (!proto.HasExtension(types::proto2_struct_aspect)) {
    return std::any();
  }
  const types::Proto2StructProperties& ext =
      proto.GetExtension(types::proto2_struct_aspect);
  std::vector<ProtoExtensionRange> extension_ranges;
  for (const types::ProtoExtensionRange& range : ext.extension_ranges()) {
    extension_ranges.push_back(
        ProtoExtensionRange{.start = range.start(), .end = range.end()});
  }
  return Proto2StructProperties(std::move(extension_ranges));
}

absl::StatusOr<std::any> Proto2SchemaProto::DecodeFieldProperties(
    const types::Struct::Field& proto) const {
  if (!proto.HasExtension(types::proto2_field_aspect)) {
    return std::any();
  }
  const types::Proto2FieldProperties& ext =
      proto.GetExtension(types::proto2_field_aspect);
  std::optional<bool> required;
  std::optional<bool> packed;
  if (ext.has_required()) {
    required = ext.required();
  }
  if (ext.has_packed()) {
    packed = ext.packed();
  }
  return Proto2FieldProperties(required, packed);
}

absl::Status Proto2SchemaProto::EncodeStructProperties(
    const std::any& properties, types::Struct* absl_nonnull proto) const {
  const auto* props = std::any_cast<Proto2StructProperties>(&properties);
  if (props == nullptr) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Invalid struct properties type for '", name(), "' aspect"));
  }
  types::Proto2StructProperties* ext =
      proto->MutableExtension(types::proto2_struct_aspect);
  ext->Clear();
  for (const ProtoExtensionRange& range : props->extension_ranges()) {
    types::ProtoExtensionRange* proto_range = ext->add_extension_ranges();
    proto_range->set_start(range.start);
    proto_range->set_end(range.end);
  }
  return absl::OkStatus();
}

absl::Status Proto2SchemaProto::EncodeFieldProperties(
    const std::any& properties,
    types::Struct::Field* absl_nonnull proto) const {
  const auto* props = std::any_cast<Proto2FieldProperties>(&properties);
  if (props == nullptr) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Invalid field properties type for '", name(), "' aspect"));
  }
  types::Proto2FieldProperties* ext =
      proto->MutableExtension(types::proto2_field_aspect);
  ext->Clear();
  if (props->required().has_value()) {
    ext->set_required(*props->required());
  }
  if (props->packed().has_value()) {
    ext->set_packed(*props->packed());
  }
  return absl::OkStatus();
}

// =============================================================================
// Proto3SchemaProto ("proto3")
// =============================================================================

absl::StatusOr<std::any> Proto3SchemaProto::DecodeFieldProperties(
    const types::Struct::Field& proto) const {
  if (!proto.HasExtension(types::proto3_field_aspect)) {
    return std::any();
  }
  const types::Proto3FieldProperties& ext =
      proto.GetExtension(types::proto3_field_aspect);
  std::optional<bool> optional;
  std::optional<bool> packed;
  if (ext.has_optional()) {
    optional = ext.optional();
  }
  if (ext.has_packed()) {
    packed = ext.packed();
  }
  return Proto3FieldProperties(optional, packed);
}

absl::Status Proto3SchemaProto::EncodeFieldProperties(
    const std::any& properties,
    types::Struct::Field* absl_nonnull proto) const {
  const auto* props = std::any_cast<Proto3FieldProperties>(&properties);
  if (props == nullptr) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Invalid field properties type for '", name(), "' aspect"));
  }
  types::Proto3FieldProperties* ext =
      proto->MutableExtension(types::proto3_field_aspect);
  ext->Clear();
  if (props->optional().has_value()) {
    ext->set_optional(*props->optional());
  }
  if (props->packed().has_value()) {
    ext->set_packed(*props->packed());
  }
  return absl::OkStatus();
}

}  // namespace cel
