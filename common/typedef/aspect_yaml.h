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

#ifndef THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_ASPECT_YAML_H_
#define THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_ASPECT_YAML_H_

#include <any>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace YAML {
class Emitter;
class Node;
}  // namespace YAML

namespace cel {

// Interface representing a YAML codec for an aspect (e.g. "json",
// "proto2", "proto3") capable of decoding and encoding aspect property
// objects.
class AspectYaml {
 public:
  virtual ~AspectYaml() = default;

  // Returns the unique aspect identifier (e.g. "json", "proto2", "proto3").
  virtual absl::string_view name() const = 0;

  virtual absl::StatusOr<std::any> DecodeStructProperties(
      absl::string_view yaml, const YAML::Node& node) const;

  virtual absl::StatusOr<std::any> DecodeFieldProperties(
      absl::string_view yaml, const YAML::Node& node) const;

  virtual absl::StatusOr<std::any> DecodeEnumProperties(
      absl::string_view yaml, const YAML::Node& node) const;

  virtual absl::StatusOr<std::any> DecodeEnumConstantProperties(
      absl::string_view yaml, const YAML::Node& node) const;

  virtual absl::Status EncodeStructProperties(const std::any& properties,
                                              YAML::Emitter& out) const;

  virtual absl::Status EncodeFieldProperties(const std::any& properties,
                                             YAML::Emitter& out) const;

  virtual absl::Status EncodeEnumProperties(const std::any& properties,
                                            YAML::Emitter& out) const;

  virtual absl::Status EncodeEnumConstantProperties(const std::any& properties,
                                                    YAML::Emitter& out) const;
};

// Read-only registry of AspectYaml codecs keyed by aspect name.
class AspectYamlRegistry {
 public:
  AspectYamlRegistry() = default;

  explicit AspectYamlRegistry(
      std::vector<std::unique_ptr<const AspectYaml>> aspects);

  template <
      typename... Aspects,
      typename = std::enable_if_t<
          (sizeof...(Aspects) > 0) &&
          (std::is_convertible_v<Aspects, std::unique_ptr<const AspectYaml>> &&
           ...)>>
  explicit AspectYamlRegistry(Aspects&&... aspects) {
    aspects_.reserve(sizeof...(Aspects));
    (Add(std::forward<Aspects>(aspects)), ...);
  }

  // Finds a registered AspectYaml by name, or returns nullptr if not
  // found.
  const AspectYaml* absl_nullable Find(absl::string_view aspect_name) const
      ABSL_ATTRIBUTE_LIFETIME_BOUND;

 private:
  void Add(std::unique_ptr<const AspectYaml> aspect) {
    if (aspect != nullptr) {
      absl::string_view name = aspect->name();
      aspects_[name] = std::move(aspect);
    }
  }

  absl::flat_hash_map<std::string, std::unique_ptr<const AspectYaml>> aspects_;
};

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_ASPECT_YAML_H_
