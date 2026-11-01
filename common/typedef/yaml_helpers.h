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

#ifndef THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_YAML_HELPERS_H_
#define THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_YAML_HELPERS_H_

#include <cstdint>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "yaml-cpp/mark.h"
#include "yaml-cpp/node/node.h"
#include "yaml-cpp/yaml.h"  // IWYU pragma: keep

namespace cel::internal {

std::string FormatYamlErrorMessage(absl::string_view yaml,
                                   absl::string_view error,
                                   const YAML::Mark& mark);

absl::StatusOr<YAML::Node> LoadYaml(absl::string_view yaml);

absl::Status YamlError(absl::string_view yaml, const YAML::Node& node,
                       absl::string_view error);

bool IsEmptyYamlNode(const YAML::Node& node);

std::string GetString(absl::string_view yaml, const YAML::Node& node);

bool IsBinary(const YAML::Node& node);

absl::StatusOr<std::string> GetBinary(absl::string_view yaml,
                                      const YAML::Node& node);

absl::StatusOr<bool> GetBool(absl::string_view yaml, absl::string_view key,
                             const YAML::Node& node);

absl::StatusOr<int32_t> GetInt32(absl::string_view yaml, absl::string_view key,
                                 const YAML::Node& node);

// Returns the key in the map `node` that has the given `value_node` as its
// value. If no such key exists, returns `value_node` itself.
YAML::Node GetContextNodeForKeyValue(const YAML::Node& node,
                                     const YAML::Node& value_node);

}  // namespace cel::internal

#endif  // THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_YAML_HELPERS_H_
