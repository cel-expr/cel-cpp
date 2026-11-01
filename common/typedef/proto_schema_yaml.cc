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

#include "common/typedef/proto_schema_yaml.h"

#include <memory>
#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "common/typedef/proto_schema.h"
#include "common/typedef/schema.h"
#include "common/typedef/yaml_helpers.h"
#include "internal/status_macros.h"
#include "yaml-cpp/emitter.h"
#include "yaml-cpp/emittermanip.h"
#include "yaml-cpp/node/node.h"
#include "yaml-cpp/yaml.h"  // IWYU pragma: keep

namespace cel {
namespace {

absl::StatusOr<std::unique_ptr<SchemaObjectProperties>>
DecodeProtoObjectPropertiesImpl(absl::string_view schema_name,
                                absl::string_view yaml,
                                const YAML::Node& node) {
  if (!node.IsDefined() || !node.IsMap()) {
    return internal::YamlError(
        yaml, node,
        absl::StrCat("Node '", schema_name, "' object schema is not a map"));
  }
  auto result = std::make_unique<ProtoObjectProperties>();
  for (const auto& kv : node) {
    if (!kv.first.IsScalar()) {
      return internal::YamlError(
          yaml, kv.first,
          absl::StrCat("Property key in '", schema_name,
                       "' object schema is not a string"));
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
          absl::StrCat("Unsupported '", schema_name, "' object property: '",
                       key, "'"));
    }
  }
  return result;
}

absl::StatusOr<std::unique_ptr<SchemaFieldProperties>>
DecodeProtoFieldPropertiesImpl(absl::string_view schema_name,
                               absl::string_view yaml, const YAML::Node& node) {
  if (!node.IsDefined() || !node.IsMap()) {
    return internal::YamlError(
        yaml, node,
        absl::StrCat("Node '", schema_name, "' field schema is not a map"));
  }
  auto result = std::make_unique<ProtoFieldProperties>();
  for (const auto& kv : node) {
    if (!kv.first.IsScalar()) {
      return internal::YamlError(
          yaml, kv.first,
          absl::StrCat("Property key in '", schema_name,
                       "' field schema is not a string"));
    }
    std::string key = internal::GetString(yaml, kv.first);
    const YAML::Node& val = kv.second;
    if (key == "name") {
      if (!val.IsScalar()) {
        return internal::YamlError(yaml, val, "Node 'name' is not a string");
      }
      result->name = internal::GetString(yaml, val);
    } else if (key == "id") {
      CEL_ASSIGN_OR_RETURN(result->id, internal::GetInt32(yaml, "id", val));
    } else if (key == "json_name") {
      if (!val.IsScalar()) {
        return internal::YamlError(yaml, val,
                                   "Node 'json_name' is not a string");
      }
      result->json_name = internal::GetString(yaml, val);
    } else {
      return internal::YamlError(yaml, kv.first,
                                 absl::StrCat("Unsupported '", schema_name,
                                              "' field property: '", key, "'"));
    }
  }
  return result;
}

absl::StatusOr<std::unique_ptr<SchemaEnumProperties>>
DecodeProtoEnumPropertiesImpl(absl::string_view schema_name,
                              absl::string_view yaml, const YAML::Node& node) {
  if (!node.IsDefined() || !node.IsMap()) {
    return internal::YamlError(
        yaml, node,
        absl::StrCat("Node '", schema_name, "' enum schema is not a map"));
  }
  auto result = std::make_unique<ProtoEnumProperties>();
  for (const auto& kv : node) {
    if (!kv.first.IsScalar()) {
      return internal::YamlError(yaml, kv.first,
                                 absl::StrCat("Property key in '", schema_name,
                                              "' enum schema is not a string"));
    }
    std::string key = internal::GetString(yaml, kv.first);
    const YAML::Node& val = kv.second;
    if (key == "name") {
      if (!val.IsScalar()) {
        return internal::YamlError(yaml, val, "Node 'name' is not a string");
      }
      result->name = internal::GetString(yaml, val);
    } else if (key == "allow_alias") {
      CEL_ASSIGN_OR_RETURN(result->allow_alias,
                           internal::GetBool(yaml, "allow_alias", val));
    } else {
      return internal::YamlError(yaml, kv.first,
                                 absl::StrCat("Unsupported '", schema_name,
                                              "' enum property: '", key, "'"));
    }
  }
  return result;
}

absl::StatusOr<std::unique_ptr<SchemaEnumConstantProperties>>
DecodeProtoEnumConstantPropertiesImpl(absl::string_view schema_name,
                                      absl::string_view yaml,
                                      const YAML::Node& node) {
  if (!node.IsDefined() || !node.IsMap()) {
    return internal::YamlError(
        yaml, node,
        absl::StrCat("Node '", schema_name,
                     "' enum constant schema is not a map"));
  }
  auto result = std::make_unique<ProtoEnumConstantProperties>();
  for (const auto& kv : node) {
    if (!kv.first.IsScalar()) {
      return internal::YamlError(
          yaml, kv.first,
          absl::StrCat("Property key in '", schema_name,
                       "' enum constant schema is not a string"));
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
          absl::StrCat("Unsupported '", schema_name,
                       "' enum constant property: '", key, "'"));
    }
  }
  return result;
}

