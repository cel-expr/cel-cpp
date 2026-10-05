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

#include "common/typedef/schema_yaml.h"

#include <memory>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "common/typedef/schema.h"
#include "common/typedef/yaml_helpers.h"

namespace cel {

absl::StatusOr<std::unique_ptr<SchemaObjectProperties>>
SchemaYaml::DecodeObjectProperties(absl::string_view yaml,
                                   const YAML::Node& node) const {
  if (internal::IsEmptyYamlNode(node)) {
    return nullptr;
  }
  return internal::YamlError(
      yaml, node,
      absl::StrCat("Custom object properties are not supported for schema '",
                   name(), "'"));
}

absl::StatusOr<std::unique_ptr<SchemaFieldProperties>>
SchemaYaml::DecodeFieldProperties(absl::string_view yaml,
                                  const YAML::Node& node) const {
  if (internal::IsEmptyYamlNode(node)) {
    return nullptr;
  }
  return internal::YamlError(
      yaml, node,
      absl::StrCat("Custom field properties are not supported for schema '",
                   name(), "'"));
}

absl::StatusOr<std::unique_ptr<SchemaEnumProperties>>
SchemaYaml::DecodeEnumProperties(absl::string_view yaml,
                                 const YAML::Node& node) const {
  if (internal::IsEmptyYamlNode(node)) {
    return nullptr;
  }
  return internal::YamlError(
      yaml, node,
      absl::StrCat("Custom enum properties are not supported for schema '",
                   name(), "'"));
}

absl::StatusOr<std::unique_ptr<SchemaEnumConstantProperties>>
SchemaYaml::DecodeEnumConstantProperties(absl::string_view yaml,
                                         const YAML::Node& node) const {
  if (internal::IsEmptyYamlNode(node)) {
    return nullptr;
  }
  return internal::YamlError(
      yaml, node,
      absl::StrCat(
          "Custom enum constant properties are not supported for schema '",
          name(), "'"));
}

absl::Status SchemaYaml::EncodeObjectProperties(
    const SchemaObjectProperties& properties, YAML::Emitter& out) const {
  return absl::UnimplementedError(absl::StrCat(
      "Custom object properties are not supported for schema '", name(), "'"));
}

absl::Status SchemaYaml::EncodeFieldProperties(
    const SchemaFieldProperties& properties, YAML::Emitter& out) const {
  return absl::UnimplementedError(absl::StrCat(
      "Custom field properties are not supported for schema '", name(), "'"));
}

absl::Status SchemaYaml::EncodeEnumProperties(
    const SchemaEnumProperties& properties, YAML::Emitter& out) const {
  return absl::UnimplementedError(absl::StrCat(
      "Custom enum properties are not supported for schema '", name(), "'"));
}

absl::Status SchemaYaml::EncodeEnumConstantProperties(
    const SchemaEnumConstantProperties& properties, YAML::Emitter& out) const {
  return absl::UnimplementedError(absl::StrCat(
      "Custom enum constant properties are not supported for schema '", name(),
      "'"));
}

SchemaYamlRegistry::SchemaYamlRegistry(
    std::vector<std::unique_ptr<const SchemaYaml>> schemas) {
  schemas_.reserve(schemas.size());
  for (std::unique_ptr<const SchemaYaml>& schema : schemas) {
    Add(std::move(schema));
  }
}

const SchemaYaml* absl_nullable SchemaYamlRegistry::Find(
    absl::string_view schema_name) const {
  auto it = schemas_.find(schema_name);
  if (it == schemas_.end()) {
    return nullptr;
  }
  return it->second.get();
}

}  // namespace cel
