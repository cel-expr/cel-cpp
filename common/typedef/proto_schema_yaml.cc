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

#include <any>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "common/typedef/proto_schema.h"
#include "common/typedef/yaml_helpers.h"
#include "internal/status_macros.h"
#include "yaml-cpp/emitter.h"
#include "yaml-cpp/emittermanip.h"
#include "yaml-cpp/node/node.h"
#include "yaml-cpp/yaml.h"  // IWYU pragma: keep

namespace cel {
namespace {

absl::StatusOr<std::string> GetNonEmptyString(absl::string_view yaml,
                                              absl::string_view key,
                                              const YAML::Node& node) {
  if (!node.IsDefined() || !node.IsScalar()) {
    return typedef_internal::YamlError(
        yaml, node, absl::StrCat("Node '", key, "' is not a string"));
  }
  std::string str = typedef_internal::GetString(node);
  if (str.empty()) {
    return typedef_internal::YamlError(
        yaml, node, absl::StrCat("Node '", key, "' cannot be empty"));
  }
  return str;
}

}  // namespace

// =============================================================================
// ProtoSchemaYaml ("proto")
// =============================================================================

absl::StatusOr<std::any> ProtoSchemaYaml::DecodeStructProperties(
    absl::string_view yaml, const YAML::Node& node) const {
  if (!node.IsDefined() || !node.IsMap()) {
    return typedef_internal::YamlError(
        yaml, node,
        absl::StrCat("Node '", name(), "' struct aspect is not a map"));
  }
  std::optional<std::string> struct_name;
  for (const auto& kv : node) {
    if (!kv.first.IsScalar()) {
      return typedef_internal::YamlError(
          yaml, kv.first,
          absl::StrCat("Property key in '", name(),
                       "' struct aspect is not a string"));
    }
    std::string key = typedef_internal::GetString(kv.first);
    const YAML::Node& val = kv.second;
    if (key == "name") {
      CEL_ASSIGN_OR_RETURN(struct_name, GetNonEmptyString(yaml, "name", val));
    } else {
      return typedef_internal::YamlError(
          yaml, kv.first,
          absl::StrCat("Unsupported '", name(), "' struct property: '", key,
                       "'"));
    }
  }
  return ProtoStructProperties(std::move(struct_name));
}

absl::StatusOr<std::any> ProtoSchemaYaml::DecodeFieldProperties(
    absl::string_view yaml, const YAML::Node& node) const {
  if (!node.IsDefined() || !node.IsMap()) {
    return typedef_internal::YamlError(
        yaml, node,
        absl::StrCat("Node '", name(), "' field aspect is not a map"));
  }
  std::optional<std::string> field_name;
  std::optional<int32_t> id;
  std::optional<std::string> json_name;
  for (const auto& kv : node) {
    if (!kv.first.IsScalar()) {
      return typedef_internal::YamlError(
          yaml, kv.first,
          absl::StrCat("Property key in '", name(),
                       "' field aspect is not a string"));
    }
    std::string key = typedef_internal::GetString(kv.first);
    const YAML::Node& val = kv.second;
    if (key == "name") {
      CEL_ASSIGN_OR_RETURN(field_name, GetNonEmptyString(yaml, "name", val));
    } else if (key == "id") {
      CEL_ASSIGN_OR_RETURN(id, typedef_internal::GetInt32(yaml, "id", val));
      if (*id <= 0 || (*id >= 19000 && *id <= 19999) || *id > 536870911) {
        return typedef_internal::YamlError(yaml, val,
                                           "Invalid protobuf field ID");
      }
    } else if (key == "json_name") {
      CEL_ASSIGN_OR_RETURN(json_name,
                           GetNonEmptyString(yaml, "json_name", val));
    } else {
      return typedef_internal::YamlError(
          yaml, kv.first,
          absl::StrCat("Unsupported '", name(), "' field property: '", key,
                       "'"));
    }
  }
  return ProtoFieldProperties(std::move(field_name), id, std::move(json_name));
}

absl::StatusOr<std::any> ProtoSchemaYaml::DecodeEnumProperties(
    absl::string_view yaml, const YAML::Node& node) const {
  if (!node.IsDefined() || !node.IsMap()) {
    return typedef_internal::YamlError(
        yaml, node,
        absl::StrCat("Node '", name(), "' enum aspect is not a map"));
  }
  std::optional<std::string> enum_name;
  std::optional<bool> allow_alias;
  for (const auto& kv : node) {
    if (!kv.first.IsScalar()) {
      return typedef_internal::YamlError(
          yaml, kv.first,
          absl::StrCat("Property key in '", name(),
                       "' enum aspect is not a string"));
    }
    std::string key = typedef_internal::GetString(kv.first);
    const YAML::Node& val = kv.second;
    if (key == "name") {
      CEL_ASSIGN_OR_RETURN(enum_name, GetNonEmptyString(yaml, "name", val));
    } else if (key == "allow_alias") {
      CEL_ASSIGN_OR_RETURN(allow_alias,
                           typedef_internal::GetBool(yaml, "allow_alias", val));
    } else {
      return typedef_internal::YamlError(
          yaml, kv.first,
          absl::StrCat("Unsupported '", name(), "' enum property: '", key,
                       "'"));
    }
  }
  return ProtoEnumProperties(std::move(enum_name), allow_alias);
}

absl::StatusOr<std::any> ProtoSchemaYaml::DecodeEnumConstantProperties(
    absl::string_view yaml, const YAML::Node& node) const {
  if (!node.IsDefined() || !node.IsMap()) {
    return typedef_internal::YamlError(
        yaml, node,
        absl::StrCat("Node '", name(), "' enum constant aspect is not a map"));
  }
  std::optional<std::string> constant_name;
  for (const auto& kv : node) {
    if (!kv.first.IsScalar()) {
      return typedef_internal::YamlError(
          yaml, kv.first,
          absl::StrCat("Property key in '", name(),
                       "' enum constant aspect is not a string"));
    }
    std::string key = typedef_internal::GetString(kv.first);
    const YAML::Node& val = kv.second;
    if (key == "name") {
      CEL_ASSIGN_OR_RETURN(constant_name, GetNonEmptyString(yaml, "name", val));
    } else {
      return typedef_internal::YamlError(
          yaml, kv.first,
          absl::StrCat("Unsupported '", name(), "' enum constant property: '",
                       key, "'"));
    }
  }
  return ProtoEnumConstantProperties(std::move(constant_name));
}

absl::Status ProtoSchemaYaml::EncodeStructProperties(const std::any& properties,
                                                     YAML::Emitter& out) const {
  const auto* props = std::any_cast<ProtoStructProperties>(&properties);
  if (props == nullptr) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Invalid struct properties type for '", name(), "' aspect"));
  }
  out << YAML::BeginMap;
  if (props->name().has_value()) {
    out << YAML::Key << "name" << YAML::Value << YAML::DoubleQuoted
        << *props->name();
  }
  out << YAML::EndMap;
  return absl::OkStatus();
}

