// Copyright 2026 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef THIRD_PARTY_CEL_CPP_POLICY_COMPILER_H_
#define THIRD_PARTY_CEL_CPP_POLICY_COMPILER_H_

#include "absl/status/statusor.h"
#include "compiler/compiler.h"
#include "policy/cel_policy.h"
#include "policy/cel_policy_validation_result.h"
#include "google/protobuf/arena.h"

namespace cel {

struct CompilePolicyOptions {
  // If greater than 0, the compiler will attempt to unnest rule branches
  // at the specified height. The overall height of the final AST may exceed
  // this by a small, fixed margin.
  //
  // To avoid slicing comprehensions, subexpressions within comprehensions
  // are not eligible for unnesting. If the height limit cannot be accommodated,
  // an error with code InvalidArgument is returned.
  //
  // If the AST is converted to proto, even relatively low levels of nesting
  // can cause problems in serialization/deserialization. This does not apply
  // if the AST is used directly by the runtime.
  int unnesting_height_limit = 0;

  google::protobuf::Arena* arena = nullptr;
};

// Compiles a CEL policy using the provided CEL compiler as a base environment.
//
// TODO(b/506179116): Implementation in progress. Functionally complete,
// but errors are not consistent with other implementations.
absl::StatusOr<CelPolicyValidationResult> CompilePolicy(
    const Compiler& compiler, const CelPolicy& policy,
    const CompilePolicyOptions& options = {});

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_POLICY_COMPILER_H_
