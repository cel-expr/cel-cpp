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

#include "common/typedef/aspect_yaml.h"

#include <any>
#include <memory>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "common/typedef/yaml_helpers.h"

namespace cel {

absl::StatusOr<std::any> AspectYaml::DecodeStructProperties(
    absl::string_view yaml, const YAML::Node& node) const {
  if (typedef_internal::IsEmptyYamlNode(node)) {
    return std::any();
  }
  return typedef_internal::YamlError(
      yaml, node,
      absl::StrCat("Custom struct properties are not supported for aspect '",
                   name(), "'"));
}

absl::StatusOr<std::any> AspectYaml::DecodeFieldProperties(
    absl::string_view yaml, const YAML::Node& node) const {
  if (typedef_internal::IsEmptyYamlNode(node)) {
    return std::any();
  }
  return typedef_internal::YamlError(
      yaml, node,
      absl::StrCat("Custom field properties are not supported for aspect '",
                   name(), "'"));
}

absl::StatusOr<std::any> AspectYaml::DecodeEnumProperties(
    absl::string_view yaml, const YAML::Node& node) const {
  if (typedef_internal::IsEmptyYamlNode(node)) {
    return std::any();
  }
  return typedef_internal::YamlError(
      yaml, node,
      absl::StrCat("Custom enum properties are not supported for aspect '",
                   name(), "'"));
}

absl::StatusOr<std::any> AspectYaml::DecodeEnumConstantProperties(
    absl::string_view yaml, const YAML::Node& node) const {
  if (typedef_internal::IsEmptyYamlNode(node)) {
    return std::any();
  }
  return typedef_internal::YamlError(
      yaml, node,
      absl::StrCat(
          "Custom enum constant properties are not supported for aspect '",
          name(), "'"));
}

absl::Status AspectYaml::EncodeStructProperties(const std::any& properties,
                                                YAML::Emitter& out) const {
  return absl::UnimplementedError(absl::StrCat(
      "Custom struct properties are not supported for aspect '", name(), "'"));
}

absl::Status AspectYaml::EncodeFieldProperties(const std::any& properties,
                                               YAML::Emitter& out) const {
  return absl::UnimplementedError(absl::StrCat(
      "Custom field properties are not supported for aspect '", name(), "'"));
}

absl::Status AspectYaml::EncodeEnumProperties(const std::any& properties,
                                              YAML::Emitter& out) const {
  return absl::UnimplementedError(absl::StrCat(
      "Custom enum properties are not supported for aspect '", name(), "'"));
}

absl::Status AspectYaml::EncodeEnumConstantProperties(
    const std::any& properties, YAML::Emitter& out) const {
  return absl::UnimplementedError(absl::StrCat(
      "Custom enum constant properties are not supported for aspect '", name(),
      "'"));
}

AspectYamlRegistry::AspectYamlRegistry(
    std::vector<std::unique_ptr<const AspectYaml>> aspects) {
  aspects_.reserve(aspects.size());
  for (std::unique_ptr<const AspectYaml>& aspect : aspects) {
    Add(std::move(aspect));
  }
}

const AspectYaml* absl_nullable AspectYamlRegistry::Find(
    absl::string_view aspect_name) const {
  auto it = aspects_.find(aspect_name);
  if (it == aspects_.end()) {
    return nullptr;
  }
  return it->second.get();
}

}  // namespace cel
