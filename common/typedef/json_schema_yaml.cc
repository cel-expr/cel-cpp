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

#include "common/typedef/json_schema_yaml.h"

#include <memory>
#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "common/typedef/json_schema.h"
#include "common/typedef/schema.h"
#include "common/typedef/yaml_helpers.h"
#include "internal/status_macros.h"
#include "yaml-cpp/emitter.h"
#include "yaml-cpp/emittermanip.h"
#include "yaml-cpp/node/node.h"
#include "yaml-cpp/yaml.h"  // IWYU pragma: keep

namespace cel {

absl::StatusOr<std::unique_ptr<SchemaObjectProperties>>
JsonSchemaYaml::DecodeObjectProperties(absl::string_view yaml,
                                       const YAML::Node& node) const {
  if (!node.IsDefined() || !node.IsMap()) {
    return internal::YamlError(yaml, node,
                               "Node 'json' object schema is not a map");
  }
  auto result = std::make_unique<JsonSchemaObjectProperties>();
  for (const auto& kv : node) {
    if (!kv.first.IsScalar()) {
      return internal::YamlError(
          yaml, kv.first,
          "Property key in 'json' object schema is not a string");
    }
    std::string key = internal::GetString(yaml, kv.first);
    const YAML::Node& val = kv.second;
    if (key == "name") {
      if (!val.IsScalar()) {
        return internal::YamlError(yaml, val, "Node 'name' is not a string");
      }
      result->name = internal::GetString(yaml, val);
    } else {
      return internal::YamlError(
          yaml, kv.first,
          absl::StrCat("Unsupported 'json' object property: '", key, "'"));
    }
  }
  return result;
}

absl::StatusOr<std::unique_ptr<SchemaFieldProperties>>
JsonSchemaYaml::DecodeFieldProperties(absl::string_view yaml,
                                      const YAML::Node& node) const {
  if (!node.IsDefined() || !node.IsMap()) {
    return internal::YamlError(yaml, node,
                               "Node 'json' field schema is not a map");
  }
  auto result = std::make_unique<JsonSchemaFieldProperties>();
  for (const auto& kv : node) {
    if (!kv.first.IsScalar()) {
      return internal::YamlError(
          yaml, kv.first,
          "Property key in 'json' field schema is not a string");
    }
    std::string key = internal::GetString(yaml, kv.first);
    const YAML::Node& val = kv.second;
    if (key == "name") {
      if (!val.IsScalar()) {
        return internal::YamlError(yaml, val, "Node 'name' is not a string");
      }
      result->name = internal::GetString(yaml, val);
    } else {
      return internal::YamlError(
          yaml, kv.first,
          absl::StrCat("Unsupported 'json' field property: '", key, "'"));
    }
  }
  return result;
}

absl::StatusOr<std::unique_ptr<SchemaEnumProperties>>
JsonSchemaYaml::DecodeEnumProperties(absl::string_view yaml,
                                     const YAML::Node& node) const {
  if (!node.IsDefined() || !node.IsMap()) {
    return internal::YamlError(yaml, node,
                               "Node 'json' enum schema is not a map");
  }
  auto result = std::make_unique<JsonSchemaEnumProperties>();
  for (const auto& kv : node) {
    if (!kv.first.IsScalar()) {
      return internal::YamlError(
          yaml, kv.first, "Property key in 'json' enum schema is not a string");
    }
    std::string key = internal::GetString(yaml, kv.first);
    const YAML::Node& val = kv.second;
    if (key == "style") {
      if (!val.IsScalar()) {
        return internal::YamlError(yaml, val, "Node 'style' is not a string");
      }
      result->style = internal::GetString(yaml, val);
    } else if (key == "omit_unspecified") {
      CEL_ASSIGN_OR_RETURN(result->omit_unspecified,
                           internal::GetBool(yaml, "omit_unspecified", val));
    } else {
      return internal::YamlError(
          yaml, kv.first,
          absl::StrCat("Unsupported 'json' enum property: '", key, "'"));
    }
  }
  return result;
}

absl::StatusOr<std::unique_ptr<SchemaEnumConstantProperties>>
JsonSchemaYaml::DecodeEnumConstantProperties(absl::string_view yaml,
                                             const YAML::Node& node) const {
  if (!node.IsDefined() || !node.IsMap()) {
    return internal::YamlError(yaml, node,
                               "Node 'json' enum constant schema is not a map");
  }
  auto result = std::make_unique<JsonSchemaEnumConstantProperties>();
  for (const auto& kv : node) {
    if (!kv.first.IsScalar()) {
      return internal::YamlError(
          yaml, kv.first,
          "Property key in 'json' enum constant schema is not a string");
    }
    std::string key = internal::GetString(yaml, kv.first);
    const YAML::Node& val = kv.second;
    if (key == "name") {
      if (!val.IsScalar()) {
        return internal::YamlError(yaml, val, "Node 'name' is not a string");
      }
      result->name = internal::GetString(yaml, val);
    } else {
      return internal::YamlError(
          yaml, kv.first,
          absl::StrCat("Unsupported 'json' enum constant property: '", key,
                       "'"));
    }
  }
  return result;
}

absl::Status JsonSchemaYaml::EncodeObjectProperties(
    const SchemaObjectProperties& properties, YAML::Emitter& out) const {
  const auto* props =
      dynamic_cast<const JsonSchemaObjectProperties*>(&properties);
  if (props == nullptr) {
    return absl::InvalidArgumentError(
        "Invalid object properties type for 'json' schema");
  }
  out << YAML::BeginMap;
  if (props->name.has_value()) {
    out << YAML::Key << "name" << YAML::Value << YAML::DoubleQuoted
        << *props->name;
  }
  out << YAML::EndMap;
  return absl::OkStatus();
}

absl::Status JsonSchemaYaml::EncodeFieldProperties(
    const SchemaFieldProperties& properties, YAML::Emitter& out) const {
  const auto* props =
      dynamic_cast<const JsonSchemaFieldProperties*>(&properties);
  if (props == nullptr) {
    return absl::InvalidArgumentError(
        "Invalid field properties type for 'json' schema");
  }
  out << YAML::BeginMap;
  if (props->name.has_value()) {
    out << YAML::Key << "name" << YAML::Value << YAML::DoubleQuoted
        << *props->name;
  }
  out << YAML::EndMap;
  return absl::OkStatus();
}

absl::Status JsonSchemaYaml::EncodeEnumProperties(
    const SchemaEnumProperties& properties, YAML::Emitter& out) const {
  const auto* props =
      dynamic_cast<const JsonSchemaEnumProperties*>(&properties);
  if (props == nullptr) {
    return absl::InvalidArgumentError(
        "Invalid enum properties type for 'json' schema");
  }
  out << YAML::BeginMap;
  if (props->style.has_value()) {
    out << YAML::Key << "style" << YAML::Value << YAML::DoubleQuoted
        << *props->style;
  }
  if (props->omit_unspecified.has_value()) {
    out << YAML::Key << "omit_unspecified" << YAML::Value
        << *props->omit_unspecified;
  }
  out << YAML::EndMap;
  return absl::OkStatus();
}

absl::Status JsonSchemaYaml::EncodeEnumConstantProperties(
    const SchemaEnumConstantProperties& properties, YAML::Emitter& out) const {
  const auto* props =
      dynamic_cast<const JsonSchemaEnumConstantProperties*>(&properties);
  if (props == nullptr) {
    return absl::InvalidArgumentError(
        "Invalid enum constant properties type for 'json' schema");
  }
  out << YAML::BeginMap;
  if (props->name.has_value()) {
    out << YAML::Key << "name" << YAML::Value << YAML::DoubleQuoted
        << *props->name;
  }
  out << YAML::EndMap;
  return absl::OkStatus();
}

}  // namespace cel
