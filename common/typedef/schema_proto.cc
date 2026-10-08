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

#include "common/typedef/schema_proto.h"

#include <memory>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "common/typedef/schema.h"
#include "common/typedef/typedef.pb.h"

namespace cel {

absl::StatusOr<std::unique_ptr<SchemaObjectProperties>>
SchemaProto::DecodeObjectProperties(const types::Object& proto) const {
  return nullptr;
}

absl::StatusOr<std::unique_ptr<SchemaFieldProperties>>
SchemaProto::DecodeFieldProperties(const types::Object::Field& proto) const {
  return nullptr;
}

absl::StatusOr<std::unique_ptr<SchemaEnumProperties>>
SchemaProto::DecodeEnumProperties(const types::Enum& proto) const {
  return nullptr;
}

absl::StatusOr<std::unique_ptr<SchemaEnumConstantProperties>>
SchemaProto::DecodeEnumConstantProperties(
    const types::Enum::EnumConstant& proto) const {
  return nullptr;
}

absl::Status SchemaProto::EncodeObjectProperties(
    const SchemaObjectProperties& properties,
    types::Object* absl_nonnull proto) const {
  return absl::UnimplementedError(absl::StrCat(
      "Custom object properties are not supported for schema '", name(), "'"));
}

absl::Status SchemaProto::EncodeFieldProperties(
    const SchemaFieldProperties& properties,
    types::Object::Field* absl_nonnull proto) const {
  return absl::UnimplementedError(absl::StrCat(
      "Custom field properties are not supported for schema '", name(), "'"));
}

absl::Status SchemaProto::EncodeEnumProperties(
    const SchemaEnumProperties& properties,
    types::Enum* absl_nonnull proto) const {
  return absl::UnimplementedError(absl::StrCat(
      "Custom enum properties are not supported for schema '", name(), "'"));
}

absl::Status SchemaProto::EncodeEnumConstantProperties(
    const SchemaEnumConstantProperties& properties,
    types::Enum::EnumConstant* absl_nonnull proto) const {
  return absl::UnimplementedError(absl::StrCat(
      "Custom enum constant properties are not supported for schema '", name(),
      "'"));
}

SchemaProtoRegistry::SchemaProtoRegistry(
    std::vector<std::unique_ptr<const SchemaProto>> schemas) {
  schemas_.reserve(schemas.size());
  for (std::unique_ptr<const SchemaProto>& schema : schemas) {
    Add(std::move(schema));
  }
}

const SchemaProto* absl_nullable SchemaProtoRegistry::Find(
    absl::string_view schema_name) const {
  auto it = schemas_.find(schema_name);
  if (it == schemas_.end()) {
    return nullptr;
  }
  return it->second.get();
}

}  // namespace cel
