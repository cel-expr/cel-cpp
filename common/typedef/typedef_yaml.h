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

#ifndef THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_TYPEDEF_YAML_H_
#define THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_TYPEDEF_YAML_H_

#include <ostream>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "common/typedef/schema_yaml.h"
#include "common/typedef/typedef.h"

namespace cel {

struct TypeDefToYamlOptions {
  // Whether to use concise type signatures (e.g., `type: "list<string>"`)
  // instead of structured type definitions (`type_name: "list"`, `params: ...`)
  // in the output YAML.
  bool use_type_signatures = true;
};

// TypeDefFromYaml creates a TypeDef from a YAML mapping string.
//
// Schema-specific properties under `schemas` are decoded by
// delegating to the corresponding `SchemaYaml` looked up in `registry`.
absl::StatusOr<TypeDef> TypeDefFromYaml(
    absl::string_view yaml, const SchemaYamlRegistry& registry = {});

// TypeDefsFromYaml creates a sequence of TypeDefs from a YAML string.
//
// Supports either a top-level YAML sequence of type definitions or a YAML map
// containing a `types` sequence key. Schema-specific properties
// under `schemas` are decoded by delegating to the corresponding
// `SchemaYaml` looked up in `registry`.
absl::StatusOr<std::vector<TypeDef>> TypeDefsFromYaml(
    absl::string_view yaml, const SchemaYamlRegistry& registry = {});

// TypeDefSetFromYaml creates a TypeDefSet from a YAML string.
//
// Supports either a top-level YAML sequence of type definitions or a YAML map
// containing a `types` sequence key. Schema-specific properties
// under `schemas` are decoded by delegating to the corresponding
// `SchemaYaml` looked up in `registry`.
absl::StatusOr<TypeDefSet> TypeDefSetFromYaml(
    absl::string_view yaml, const SchemaYamlRegistry& registry = {});

// TypeDefToYaml serializes a single TypeDef as a YAML mapping to `os`.
//
// Schema-specific properties under `schemas` are encoded by
// delegating to the corresponding `SchemaYaml` looked up in `registry`.
absl::Status TypeDefToYaml(const TypeDef& type_def, std::ostream& os,
                           const TypeDefToYamlOptions& options = {},
                           const SchemaYamlRegistry& registry = {});

absl::Status TypeDefToYaml(const TypeDef& type_def,
                           const SchemaYamlRegistry& registry, std::ostream& os,
                           const TypeDefToYamlOptions& options = {});

// TypeDefsToYaml serializes a sequence of TypeDefs as a YAML sequence to `os`.
//
// Schema-specific properties under `schemas` are encoded by
// delegating to the corresponding `SchemaYaml` looked up in `registry`.
absl::Status TypeDefsToYaml(absl::Span<const TypeDef> type_defs,
                            std::ostream& os,
                            const TypeDefToYamlOptions& options = {},
                            const SchemaYamlRegistry& registry = {});

absl::Status TypeDefsToYaml(absl::Span<const TypeDef> type_defs,
                            const SchemaYamlRegistry& registry,
                            std::ostream& os,
                            const TypeDefToYamlOptions& options = {});

// TypeDefSetToYaml serializes a TypeDefSet as a YAML sequence to `os`.
//
// Schema-specific properties under `schemas` are encoded by
// delegating to the corresponding `SchemaYaml` looked up in `registry`.
absl::Status TypeDefSetToYaml(const TypeDefSet& type_def_set, std::ostream& os,
                              const TypeDefToYamlOptions& options = {},
                              const SchemaYamlRegistry& registry = {});

absl::Status TypeDefSetToYaml(const TypeDefSet& type_def_set,
                              const SchemaYamlRegistry& registry,
                              std::ostream& os,
                              const TypeDefToYamlOptions& options = {});

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_TYPEDEF_YAML_H_
