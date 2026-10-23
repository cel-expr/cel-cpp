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

#ifndef THIRD_PARTY_CEL_CPP_EXTENSIONS_SET_MEMBERSHIP_OPTIMIZATION_H_
#define THIRD_PARTY_CEL_CPP_EXTENSIONS_SET_MEMBERSHIP_OPTIMIZATION_H_

#include "absl/status/status.h"
#include "eval/compiler/flat_expr_builder_extensions.h"
#include "runtime/runtime_builder.h"

namespace cel::extensions {

// Options for set membership optimization.
// TODO(b/572083203): Add options for non-bitset constant list optimizations.
struct SetMembershipOptimizationOptions {};

// Enable set membership optimization on the given RuntimeBuilder.
//
// This should only be called *once* on a given runtime builder.
//
// This optimization assumes that `enable_heterogeneous_equality` is enabled.
//
// When enabled, the runtime will attempt to optimize branches that appear
// to implement a set membership tests against a constant list. For small
// integer lists, this is implemented via a precomputed bitset. list<int> is
// emphasized to cover the common case of testing protobuf enums against a
// constant list.
//
// Examples:
// foo in [EnumA, EnumB, EnumC]
// sets.contains(foos, [EnumA, EnumB, EnumC]) // foos has all
// sets.intersects(foos, [EnumA, EnumB, EnumC]) // foos has any
// sets.equivalent(foos, [EnumA, EnumB, EnumC])
//
// Should be called after `EnableConstantFolding` if enabled.
absl::Status EnableSetMembershipOptimization(
    cel::RuntimeBuilder& builder,
    const SetMembershipOptimizationOptions& options = {});

// ===============================================================
// Implementation details -- CEL users should not depend on these.
// Exposed here for enabling on Legacy APIs. They expose internal details
// which are not guaranteed to be stable.
// ===============================================================

google::api::expr::runtime::ProgramOptimizerFactory
CreateSetMembershipOptimizationProgramOptimizer(
    const SetMembershipOptimizationOptions& options = {});

}  // namespace cel::extensions

#endif  // THIRD_PARTY_CEL_CPP_EXTENSIONS_SET_MEMBERSHIP_OPTIMIZATION_H_
