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

#ifndef THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_SCHEMA_YAML_H_
#define THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_SCHEMA_YAML_H_

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "common/typedef/schema.h"

namespace YAML {
class Emitter;
class Node;
}  // namespace YAML

namespace cel {

// Interface representing a YAML codec for a schema (e.g. "json",
// "proto2", "proto3") capable of decoding and encoding schema-specific property
// objects.
class SchemaYaml {
 public:
  virtual ~SchemaYaml() = default;

  // Returns the unique schema identifier (e.g. "json", "proto2", "proto3").
  virtual absl::string_view name() const = 0;

  virtual absl::StatusOr<std::unique_ptr<SchemaObjectProperties>>
  DecodeObjectProperties(absl::string_view yaml, const YAML::Node& node) const;

  virtual absl::StatusOr<std::unique_ptr<SchemaFieldProperties>>
  DecodeFieldProperties(absl::string_view yaml, const YAML::Node& node) const;

  virtual absl::StatusOr<std::unique_ptr<SchemaEnumProperties>>
  DecodeEnumProperties(absl::string_view yaml, const YAML::Node& node) const;

  virtual absl::StatusOr<std::unique_ptr<SchemaEnumConstantProperties>>
  DecodeEnumConstantProperties(absl::string_view yaml,
                               const YAML::Node& node) const;

  virtual absl::Status EncodeObjectProperties(
      const SchemaObjectProperties& properties, YAML::Emitter& out) const;

  virtual absl::Status EncodeFieldProperties(
      const SchemaFieldProperties& properties, YAML::Emitter& out) const;

  virtual absl::Status EncodeEnumProperties(
      const SchemaEnumProperties& properties, YAML::Emitter& out) const;

  virtual absl::Status EncodeEnumConstantProperties(
      const SchemaEnumConstantProperties& properties, YAML::Emitter& out) const;
};

// Read-only registry of SchemaYaml codecs keyed by schema name.
class SchemaYamlRegistry {
 public:
  SchemaYamlRegistry() = default;

  explicit SchemaYamlRegistry(
      std::vector<std::unique_ptr<const SchemaYaml>> schemas);

  template <
      typename... Schemas,
      typename = std::enable_if_t<
          (sizeof...(Schemas) > 0) &&
          (std::is_convertible_v<Schemas, std::unique_ptr<const SchemaYaml>> &&
           ...)>>
  explicit SchemaYamlRegistry(Schemas&&... schemas) {
    schemas_.reserve(sizeof...(Schemas));
    (Add(std::forward<Schemas>(schemas)), ...);
  }

  // Finds a registered SchemaYaml by name, or returns nullptr if not
  // found.
  const SchemaYaml* absl_nullable Find(absl::string_view schema_name) const;

 private:
  void Add(std::unique_ptr<const SchemaYaml> schema) {
    if (schema != nullptr) {
      absl::string_view name = schema->name();
      schemas_[name] = std::move(schema);
    }
  }

  absl::flat_hash_map<std::string, std::unique_ptr<const SchemaYaml>> schemas_;
};

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_SCHEMA_YAML_H_
