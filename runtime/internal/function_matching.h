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

#ifndef THIRD_PARTY_CEL_CPP_RUNTIME_INTERNAL_FUNCTION_MATCHING_H_
#define THIRD_PARTY_CEL_CPP_RUNTIME_INTERNAL_FUNCTION_MATCHING_H_

#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "common/function_descriptor.h"
#include "common/value.h"
#include "runtime/function.h"

namespace cel::runtime_internal {

// These guards include the implementation's strictness. A registration's
// strictness does not replace that of the implementation supplied at runtime.
absl::StatusOr<bool> MatchFunctionArguments(
    const FunctionDescriptor& implementation, absl::Span<const Value> arguments,
    const Function::InvokeContext& context, bool type_level);

absl::StatusOr<bool> MatchFunctionArguments(
    const FunctionDescriptor& registration,
    const FunctionDescriptor& implementation, absl::Span<const Value> arguments,
    const Function::InvokeContext& context, bool type_level);

// Tests only: bypass the shallow preflight to independently check its
// conservativity. Retains the normal guards and the shared content solver.
absl::StatusOr<bool> MatchFunctionArgumentsContentForTesting(
    const FunctionDescriptor& implementation, absl::Span<const Value> arguments,
    const Function::InvokeContext& context);

absl::StatusOr<bool> MatchFunctionArgumentsContentForTesting(
    const FunctionDescriptor& registration,
    const FunctionDescriptor& implementation, absl::Span<const Value> arguments,
    const Function::InvokeContext& context);

}  // namespace cel::runtime_internal

#endif  // THIRD_PARTY_CEL_CPP_RUNTIME_INTERNAL_FUNCTION_MATCHING_H_
