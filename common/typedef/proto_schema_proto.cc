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

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "common/typedef/proto_schema.h"
#include "common/typedef/proto_schema.pb.h"
#include "common/typedef/typedef.pb.h"

namespace cel {

absl::StatusOr<std::any> ProtoSchemaProto::DecodeStructProperties(
    const types::Struct& proto) const {
  const bool has_ext = (version_ == ProtoVersion::kProto2)
                           ? proto.HasExtension(types::proto2_struct_aspect)
                           : proto.HasExtension(types::proto3_struct_aspect);
  if (!has_ext) {
    return std::any();
  }
  const types::ProtoStructProperties& ext =
      (version_ == ProtoVersion::kProto2)
          ? proto.GetExtension(types::proto2_struct_aspect)
          : proto.GetExtension(types::proto3_struct_aspect);
  std::optional<std::string> name;
  if (ext.has_name()) {
    name = ext.name();
  }
  return ProtoStructProperties(std::move(name));
}

absl::StatusOr<std::any> ProtoSchemaProto::DecodeFieldProperties(
    const types::Struct::Field& proto) const {
  const bool has_ext = (version_ == ProtoVersion::kProto2)
                           ? proto.HasExtension(types::proto2_field_aspect)
                           : proto.HasExtension(types::proto3_field_aspect);
  if (!has_ext) {
    return std::any();
  }
  const types::ProtoFieldProperties& ext =
      (version_ == ProtoVersion::kProto2)
          ? proto.GetExtension(types::proto2_field_aspect)
          : proto.GetExtension(types::proto3_field_aspect);
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
  const bool has_ext = (version_ == ProtoVersion::kProto2)
                           ? proto.HasExtension(types::proto2_enum_aspect)
                           : proto.HasExtension(types::proto3_enum_aspect);
  if (!has_ext) {
    return std::any();
  }
  const types::ProtoEnumProperties& ext =
      (version_ == ProtoVersion::kProto2)
          ? proto.GetExtension(types::proto2_enum_aspect)
          : proto.GetExtension(types::proto3_enum_aspect);
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
  const bool has_ext =
      (version_ == ProtoVersion::kProto2)
          ? proto.HasExtension(types::proto2_enum_constant_aspect)
          : proto.HasExtension(types::proto3_enum_constant_aspect);
  if (!has_ext) {
    return std::any();
  }
  const types::ProtoEnumConstantProperties& ext =
      (version_ == ProtoVersion::kProto2)
          ? proto.GetExtension(types::proto2_enum_constant_aspect)
          : proto.GetExtension(types::proto3_enum_constant_aspect);
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
      (version_ == ProtoVersion::kProto2)
          ? proto->MutableExtension(types::proto2_struct_aspect)
          : proto->MutableExtension(types::proto3_struct_aspect);
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
      (version_ == ProtoVersion::kProto2)
          ? proto->MutableExtension(types::proto2_field_aspect)
          : proto->MutableExtension(types::proto3_field_aspect);
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
      (version_ == ProtoVersion::kProto2)
          ? proto->MutableExtension(types::proto2_enum_aspect)
          : proto->MutableExtension(types::proto3_enum_aspect);
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
      (version_ == ProtoVersion::kProto2)
          ? proto->MutableExtension(types::proto2_enum_constant_aspect)
          : proto->MutableExtension(types::proto3_enum_constant_aspect);
  ext->Clear();
  if (props->name().has_value()) {
    ext->set_name(*props->name());
  }
  return absl::OkStatus();
}

}  // namespace cel
