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

#ifndef THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_SCHEMA_H_
#define THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_SCHEMA_H_

namespace cel {

// A schema (e.g. "proto2", "proto3", "json") represents an external schema
// or data representation that a CEL TypeDef maps to or from. While TypeDef
// describes types in a schema-agnostic way, schema properties store the
// schema-specific metadata (such as protobuf field IDs or JSON property names)
// attached to objects, fields, enums, and enum constants under `schemas`.

// Opaque base class for schema-specific properties attached to an
// ObjectTypeDef.
class SchemaObjectProperties {
 public:
  virtual ~SchemaObjectProperties() = default;
};

// Opaque base class for schema-specific properties attached to an
// ObjectTypeDef::Field.
class SchemaFieldProperties {
 public:
  virtual ~SchemaFieldProperties() = default;
};

// Opaque base class for schema-specific properties attached to an
// EnumTypeDef.
class SchemaEnumProperties {
 public:
  virtual ~SchemaEnumProperties() = default;
};

// Opaque base class for schema-specific properties attached to an
// EnumTypeDef::EnumConstant.
class SchemaEnumConstantProperties {
 public:
  virtual ~SchemaEnumConstantProperties() = default;
};

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_SCHEMA_H_