absl::Status EncodeProtoObjectPropertiesImpl(
    absl::string_view schema_name, const SchemaObjectProperties& properties,
    YAML::Emitter& out) {
  const auto* props = dynamic_cast<const ProtoObjectProperties*>(&properties);
  if (props == nullptr) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Invalid object properties type for '", schema_name, "' schema"));
  }
  out << YAML::BeginMap;
  if (props->name.has_value()) {
    out << YAML::Key << "name" << YAML::Value << YAML::DoubleQuoted
        << *props->name;
  }
  out << YAML::EndMap;
  return absl::OkStatus();
}

absl::Status EncodeProtoFieldPropertiesImpl(
    absl::string_view schema_name, const SchemaFieldProperties& properties,
    YAML::Emitter& out) {
  const auto* props = dynamic_cast<const ProtoFieldProperties*>(&properties);
  if (props == nullptr) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Invalid field properties type for '", schema_name, "' schema"));
  }
  out << YAML::BeginMap;
  if (props->name.has_value()) {
    out << YAML::Key << "name" << YAML::Value << YAML::DoubleQuoted
        << *props->name;
  }
  if (props->id.has_value()) {
    out << YAML::Key << "id" << YAML::Value << *props->id;
  }
  if (props->json_name.has_value()) {
    out << YAML::Key << "json_name" << YAML::Value << YAML::DoubleQuoted
        << *props->json_name;
  }
  out << YAML::EndMap;
  return absl::OkStatus();
}

absl::Status EncodeProtoEnumPropertiesImpl(
    absl::string_view schema_name, const SchemaEnumProperties& properties,
    YAML::Emitter& out) {
  const auto* props = dynamic_cast<const ProtoEnumProperties*>(&properties);
  if (props == nullptr) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Invalid enum properties type for '", schema_name, "' schema"));
  }
  out << YAML::BeginMap;
  if (props->name.has_value()) {
    out << YAML::Key << "name" << YAML::Value << YAML::DoubleQuoted
        << *props->name;
  }
  if (props->allow_alias.has_value()) {
    out << YAML::Key << "allow_alias" << YAML::Value << *props->allow_alias;
  }
  out << YAML::EndMap;
  return absl::OkStatus();
}

absl::Status EncodeProtoEnumConstantPropertiesImpl(
    absl::string_view schema_name,
    const SchemaEnumConstantProperties& properties, YAML::Emitter& out) {
  const auto* props =
      dynamic_cast<const ProtoEnumConstantProperties*>(&properties);
  if (props == nullptr) {
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid enum constant properties type for '", schema_name,
                     "' schema"));
  }
  out << YAML::BeginMap;
  if (props->name.has_value()) {
    out << YAML::Key << "name" << YAML::Value << YAML::DoubleQuoted
        << *props->name;
  }
  out << YAML::EndMap;
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<std::unique_ptr<SchemaObjectProperties>>
Proto2SchemaYaml::DecodeObjectProperties(absl::string_view yaml,
                                         const YAML::Node& node) const {
  return DecodeProtoObjectPropertiesImpl(kName, yaml, node);
}

