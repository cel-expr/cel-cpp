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

#ifndef THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_JSON_SCHEMA_H_
#define THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_JSON_SCHEMA_H_

#include <optional>
#include <string>

#include "absl/strings/string_view.h"
#include "common/typedef/schema.h"

namespace cel {

inline constexpr absl::string_view kJsonSchemaName = "json";

class JsonSchemaObjectProperties final : public SchemaObjectProperties {
 public:
  std::optional<std::string> name;
};

class JsonSchemaFieldProperties final : public SchemaFieldProperties {
 public:
  std::optional<std::string> name;
};

class JsonSchemaEnumProperties final : public SchemaEnumProperties {
 public:
  std::optional<std::string> style;
  std::optional<bool> omit_unspecified;
};

class JsonSchemaEnumConstantProperties final
    : public SchemaEnumConstantProperties {
 public:
  std::optional<std::string> name;
};

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_JSON_SCHEMA_H_
