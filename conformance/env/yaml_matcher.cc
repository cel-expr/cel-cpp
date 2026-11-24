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

#include "conformance/env/yaml_matcher.h"

#include <cmath>
#include <cstddef>
#include <limits>
#include <ostream>
#include <string>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "internal/testing.h"
#include "yaml-cpp/exceptions.h"
#include "yaml-cpp/node/node.h"
#include "yaml-cpp/node/parse.h"
#include "yaml-cpp/node/type.h"
#include "yaml-cpp/yaml.h"  // IWYU pragma: keep

namespace cel {
namespace {

constexpr int kMaxRecursionDepth = 100;

absl::string_view NodeTypeName(YAML::NodeType::value type) {
  switch (type) {
    case YAML::NodeType::Undefined:
      return "undefined";
    case YAML::NodeType::Null:
      return "null";
    case YAML::NodeType::Scalar:
      return "scalar";
    case YAML::NodeType::Sequence:
      return "sequence";
    case YAML::NodeType::Map:
      return "map";
  }
  return "unknown";
}

bool HasExplicitTag(const YAML::Node& node) {
  const std::string& tag = node.Tag();
  return !tag.empty() && tag != "?" && tag != "!";
}

bool IsFloatCandidateScalar(const YAML::Node& node) {
  const std::string& tag = node.Tag();
  return tag == "?" || tag == "tag:yaml.org,2002:float";
}

bool IsYamlSpecialFloat(absl::string_view s) {
  return absl::EqualsIgnoreCase(s, ".nan") ||
         absl::EqualsIgnoreCase(s, ".inf") ||
         absl::EqualsIgnoreCase(s, "+.inf") ||
         absl::EqualsIgnoreCase(s, "-.inf");
}

bool LooksLikeYamlFloat(absl::string_view s) {
  if (IsYamlSpecialFloat(s)) {
    return true;
  }
  return s.find_first_of(".eE") != absl::string_view::npos;
}

bool ParseYamlDouble(absl::string_view s, double* out) {
  if (s.empty()) {
    return false;
  }
  if (absl::EqualsIgnoreCase(s, ".nan")) {
    *out = std::numeric_limits<double>::quiet_NaN();
    return true;
  }
  if (absl::EqualsIgnoreCase(s, ".inf") || absl::EqualsIgnoreCase(s, "+.inf")) {
    *out = std::numeric_limits<double>::infinity();
    return true;
  }
  if (absl::EqualsIgnoreCase(s, "-.inf")) {
    *out = -std::numeric_limits<double>::infinity();
    return true;
  }
  absl::string_view unsigned_s = s;
  if (unsigned_s.front() == '+' || unsigned_s.front() == '-') {
    unsigned_s.remove_prefix(1);
  }
  if (unsigned_s.empty() ||
      (unsigned_s.front() != '.' &&
       (unsigned_s.front() < '0' || unsigned_s.front() > '9'))) {
    return false;
  }
  if (absl::StartsWithIgnoreCase(unsigned_s, "0x")) {
    return false;
  }
  double value = 0.0;
  if (!absl::SimpleAtod(s, &value) || !std::isfinite(value)) {
    return false;
  }
  *out = value;
  return true;
}

bool ScalarsEqual(const YAML::Node& actual, const YAML::Node& expected) {
  if (actual.Scalar() == expected.Scalar()) {
    return true;
  }
  if (!IsFloatCandidateScalar(actual) || !IsFloatCandidateScalar(expected)) {
    return false;
  }
  if (!LooksLikeYamlFloat(actual.Scalar()) &&
      !LooksLikeYamlFloat(expected.Scalar())) {
    return false;
  }
  double actual_val = 0.0;
  double expected_val = 0.0;
  if (!ParseYamlDouble(actual.Scalar(), &actual_val) ||
      !ParseYamlDouble(expected.Scalar(), &expected_val)) {
    return false;
  }
  if (std::isnan(actual_val) && std::isnan(expected_val)) {
    return true;
  }
  return actual_val == expected_val;
}

bool NodesEqual(const YAML::Node& actual, const YAML::Node& expected,
                absl::string_view path, int depth,
                testing::MatchResultListener* listener) {
  if (depth > kMaxRecursionDepth) {
    if (listener != nullptr) {
      *listener << "at " << path << ": exceeded maximum nesting depth ("
                << kMaxRecursionDepth << ")";
    }
    return false;
  }

  if (actual.Type() != expected.Type()) {
    if (listener != nullptr) {
      *listener << "at " << path
                << ": type mismatch (actual: " << NodeTypeName(actual.Type())
                << ", expected: " << NodeTypeName(expected.Type()) << ")";
    }
    return false;
  }

  if ((HasExplicitTag(actual) || HasExplicitTag(expected)) &&
      actual.Tag() != expected.Tag()) {
    if (listener != nullptr) {
      *listener << "at " << path << ": tag mismatch (actual: \"" << actual.Tag()
                << "\", expected: \"" << expected.Tag() << "\")";
    }
    return false;
  }

  switch (expected.Type()) {
    case YAML::NodeType::Undefined:
    case YAML::NodeType::Null:
      return true;

    case YAML::NodeType::Scalar:
      if (!ScalarsEqual(actual, expected)) {
        if (listener != nullptr) {
          *listener << "at " << path << ": scalar mismatch (actual: \""
                    << actual.Scalar() << "\", expected: \""
                    << expected.Scalar() << "\")";
        }
        return false;
      }
      return true;

    case YAML::NodeType::Sequence: {
      if (actual.size() != expected.size()) {
        if (listener != nullptr) {
          *listener << "at " << path
                    << ": sequence size mismatch (actual: " << actual.size()
                    << ", expected: " << expected.size() << ")";
        }
        return false;
      }
      for (size_t i = 0; i < expected.size(); ++i) {
        if (!NodesEqual(actual[i], expected[i], absl::StrCat(path, "[", i, "]"),
                        depth + 1, listener)) {
          return false;
        }
      }
      return true;
    }

    case YAML::NodeType::Map: {
      absl::flat_hash_set<std::string> expected_keys;
      for (const auto& entry : expected) {
        const YAML::Node& key = entry.first;
        if (!key.IsScalar()) {
          if (listener != nullptr) {
            *listener << "at " << path
                      << ": unsupported non-scalar map key in expected YAML"
                      << " (type: " << NodeTypeName(key.Type()) << ")";
          }
          return false;
        }
        const std::string& key_str = key.Scalar();
        if (!expected_keys.insert(key_str).second) {
          if (listener != nullptr) {
            *listener << "at " << path << ": duplicate key \"" << key_str
                      << "\" in expected YAML";
          }
          return false;
        }
      }

      absl::flat_hash_set<std::string> actual_keys;
      for (const auto& entry : actual) {
        const YAML::Node& key = entry.first;
        if (!key.IsScalar()) {
          if (listener != nullptr) {
            *listener << "at " << path
                      << ": unsupported non-scalar map key in actual YAML"
                      << " (type: " << NodeTypeName(key.Type()) << ")";
          }
          return false;
        }
        const std::string& key_str = key.Scalar();
        if (!actual_keys.insert(key_str).second) {
          if (listener != nullptr) {
            *listener << "at " << path << ": duplicate key \"" << key_str
                      << "\" in actual YAML";
          }
          return false;
        }
      }

      for (const auto& entry : expected) {
        const std::string& key_str = entry.first.Scalar();
        const YAML::Node& expected_value = entry.second;
        const YAML::Node actual_value = actual[key_str];
        if (!actual_value.IsDefined()) {
          if (listener != nullptr) {
            *listener << "at " << path << ": missing key \"" << key_str << "\"";
          }
          return false;
        }
        if (!NodesEqual(actual_value, expected_value,
                        absl::StrCat(path, ".", key_str), depth + 1,
                        listener)) {
          return false;
        }
      }

      for (const auto& entry : actual) {
        const std::string& key_str = entry.first.Scalar();
        if (!expected_keys.contains(key_str)) {
          if (listener != nullptr) {
            *listener << "at " << path << ": unexpected key \"" << key_str
                      << "\"";
          }
          return false;
        }
      }
      return true;
    }
  }
  return false;
}

class YamlMatcherImpl : public testing::MatcherInterface<absl::string_view> {
 public:
  explicit YamlMatcherImpl(absl::string_view expected_yaml)
      : expected_yaml_(expected_yaml) {}

  bool MatchAndExplain(absl::string_view actual_yaml,
                       testing::MatchResultListener* listener) const override {
    YAML::Node expected_node;
    try {
      expected_node = YAML::Load(expected_yaml_);
    } catch (const YAML::Exception& e) {
      if (listener != nullptr) {
        *listener << "failed to parse expected YAML: " << e.what();
      }
      return false;
    }

    YAML::Node actual_node;
    try {
      actual_node = YAML::Load(std::string(actual_yaml));
    } catch (const YAML::Exception& e) {
      if (listener != nullptr) {
        *listener << "failed to parse actual YAML: " << e.what();
      }
      return false;
    }

    return NodesEqual(actual_node, expected_node, "$", /*depth=*/0, listener);
  }

  void DescribeTo(std::ostream* os) const override {
    *os << "equals YAML:\n" << expected_yaml_;
  }

  void DescribeNegationTo(std::ostream* os) const override {
    *os << "does not equal YAML:\n" << expected_yaml_;
  }

 private:
  std::string expected_yaml_;
};

}  // namespace

testing::Matcher<absl::string_view> EqualsYaml(
    absl::string_view expected_yaml) {
  return testing::MakeMatcher(new YamlMatcherImpl(expected_yaml));
}

}  // namespace cel
