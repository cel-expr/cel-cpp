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

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "common/typedef/aspect_yaml.h"
#include "common/typedef/typedef.h"

namespace cel {

struct TypeDefToYamlOptions {
  // Whether to use concise type signatures (e.g., `type: "list<string>"`)
  // instead of structured type definitions (`type_name: "list"`, `params: ...`)
  // in the output YAML.
  bool use_type_signatures = true;
  // Optional registry of AspectYaml codecs used to encode aspect
  // properties under `aspects`.
  const AspectYamlRegistry* absl_nullable registry = nullptr;
};

// TypeDefFromYaml creates a TypeDef from a YAML mapping string.
//
// Aspects under `aspects` are decoded by
// delegating to the corresponding `AspectYaml` looked up in `registry`.
absl::StatusOr<TypeDef> TypeDefFromYaml(
    absl::string_view yaml, const AspectYamlRegistry& registry = {});

// TypeDefToYaml serializes a single TypeDef as a YAML mapping to `os`.
//
// Aspects under `aspects` are encoded by
// delegating to the corresponding `AspectYaml` looked up in `options.registry`.
absl::Status TypeDefToYaml(const TypeDef& type_def, std::ostream& os,
                           const TypeDefToYamlOptions& options = {});

namespace typedef_internal {

absl::StatusOr<TypeDef> ParseTypeDefNode(
    absl::string_view yaml, const YAML::Node& node,
    const AspectYamlRegistry& registry = {});

absl::Status EmitTypeDefNode(const TypeDef& type_def, YAML::Emitter& out,
                             const TypeDefToYamlOptions& options = {});

}  // namespace typedef_internal

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_TYPEDEF_YAML_H_
