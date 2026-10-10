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

#ifndef THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_SCHEMA_PROTO_H_
#define THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_SCHEMA_PROTO_H_

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
#include "common/typedef/typedef.pb.h"

namespace cel {

// Interface representing a Protobuf codec for a schema (e.g. "json",
// "proto2", "proto3") capable of decoding and encoding schema-specific property
// objects via proto extensions on cel.types messages.
class SchemaProto {
 public:
  virtual ~SchemaProto() = default;

  // Returns the unique schema identifier (e.g. "json", "proto2", "proto3").
  virtual absl::string_view name() const = 0;

  // Decodes schema-specific object properties from extensions on `proto`.
  // Returns nullptr if this schema has no properties present on `proto`.
  virtual absl::StatusOr<std::unique_ptr<SchemaObjectProperties>>
  DecodeObjectProperties(const types::Object& proto) const;

  // Decodes schema-specific field properties from extensions on `proto`.
  // Returns nullptr if this schema has no properties present on `proto`.
  virtual absl::StatusOr<std::unique_ptr<SchemaFieldProperties>>
  DecodeFieldProperties(const types::Object::Field& proto) const;

  // Decodes schema-specific enum properties from extensions on `proto`.
  // Returns nullptr if this schema has no properties present on `proto`.
  virtual absl::StatusOr<std::unique_ptr<SchemaEnumProperties>>
  DecodeEnumProperties(const types::Enum& proto) const;

  // Decodes schema-specific enum constant properties from extensions on
  // `proto`. Returns nullptr if this schema has no properties present on
  // `proto`.
  virtual absl::StatusOr<std::unique_ptr<SchemaEnumConstantProperties>>
  DecodeEnumConstantProperties(const types::Enum::EnumConstant& proto) const;

  virtual absl::Status EncodeObjectProperties(
      const SchemaObjectProperties& properties,
      types::Object* absl_nonnull proto) const;

  virtual absl::Status EncodeFieldProperties(
      const SchemaFieldProperties& properties,
      types::Object::Field* absl_nonnull proto) const;

  virtual absl::Status EncodeEnumProperties(
      const SchemaEnumProperties& properties,
      types::Enum* absl_nonnull proto) const;

  virtual absl::Status EncodeEnumConstantProperties(
      const SchemaEnumConstantProperties& properties,
      types::Enum::EnumConstant* absl_nonnull proto) const;
};

// Read-only registry of SchemaProto codecs keyed by schema name.
class SchemaProtoRegistry {
 public:
  SchemaProtoRegistry() = default;

  explicit SchemaProtoRegistry(
      std::vector<std::unique_ptr<const SchemaProto>> schemas);

  template <
      typename... Schemas,
      typename = std::enable_if_t<
          (sizeof...(Schemas) > 0) &&
          (std::is_convertible_v<Schemas, std::unique_ptr<const SchemaProto>> &&
           ...)>>
  explicit SchemaProtoRegistry(Schemas&&... schemas) {
    schemas_.reserve(sizeof...(Schemas));
    (Add(std::forward<Schemas>(schemas)), ...);
  }

  // Finds a registered SchemaProto by name, or returns nullptr if not found.
  const SchemaProto* absl_nullable Find(absl::string_view schema_name) const;

  const absl::flat_hash_map<std::string, std::unique_ptr<const SchemaProto>>&
  schemas() const {
    return schemas_;
  }

 private:
  void Add(std::unique_ptr<const SchemaProto> schema) {
    if (schema != nullptr) {
      absl::string_view name = schema->name();
      schemas_[name] = std::move(schema);
    }
  }

  absl::flat_hash_map<std::string, std::unique_ptr<const SchemaProto>> schemas_;
};

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_SCHEMA_PROTO_H_
