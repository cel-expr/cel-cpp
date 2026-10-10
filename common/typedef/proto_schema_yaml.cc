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

absl::StatusOr<std::unique_ptr<SchemaObjectProperties>>
ProtoSchemaYaml::DecodeObjectProperties(absl::string_view yaml,
                                        const YAML::Node& node) const {
  if (!node.IsDefined() || !node.IsMap()) {
    return internal::YamlError(
        yaml, node,
        absl::StrCat("Node '", name(), "' object schema is not a map"));
  }
  auto result = std::make_unique<ProtoObjectProperties>();
  for (const auto& kv : node) {
    if (!kv.first.IsScalar()) {
      return internal::YamlError(
          yaml, kv.first,
          absl::StrCat("Property key in '", name(),
                       "' object schema is not a string"));
    }
    std::string key = internal::GetString(yaml, kv.first);
    const YAML::Node& val = kv.second;
    if (key == "name") {
      if (!val.IsScalar()) {
        return internal::YamlError(yaml, val, "Node 'name' is not a string");
      }
      result->name = internal::GetString(yaml, val);
    } else if (key == "file_name") {
      if (!val.IsScalar()) {
        return internal::YamlError(yaml, val,
                                   "Node 'file_name' is not a string");
      }
      result->file_name = internal::GetString(yaml, val);
    } else if (key == "extension_ranges") {
      if (!val.IsSequence()) {
        return internal::YamlError(yaml, val,
                                   "Node 'extension_ranges' is not a sequence");
      }
      for (const YAML::Node& range_node : val) {
        if (!range_node.IsMap()) {
          return internal::YamlError(
              yaml, range_node, "Element of 'extension_ranges' is not a map");
        }
        ProtoExtensionRange range;
        bool has_start = false;
        bool has_end = false;
        for (const auto& rkv : range_node) {
          if (!rkv.first.IsScalar()) {
            return internal::YamlError(
                yaml, rkv.first,
                "Property key in 'extension_ranges' element is not a string");
          }
          std::string rkey = internal::GetString(yaml, rkv.first);
          if (rkey == "start") {
            CEL_ASSIGN_OR_RETURN(range.start,
                                 internal::GetInt32(yaml, "start", rkv.second));
            has_start = true;
          } else if (rkey == "end") {
            CEL_ASSIGN_OR_RETURN(range.end,
                                 internal::GetInt32(yaml, "end", rkv.second));
            has_end = true;
          } else {
            return internal::YamlError(
                yaml, rkv.first,
                absl::StrCat("Unsupported 'extension_ranges' property: '", rkey,
                             "'"));
          }
        }
        if (!has_start || !has_end) {
          return internal::YamlError(
              yaml, range_node,
              "Element of 'extension_ranges' must specify both 'start' and "
              "'end'");
        }
        result->extension_ranges.push_back(range);
      }
    } else {
      return internal::YamlError(
          yaml, kv.first,
          absl::StrCat("Unsupported '", name(), "' object property: '", key,
                       "'"));
    }
  }
  return result;
}

