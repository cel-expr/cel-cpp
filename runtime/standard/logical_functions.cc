// Copyright 2022 Google LLC
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

#include "runtime/standard/logical_functions.h"

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "base/builtins.h"
#include "common/function_descriptor.h"
#include "common/kind.h"
#include "common/value.h"
#include "internal/status_macros.h"
#include "runtime/function.h"
#include "runtime/function_registry.h"
#include "runtime/internal/errors.h"
#include "runtime/runtime_options.h"

namespace cel {
namespace {

using ::cel::runtime_internal::CreateNoMatchingOverloadError;

class LogicalNotFunction final : public Function {
 public:
  absl::StatusOr<Value> Invoke(absl::Span<const Value> args,
                               const InvokeContext&) const override {
    if (args.size() != 1) {
      return absl::InvalidArgumentError(
          "unexpected number of arguments for unary function");
    }
    if (!args[0].IsBool()) {
      return absl::InvalidArgumentError("expected bool value");
    }
    return BoolValue(!args[0].GetBool().NativeValue());
  }
};

class NotStrictlyFalseFunction final : public Function {
 public:
  absl::StatusOr<Value> Invoke(absl::Span<const Value> args,
                               const InvokeContext& context) const override {
    if (args.size() != 1) {
      return absl::InvalidArgumentError(
          "unexpected number of arguments for unary function");
    }
    const Value& value = args[0];
    if (value.IsBool()) {
      return value;
    }

    if (value.IsError() || value.IsUnknown()) {
      return TrueValue();
    }

    // Should only accept bool unknown or error.
    return ErrorValue::From(
        CreateNoMatchingOverloadError(builtin::kNotStrictlyFalse),
        context.arena());
  }
};

}  // namespace

absl::Status RegisterLogicalFunctions(FunctionRegistry& registry,
                                      const RuntimeOptions& options) {
  // logical NOT
  CEL_RETURN_IF_ERROR(registry.Register(
      FunctionDescriptor(builtin::kNot, /*is_receiver_style=*/false,
                         {Kind::kBool}),
      std::make_unique<LogicalNotFunction>()));

  // Strictness
  CEL_RETURN_IF_ERROR(registry.Register(
      FunctionDescriptor(builtin::kNotStrictlyFalse,
                         /*is_receiver_style=*/false, {Kind::kAny},
                         /*is_strict=*/false),
      std::make_unique<NotStrictlyFalseFunction>()));

  return registry.Register(
      FunctionDescriptor(builtin::kNotStrictlyFalseDeprecated,
                         /*is_receiver_style=*/false, {Kind::kAny},
                         /*is_strict=*/false),
      std::make_unique<NotStrictlyFalseFunction>());
}

}  // namespace cel
