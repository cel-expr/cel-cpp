// Copyright 2026 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "env/env_yaml.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/no_destructor.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "common/ast.h"
#include "common/constant.h"
#include "common/signature.h"
#include "common/typedef/type_ref.h"
#include "env/config.h"
#include "internal/status_macros.h"
#include "internal/strings.h"
#include "yaml-cpp/emitter.h"
#include "yaml-cpp/emittermanip.h"
#include "yaml-cpp/exceptions.h"
#include "yaml-cpp/mark.h"
#include "yaml-cpp/node/node.h"
#include "yaml-cpp/node/parse.h"
#include "yaml-cpp/null.h"
#include "yaml-cpp/yaml.h"  // IWYU pragma: keep

namespace cel {

namespace {

std::string FormatYamlErrorMessage(absl::string_view yaml,
                                   absl::string_view error,
                                   const YAML::Mark& mark) {
  if (mark.is_null()) {
    return std::string(error);
  }
  std::string message;
  absl::StrAppend(&message, mark.line + 1, ":", mark.column + 1, ": ", error,
                  "\n|");
  size_t start = mark.pos - mark.column;
  size_t end = yaml.find('\n', mark.pos);
  if (end == std::string::npos) {
    end = yaml.size();
  }

  absl::StrAppend(&message, yaml.substr(start, end - start), "\n|",
                  std::string(mark.column, ' '), "^");

  return message;
}

absl::StatusOr<YAML::Node> LoadYaml(const std::string& yaml) {
  try {
    return YAML::Load(yaml);
  } catch (YAML::ParserException& e) {
    return absl::InvalidArgumentError(
        FormatYamlErrorMessage(yaml, e.msg, e.mark));
  }
}

absl::Status YamlError(absl::string_view yaml, const YAML::Node& node,
                       absl::string_view error) {
  return absl::InvalidArgumentError(
      FormatYamlErrorMessage(yaml, error, node.Mark()));
}

std::string GetString(absl::string_view yaml, const YAML::Node& node) {
  if (!node.IsDefined() || !node.IsScalar()) {
    return "";
  }
  try {
    return node.as<std::string>();
  } catch (YAML::Exception& e) {
    // This should never happen since we already checked that the node is a
    // scalar and all scalars can be converted to strings.
    return "";
  }
}

bool IsBinary(const YAML::Node& node) {
  return node.Tag() == "!!binary" || node.Tag() == "tag:yaml.org,2002:binary";
}

absl::StatusOr<std::string> GetBinary(absl::string_view yaml,
                                      const YAML::Node& node) {
  if (!node.IsDefined() || !node.IsScalar() || !IsBinary(node)) {
    return "";
  }
  std::string binary;
  // Instead of using the YAML::Binary type, we use absl::Base64Unescape
  // because YAML::Binary is lenient to Base64 decoding errors.
  if (absl::Base64Unescape(GetString(yaml, node), &binary)) {
    return binary;
  } else {
    return YamlError(yaml, node,
                     absl::StrCat("Node '", GetString(yaml, node),
                                  "' is not a valid Base64 encoded binary"));
  }
}

absl::StatusOr<bool> GetBool(absl::string_view yaml, absl::string_view key,
                             const YAML::Node& node) {
  if (!node.IsDefined() || !node.IsScalar()) {
    return false;
  }
  try {
    return node.as<bool>();
  } catch (YAML::Exception& e) {
    return YamlError(yaml, node,
                     absl::StrCat("Node '", key, "' is not a boolean"));
  }
}

// Returns the key in the map `node` that has the given `value_node` as its
// value. If no such key exists, returns `value_node` itself.
YAML::Node GetContextNodeForKeyValue(const YAML::Node& node,
                                     const YAML::Node& value_node) {
  for (const auto& kv : node) {
    if (kv.second.IsDefined() && kv.second.is(value_node)) {
      return kv.first;
    }
  }
  return value_node;
}

absl::Status ParseName(Config& config, absl::string_view yaml,
                       const YAML::Node& root) {
  const YAML::Node name = root["name"];
  if (name.IsDefined()) {
    if (!name.IsScalar()) {
      return YamlError(yaml, name, "Node 'name' is not a string");
    }
    config.SetName(GetString(yaml, name));
  }
  return absl::OkStatus();
}

absl::Status ParseContainerConfig(Config& config, absl::string_view yaml,
                                  const YAML::Node& root) {
  const YAML::Node container = root["container"];
  if (!container.IsDefined()) {
    return absl::OkStatus();
  }

  if (container.IsScalar()) {
    config.SetContainerConfig({.name = GetString(yaml, container)});
    return absl::OkStatus();
  }

  if (!container.IsMap()) {
    return YamlError(yaml, container,
                     "Node 'container' is neither a string nor a map");
  }

  Config::ContainerConfig container_config;

  const YAML::Node name = container["name"];
  if (name.IsDefined()) {
    if (!name.IsScalar()) {
      return YamlError(yaml, name, "Node 'name' in container is not a string");
    }
    container_config.name = GetString(yaml, name);
  }

  const YAML::Node abbreviations = container["abbreviations"];
  if (abbreviations.IsDefined()) {
    if (!abbreviations.IsSequence()) {
      return YamlError(yaml, abbreviations,
                       "Node 'abbreviations' is not a sequence");
    }
    for (const YAML::Node& abbr : abbreviations) {
      if (!abbr.IsScalar()) {
        return YamlError(yaml, abbr, "Abbreviation is not a string");
      }
      container_config.abbreviations.push_back(GetString(yaml, abbr));
    }
  }

  const YAML::Node aliases = container["aliases"];
  if (aliases.IsDefined()) {
    if (!aliases.IsSequence()) {
      return YamlError(yaml, aliases, "Node 'aliases' is not a sequence");
    }
    for (const YAML::Node& alias_node : aliases) {
      if (!alias_node.IsMap()) {
        return YamlError(yaml, alias_node, "Alias entry is not a map");
      }
      const YAML::Node alias_key = alias_node["alias"];
      const YAML::Node qualified_name_key = alias_node["qualified_name"];

      if (!alias_key.IsDefined() || !alias_key.IsScalar()) {
        return YamlError(yaml, alias_node,
                         "Alias entry missing 'alias' string");
      }
      if (!qualified_name_key.IsDefined() || !qualified_name_key.IsScalar()) {
        return YamlError(yaml, alias_node,
                         "Alias entry missing 'qualified_name' string");
      }

      container_config.aliases.push_back(
          {.alias = GetString(yaml, alias_key),
           .qualified_name = GetString(yaml, qualified_name_key)});
    }
  }

  config.SetContainerConfig(std::move(container_config));
  return absl::OkStatus();
}

absl::Status ParseExtensionConfigs(Config& config, absl::string_view yaml,
                                   const YAML::Node& root) {
  const YAML::Node extensions = root["extensions"];
  if (!extensions.IsDefined()) {
    return absl::OkStatus();
  }
  if (!extensions.IsSequence()) {
    return YamlError(yaml, extensions, "Node 'extensions' is not a sequence");
  }

  for (const YAML::Node& extension : extensions) {
    if (!extension || !extension.IsMap()) {
      return YamlError(yaml, extension, "Extension is not a map");
    }
    const YAML::Node name = extension["name"];
    if (!name || !name.IsScalar()) {
      return YamlError(yaml, name, "Extension name is not a string");
    }
    std::string name_str = GetString(yaml, name);

    const YAML::Node version = extension["version"];
    std::string version_str = GetString(yaml, version);
    int extension_version;
    if (version.IsDefined()) {
      bool is_valid_version = false;
      if (version.IsScalar()) {
        if (version_str == "latest") {
          extension_version = Config::ExtensionConfig::kLatest;
          is_valid_version = true;
        } else {
          if (absl::SimpleAtoi(version_str, &extension_version) &&
              extension_version >= 0) {
            is_valid_version = true;
          }
        }
      }
      if (!is_valid_version) {
        return YamlError(
            yaml, version,
            absl::StrCat("Extension '", name_str,
                         "' version is not a valid number or 'latest'"));
      }
    } else {
      extension_version = Config::ExtensionConfig::kLatest;
    }
    absl::Status add_status =
        config.AddExtensionConfig(name_str, extension_version);
    if (!add_status.ok()) {
      return YamlError(yaml, extension, add_status.message());
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<absl::flat_hash_set<std::string>> ParseMacroList(
    absl::string_view yaml, const YAML::Node& standard_library,
    absl::string_view key) {
  absl::flat_hash_set<std::string> macro_set;
  const YAML::Node macros = standard_library[std::string(key)];
  if (!macros.IsDefined()) {
    return macro_set;
  }
  if (!macros.IsSequence()) {
    return YamlError(yaml, macros,
                     absl::StrCat("Node '", key, "' is not a sequence"));
  }
  for (const YAML::Node& macro : macros) {
    if (!macro.IsScalar()) {
      return YamlError(yaml, macro,
                       absl::StrCat("Entry in '", key, "' is not a string"));
    }
    macro_set.insert(GetString(yaml, macro));
  }
  return macro_set;
}

absl::StatusOr<absl::flat_hash_set<std::pair<std::string, std::string>>>
ParseFunctionList(absl::string_view yaml, const YAML::Node& standard_library,
                  absl::string_view key) {
  absl::flat_hash_set<std::pair<std::string, std::string>> function_set;
  const YAML::Node functions = standard_library[std::string(key)];
  if (!functions.IsDefined()) {
    return function_set;
  }
  if (!functions.IsSequence()) {
    return YamlError(yaml, functions,
                     absl::StrCat("Node '", key, "' is not a sequence"));
  }
  for (const YAML::Node& function : functions) {
    if (!function.IsMap()) {
      return YamlError(yaml, function,
                       absl::StrCat("Entry in '", key, "' is not a map"));
    }
    const YAML::Node name = function["name"];
    if (!name.IsDefined()) {
      return YamlError(
          yaml, function,
          absl::StrCat("Function name in not specified in '", key, "'"));
    }
    if (!name.IsScalar()) {
      return YamlError(
          yaml, name,
          absl::StrCat("Function name in '", key, "' entry is not a string"));
    }
    std::string name_str = GetString(yaml, name);
    const YAML::Node overloads = function["overloads"];
    if (!overloads.IsDefined()) {
      function_set.insert(std::make_pair(name_str, ""));
    } else {
      if (!overloads.IsSequence()) {
        return YamlError(
            yaml, overloads,
            absl::StrCat("Overloads in '", key, "' entry is not a sequence"));
      }
      for (const YAML::Node& overload : overloads) {
        if (!overload.IsMap()) {
          return YamlError(
              yaml, overload,
              absl::StrCat("Overload in '", key, "' entry is not a map"));
        }
        const YAML::Node id = overload["id"];
        if (!id || !id.IsScalar()) {
          return YamlError(
              yaml, id,
              absl::StrCat("Overload id in '", key, "' entry is not a string"));
        }
        function_set.insert(std::make_pair(name_str, GetString(yaml, id)));
      }
    }
  }
  return function_set;
}

absl::Status ParseStandardLibraryConfig(Config& config, absl::string_view yaml,
                                        const YAML::Node& root) {
  const YAML::Node standard_library = root["stdlib"];
  if (!standard_library.IsDefined()) {
    return absl::OkStatus();
  }

  if (!standard_library.IsMap()) {
    return YamlError(yaml, standard_library,
                     "Standard library config ('stdlib') is not a map");
  }

  Config::StandardLibraryConfig standard_library_config;

  const YAML::Node disable = standard_library["disable"];
  if (disable.IsDefined()) {
    if (!disable.IsScalar()) {
      return YamlError(yaml, disable, "Node 'disable' is not a boolean");
    }
    CEL_ASSIGN_OR_RETURN(standard_library_config.disable,
                         GetBool(yaml, "disable", disable));
  }

  const YAML::Node disable_macros = standard_library["disable_macros"];
  if (disable_macros.IsDefined()) {
    if (!disable_macros.IsScalar()) {
      return YamlError(yaml, disable_macros,
                       "Node 'disable_macros' is not a boolean");
    }
    CEL_ASSIGN_OR_RETURN(standard_library_config.disable_macros,
                         GetBool(yaml, "disable_macros", disable_macros));
  }

  CEL_ASSIGN_OR_RETURN(
      standard_library_config.included_macros,
      ParseMacroList(yaml, standard_library, "include_macros"));

  CEL_ASSIGN_OR_RETURN(
      standard_library_config.excluded_macros,
      ParseMacroList(yaml, standard_library, "exclude_macros"));

  CEL_ASSIGN_OR_RETURN(
      standard_library_config.included_functions,
      ParseFunctionList(yaml, standard_library, "include_functions"));

  CEL_ASSIGN_OR_RETURN(
      standard_library_config.excluded_functions,
      ParseFunctionList(yaml, standard_library, "exclude_functions"));

  return config.SetStandardLibraryConfig(standard_library_config);
}

absl::StatusOr<TypeRef> ParseTypeRef(const YAML::Node& node,
                                     absl::string_view yaml) {
  TypeRef type_ref;
  const YAML::Node type = node["type"];
  const YAML::Node type_name = node["type_name"];
  if (type.IsDefined() && type_name.IsDefined()) {
    return YamlError(yaml, GetContextNodeForKeyValue(node, type_name),
                     "Node 'type' and 'type_name' are mutually exclusive");
  }

  if (type.IsDefined()) {
    if (!type.IsScalar()) {
      return YamlError(yaml, type, "Node 'type' is not a string");
    }
    CEL_ASSIGN_OR_RETURN(auto type_spec, ParseTypeSpec(GetString(yaml, type)));
    CEL_ASSIGN_OR_RETURN(TypeRef type_ref, TypeSpecToTypeRef(type_spec));
    return type_ref;
  }

  if (!type_name.IsDefined()) {
    return type_ref;
  }
  if (!type_name || !type_name.IsScalar()) {
    return YamlError(yaml, type_name, "Node 'type_name' is not a string");
  }
  type_ref.name = GetString(yaml, type_name);

  const YAML::Node is_type_param = node["is_type_param"];
  if (is_type_param.IsDefined()) {
    if (!is_type_param.IsScalar()) {
      return YamlError(yaml, is_type_param,
                       "Node 'is_type_param' is not a boolean");
    }
    CEL_ASSIGN_OR_RETURN(type_ref.is_type_param,
                         GetBool(yaml, "is_type_param", is_type_param));
  }

  const YAML::Node params = node["params"];
  if (!params.IsDefined()) {
    return type_ref;
  }
  if (!params.IsSequence()) {
    return YamlError(yaml, params, "Node 'params' is not a sequence");
  }
  for (const YAML::Node& param : params) {
    CEL_ASSIGN_OR_RETURN(TypeRef param_ref, ParseTypeRef(param, yaml));
    type_ref.params.push_back(param_ref);
  }

  return type_ref;
}

bool CompareTypeRef(const TypeRef& a, const TypeRef& b) {
  if (a.name != b.name) {
    return a.name < b.name;
  }
  if (a.params.size() != b.params.size()) {
    return a.params.size() < b.params.size();
  }
  for (size_t i = 0; i < a.params.size(); ++i) {
    if (CompareTypeRef(a.params[i], b.params[i])) {
      return true;
    }
    if (CompareTypeRef(b.params[i], a.params[i])) {
      return false;
    }
  }
  return false;  // They are equal
}

ConstantKindCase GetConstantKindCase(absl::string_view type_name) {
  static const auto kTypeNameToConstantKindCase =
      absl::NoDestructor<absl::flat_hash_map<std::string, ConstantKindCase>>({
          {"null", ConstantKindCase::kNull},
          {"bool", ConstantKindCase::kBool},
          {"int", ConstantKindCase::kInt},
          {"uint", ConstantKindCase::kUint},
          {"double", ConstantKindCase::kDouble},
          {"string", ConstantKindCase::kString},
          {"bytes", ConstantKindCase::kBytes},
          {"duration", ConstantKindCase::kDuration},
          {"timestamp", ConstantKindCase::kTimestamp},
      });
  if (auto it = kTypeNameToConstantKindCase->find(type_name);
      it != kTypeNameToConstantKindCase->end()) {
    return it->second;
  }
  return ConstantKindCase::kUnspecified;
}

absl::StatusOr<Constant> ParseConstantValue(absl::string_view yaml,
                                            const YAML::Node& node,
                                            ConstantKindCase constant_kind_case,
                                            absl::string_view value) {
  switch (constant_kind_case) {
    case ConstantKindCase::kNull:
      if (!value.empty()) {
        return YamlError(yaml, node, "Failed to parse null constant");
      }
      return Constant(nullptr);
    case ConstantKindCase::kBool:
      if (absl::EqualsIgnoreCase(value, "true")) {
        return Constant(true);
      } else if (absl::EqualsIgnoreCase(value, "false")) {
        return Constant(false);
      } else {
        return YamlError(yaml, node, "Failed to parse bool constant");
      }
    case ConstantKindCase::kInt:
      int64_t int_value;
      if (!absl::SimpleAtoi(value, &int_value)) {
        return YamlError(yaml, node, "Failed to parse int constant");
      }
      return Constant(int_value);
    case ConstantKindCase::kUint:
      uint64_t uint_value;
      if (absl::EndsWith(value, "u")) {
        value = value.substr(0, value.size() - 1);
      }
      if (!absl::SimpleAtoi(value, &uint_value)) {
        return YamlError(yaml, node, "Failed to parse uint constant");
      }
      return Constant(uint_value);
    case ConstantKindCase::kDouble:
      double double_value;
      if (!absl::SimpleAtod(value, &double_value)) {
        return YamlError(yaml, node, "Failed to parse double constant");
      }
      return Constant(double_value);
    case ConstantKindCase::kBytes: {
      if (!IsBinary(node)) {
        absl::StatusOr<std::string> bytes_literal =
            internal::ParseBytesLiteral(value);
        if (bytes_literal.ok()) {
          return Constant(BytesConstant(*bytes_literal));
        }
      }
      return Constant(BytesConstant(value));
    }
    case ConstantKindCase::kString:
      return Constant(StringConstant(value));
    case ConstantKindCase::kDuration: {
      // Duration is deprecated as a builtin type, but still supported for
      // compatibility.
      absl::Duration duration_value;
      if (!absl::ParseDuration(value, &duration_value)) {
        return YamlError(yaml, node, "Failed to parse duration constant");
      }
      return Constant(duration_value);
    }
    case ConstantKindCase::kTimestamp: {
      // Timestamp is deprecated as a builtin type, but still supported for
      // compatibility.
      absl::Time timestamp_value;
      std::string error;
      // Format: YYYY-MM-DDThh:mm:ssZ
      if (!absl::ParseTime("%Y-%m-%d%ET%H:%M:%E*SZ", value, &timestamp_value,
                           &error)) {
        return YamlError(
            yaml, node,
            absl::StrCat("Failed to parse timestamp constant: ", error,
                         " supported format: YYYY-MM-DDThh:mm:ssZ"));
      }
      return Constant(timestamp_value);
    }
    default:
      // This should never happen.
      return YamlError(yaml, node, "Constant type is not supported");
  }
}

absl::Status ParseVariableConfigs(Config& config, absl::string_view yaml,
                                  const YAML::Node& root) {
  const YAML::Node variables = root["variables"];
  if (!variables.IsDefined()) {
    return absl::OkStatus();
  }
  if (!variables.IsSequence()) {
    return YamlError(yaml, variables, "Node 'variables' is not a sequence");
  }

  for (const YAML::Node& variable : variables) {
    Config::VariableConfig variable_config;
    if (!variable || !variable.IsMap()) {
      return YamlError(yaml, variable, "Variable is not a map");
    }
    const YAML::Node name = variable["name"];
    if (!name || !name.IsScalar()) {
      return YamlError(yaml, name, "Variable name is not a string");
    }
    variable_config.name = GetString(yaml, name);
    const YAML::Node description = variable["description"];
    if (description.IsDefined()) {
      if (!description.IsScalar()) {
        return YamlError(yaml, description,
                         "Variable description is not a string");
      }
      variable_config.description = GetString(yaml, description);
    }
    const YAML::Node type = variable["type"];
    TypeRef type_ref;
    if (type.IsDefined() && !type.IsScalar()) {
      // Old format, type spec is in 'type' instead of directly embedded.
      CEL_ASSIGN_OR_RETURN(type_ref, ParseTypeRef(variable["type"], yaml));
    } else {
      CEL_ASSIGN_OR_RETURN(type_ref, ParseTypeRef(variable, yaml));
    }
    ConstantKindCase constant_kind_case = GetConstantKindCase(type_ref.name);
    std::string value_str;
    YAML::Node value = variable["value"];
    if (value.IsDefined()) {
      if (constant_kind_case == ConstantKindCase::kUnspecified) {
        return YamlError(yaml, value,
                         absl::StrCat("Constant type '", type_ref.name,
                                      "' is not supported"));
      }
      if (!value.IsScalar()) {
        return YamlError(yaml, value, "Variable value is not a scalar");
      }
      if (IsBinary(value)) {
        CEL_ASSIGN_OR_RETURN(value_str, GetBinary(yaml, value));
      } else {
        value_str = GetString(yaml, value);
      }
    }

    variable_config.type_info = type_ref;

    if (constant_kind_case != ConstantKindCase::kUnspecified &&
        !value_str.empty()) {
      CEL_ASSIGN_OR_RETURN(
          variable_config.value,
          ParseConstantValue(yaml, value, constant_kind_case, value_str));
    } else if (constant_kind_case == ConstantKindCase::kNull) {
      variable_config.value = Constant(nullptr);
    }

    CEL_RETURN_IF_ERROR(config.AddVariableConfig(variable_config));
  }
  return absl::OkStatus();
}

absl::StatusOr<Config::FunctionOverloadConfig> ParseFunctionOverloadConfig(
    absl::string_view yaml, const YAML::Node& overload,
    absl::string_view function_name) {
  Config::FunctionOverloadConfig overload_config;
  if (!overload || !overload.IsMap()) {
    return YamlError(yaml, overload, "Function overload is not a map");
  }
  const YAML::Node id = overload["id"];
  if (id.IsDefined()) {
    if (!id.IsScalar()) {
      return YamlError(yaml, id, "Function overload id is not a string");
    }
    overload_config.overload_id = GetString(yaml, id);
  }
  const YAML::Node examples = overload["examples"];
  if (examples.IsDefined()) {
    if (!examples.IsSequence()) {
      return YamlError(yaml, examples,
                       "Function overload examples is not a sequence");
    }
    for (const YAML::Node& example : examples) {
      if (!example.IsScalar()) {
        return YamlError(yaml, example,
                         "Function overload example is not a string");
      }
      overload_config.examples.push_back(GetString(yaml, example));
    }
  }

  const YAML::Node signature_node = overload["signature"];
  const YAML::Node target = overload["target"];
  const YAML::Node args = overload["args"];
  if (signature_node.IsDefined()) {
    if (!signature_node.IsScalar()) {
      return YamlError(yaml, signature_node,
                       "Function overload signature is not a string");
    }

    if (target.IsDefined()) {
      return YamlError(yaml, GetContextNodeForKeyValue(overload, target),
                       "Function overload signature and target are mutually "
                       "exclusive");
    }
    if (args.IsDefined()) {
      return YamlError(yaml, GetContextNodeForKeyValue(overload, args),
                       "Function overload signature and args are mutually "
                       "exclusive");
    }

    std::string signature = GetString(yaml, signature_node);
    CEL_ASSIGN_OR_RETURN(ParsedFunctionOverload parsed_signature,
                         ParseFunctionSignature(signature));
    if (parsed_signature.function_name != function_name) {
      return YamlError(yaml, signature_node,
                       absl::StrCat("Function overload name \"",
                                    parsed_signature.function_name,
                                    "\" does not match function name \"",
                                    function_name, "\""));
    }
    overload_config.is_member_function = parsed_signature.is_member;
    if (overload_config.overload_id.empty()) {
      overload_config.overload_id = signature;
    }
    if (!parsed_signature.signature_type.has_function()) {
      return absl::InternalError(absl::StrCat(
          "Function overload signature has no function type: ", signature));
    }
    const FunctionTypeSpec& function_type_spec =
        parsed_signature.signature_type.function();
    for (const auto& arg : function_type_spec.arg_types()) {
      CEL_ASSIGN_OR_RETURN(TypeRef type_ref, TypeSpecToTypeRef(arg));
      overload_config.parameters.push_back(std::move(type_ref));
    }
  } else {
    if (target.IsDefined()) {
      if (!target.IsMap()) {
        return YamlError(yaml, target, "Function overload target is not a map");
      }
      CEL_ASSIGN_OR_RETURN(TypeRef type_ref, ParseTypeRef(target, yaml));
      overload_config.is_member_function = true;
      overload_config.parameters.push_back(type_ref);
    }

    if (args.IsDefined()) {
      if (!args.IsSequence()) {
        return YamlError(yaml, args,
                         "Function overload args is not a sequence");
      }
      for (const YAML::Node& arg : args) {
        if (!arg.IsMap()) {
          return YamlError(yaml, arg, "Function overload arg is not a map");
        }
        CEL_ASSIGN_OR_RETURN(TypeRef type_ref, ParseTypeRef(arg, yaml));
        overload_config.parameters.push_back(type_ref);
      }
    }
  }
  const YAML::Node return_type = overload["return"];
  if (return_type.IsDefined()) {
    if (return_type.IsScalar()) {
      CEL_ASSIGN_OR_RETURN(auto type_spec,
                           ParseTypeSpec(GetString(yaml, return_type)));
      CEL_ASSIGN_OR_RETURN(overload_config.return_type,
                           TypeSpecToTypeRef(type_spec));
    } else if (return_type.IsMap()) {
      CEL_ASSIGN_OR_RETURN(overload_config.return_type,
                           ParseTypeRef(return_type, yaml));
    } else {
      return YamlError(
          yaml, return_type,
          "Function overload return type is neither a string nor a map");
    }
  }
  return overload_config;
}

absl::Status ParseFunctionConfigs(Config& config, absl::string_view yaml,
                                  const YAML::Node& root) {
  const YAML::Node functions = root["functions"];
  if (!functions.IsDefined()) {
    return absl::OkStatus();
  }
  if (!functions.IsSequence()) {
    return YamlError(yaml, functions, "Node 'functions' is not a sequence");
  }

  for (const YAML::Node& function : functions) {
    Config::FunctionConfig function_config;
    if (!function || !function.IsMap()) {
      return YamlError(yaml, function, "Function is not a map");
    }
    const YAML::Node name = function["name"];
    if (!name || !name.IsScalar()) {
      return YamlError(yaml, name, "Function name is not a string");
    }
    function_config.name = GetString(yaml, name);
    const YAML::Node description = function["description"];
    if (description.IsDefined()) {
      if (!description.IsScalar()) {
        return YamlError(yaml, description,
                         "Function description is not a string");
      }
      function_config.description = GetString(yaml, description);
    }
    const YAML::Node overloads = function["overloads"];
    if (overloads.IsDefined()) {
      if (!overloads.IsSequence()) {
        return YamlError(yaml, overloads,
                         "Function 'overloads' item is not a sequence");
      }

      for (const YAML::Node& overload : overloads) {
        CEL_ASSIGN_OR_RETURN(
            Config::FunctionOverloadConfig overload_config,
            ParseFunctionOverloadConfig(yaml, overload, function_config.name));
        function_config.overload_configs.push_back(std::move(overload_config));
      }
    }

    CEL_RETURN_IF_ERROR(config.AddFunctionConfig(function_config));
  }
  return absl::OkStatus();
}

void EmitContainerConfig(const Config& env_config, YAML::Emitter& out) {
  const auto& container_config = env_config.GetContainerConfig();
  if (container_config.IsEmpty()) {
    return;
  }

  out << YAML::Key << "container";
  if (container_config.abbreviations.empty() &&
      container_config.aliases.empty()) {
    out << YAML::Value << YAML::DoubleQuoted << container_config.name;
  } else {
    out << YAML::Value << YAML::BeginMap;
    if (!container_config.name.empty()) {
      out << YAML::Key << "name" << YAML::Value << YAML::DoubleQuoted
          << container_config.name;
    }
    if (!container_config.abbreviations.empty()) {
      std::vector<std::string> sorted_abbrs = container_config.abbreviations;
      absl::c_sort(sorted_abbrs);
      out << YAML::Key << "abbreviations" << YAML::Value << YAML::BeginSeq;
      for (const auto& abbr : sorted_abbrs) {
        out << YAML::Value << YAML::DoubleQuoted << abbr;
      }
      out << YAML::EndSeq;
    }
    if (!container_config.aliases.empty()) {
      std::vector<Config::ContainerConfig::Alias> sorted_aliases =
          container_config.aliases;
      absl::c_sort(sorted_aliases, [](const Config::ContainerConfig::Alias& a,
                                      const Config::ContainerConfig::Alias& b) {
        return a.alias < b.alias;
      });
      out << YAML::Key << "aliases" << YAML::Value << YAML::BeginSeq;
      for (const auto& alias : sorted_aliases) {
        out << YAML::BeginMap;
        out << YAML::Key << "alias" << YAML::Value << YAML::DoubleQuoted
            << alias.alias;
        out << YAML::Key << "qualified_name" << YAML::Value
            << YAML::DoubleQuoted << alias.qualified_name;
        out << YAML::EndMap;
      }
      out << YAML::EndSeq;
    }
    out << YAML::EndMap;
  }
}

void EmitExtensionConfigs(const Config& env_config, YAML::Emitter& out) {
  if (env_config.GetExtensionConfigs().empty()) {
    return;
  }

  // Sort the extensions to make the output deterministic.
  std::vector<Config::ExtensionConfig> sorted_extensions =
      env_config.GetExtensionConfigs();
  absl::c_sort(sorted_extensions, [](const Config::ExtensionConfig& a,
                                     const Config::ExtensionConfig& b) {
    return a.name < b.name;
  });
  out << YAML::Key << "extensions";
  out << YAML::Value << YAML::BeginSeq;
  for (const Config::ExtensionConfig& extension_config : sorted_extensions) {
    out << YAML::BeginMap;
    out << YAML::Key << "name";
    out << YAML::Value << YAML::DoubleQuoted << extension_config.name;
    if (extension_config.version != Config::ExtensionConfig::kLatest) {
      out << YAML::Key << "version";
      out << YAML::Value << extension_config.version;
    }
    out << YAML::EndMap;
  }
  out << YAML::EndSeq;
}

void EmitMacroList(YAML::Emitter& out, absl::string_view key,
                   const absl::flat_hash_set<std::string>& macros) {
  if (macros.empty()) {
    return;
  }
  out << YAML::Key << std::string(key);
  out << YAML::Value << YAML::BeginSeq;
  std::vector<std::string> sorted_macros(macros.begin(), macros.end());
  absl::c_sort(sorted_macros);
  for (const std::string& macro : sorted_macros) {
    out << YAML::Value << YAML::DoubleQuoted << macro;
  }
  out << YAML::EndSeq;
}

void EmitFunctionList(
    YAML::Emitter& out, absl::string_view key,
    const absl::flat_hash_set<std::pair<std::string, std::string>>& functions) {
  if (functions.empty()) {
    return;
  }

  // Build a map from function name to a vector of overload ids.
  // Using std::map ensures function names are sorted.
  std::map<std::string, std::vector<std::string>> function_overloads;
  for (const auto& pair : functions) {
    function_overloads[pair.first].push_back(pair.second);
  }

  out << YAML::Key << std::string(key) << YAML::Value << YAML::BeginSeq;
  for (auto const& [name, overloads] : function_overloads) {
    out << YAML::BeginMap;
    out << YAML::Key << "name";
    out << YAML::Value << YAML::DoubleQuoted << name;

    // If the only overload is the empty string, it signifies that all overloads
    // of the function are included/excluded. In this case, we don't emit the
    // "overloads" key. Otherwise, emit the specific overloads.
    if (!(overloads.size() == 1 && overloads[0].empty())) {
      // Sort overloads for deterministic output.
      std::vector<std::string> sorted_overloads = overloads;
      absl::c_sort(sorted_overloads);

      out << YAML::Key << "overloads" << YAML::Value << YAML::BeginSeq;
      for (const std::string& overload : sorted_overloads) {
        out << YAML::BeginMap;
        out << YAML::Key << "id";
        out << YAML::Value << YAML::DoubleQuoted << overload;
        out << YAML::EndMap;
      }
      out << YAML::EndSeq;
    }
    out << YAML::EndMap;
  }
  out << YAML::EndSeq;
}

void EmitStandardLibraryConfig(const Config& env_config, YAML::Emitter& out) {
  const Config::StandardLibraryConfig& standard_library_config =
      env_config.GetStandardLibraryConfig();
  if (standard_library_config.IsEmpty()) {
    return;
  }

  out << YAML::Key << "stdlib" << YAML::Value << YAML::BeginMap;
  if (standard_library_config.disable) {
    out << YAML::Key << "disable" << YAML::Value << true;
  }
  if (standard_library_config.disable_macros) {
    out << YAML::Key << "disable_macros" << YAML::Value << true;
  }
  EmitMacroList(out, "include_macros", standard_library_config.included_macros);
  EmitMacroList(out, "exclude_macros", standard_library_config.excluded_macros);
  EmitFunctionList(out, "include_functions",
                   standard_library_config.included_functions);
  EmitFunctionList(out, "exclude_functions",
                   standard_library_config.excluded_functions);
  out << YAML::EndMap;
}

void EmitTypeRef(const TypeRef& type_ref, YAML::Emitter& out,
                 const EnvConfigToYamlOptions& options) {
  // Note: the map is already started when this is called, so we don't emit
  // BeginMap here or EndMap at the end.
  bool signature_generated = false;
  if (options.use_type_signatures) {
    absl::StatusOr<TypeSpec> type_spec = TypeRefToTypeSpec(type_ref);
    if (type_spec.ok()) {
      absl::StatusOr<std::string> signature = MakeTypeSpecSignature(*type_spec);
      if (signature.ok()) {
        out << YAML::Key << "type";
        out << YAML::Value << YAML::DoubleQuoted << *signature;
        signature_generated = true;
      }
    }
  }
  if (!signature_generated) {
    out << YAML::Key << "type_name";
    out << YAML::Value << YAML::DoubleQuoted << type_ref.name;
    if (type_ref.is_type_param) {
      out << YAML::Key << "is_type_param" << YAML::Value << true;
    }
    if (!type_ref.params.empty()) {
      out << YAML::Key << "params" << YAML::Value << YAML::BeginSeq;
      for (const TypeRef& param : type_ref.params) {
        out << YAML::BeginMap;
        EmitTypeRef(param, out, options);
        out << YAML::EndMap;
      }
      out << YAML::EndSeq;
    }
  }
}

void EmitVariableConfigs(const Config& env_config, YAML::Emitter& out,
                         const EnvConfigToYamlOptions& options) {
  const auto& variable_configs = env_config.GetVariableConfigs();
  if (variable_configs.empty()) {
    return;
  }

  // Sort variable_configs by name to ensure deterministic output.
  std::vector<Config::VariableConfig> sorted_variable_configs =
      variable_configs;
  absl::c_sort(sorted_variable_configs,
               [](const Config::VariableConfig& a,
                  const Config::VariableConfig& b) { return a.name < b.name; });

  out << YAML::Key << "variables";
  out << YAML::Value << YAML::BeginSeq;
  for (const Config::VariableConfig& variable_config :
       sorted_variable_configs) {
    out << YAML::BeginMap;
    out << YAML::Key << "name";
    out << YAML::Value << YAML::DoubleQuoted << variable_config.name;
    if (!variable_config.description.empty()) {
      out << YAML::Key << "description";
      out << YAML::Value << YAML::DoubleQuoted << variable_config.description;
    }
    EmitTypeRef(variable_config.type_info, out, options);
    if (variable_config.value.has_value()) {
      const Constant& constant = variable_config.value;
      switch (constant.kind_case()) {
        case ConstantKindCase::kUnspecified:
        case ConstantKindCase::kNull:
          break;
        case ConstantKindCase::kBool:
          out << YAML::Key << "value" << YAML::Value << constant.bool_value();
          break;
        case ConstantKindCase::kInt:
          out << YAML::Key << "value" << YAML::Value << constant.int_value();
          break;
        case ConstantKindCase::kUint:
          out << YAML::Key << "value" << YAML::Value << constant.uint_value();
          break;
        case ConstantKindCase::kDouble:
          out << YAML::Key << "value" << YAML::Value << constant.double_value();
          break;
        case ConstantKindCase::kBytes: {
          out << YAML::Key << "value";
          const std::string& bytes_value = constant.bytes_value();
          std::string hex_escaped = "b\"";
          for (unsigned char byte : bytes_value) {
            absl::StrAppend(&hex_escaped, "\\x");
            absl::StrAppendFormat(&hex_escaped, "%02x", byte);
          }
          absl::StrAppend(&hex_escaped, "\"");
          out << YAML::Value << hex_escaped;
          break;
        }
        case ConstantKindCase::kString:
          out << YAML::Key << "value";
          out << YAML::Value << YAML::DoubleQuoted << constant.string_value();
          break;
        case ConstantKindCase::kDuration:
          out << YAML::Key << "value" << YAML::Value;
          // NOLINTNEXTLINE(clang-diagnostic-deprecated-declarations)
          out << absl::FormatDuration(constant.duration_value());
          break;
        case ConstantKindCase::kTimestamp:
          out << YAML::Key << "value" << YAML::Value;
          out << absl::FormatTime(
              "%Y-%m-%d%ET%H:%M:%E*SZ",
              // NOLINTNEXTLINE(clang-diagnostic-deprecated-declarations)
              constant.timestamp_value(), absl::UTCTimeZone());
          break;
      }
    }
    out << YAML::EndMap;
  }
  out << YAML::EndSeq;
}

void EmitFunctionOverloadConfig(
    absl::string_view function_name,
    const Config::FunctionOverloadConfig& overload_config, YAML::Emitter& out,
    const EnvConfigToYamlOptions& options) {
  out << YAML::BeginMap;
  bool signature_generated = false;
  std::string signature_str;
  if (options.use_type_signatures) {
    bool param_type_spec_generated = true;
    std::vector<TypeSpec> params;
    params.reserve(overload_config.parameters.size());
    for (const auto& parameter : overload_config.parameters) {
      absl::StatusOr<TypeSpec> type_spec = TypeRefToTypeSpec(parameter);
      if (!type_spec.ok()) {
        param_type_spec_generated = false;
        break;
      }
      params.push_back(std::move(*type_spec));
    }
    if (param_type_spec_generated) {
      absl::StatusOr<std::string> signature = MakeOverloadSignature(
          function_name, params, overload_config.is_member_function);
      if (signature.ok()) {
        signature_str = std::move(*signature);
        signature_generated = true;
      }
    }
  }
  if (!overload_config.overload_id.empty()) {
    if (!signature_generated || overload_config.overload_id != signature_str) {
      out << YAML::Key << "id";
      out << YAML::Value << YAML::DoubleQuoted << overload_config.overload_id;
    }
  }
  if (signature_generated) {
    out << YAML::Key << "signature";
    out << YAML::Value << YAML::DoubleQuoted << signature_str;
  }
  if (!signature_generated) {
    if (overload_config.is_member_function) {
      out << YAML::Key << "target" << YAML::Value;
      out << YAML::BeginMap;
      if (overload_config.parameters.empty()) {
        // This should never happen, but if it does, emit a dynamic type.
        EmitTypeRef({.name = "dyn"}, out, options);
      } else {
        EmitTypeRef(overload_config.parameters[0], out, options);
      }
      out << YAML::EndMap;
      if (overload_config.parameters.size() > 1) {
        out << YAML::Key << "args";
        out << YAML::Value << YAML::BeginSeq;
        for (size_t i = 1; i < overload_config.parameters.size(); ++i) {
          out << YAML::BeginMap;
          EmitTypeRef(overload_config.parameters[i], out, options);
          out << YAML::EndMap;
        }
        out << YAML::EndSeq;
      }
    } else {
      if (!overload_config.parameters.empty()) {
        out << YAML::Key << "args";
        out << YAML::Value << YAML::BeginSeq;
        for (const TypeRef& parameter : overload_config.parameters) {
          out << YAML::BeginMap;
          EmitTypeRef(parameter, out, options);
          out << YAML::EndMap;
        }
        out << YAML::EndSeq;
      }
    }
  }
  bool return_type_signature_generated = false;
  if (options.use_type_signatures) {
    absl::StatusOr<TypeSpec> type_spec =
        TypeRefToTypeSpec(overload_config.return_type);
    if (type_spec.ok()) {
      absl::StatusOr<std::string> signature = MakeTypeSpecSignature(*type_spec);
      if (signature.ok()) {
        out << YAML::Key << "return";
        out << YAML::Value << YAML::DoubleQuoted << *signature;
        return_type_signature_generated = true;
      }
    }
  }
  if (!return_type_signature_generated) {
    out << YAML::Key << "return";
    out << YAML::Value << YAML::BeginMap;
    EmitTypeRef(overload_config.return_type, out, options);
    out << YAML::EndMap;
  }
  out << YAML::EndMap;
}

void EmitFunctionConfigs(const Config& env_config, YAML::Emitter& out,
                         const EnvConfigToYamlOptions& options) {
  const std::vector<Config::FunctionConfig>& function_configs =
      env_config.GetFunctionConfigs();
  if (function_configs.empty()) {
    return;
  }

  // Sort function_configs by name to ensure deterministic output.
  std::vector<Config::FunctionConfig> sorted_function_configs =
      function_configs;
  absl::c_sort(sorted_function_configs,
               [](const Config::FunctionConfig& a,
                  const Config::FunctionConfig& b) { return a.name < b.name; });

  out << YAML::Key << "functions";
  out << YAML::Value << YAML::BeginSeq;
  for (const Config::FunctionConfig& function_config :
       sorted_function_configs) {
    out << YAML::BeginMap;
    out << YAML::Key << "name";
    out << YAML::Value << YAML::DoubleQuoted << function_config.name;
    if (!function_config.description.empty()) {
      out << YAML::Key << "description";
      out << YAML::Value << YAML::DoubleQuoted << function_config.description;
    }
    if (!function_config.overload_configs.empty()) {
      // Sort overloads for deterministic output.
      std::vector<Config::FunctionOverloadConfig> sorted_overloads =
          function_config.overload_configs;
      absl::c_sort(sorted_overloads,
                   [](const Config::FunctionOverloadConfig& a,
                      const Config::FunctionOverloadConfig& b) {
                     for (size_t i = 0; i < a.parameters.size(); ++i) {
                       // Order like this: foo(a), foo(a, b)
                       if (i >= b.parameters.size()) {
                         return false;
                       }
                       if (CompareTypeRef(a.parameters[i], b.parameters[i])) {
                         return true;
                       }
                       if (CompareTypeRef(b.parameters[i], a.parameters[i])) {
                         return false;
                       }
                     }
                     return false;
                   });

      out << YAML::Key << "overloads" << YAML::Value << YAML::BeginSeq;
      for (const Config::FunctionOverloadConfig& overload_config :
           sorted_overloads) {
        EmitFunctionOverloadConfig(function_config.name, overload_config, out,
                                   options);
      }
      out << YAML::EndSeq;
    }
    out << YAML::EndMap;
  }
  out << YAML::EndSeq;
}

absl::Status ParseContextVariableConfig(Config& config, absl::string_view yaml,
                                        const YAML::Node& root) {
  const YAML::Node context_variable = root["context_variable"];
  if (!context_variable.IsDefined()) {
    return absl::OkStatus();
  }
  if (!context_variable.IsMap()) {
    return YamlError(yaml, context_variable,
                     "Node 'context_variable' is not a map");
  }

  const YAML::Node type_name = context_variable["type_name"];
  const YAML::Node type = context_variable["type"];
  const YAML::Node* type_node = nullptr;
  if (type.IsDefined() && type.IsScalar()) {
    type_node = &type;
  } else if (type_name.IsDefined() && type_name.IsScalar()) {
    type_node = &type_name;
  } else {
    return YamlError(yaml, context_variable,
                     "Node 'context_variable' does not have a valid type");
  }
  ABSL_DCHECK(type_node != nullptr);
  config.SetContextType(GetString(yaml, *type_node));
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<Config> EnvConfigFromYaml(const std::string& yaml) {
  Config config;
  CEL_ASSIGN_OR_RETURN(YAML::Node root, LoadYaml(yaml));
  if (!root.IsDefined() || root.IsNull()) {
    return config;
  }

  if (!root.IsMap()) {
    return absl::InvalidArgumentError(FormatYamlErrorMessage(
        yaml, "Invalid CEL environment config YAML", root.Mark()));
  }

  CEL_RETURN_IF_ERROR(ParseName(config, yaml, root));
  CEL_RETURN_IF_ERROR(ParseContainerConfig(config, yaml, root));
  CEL_RETURN_IF_ERROR(ParseExtensionConfigs(config, yaml, root));
  CEL_RETURN_IF_ERROR(ParseStandardLibraryConfig(config, yaml, root));
  CEL_RETURN_IF_ERROR(ParseContextVariableConfig(config, yaml, root));
  CEL_RETURN_IF_ERROR(ParseVariableConfigs(config, yaml, root));
  CEL_RETURN_IF_ERROR(ParseFunctionConfigs(config, yaml, root));
  return config;
}

void EnvConfigToYaml(const Config& env_config, std::ostream& os,
                     const EnvConfigToYamlOptions& options) {
  YAML::Emitter out(os);
  out.SetIndent(2);
  out << YAML::BeginMap;
  if (!env_config.GetName().empty()) {
    out << YAML::Key << "name";
    out << YAML::Value << YAML::DoubleQuoted << env_config.GetName();
  }
  EmitContainerConfig(env_config, out);
  EmitExtensionConfigs(env_config, out);
  EmitStandardLibraryConfig(env_config, out);
  EmitVariableConfigs(env_config, out, options);
  EmitFunctionConfigs(env_config, out, options);
  out << YAML::EndMap;
}

}  // namespace cel
