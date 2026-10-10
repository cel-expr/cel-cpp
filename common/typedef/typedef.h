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

#ifndef THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_TYPEDEF_H_
#define THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_TYPEDEF_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "common/ast.h"
#include "common/constant.h"
#include "common/typedef/schema.h"

namespace cel {

using ObjectSchemaSpecificProperties =
    absl::flat_hash_map<std::string,
                        std::shared_ptr<const SchemaObjectProperties>>;
using FieldSchemaSpecificProperties =
    absl::flat_hash_map<std::string,
                        std::shared_ptr<const SchemaFieldProperties>>;
using EnumSchemaSpecificProperties =
    absl::flat_hash_map<std::string,
                        std::shared_ptr<const SchemaEnumProperties>>;
using EnumConstantSchemaSpecificProperties =
    absl::flat_hash_map<std::string,
                        std::shared_ptr<const SchemaEnumConstantProperties>>;

// Represents a structured object type (`object:`) with typed fields.
struct ObjectTypeDef {
  struct Field {
    std::string name;
    TypeSpec type;
    std::string doc;
    // Optional scalar default value; check `default_value.has_value()`.
    Constant default_value;
    // Target schema-specific properties keyed by target schema name.
    FieldSchemaSpecificProperties schema_specific_properties;
  };

  std::string name;
  std::string doc;
  std::vector<Field> fields;
  // Target schema-specific properties keyed by target schema name.
  ObjectSchemaSpecificProperties schema_specific_properties;

  absl::Status AddField(Field field);
  const Field* absl_nullable FindField(absl::string_view field_name) const;
};

// Represents a named enumeration type (`enum:`).
struct EnumTypeDef {
  struct EnumConstant {
    std::string name;
    int32_t id = 0;
    std::string doc;
    // Target schema-specific properties keyed by target schema name.
    EnumConstantSchemaSpecificProperties schema_specific_properties;
  };

  std::string name;
  std::string doc;
  std::vector<EnumConstant> constants;
  // Target schema-specific properties keyed by target schema name.
  EnumSchemaSpecificProperties schema_specific_properties;

  absl::Status AddConstant(EnumConstant constant);
  const EnumConstant* absl_nullable FindConstant(
      absl::string_view constant_name) const;
};

enum class TypeDefKindCase {
  kObject,
  kEnum,
};

// Variant wrapper holding either an ObjectTypeDef or EnumTypeDef.
class TypeDef {
 public:
  TypeDef() = default;
  explicit TypeDef(ObjectTypeDef object_type)
      : variant_(std::move(object_type)) {}
  explicit TypeDef(EnumTypeDef enum_type) : variant_(std::move(enum_type)) {}

  TypeDefKindCase kind_case() const;
  absl::string_view name() const;
  absl::string_view doc() const;

  bool is_object() const {
    return std::holds_alternative<ObjectTypeDef>(variant_);
  }
  const ObjectTypeDef& object_type() const {
    return std::get<ObjectTypeDef>(variant_);
  }

  bool is_enum() const { return std::holds_alternative<EnumTypeDef>(variant_); }
  const EnumTypeDef& enum_type() const {
    return std::get<EnumTypeDef>(variant_);
  }

 private:
  std::variant<ObjectTypeDef, EnumTypeDef> variant_;
};

// Represents a collection of unique TypeDefs (objects and enums) indexed by
// their fully-qualified CEL type name.
class TypeDefSet {
 public:
  TypeDefSet() = default;

  // Adds a TypeDef to the set. Returns InvalidArgumentError if the type name
  // is empty, or AlreadyExistsError if a TypeDef with the same name is already
  // present.
  absl::Status AddTypeDef(TypeDef type_def);

  // Looks up a TypeDef by its fully-qualified name, or returns nullptr if not
  // found.
  const TypeDef* absl_nullable FindTypeDef(absl::string_view type_name) const;

  // Returns the TypeDefs in the order they were added.
  absl::Span<const TypeDef> types() const { return types_; }

 private:
  std::vector<TypeDef> types_;
  absl::flat_hash_map<std::string, size_t> index_by_name_;
};

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_TYPEDEF_H_
