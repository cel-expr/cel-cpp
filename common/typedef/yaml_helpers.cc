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

#include "common/typedef/yaml_helpers.h"

#include <cstddef>
#include <cstdint>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "yaml-cpp/exceptions.h"
#include "yaml-cpp/mark.h"
#include "yaml-cpp/node/node.h"
#include "yaml-cpp/node/parse.h"
#include "yaml-cpp/yaml.h"  // IWYU pragma: keep

namespace cel::internal {

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

absl::StatusOr<YAML::Node> LoadYaml(absl::string_view yaml) {
  try {
    return YAML::Load(std::string(yaml));
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

bool IsEmptyYamlNode(const YAML::Node& node) {
  if (!node.IsDefined() || node.IsNull()) {
    return true;
  }
  if (node.IsScalar()) {
    return node.Scalar().empty();
  }
  return node.size() == 0;
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
    return YamlError(yaml, node,
                     absl::StrCat("Node '", key, "' is not a boolean"));
  }
  try {
    return node.as<bool>();
  } catch (YAML::Exception& e) {
    return YamlError(yaml, node,
                     absl::StrCat("Node '", key, "' is not a boolean"));
  }
}

absl::StatusOr<int32_t> GetInt32(absl::string_view yaml, absl::string_view key,
                                 const YAML::Node& node) {
  if (!node.IsDefined() || !node.IsScalar()) {
    return YamlError(yaml, node,
                     absl::StrCat("Node '", key, "' is not an integer"));
  }
  int32_t value = 0;
  if (!absl::SimpleAtoi(GetString(yaml, node), &value)) {
    return YamlError(yaml, node,
                     absl::StrCat("Node '", key, "' is not an integer"));
  }
  return value;
}

YAML::Node GetContextNodeForKeyValue(const YAML::Node& node,
                                     const YAML::Node& value_node) {
  for (const auto& kv : node) {
    if (kv.second.IsDefined() && kv.second.is(value_node)) {
      return kv.first;
    }
  }
  return value_node;
}

}  // namespace cel::internal