absl::Status ProtoSchemaYaml::EncodeFieldProperties(const std::any& properties,
                                                    YAML::Emitter& out) const {
  const auto* props = std::any_cast<ProtoFieldProperties>(&properties);
  if (props == nullptr) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Invalid field properties type for '", name(), "' aspect"));
  }
  out << YAML::BeginMap;
  if (props->name().has_value()) {
    out << YAML::Key << "name" << YAML::Value << YAML::DoubleQuoted
        << *props->name();
  }
  if (props->id().has_value()) {
    out << YAML::Key << "id" << YAML::Value << *props->id();
  }
  if (props->json_name().has_value()) {
    out << YAML::Key << "json_name" << YAML::Value << YAML::DoubleQuoted
        << *props->json_name();
  }
  out << YAML::EndMap;
  return absl::OkStatus();
}

absl::Status ProtoSchemaYaml::EncodeEnumProperties(const std::any& properties,
                                                   YAML::Emitter& out) const {
  const auto* props = std::any_cast<ProtoEnumProperties>(&properties);
  if (props == nullptr) {
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid enum properties type for '", name(), "' aspect"));
  }
  out << YAML::BeginMap;
  if (props->name().has_value()) {
    out << YAML::Key << "name" << YAML::Value << YAML::DoubleQuoted
        << *props->name();
  }
  if (props->allow_alias().has_value()) {
    out << YAML::Key << "allow_alias" << YAML::Value << *props->allow_alias();
  }
  out << YAML::EndMap;
  return absl::OkStatus();
}

