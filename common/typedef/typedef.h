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

#include <any>
#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "common/ast.h"
#include "common/constant.h"

namespace cel {

// Represents a struct type with typed fields.
class StructTypeDef {
 public:
  class Field {
   public:
    Field() = default;
    Field(std::string name, TypeSpec type, std::string doc = "",
          Constant default_value = {},
          absl::flat_hash_map<std::string, std::any> aspects = {})
        : name_(std::move(name)),
          type_(std::move(type)),
          doc_(std::move(doc)),
          default_value_(std::move(default_value)),
          aspects_(std::move(aspects)) {}

    absl::string_view name() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
      return name_;
    }
    const TypeSpec& type() const ABSL_ATTRIBUTE_LIFETIME_BOUND { return type_; }
    absl::string_view doc() const ABSL_ATTRIBUTE_LIFETIME_BOUND { return doc_; }
    // Optional scalar default value; check `default_value().has_value()`.
    // When the field type is an enum type, `default_value` is the name of the
    // corresponding `EnumConstant`.
    const Constant& default_value() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
      return default_value_;
    }
    // Aspects (e.g. schema-specific properties) keyed by aspect name.
    const absl::flat_hash_map<std::string, std::any>& aspects() const
        ABSL_ATTRIBUTE_LIFETIME_BOUND {
      return aspects_;
    }

   private:
    std::string name_;
    TypeSpec type_;
    std::string doc_;
    Constant default_value_;
    absl::flat_hash_map<std::string, std::any> aspects_;
  };

  StructTypeDef() = default;
  explicit StructTypeDef(
      std::string name, std::string doc = "",
      absl::flat_hash_map<std::string, std::any> aspects = {})
      : name_(std::move(name)),
        doc_(std::move(doc)),
        aspects_(std::move(aspects)) {}

  absl::string_view name() const ABSL_ATTRIBUTE_LIFETIME_BOUND { return name_; }
  absl::string_view doc() const ABSL_ATTRIBUTE_LIFETIME_BOUND { return doc_; }
  const std::vector<Field>& fields() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return fields_;
  }
  // Aspects (e.g. schema-specific properties) keyed by aspect name.
  const absl::flat_hash_map<std::string, std::any>& aspects() const
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return aspects_;
  }

  // Adds a field to the struct definition. Field names must be non-empty and
  // unique.
  absl::Status AddField(Field field);
  const Field* absl_nullable FindField(absl::string_view field_name) const
      ABSL_ATTRIBUTE_LIFETIME_BOUND;

 private:
  std::string name_;
  std::string doc_;
  std::vector<Field> fields_;
  absl::flat_hash_map<std::string, std::any> aspects_;
};

// Represents a named enumeration type with named and numbered constants.
class EnumTypeDef {
 public:
  class EnumConstant {
   public:
    EnumConstant() = default;
    explicit EnumConstant(
        std::string name, int32_t id = 0, std::string doc = "",
        absl::flat_hash_map<std::string, std::any> aspects = {})
        : name_(std::move(name)),
          id_(id),
          doc_(std::move(doc)),
          aspects_(std::move(aspects)) {}

    absl::string_view name() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
      return name_;
    }
    int32_t id() const { return id_; }
    absl::string_view doc() const ABSL_ATTRIBUTE_LIFETIME_BOUND { return doc_; }
    // Aspects (e.g. schema-specific properties) keyed by aspect name.
    const absl::flat_hash_map<std::string, std::any>& aspects() const
        ABSL_ATTRIBUTE_LIFETIME_BOUND {
      return aspects_;
    }

   private:
    std::string name_;
    int32_t id_ = 0;
    std::string doc_;
    absl::flat_hash_map<std::string, std::any> aspects_;
  };

  EnumTypeDef() = default;
  explicit EnumTypeDef(std::string name, std::string doc = "",
                       absl::flat_hash_map<std::string, std::any> aspects = {})
      : name_(std::move(name)),
        doc_(std::move(doc)),
        aspects_(std::move(aspects)) {}

  absl::string_view name() const ABSL_ATTRIBUTE_LIFETIME_BOUND { return name_; }
  absl::string_view doc() const ABSL_ATTRIBUTE_LIFETIME_BOUND { return doc_; }
  const std::vector<EnumConstant>& constants() const
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return constants_;
  }
  // Aspects (e.g. schema-specific properties) keyed by aspect name.
  const absl::flat_hash_map<std::string, std::any>& aspects() const
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return aspects_;
  }

  // Adds a constant to the enum definition. Constant names must be non-empty
  // and unique; numeric IDs may alias.
  absl::Status AddConstant(EnumConstant constant);
  const EnumConstant* absl_nullable FindConstant(
      absl::string_view constant_name) const ABSL_ATTRIBUTE_LIFETIME_BOUND;

 private:
  std::string name_;
  std::string doc_;
  std::vector<EnumConstant> constants_;
  absl::flat_hash_map<std::string, std::any> aspects_;
};

enum class TypeDefKindCase {
  kStruct,
  kEnum,
};

// Variant wrapper holding either a StructTypeDef or EnumTypeDef.
class TypeDef {
 public:
  TypeDef() = default;
  explicit TypeDef(StructTypeDef struct_type)
      : variant_(std::move(struct_type)) {}
  explicit TypeDef(EnumTypeDef enum_type) : variant_(std::move(enum_type)) {}

  TypeDefKindCase kind_case() const;

  bool is_struct() const {
    return std::holds_alternative<StructTypeDef>(variant_);
  }
  // REQUIRES: `is_struct()` is true.
  const StructTypeDef& struct_type() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    ABSL_DCHECK(is_struct());
    return std::get<StructTypeDef>(variant_);
  }

  bool is_enum() const { return std::holds_alternative<EnumTypeDef>(variant_); }
  // REQUIRES: `is_enum()` is true.
  const EnumTypeDef& enum_type() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    ABSL_DCHECK(is_enum());
    return std::get<EnumTypeDef>(variant_);
  }

 private:
  std::variant<StructTypeDef, EnumTypeDef> variant_;
};

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_TYPEDEF_H_
