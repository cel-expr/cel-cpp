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

#ifndef THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_TYPEDEF_YAML_INTERNAL_H_
#define THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_TYPEDEF_YAML_INTERNAL_H_

#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "common/typedef/schema_yaml.h"
#include "common/typedef/typedef.h"
#include "common/typedef/typedef_yaml.h"
#include "yaml-cpp/emitter.h"
#include "yaml-cpp/node/node.h"

namespace cel::internal {

absl::StatusOr<TypeDef> ParseTypeDefNode(
    absl::string_view yaml, const YAML::Node& node,
    const SchemaYamlRegistry& registry = {});

absl::StatusOr<std::vector<TypeDef>> ParseTypeDefsNode(
    absl::string_view yaml, const YAML::Node& seq_node,
    const SchemaYamlRegistry& registry = {});

absl::Status EmitTypeDefNode(const TypeDef& type_def, YAML::Emitter& out,
                             const TypeDefToYamlOptions& options,
                             const SchemaYamlRegistry& registry = {});

absl::Status EmitTypeDefsSeq(absl::Span<const TypeDef> type_defs,
                             YAML::Emitter& out,
                             const TypeDefToYamlOptions& options,
                             const SchemaYamlRegistry& registry = {});

}  // namespace cel::internal

#endif  // THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_TYPEDEF_YAML_INTERNAL_H_
