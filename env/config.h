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

#ifndef THIRD_PARTY_CEL_CPP_ENV_CONFIG_H_
#define THIRD_PARTY_CEL_CPP_ENV_CONFIG_H_

#include <limits>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "common/constant.h"
#include "common/typedef/type_ref.h"  // IWYU pragma: export

namespace cel {

class Config {
 public:
  void SetName(std::string name) { name_ = std::move(name); }
  std::string GetName() const { return name_; }

  void SetContextType(std::string context_type) {
    context_type_ = std::move(context_type);
  }
  std::string GetContextType() const { return context_type_; }

  struct ContainerConfig {
    std::string name;
    std::vector<std::string> abbreviations;
    struct Alias {
      std::string alias;
      std::string qualified_name;
    };
    std::vector<Alias> aliases;

    bool IsEmpty() const {
      return name.empty() && abbreviations.empty() && aliases.empty();
    }
  };

  void SetContainerConfig(ContainerConfig container_config) {
    container_config_ = std::move(container_config);
  }

  const ContainerConfig& GetContainerConfig() const {
    return container_config_;
  }

  struct ExtensionConfig {
    static constexpr int kLatest = std::numeric_limits<int>::max();

    std::string name;
    int version = kLatest;
  };

  absl::Status AddExtensionConfig(std::string name,
                                  int version = ExtensionConfig::kLatest);

  const std::vector<ExtensionConfig>& GetExtensionConfigs() const {
    return extension_configs_;
  }

  struct StandardLibraryConfig {
    // Exclude the entire standard library.
    bool disable = false;

    // Exclude all standard library macros.
    bool disable_macros = false;

    // Either included or excluded macros can be set, not both. If neither are
    // set, all standard library macros are included.
    absl::flat_hash_set<std::string> included_macros;
    absl::flat_hash_set<std::string> excluded_macros;

    // Sets of pairs of function name and overload id to include or exclude.
    // Either included or excluded functions can be set, not both. If neither
    // are set, all standard library functions are included.
    // If an overload is specified, only that overload is included or excluded.
    // If no overload is specified (empty second element of pair), all overloads
    // are included or excluded.
    absl::flat_hash_set<std::pair<std::string, std::string>> included_functions;
    absl::flat_hash_set<std::pair<std::string, std::string>> excluded_functions;

    bool IsEmpty() const {
      return !disable && !disable_macros && included_macros.empty() &&
             excluded_macros.empty() && included_functions.empty() &&
             excluded_functions.empty();
    }
  };

  absl::Status SetStandardLibraryConfig(
      const StandardLibraryConfig& standard_library_config);

  const StandardLibraryConfig& GetStandardLibraryConfig() const {
    return standard_library_config_;
  }

  using TypeInfo = cel::TypeRef;

  struct VariableConfig {
    std::string name;
    std::string description;
    TypeRef type_info;
    Constant value;
  };

  // Adds a variable config to the environment. The variable name and type
  // are used by the CEL type checker to validate expressions. The variable
  // value is used as an input value at runtime.
  //
  // Returns an error if a variable with the same name already exists, or if the
  // type of the constant value does not match the specified type.
  absl::Status AddVariableConfig(const VariableConfig& variable_config);

  const std::vector<VariableConfig>& GetVariableConfigs() const {
    return variable_configs_;
  }

  struct FunctionOverloadConfig {
    std::string overload_id;
    std::vector<std::string> examples;
    bool is_member_function = false;
    std::vector<TypeRef> parameters;
    TypeRef return_type;
  };

  struct FunctionConfig {
    std::string name;
    std::string description;
    std::vector<FunctionOverloadConfig> overload_configs;
  };

  absl::Status AddFunctionConfig(const FunctionConfig& function_config);

  const std::vector<FunctionConfig>& GetFunctionConfigs() const {
    return function_configs_;
  }

 private:
  std::string name_;
  std::string context_type_;
  ContainerConfig container_config_;
  std::vector<ExtensionConfig> extension_configs_;
  StandardLibraryConfig standard_library_config_;
  std::vector<VariableConfig> variable_configs_;
  std::vector<FunctionConfig> function_configs_;

  absl::Status ValidateFunctionConfig(const FunctionConfig& function_config);
};

std::ostream& operator<<(std::ostream& os,
                         const Config::StandardLibraryConfig& config);

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_ENV_CONFIG_H_
