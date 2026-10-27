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

#ifndef THIRD_PARTY_CEL_CPP_RUNTIME_OPTIMIZED_RUNTIME_BUILDER_FACTORY_H_
#define THIRD_PARTY_CEL_CPP_RUNTIME_OPTIMIZED_RUNTIME_BUILDER_FACTORY_H_

#include <memory>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/status/statusor.h"
#include "runtime/runtime_builder.h"
#include "runtime/runtime_options.h"
#include "google/protobuf/descriptor.h"

namespace cel {

// Returns `RuntimeOptions` preconfigured with the default optimization flags
// used by `CreateOptimizedRuntimeBuilder`:
// - `enable_comprehension_list_append = true`
// - `enable_comprehension_mutable_map = true`
// - `enable_subexpression_inlining = true`
inline RuntimeOptions OptimizedRuntimeOptions() {
  RuntimeOptions options;
  options.enable_comprehension_list_append = true;
  options.enable_comprehension_mutable_map = true;
  options.enable_subexpression_inlining = true;
  return options;
}

// Creates a `RuntimeBuilder` preconfigured with CEL standard definitions and
// extended planner and runtime optimizations enabled.
//
// In addition to the standard functions registered by
// `CreateStandardRuntimeBuilder`, this factory enables:
// - In-place mutable list and map construction for comprehensions
//   (`enable_comprehension_list_append` and
//   `enable_comprehension_mutable_map` via `OptimizedRuntimeOptions`).
// - Inlining of trivial lazy subexpressions bound via `cel.bind` or
//   `cel.@block` (`enable_subexpression_inlining` via
//   `OptimizedRuntimeOptions`).
// - Qualified reference resolution for type-checked expressions
//   (`EnableReferenceResolver` with `kCheckedExpressionOnly`).
// - Constant folding (`extensions::EnableConstantFolding`).
// - Regular expression precompilation
//   (`extensions::EnableRegexPrecompilation`).
// - Batched struct/map/list select path optimization
//   (`extensions::EnableSelectOptimization`).
// - Precomputed bitest and hash set optimization for constant integer/enum and
//   string set membership checks
//   (`extensions::EnableSetMembershipOptimization`).
//
// When passing custom `RuntimeOptions`, initialize from
// `OptimizedRuntimeOptions()` to retain the option-controlled optimizations.
//
// Trade-offs and requirements:
// - **Type-checked ASTs**: These optimizations generally expect a validated,
//   type-checked AST (e.g., produced by `cel::Compiler`). Parse-only ASTs will
//   skip type-dependent optimizations (such as reference resolution, select
//   optimization, and typed list optimizations), and hand-rolled ASTs
//   that violate comprehension accumulator invariants are not safe with
//   in-place comprehension mutations.
// - **Higher planning cost**: `Runtime::CreateProgram` performs additional AST
//   rewrites, type lookups, constant sub-expression evaluation, and regex
//   compilation. This increases planning latency and memory usage in exchange
//   for faster program evaluation, making it best suited for workflows that
//   plan once and evaluate many times.
// - **Larger binary footprint**: Linking this library pulls in the extended
//   optimizer passes and specialized evaluation steps.
// - **Tracing fidelity**: Optimizations rewrite the AST and collapse multiple
//   AST nodes (such as constant subtrees, select chains, or set membership
//   expressions) into single execution steps. As a result, evaluation traces
//   (`Runtime::Trace`) will not invoke callbacks for elided intermediate nodes
//   and can be difficult to map back to the original input AST.
//
// See `CreateRuntimeBuilder` for a description of the requirements related to
// `descriptor_pool`.
absl::StatusOr<RuntimeBuilder> CreateOptimizedRuntimeBuilder(
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool
        ABSL_ATTRIBUTE_LIFETIME_BOUND,
    const RuntimeOptions& options = OptimizedRuntimeOptions());
absl::StatusOr<RuntimeBuilder> CreateOptimizedRuntimeBuilder(
    absl_nonnull std::shared_ptr<const google::protobuf::DescriptorPool> descriptor_pool,
    const RuntimeOptions& options = OptimizedRuntimeOptions());

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_RUNTIME_OPTIMIZED_RUNTIME_BUILDER_FACTORY_H_
