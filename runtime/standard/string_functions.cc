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

#include "runtime/standard/string_functions.h"

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "base/builtins.h"
#include "common/function_descriptor.h"
#include "common/kind.h"
#include "common/value.h"
#include "common/value_kind.h"
#include "internal/status_macros.h"
#include "runtime/function.h"
#include "runtime/function_registry.h"
#include "runtime/runtime_options.h"

namespace cel {
namespace {

class StringFunction final : public Function {
 public:
  enum class Op { kContains, kEndsWith, kStartsWith, kConcat, kSize };

  explicit StringFunction(Op op) : op_(op) {}

  absl::StatusOr<Value> Invoke(absl::Span<const Value> args,
                               const InvokeContext& context) const override {
    if (op_ == Op::kSize) {
      if (args.size() != 1) {
        return absl::InvalidArgumentError(
            "unexpected number of arguments for unary function");
      }
      switch (args[0].kind()) {
        case ValueKind::kString:
          return IntValue(args[0].GetString().Size());
        case ValueKind::kBytes:
          return IntValue(args[0].GetBytes().Size());
        default:
          return absl::InvalidArgumentError("expected string or bytes value");
      }
    }

    if (args.size() != 2) {
      return absl::InvalidArgumentError(
          "unexpected number of arguments for binary function");
    }
    if (op_ == Op::kConcat) {
      if (args[0].kind() != args[1].kind()) {
        return absl::InvalidArgumentError("expected matching types for concat");
      }
      switch (args[0].kind()) {
        case ValueKind::kString:
          return StringValue::Concat(args[0].GetString(), args[1].GetString(),
                                     context.arena());
        case ValueKind::kBytes:
          return BytesValue::Concat(args[0].GetBytes(), args[1].GetBytes(),
                                    context.arena());
        default:
          return absl::InvalidArgumentError("expected string or bytes value");
      }
    }

    if (!args[0].IsString() || !args[1].IsString()) {
      return absl::InvalidArgumentError("expected string value");
    }
    const StringValue& value = args[0].GetString();
    const StringValue& substr = args[1].GetString();
    switch (op_) {
      case Op::kContains:
        return BoolValue(value.Contains(substr));
      case Op::kEndsWith:
        return BoolValue(value.EndsWith(substr));
      case Op::kStartsWith:
        return BoolValue(value.StartsWith(substr));
      case Op::kConcat:
      case Op::kSize:
        return absl::InternalError("unreachable");
    }
  }

 private:
  Op op_;
};

absl::Status RegisterSizeFunctions(FunctionRegistry& registry) {
  for (bool receiver_style : {true, false}) {
    for (Kind kind : {Kind::kString, Kind::kBytes}) {
      CEL_RETURN_IF_ERROR(registry.Register(
          FunctionDescriptor(cel::builtin::kSize, receiver_style, {kind}),
          std::make_unique<StringFunction>(StringFunction::Op::kSize)));
    }
  }
  return absl::OkStatus();
}

absl::Status RegisterConcatFunctions(FunctionRegistry& registry) {
  for (Kind kind : {Kind::kString, Kind::kBytes}) {
    CEL_RETURN_IF_ERROR(registry.Register(
        FunctionDescriptor(cel::builtin::kAdd, /*is_receiver_style=*/false,
                           {kind, kind}),
        std::make_unique<StringFunction>(StringFunction::Op::kConcat)));
  }
  return absl::OkStatus();
}

}  // namespace

absl::Status RegisterStringFunctions(FunctionRegistry& registry,
                                     const RuntimeOptions& options) {
  // Basic substring tests (contains, startsWith, endsWith)
  static constexpr struct {
    absl::string_view name;
    StringFunction::Op op;
  } kSubstringOps[] = {
      {cel::builtin::kStringContains, StringFunction::Op::kContains},
      {cel::builtin::kStringEndsWith, StringFunction::Op::kEndsWith},
      {cel::builtin::kStringStartsWith, StringFunction::Op::kStartsWith},
  };
  for (bool receiver_style : {true, false}) {
    for (const auto& entry : kSubstringOps) {
      CEL_RETURN_IF_ERROR(
          registry.Register(FunctionDescriptor(entry.name, receiver_style,
                                               {Kind::kString, Kind::kString}),
                            std::make_unique<StringFunction>(entry.op)));
    }
  }

  // string concatenation if enabled
  if (options.enable_string_concat) {
    CEL_RETURN_IF_ERROR(RegisterConcatFunctions(registry));
  }

  return RegisterSizeFunctions(registry);
}

}  // namespace cel
