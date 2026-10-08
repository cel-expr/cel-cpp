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

#include "common/typedef/json_schema_proto.h"

#include <memory>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/typedef/json_schema.h"
#include "common/typedef/json_schema.pb.h"
#include "common/typedef/schema.h"
#include "common/typedef/typedef.pb.h"

namespace cel {

absl::StatusOr<std::unique_ptr<SchemaObjectProperties>>
JsonSchemaProto::DecodeObjectProperties(const types::Object& proto) const {
  if (!proto.HasExtension(types::json_object_schema)) {
    return nullptr;
  }
  const types::JsonSchemaObjectProperties& ext =
      proto.GetExtension(types::json_object_schema);
  auto result = std::make_unique<JsonSchemaObjectProperties>();
  if (ext.has_name()) {
    result->name = ext.name();
  }
  return result;
}

absl::StatusOr<std::unique_ptr<SchemaFieldProperties>>
JsonSchemaProto::DecodeFieldProperties(
    const types::Object::Field& proto) const {
  if (!proto.HasExtension(types::json_field_schema)) {
    return nullptr;
  }
  const types::JsonSchemaFieldProperties& ext =
      proto.GetExtension(types::json_field_schema);
  auto result = std::make_unique<JsonSchemaFieldProperties>();
  if (ext.has_name()) {
    result->name = ext.name();
  }
  return result;
}

absl::StatusOr<std::unique_ptr<SchemaEnumProperties>>
JsonSchemaProto::DecodeEnumProperties(const types::Enum& proto) const {
  if (!proto.HasExtension(types::json_enum_schema)) {
    return nullptr;
  }
  const types::JsonSchemaEnumProperties& ext =
      proto.GetExtension(types::json_enum_schema);
  auto result = std::make_unique<JsonSchemaEnumProperties>();
  if (ext.has_style()) {
    result->style = ext.style();
  }
  if (ext.has_omit_unspecified()) {
    result->omit_unspecified = ext.omit_unspecified();
  }
  return result;
}

absl::StatusOr<std::unique_ptr<SchemaEnumConstantProperties>>
JsonSchemaProto::DecodeEnumConstantProperties(
    const types::Enum::EnumConstant& proto) const {
  if (!proto.HasExtension(types::json_enum_constant_schema)) {
    return nullptr;
  }
  const types::JsonSchemaEnumConstantProperties& ext =
      proto.GetExtension(types::json_enum_constant_schema);
  auto result = std::make_unique<JsonSchemaEnumConstantProperties>();
  if (ext.has_name()) {
    result->name = ext.name();
  }
  return result;
}

absl::Status JsonSchemaProto::EncodeObjectProperties(
    const SchemaObjectProperties& properties,
    types::Object* absl_nonnull proto) const {
  const auto* props =
      dynamic_cast<const JsonSchemaObjectProperties*>(&properties);
  if (props == nullptr) {
    return absl::InvalidArgumentError(
        "Invalid object properties type for 'json' schema");
  }
  types::JsonSchemaObjectProperties* ext =
      proto->MutableExtension(types::json_object_schema);
  ext->Clear();
  if (props->name.has_value()) {
    ext->set_name(*props->name);
  }
  return absl::OkStatus();
}

absl::Status JsonSchemaProto::EncodeFieldProperties(
    const SchemaFieldProperties& properties,
    types::Object::Field* absl_nonnull proto) const {
  const auto* props =
      dynamic_cast<const JsonSchemaFieldProperties*>(&properties);
  if (props == nullptr) {
    return absl::InvalidArgumentError(
        "Invalid field properties type for 'json' schema");
  }
  types::JsonSchemaFieldProperties* ext =
      proto->MutableExtension(types::json_field_schema);
  ext->Clear();
  if (props->name.has_value()) {
    ext->set_name(*props->name);
  }
  return absl::OkStatus();
}

absl::Status JsonSchemaProto::EncodeEnumProperties(
    const SchemaEnumProperties& properties,
    types::Enum* absl_nonnull proto) const {
  const auto* props =
      dynamic_cast<const JsonSchemaEnumProperties*>(&properties);
  if (props == nullptr) {
    return absl::InvalidArgumentError(
        "Invalid enum properties type for 'json' schema");
  }
  types::JsonSchemaEnumProperties* ext =
      proto->MutableExtension(types::json_enum_schema);
  ext->Clear();
  if (props->style.has_value()) {
    ext->set_style(*props->style);
  }
  if (props->omit_unspecified.has_value()) {
    ext->set_omit_unspecified(*props->omit_unspecified);
  }
  return absl::OkStatus();
}

absl::Status JsonSchemaProto::EncodeEnumConstantProperties(
    const SchemaEnumConstantProperties& properties,
    types::Enum::EnumConstant* absl_nonnull proto) const {
  const auto* props =
      dynamic_cast<const JsonSchemaEnumConstantProperties*>(&properties);
  if (props == nullptr) {
    return absl::InvalidArgumentError(
        "Invalid enum constant properties type for 'json' schema");
  }
  types::JsonSchemaEnumConstantProperties* ext =
      proto->MutableExtension(types::json_enum_constant_schema);
  ext->Clear();
  if (props->name.has_value()) {
    ext->set_name(*props->name);
  }
  return absl::OkStatus();
}

}  // namespace cel
