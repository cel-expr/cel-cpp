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

#include "env/config.h"

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <utility>
#include <variant>

#include "absl/container/flat_hash_set.h"
#include "absl/functional/overload.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "common/constant.h"
#include "internal/status_macros.h"

namespace cel {

namespace {

std::string VersionToString(int version) {
  return version == Config::ExtensionConfig::kLatest ? "'latest'"
                                                     : absl::StrCat(version);
}

const char* ConstantKindToTypeName(const ConstantKind& kind) {
  return std::visit(absl::Overload{
                        [](const std::monostate& arg) { return "dyn"; },
                        [](const std::nullptr_t& arg) { return "null"; },
                        [](bool arg) { return "bool"; },
                        [](int64_t arg) { return "int"; },
                        [](uint64_t arg) { return "uint"; },
                        [](double arg) { return "double"; },
                        [](const BytesConstant& arg) { return "bytes"; },
                        [](const StringConstant& arg) { return "string"; },
                        [](absl::Duration arg) { return "duration"; },
                        [](absl::Time arg) { return "timestamp"; },
                    },
                    kind);
}
}  // namespace

absl::Status Config::AddExtensionConfig(std::string name, int version) {
  for (const ExtensionConfig& extension_config : extension_configs_) {
    if (extension_config.name == name) {
      if (extension_config.version == version) {
        return absl::OkStatus();
      }
      return absl::AlreadyExistsError(
          absl::StrCat("Extension '", name, "' version ",
                       VersionToString(extension_config.version),
                       " is already included. Cannot also include version ",
                       VersionToString(version)));
    }
  }
  extension_configs_.push_back(
      ExtensionConfig{.name = std::move(name), .version = version});
  return absl::OkStatus();
}

absl::Status Config::SetStandardLibraryConfig(
    const Config::StandardLibraryConfig& standard_library_config) {
  if (!standard_library_config.included_macros.empty() &&
      !standard_library_config.excluded_macros.empty()) {
    return absl::InvalidArgumentError(
        "Cannot set both included and excluded macros.");
  }

  if (!standard_library_config.included_functions.empty() &&
      !standard_library_config.excluded_functions.empty()) {
    return absl::InvalidArgumentError(
        "Cannot set both included and excluded functions.");
  }

  absl::flat_hash_set<std::string> included_function_names;
  for (const auto& function : standard_library_config.included_functions) {
    if (function.second.empty()) {
      included_function_names.insert(function.first);
    }
  }
  for (const auto& function : standard_library_config.included_functions) {
    if (included_function_names.contains(function.first) &&
        !function.second.empty()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Cannot include function '", function.first,
          "' and also its specific overload '", function.second, "'"));
    }
  }

  absl::flat_hash_set<std::string> excluded_function_names;
  for (const auto& function : standard_library_config.excluded_functions) {
    if (function.second.empty()) {
      excluded_function_names.insert(function.first);
    }
  }
  for (const auto& function : standard_library_config.excluded_functions) {
    if (excluded_function_names.contains(function.first) &&
        !function.second.empty()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Cannot exclude function '", function.first,
          "' and also its specific overload '", function.second, "'"));
    }
  }

  standard_library_config_ = standard_library_config;
  return absl::OkStatus();
}

absl::Status Config::AddVariableConfig(const VariableConfig& variable_config) {
  for (const VariableConfig& existing_variable_config : variable_configs_) {
    if (existing_variable_config.name == variable_config.name) {
      return absl::AlreadyExistsError(absl::StrCat(
          "Variable '", variable_config.name, "' is already included."));
    }
  }
  if (variable_config.value.has_value()) {
    absl::string_view constant_type_name =
        ConstantKindToTypeName(variable_config.value.kind());
    if (constant_type_name != variable_config.type_info.name) {
      return absl::InvalidArgumentError(
          absl::StrCat("Variable '", variable_config.name, "' has type ",
                       variable_config.type_info.name,
                       " but is assigned a constant value of type ",
                       constant_type_name, "."));
    }
  }
  variable_configs_.push_back(variable_config);
  return absl::OkStatus();
}

absl::Status Config::ValidateFunctionConfig(
    const FunctionConfig& function_config) {
  for (const auto& overload : function_config.overload_configs) {
    if (overload.is_member_function && overload.parameters.empty()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Function '", function_config.name, "' overload '",
          overload.overload_id,
          "' is marked as a member function but has no parameters. Member "
          "functions must have at least one parameter (target)."));
    }
  }
  return absl::OkStatus();
}

absl::Status Config::AddFunctionConfig(const FunctionConfig& function_config) {
  CEL_RETURN_IF_ERROR(ValidateFunctionConfig(function_config));
  function_configs_.push_back(function_config);
  return absl::OkStatus();
}

std::ostream& operator<<(std::ostream& os,
                         const Config::StandardLibraryConfig& config) {
  os << "StandardLibraryConfig(";
  if (!config.included_macros.empty()) {
    os << "\n  included_macros=" << absl::StrJoin(config.included_macros, ", ");
  }
  if (!config.excluded_macros.empty()) {
    os << "\n  excluded_macros=" << absl::StrJoin(config.excluded_macros, ", ");
  }
  if (!config.included_functions.empty()) {
    os << "\n  included_functions="
       << absl::StrJoin(config.included_functions, ", ",
                        [](std::string* out,
                           const std::pair<std::string, std::string>& p) {
                          absl::StrAppend(out, p.first, ":", p.second);
                        });
  }
  if (!config.excluded_functions.empty()) {
    os << "\n  excluded_functions="
       << absl::StrJoin(config.excluded_functions, ", ",
                        [](std::string* out,
                           const std::pair<std::string, std::string>& p) {
                          absl::StrAppend(out, p.first, ":", p.second);
                        });
  }
  os << "\n)";
  return os;
}

}  // namespace cel