absl::StatusOr<std::unique_ptr<SchemaFieldProperties>>
Proto2SchemaYaml::DecodeFieldProperties(absl::string_view yaml,
                                        const YAML::Node& node) const {
  return DecodeProtoFieldPropertiesImpl(kName, yaml, node);
}

absl::StatusOr<std::unique_ptr<SchemaEnumProperties>>
Proto2SchemaYaml::DecodeEnumProperties(absl::string_view yaml,
                                       const YAML::Node& node) const {
  return DecodeProtoEnumPropertiesImpl(kName, yaml, node);
}

absl::StatusOr<std::unique_ptr<SchemaEnumConstantProperties>>
Proto2SchemaYaml::DecodeEnumConstantProperties(absl::string_view yaml,
                                               const YAML::Node& node) const {
  return DecodeProtoEnumConstantPropertiesImpl(kName, yaml, node);
}

absl::Status Proto2SchemaYaml::EncodeObjectProperties(
    const SchemaObjectProperties& properties, YAML::Emitter& out) const {
  return EncodeProtoObjectPropertiesImpl(kName, properties, out);
}

absl::Status Proto2SchemaYaml::EncodeFieldProperties(
    const SchemaFieldProperties& properties, YAML::Emitter& out) const {
  return EncodeProtoFieldPropertiesImpl(kName, properties, out);
}

absl::Status Proto2SchemaYaml::EncodeEnumProperties(
    const SchemaEnumProperties& properties, YAML::Emitter& out) const {
  return EncodeProtoEnumPropertiesImpl(kName, properties, out);
}

absl::Status Proto2SchemaYaml::EncodeEnumConstantProperties(
    const SchemaEnumConstantProperties& properties, YAML::Emitter& out) const {
  return EncodeProtoEnumConstantPropertiesImpl(kName, properties, out);
}

absl::StatusOr<std::unique_ptr<SchemaObjectProperties>>
Proto3SchemaYaml::DecodeObjectProperties(absl::string_view yaml,
                                         const YAML::Node& node) const {
  return DecodeProtoObjectPropertiesImpl(kName, yaml, node);
}

absl::StatusOr<std::unique_ptr<SchemaFieldProperties>>
Proto3SchemaYaml::DecodeFieldProperties(absl::string_view yaml,
                                        const YAML::Node& node) const {
  return DecodeProtoFieldPropertiesImpl(kName, yaml, node);
}

absl::StatusOr<std::unique_ptr<SchemaEnumProperties>>
Proto3SchemaYaml::DecodeEnumProperties(absl::string_view yaml,
                                       const YAML::Node& node) const {
  return DecodeProtoEnumPropertiesImpl(kName, yaml, node);
}

absl::StatusOr<std::unique_ptr<SchemaEnumConstantProperties>>
Proto3SchemaYaml::DecodeEnumConstantProperties(absl::string_view yaml,
                                               const YAML::Node& node) const {
  return DecodeProtoEnumConstantPropertiesImpl(kName, yaml, node);
}

absl::Status Proto3SchemaYaml::EncodeObjectProperties(
    const SchemaObjectProperties& properties, YAML::Emitter& out) const {
  return EncodeProtoObjectPropertiesImpl(kName, properties, out);
}

absl::Status Proto3SchemaYaml::EncodeFieldProperties(
    const SchemaFieldProperties& properties, YAML::Emitter& out) const {
  return EncodeProtoFieldPropertiesImpl(kName, properties, out);
}

absl::Status Proto3SchemaYaml::EncodeEnumProperties(
    const SchemaEnumProperties& properties, YAML::Emitter& out) const {
  return EncodeProtoEnumPropertiesImpl(kName, properties, out);
}

absl::Status Proto3SchemaYaml::EncodeEnumConstantProperties(
    const SchemaEnumConstantProperties& properties, YAML::Emitter& out) const {
  return EncodeProtoEnumConstantPropertiesImpl(kName, properties, out);
}

}  // namespace cel
