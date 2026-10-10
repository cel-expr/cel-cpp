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

#include <memory>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "common/typedef/proto_schema.h"
#include "common/typedef/proto_schema.pb.h"
#include "common/typedef/schema.h"
#include "common/typedef/typedef.pb.h"

namespace cel {

absl::StatusOr<std::unique_ptr<SchemaObjectProperties>>
ProtoSchemaProto::DecodeObjectProperties(const types::Object& proto) const {
  const bool has_ext = (version_ == ProtoVersion::kProto2)
                           ? proto.HasExtension(types::proto2_object_schema)
                           : proto.HasExtension(types::proto3_object_schema);
  if (!has_ext) {
    return nullptr;
  }
  const types::ProtoObjectProperties& ext =
      (version_ == ProtoVersion::kProto2)
          ? proto.GetExtension(types::proto2_object_schema)
          : proto.GetExtension(types::proto3_object_schema);
  auto result = std::make_unique<ProtoObjectProperties>();
  if (ext.has_name()) {
    result->name = ext.name();
  }
  return result;
}

absl::StatusOr<std::unique_ptr<SchemaFieldProperties>>
ProtoSchemaProto::DecodeFieldProperties(
    const types::Object::Field& proto) const {
  const bool has_ext = (version_ == ProtoVersion::kProto2)
                           ? proto.HasExtension(types::proto2_field_schema)
                           : proto.HasExtension(types::proto3_field_schema);
  if (!has_ext) {
    return nullptr;
  }
  const types::ProtoFieldProperties& ext =
      (version_ == ProtoVersion::kProto2)
          ? proto.GetExtension(types::proto2_field_schema)
          : proto.GetExtension(types::proto3_field_schema);
  auto result = std::make_unique<ProtoFieldProperties>();
  if (ext.has_name()) {
    result->name = ext.name();
  }
  if (ext.has_id()) {
    result->id = ext.id();
  }
  if (ext.has_json_name()) {
    result->json_name = ext.json_name();
  }
  return result;
}

absl::StatusOr<std::unique_ptr<SchemaEnumProperties>>
ProtoSchemaProto::DecodeEnumProperties(const types::Enum& proto) const {
  const bool has_ext = (version_ == ProtoVersion::kProto2)
                           ? proto.HasExtension(types::proto2_enum_schema)
                           : proto.HasExtension(types::proto3_enum_schema);
  if (!has_ext) {
    return nullptr;
  }
  const types::ProtoEnumProperties& ext =
      (version_ == ProtoVersion::kProto2)
          ? proto.GetExtension(types::proto2_enum_schema)
          : proto.GetExtension(types::proto3_enum_schema);
  auto result = std::make_unique<ProtoEnumProperties>();
  if (ext.has_name()) {
    result->name = ext.name();
  }
  if (ext.has_allow_alias()) {
    result->allow_alias = ext.allow_alias();
  }
  return result;
}

absl::StatusOr<std::unique_ptr<SchemaEnumConstantProperties>>
ProtoSchemaProto::DecodeEnumConstantProperties(
    const types::Enum::EnumConstant& proto) const {
  const bool has_ext =
      (version_ == ProtoVersion::kProto2)
          ? proto.HasExtension(types::proto2_enum_constant_schema)
          : proto.HasExtension(types::proto3_enum_constant_schema);
  if (!has_ext) {
    return nullptr;
  }
  const types::ProtoEnumConstantProperties& ext =
      (version_ == ProtoVersion::kProto2)
          ? proto.GetExtension(types::proto2_enum_constant_schema)
          : proto.GetExtension(types::proto3_enum_constant_schema);
  auto result = std::make_unique<ProtoEnumConstantProperties>();
  if (ext.has_name()) {
    result->name = ext.name();
  }
  return result;
}

absl::Status ProtoSchemaProto::EncodeObjectProperties(
    const SchemaObjectProperties& properties,
    types::Object* absl_nonnull proto) const {
  const auto* props = dynamic_cast<const ProtoObjectProperties*>(&properties);
  if (props == nullptr) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Invalid object properties type for '", name(), "' schema"));
  }
  types::ProtoObjectProperties* ext =
      (version_ == ProtoVersion::kProto2)
          ? proto->MutableExtension(types::proto2_object_schema)
          : proto->MutableExtension(types::proto3_object_schema);
  ext->Clear();
  if (props->name.has_value()) {
    ext->set_name(*props->name);
  }
  return absl::OkStatus();
}

absl::Status ProtoSchemaProto::EncodeFieldProperties(
    const SchemaFieldProperties& properties,
    types::Object::Field* absl_nonnull proto) const {
  const auto* props = dynamic_cast<const ProtoFieldProperties*>(&properties);
  if (props == nullptr) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Invalid field properties type for '", name(), "' schema"));
  }
  types::ProtoFieldProperties* ext =
      (version_ == ProtoVersion::kProto2)
          ? proto->MutableExtension(types::proto2_field_schema)
          : proto->MutableExtension(types::proto3_field_schema);
  ext->Clear();
  if (props->name.has_value()) {
    ext->set_name(*props->name);
  }
  if (props->id.has_value()) {
    ext->set_id(*props->id);
  }
  if (props->json_name.has_value()) {
    ext->set_json_name(*props->json_name);
  }
  return absl::OkStatus();
}

absl::Status ProtoSchemaProto::EncodeEnumProperties(
    const SchemaEnumProperties& properties,
    types::Enum* absl_nonnull proto) const {
  const auto* props = dynamic_cast<const ProtoEnumProperties*>(&properties);
  if (props == nullptr) {
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid enum properties type for '", name(), "' schema"));
  }
  types::ProtoEnumProperties* ext =
      (version_ == ProtoVersion::kProto2)
          ? proto->MutableExtension(types::proto2_enum_schema)
          : proto->MutableExtension(types::proto3_enum_schema);
  ext->Clear();
  if (props->name.has_value()) {
    ext->set_name(*props->name);
  }
  if (props->allow_alias.has_value()) {
    ext->set_allow_alias(*props->allow_alias);
  }
  return absl::OkStatus();
}

absl::Status ProtoSchemaProto::EncodeEnumConstantProperties(
    const SchemaEnumConstantProperties& properties,
    types::Enum::EnumConstant* absl_nonnull proto) const {
  const auto* props =
      dynamic_cast<const ProtoEnumConstantProperties*>(&properties);
  if (props == nullptr) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Invalid enum constant properties type for '", name(), "' schema"));
  }
  types::ProtoEnumConstantProperties* ext =
      (version_ == ProtoVersion::kProto2)
          ? proto->MutableExtension(types::proto2_enum_constant_schema)
          : proto->MutableExtension(types::proto3_enum_constant_schema);
  ext->Clear();
  if (props->name.has_value()) {
    ext->set_name(*props->name);
  }
  return absl::OkStatus();
}

}  // namespace cel