absl::StatusOr<std::unique_ptr<SchemaFieldProperties>>
ProtoSchemaYaml::DecodeFieldProperties(absl::string_view yaml,
                                       const YAML::Node& node) const {
  if (!node.IsDefined() || !node.IsMap()) {
    return internal::YamlError(
        yaml, node,
        absl::StrCat("Node '", name(), "' field schema is not a map"));
  }
  auto result = std::make_unique<ProtoFieldProperties>();
  for (const auto& kv : node) {
    if (!kv.first.IsScalar()) {
      return internal::YamlError(
          yaml, kv.first,
          absl::StrCat("Property key in '", name(),
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
    } else if (key == "type") {
      if (!val.IsScalar()) {
        return internal::YamlError(yaml, val, "Node 'type' is not a string");
      }
      result->type = internal::GetString(yaml, val);
    } else if (key == "label") {
      if (!val.IsScalar()) {
        return internal::YamlError(yaml, val, "Node 'label' is not a string");
      }
      result->label = internal::GetString(yaml, val);
    } else if (key == "oneof") {
      if (!val.IsScalar()) {
        return internal::YamlError(yaml, val, "Node 'oneof' is not a string");
      }
      result->oneof = internal::GetString(yaml, val);
    } else if (key == "default_value") {
      if (!val.IsScalar()) {
        return internal::YamlError(yaml, val,
                                   "Node 'default_value' is not a string");
      }
      result->default_value = internal::GetString(yaml, val);
    } else {
      return internal::YamlError(yaml, kv.first,
                                 absl::StrCat("Unsupported '", name(),
                                              "' field property: '", key, "'"));
    }
  }
  return result;
}

absl::StatusOr<std::unique_ptr<SchemaEnumProperties>>
ProtoSchemaYaml::DecodeEnumProperties(absl::string_view yaml,
                                      const YAML::Node& node) const {
  if (!node.IsDefined() || !node.IsMap()) {
    return internal::YamlError(
        yaml, node,
        absl::StrCat("Node '", name(), "' enum schema is not a map"));
  }
  auto result = std::make_unique<ProtoEnumProperties>();
  for (const auto& kv : node) {
    if (!kv.first.IsScalar()) {
      return internal::YamlError(yaml, kv.first,
                                 absl::StrCat("Property key in '", name(),
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
    } else if (key == "file_name") {
      if (!val.IsScalar()) {
        return internal::YamlError(yaml, val,
                                   "Node 'file_name' is not a string");
      }
      result->file_name = internal::GetString(yaml, val);
    } else {
      return internal::YamlError(yaml, kv.first,
                                 absl::StrCat("Unsupported '", name(),
                                              "' enum property: '", key, "'"));
    }
  }
  return result;
}

absl::StatusOr<std::unique_ptr<SchemaEnumConstantProperties>>
ProtoSchemaYaml::DecodeEnumConstantProperties(absl::string_view yaml,
                                              const YAML::Node& node) const {
  if (!node.IsDefined() || !node.IsMap()) {
    return internal::YamlError(
        yaml, node,
        absl::StrCat("Node '", name(), "' enum constant schema is not a map"));
  }
  auto result = std::make_unique<ProtoEnumConstantProperties>();
  for (const auto& kv : node) {
    if (!kv.first.IsScalar()) {
      return internal::YamlError(
          yaml, kv.first,
          absl::StrCat("Property key in '", name(),
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
          absl::StrCat("Unsupported '", name(), "' enum constant property: '",
                       key, "'"));
    }
  }
  return result;
}

absl::Status ProtoSchemaYaml::EncodeObjectProperties(
    const SchemaObjectProperties& properties, YAML::Emitter& out) const {
  const auto* props = dynamic_cast<const ProtoObjectProperties*>(&properties);
  if (props == nullptr) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Invalid object properties type for '", name(), "' schema"));
  }
  out << YAML::BeginMap;
  if (props->name.has_value()) {
    out << YAML::Key << "name" << YAML::Value << YAML::DoubleQuoted
        << *props->name;
  }
  if (props->file_name.has_value()) {
    out << YAML::Key << "file_name" << YAML::Value << YAML::DoubleQuoted
        << *props->file_name;
  }
  if (!props->extension_ranges.empty()) {
    out << YAML::Key << "extension_ranges" << YAML::Value << YAML::BeginSeq;
    for (const ProtoExtensionRange& range : props->extension_ranges) {
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

absl::Status ProtoSchemaYaml::EncodeFieldProperties(
    const SchemaFieldProperties& properties, YAML::Emitter& out) const {
  const auto* props = dynamic_cast<const ProtoFieldProperties*>(&properties);
  if (props == nullptr) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Invalid field properties type for '", name(), "' schema"));
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
  if (props->type.has_value()) {
    out << YAML::Key << "type" << YAML::Value << YAML::DoubleQuoted
        << *props->type;
  }
  if (props->label.has_value()) {
    out << YAML::Key << "label" << YAML::Value << YAML::DoubleQuoted
        << *props->label;
  }
  if (props->oneof.has_value()) {
    out << YAML::Key << "oneof" << YAML::Value << YAML::DoubleQuoted
        << *props->oneof;
  }
  if (props->default_value.has_value()) {
    out << YAML::Key << "default_value" << YAML::Value << YAML::DoubleQuoted
        << *props->default_value;
  }
  out << YAML::EndMap;
  return absl::OkStatus();
}

absl::Status ProtoSchemaYaml::EncodeEnumProperties(
    const SchemaEnumProperties& properties, YAML::Emitter& out) const {
  const auto* props = dynamic_cast<const ProtoEnumProperties*>(&properties);
  if (props == nullptr) {
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid enum properties type for '", name(), "' schema"));
  }
  out << YAML::BeginMap;
  if (props->name.has_value()) {
    out << YAML::Key << "name" << YAML::Value << YAML::DoubleQuoted
        << *props->name;
  }
  if (props->allow_alias.has_value()) {
    out << YAML::Key << "allow_alias" << YAML::Value << *props->allow_alias;
  }
  if (props->file_name.has_value()) {
    out << YAML::Key << "file_name" << YAML::Value << YAML::DoubleQuoted
        << *props->file_name;
  }
  out << YAML::EndMap;
  return absl::OkStatus();
}

absl::Status ProtoSchemaYaml::EncodeEnumConstantProperties(
    const SchemaEnumConstantProperties& properties, YAML::Emitter& out) const {
  const auto* props =
      dynamic_cast<const ProtoEnumConstantProperties*>(&properties);
  if (props == nullptr) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Invalid enum constant properties type for '", name(), "' schema"));
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
