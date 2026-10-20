// Copyright 2023 Google LLC
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

#ifndef THIRD_PARTY_CEL_CPP_EXTENSIONS_SELECT_OPTIMIZATION_H_
#define THIRD_PARTY_CEL_CPP_EXTENSIONS_SELECT_OPTIMIZATION_H_

#include "absl/status/status.h"
#include "common/ast.h"
#include "eval/compiler/flat_expr_builder_extensions.h"
#include "runtime/runtime_builder.h"

namespace cel::extensions {

constexpr char kCelAttribute[] = "cel.@attribute";
constexpr char kCelHasField[] = "cel.@hasField";

// Configuration options for the select optimization.
struct SelectOptimizationOptions {
  // Force the program to use the fallback implementation for the select.
  // This implementation simply collapses the select operation into one program
  // step and calls the normal field accessors on the Struct value.
  //
  // Normally, the fallback implementation is used when the Qualify operation is
  // unimplemented for a given StructType. This option is exposed for testing or
  // to more closely match behavior of unoptimized expressions.
  bool force_fallback_implementation = false;

  // The minimum depth of a select chain to be considered for optimization.
  // For example, if this is set to 2, then `a.b.c` will be optimized, but
  // `a.b` will not be.
  //
  // This is applied in the pre-pass phase that rewrites the AST. If the
  // optimization is already applied by a compiler / static optimizer, this
  // option has no effect.
  int min_field_depth_for_optimization = 0;

  // Whether to try to cache field descriptor lookups for the select
  // optimization at plan time.
  //
  // This is temporary to make the change rollback safe for existing users.
  bool enable_field_descriptor_caching = false;
};

// Enable select optimization on the given RuntimeBuilder, replacing long
// select chains with a single operation. Also enables planning ASTs tagged with
// the `select_optimization` runtime extension.
//
// This assumes that the type information at check time agrees with the
// configured types at runtime.
//
// Important: The select optimization follows spec behavior for traversals.
//  - `enable_empty_wrapper_null_unboxing` is ignored and optimized traversals
//    always operates as though it is `true`.
//  - `enable_heterogeneous_equality` is ignored and optimized traversals
//    always operate as though it is `true`.
//
// This should only be called *once* on a given runtime builder.
//
// Assumes the default runtime implementation, an error with code
// InvalidArgument is returned if it is not.
//
// Note: implementation does not support optional field traversal, and will
// instead revert to the normal implementation instead of trying to optimize.
absl::Status EnableSelectOptimization(
    cel::RuntimeBuilder& builder,
    const SelectOptimizationOptions& options = {});

// ===============================================================
// Implementation details -- CEL users should not depend on these.
// Exposed here for enabling on Legacy APIs. They expose internal details
// which are not guaranteed to be stable.
// ===============================================================

// Scans ast for optimizable select branches.
//
// In general, this should be done by a type checker but may be deferred to
// runtime.
//
// This assumes the runtime type registry has the same definitions as the one
// used by the type checker.
class SelectOptimizationAstUpdater
    : public google::api::expr::runtime::AstTransform {
 public:
  explicit SelectOptimizationAstUpdater(
      const SelectOptimizationOptions& options = {})
      : options_(options) {}

  absl::Status UpdateAst(google::api::expr::runtime::PlannerContext& context,
                         cel::Ast& ast) const override;

 private:
  SelectOptimizationOptions options_;
};

google::api::expr::runtime::ProgramOptimizerFactory
CreateSelectOptimizationProgramOptimizer(
    const SelectOptimizationOptions& options = {});

}  // namespace cel::extensions
#endif  // THIRD_PARTY_CEL_CPP_EXTENSIONS_SELECT_OPTIMIZATION_H_