absl::Status ProtoSchemaYaml::EncodeEnumConstantProperties(
    const std::any& properties, YAML::Emitter& out) const {
  const auto* props = std::any_cast<ProtoEnumConstantProperties>(&properties);
  if (props == nullptr) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Invalid enum constant properties type for '", name(), "' aspect"));
  }
  out << YAML::BeginMap;
  if (props->name().has_value()) {
    out << YAML::Key << "name" << YAML::Value << YAML::DoubleQuoted
        << *props->name();
  }
  out << YAML::EndMap;
  return absl::OkStatus();
}

// =============================================================================
// Proto2SchemaYaml ("proto2")
// =============================================================================

absl::StatusOr<std::any> Proto2SchemaYaml::DecodeStructProperties(
    absl::string_view yaml, const YAML::Node& node) const {
  if (!node.IsDefined() || !node.IsMap()) {
    return typedef_internal::YamlError(
        yaml, node,
        absl::StrCat("Node '", name(), "' struct aspect is not a map"));
  }
  std::vector<ProtoExtensionRange> extension_ranges;
  for (const auto& kv : node) {
    if (!kv.first.IsScalar()) {
      return typedef_internal::YamlError(
          yaml, kv.first,
          absl::StrCat("Property key in '", name(),
                       "' struct aspect is not a string"));
    }
    std::string key = typedef_internal::GetString(kv.first);
    const YAML::Node& val = kv.second;
    if (key == "extension_ranges") {
      if (!val.IsSequence()) {
        return typedef_internal::YamlError(
            yaml, val, "Node 'extension_ranges' is not a sequence");
      }
      for (const YAML::Node& range_node : val) {
        if (!range_node.IsMap()) {
          return typedef_internal::YamlError(
              yaml, range_node, "Element of 'extension_ranges' is not a map");
        }
        ProtoExtensionRange range;
        bool has_start = false;
        bool has_end = false;
        for (const auto& rkv : range_node) {
          if (!rkv.first.IsScalar()) {
            return typedef_internal::YamlError(
                yaml, rkv.first,
                "Property key in 'extension_ranges' element is not a string");
          }
          std::string rkey = typedef_internal::GetString(rkv.first);
          if (rkey == "start") {
            CEL_ASSIGN_OR_RETURN(range.start, typedef_internal::GetInt32(
                                                  yaml, "start", rkv.second));
            has_start = true;
          } else if (rkey == "end") {
            CEL_ASSIGN_OR_RETURN(
                range.end, typedef_internal::GetInt32(yaml, "end", rkv.second));
            has_end = true;
          } else {
            return typedef_internal::YamlError(
                yaml, rkv.first,
                absl::StrCat("Unsupported 'extension_ranges' property: '", rkey,
                             "'"));
          }
        }
        if (!has_start || !has_end) {
          return typedef_internal::YamlError(
              yaml, range_node,
              "Element of 'extension_ranges' must specify both 'start' and "
              "'end'");
        }
        extension_ranges.push_back(range);
      }
    } else {
      return typedef_internal::YamlError(
          yaml, kv.first,
          absl::StrCat("Unsupported '", name(), "' struct property: '", key,
                       "'"));
    }
  }
  return Proto2StructProperties(std::move(extension_ranges));
}

absl::StatusOr<std::any> Proto2SchemaYaml::DecodeFieldProperties(
    absl::string_view yaml, const YAML::Node& node) const {
  if (!node.IsDefined() || !node.IsMap()) {
    return typedef_internal::YamlError(
        yaml, node,
        absl::StrCat("Node '", name(), "' field aspect is not a map"));
  }
  std::optional<bool> required;
  std::optional<bool> packed;
  for (const auto& kv : node) {
    if (!kv.first.IsScalar()) {
      return typedef_internal::YamlError(
          yaml, kv.first,
          absl::StrCat("Property key in '", name(),
                       "' field aspect is not a string"));
    }
    std::string key = typedef_internal::GetString(kv.first);
    const YAML::Node& val = kv.second;
    if (key == "required") {
      CEL_ASSIGN_OR_RETURN(required,
                           typedef_internal::GetBool(yaml, "required", val));
    } else if (key == "packed") {
      CEL_ASSIGN_OR_RETURN(packed,
                           typedef_internal::GetBool(yaml, "packed", val));
    } else {
      return typedef_internal::YamlError(
          yaml, kv.first,
          absl::StrCat("Unsupported '", name(), "' field property: '", key,
                       "'"));
    }
  }
  return Proto2FieldProperties(required, packed);
}

absl::Status Proto2SchemaYaml::EncodeStructProperties(
    const std::any& properties, YAML::Emitter& out) const {
  const auto* props = std::any_cast<Proto2StructProperties>(&properties);
  if (props == nullptr) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Invalid struct properties type for '", name(), "' aspect"));
  }
  out << YAML::BeginMap;
  if (!props->extension_ranges().empty()) {
    out << YAML::Key << "extension_ranges" << YAML::Value << YAML::BeginSeq;
    for (const ProtoExtensionRange& range : props->extension_ranges()) {
      out << YAML::BeginMap;
      out << YAML::Key << "start" << YAML::Value << range.start;
      out << YAML::Key << "end" << YAML::Value << range.end;
      out << YAML::EndMap;
    }
    out << YAML::EndSeq;
  }
  out << YAML::EndMap;
  return absl::OkStatus();
}

absl::Status Proto2SchemaYaml::EncodeFieldProperties(const std::any& properties,
                                                     YAML::Emitter& out) const {
  const auto* props = std::any_cast<Proto2FieldProperties>(&properties);
  if (props == nullptr) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Invalid field properties type for '", name(), "' aspect"));
  }
  out << YAML::BeginMap;
  if (props->required().has_value()) {
    out << YAML::Key << "required" << YAML::Value << *props->required();
  }
  if (props->packed().has_value()) {
    out << YAML::Key << "packed" << YAML::Value << *props->packed();
  }
  out << YAML::EndMap;
  return absl::OkStatus();
}

// =============================================================================
// Proto3SchemaYaml ("proto3")
// =============================================================================

absl::StatusOr<std::any> Proto3SchemaYaml::DecodeFieldProperties(
    absl::string_view yaml, const YAML::Node& node) const {
  if (!node.IsDefined() || !node.IsMap()) {
    return typedef_internal::YamlError(
        yaml, node,
        absl::StrCat("Node '", name(), "' field aspect is not a map"));
  }
  std::optional<bool> optional;
  std::optional<bool> packed;
  for (const auto& kv : node) {
    if (!kv.first.IsScalar()) {
      return typedef_internal::YamlError(
          yaml, kv.first,
          absl::StrCat("Property key in '", name(),
                       "' field aspect is not a string"));
    }
    std::string key = typedef_internal::GetString(kv.first);
    const YAML::Node& val = kv.second;
    if (key == "optional") {
      CEL_ASSIGN_OR_RETURN(optional,
                           typedef_internal::GetBool(yaml, "optional", val));
    } else if (key == "packed") {
      CEL_ASSIGN_OR_RETURN(packed,
                           typedef_internal::GetBool(yaml, "packed", val));
    } else {
      return typedef_internal::YamlError(
          yaml, kv.first,
          absl::StrCat("Unsupported '", name(), "' field property: '", key,
                       "'"));
    }
  }
  return Proto3FieldProperties(optional, packed);
}

absl::Status Proto3SchemaYaml::EncodeFieldProperties(const std::any& properties,
                                                     YAML::Emitter& out) const {
  const auto* props = std::any_cast<Proto3FieldProperties>(&properties);
  if (props == nullptr) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Invalid field properties type for '", name(), "' aspect"));
  }
  out << YAML::BeginMap;
  if (props->optional().has_value()) {
    out << YAML::Key << "optional" << YAML::Value << *props->optional();
  }
  if (props->packed().has_value()) {
    out << YAML::Key << "packed" << YAML::Value << *props->packed();
  }
  out << YAML::EndMap;
  return absl::OkStatus();
}

}  // namespace cel
