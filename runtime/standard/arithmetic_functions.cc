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

#include "runtime/standard/arithmetic_functions.h"

#include <cstdint>
#include <limits>
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
#include "internal/overflow.h"
#include "internal/status_macros.h"
#include "runtime/function.h"
#include "runtime/function_registry.h"
#include "runtime/runtime_options.h"

namespace cel {
namespace {

class ArithmeticBinaryFunction final : public Function {
 public:
  enum class Op { kAdd, kSubtract, kMultiply, kDivide, kModulo };

  explicit ArithmeticBinaryFunction(Op op) : op_(op) {}

  absl::StatusOr<Value> Invoke(absl::Span<const Value> args,
                               const InvokeContext& context) const override {
    if (args.size() != 2) {
      return absl::InvalidArgumentError(
          "unexpected number of arguments for binary function");
    }
    if (args[0].kind() != args[1].kind()) {
      return absl::InvalidArgumentError(
          "unexpected argument kinds for arithmetic function");
    }
    switch (args[0].kind()) {
      case ValueKind::kInt: {
        int64_t lhs = args[0].GetInt().NativeValue();
        int64_t rhs = args[1].GetInt().NativeValue();
        absl::StatusOr<int64_t> result;
        switch (op_) {
          case Op::kAdd:
            result = cel::internal::CheckedAdd(lhs, rhs);
            break;
          case Op::kSubtract:
            result = cel::internal::CheckedSub(lhs, rhs);
            break;
          case Op::kMultiply:
            result = cel::internal::CheckedMul(lhs, rhs);
            break;
          case Op::kDivide:
            result = cel::internal::CheckedDiv(lhs, rhs);
            break;
          case Op::kModulo:
            result = cel::internal::CheckedMod(lhs, rhs);
            break;
        }
        if (!result.ok()) {
          return ErrorValue::From(result.status(), context.arena());
        }
        return IntValue(*result);
      }
      case ValueKind::kUint: {
        uint64_t lhs = args[0].GetUint().NativeValue();
        uint64_t rhs = args[1].GetUint().NativeValue();
        absl::StatusOr<uint64_t> result;
        switch (op_) {
          case Op::kAdd:
            result = cel::internal::CheckedAdd(lhs, rhs);
            break;
          case Op::kSubtract:
            result = cel::internal::CheckedSub(lhs, rhs);
            break;
          case Op::kMultiply:
            result = cel::internal::CheckedMul(lhs, rhs);
            break;
          case Op::kDivide:
            result = cel::internal::CheckedDiv(lhs, rhs);
            break;
          case Op::kModulo:
            result = cel::internal::CheckedMod(lhs, rhs);
            break;
        }
        if (!result.ok()) {
          return ErrorValue::From(result.status(), context.arena());
        }
        return UintValue(*result);
      }
      case ValueKind::kDouble: {
        static_assert(std::numeric_limits<double>::is_iec559,
                      "Division by zero for doubles must be supported");
        double lhs = args[0].GetDouble().NativeValue();
        double rhs = args[1].GetDouble().NativeValue();
        switch (op_) {
          case Op::kAdd:
            return DoubleValue(lhs + rhs);
          case Op::kSubtract:
            return DoubleValue(lhs - rhs);
          case Op::kMultiply:
            return DoubleValue(lhs * rhs);
          case Op::kDivide:
            // For double, division will result in +/- inf
            return DoubleValue(lhs / rhs);
          case Op::kModulo:
            return absl::InvalidArgumentError(
                "modulo is not defined for double");
        }
      }
      default:
        return absl::InvalidArgumentError("expected numeric value");
    }
  }

 private:
  Op op_;
};

class ArithmeticNegateFunction final : public Function {
 public:
  absl::StatusOr<Value> Invoke(absl::Span<const Value> args,
                               const InvokeContext& context) const override {
    if (args.size() != 1) {
      return absl::InvalidArgumentError(
          "unexpected number of arguments for unary function");
    }
    switch (args[0].kind()) {
      case ValueKind::kInt: {
        auto inv =
            cel::internal::CheckedNegation(args[0].GetInt().NativeValue());
        if (!inv.ok()) {
          return ErrorValue::From(inv.status(), context.arena());
        }
        return IntValue(*inv);
      }
      case ValueKind::kDouble:
        return DoubleValue(-args[0].GetDouble().NativeValue());
      default:
        return absl::InvalidArgumentError("expected int or double value");
    }
  }
};

absl::Status RegisterBinaryOverloads(FunctionRegistry& registry,
                                     absl::string_view name,
                                     ArithmeticBinaryFunction::Op op,
                                     absl::Span<const Kind> kinds) {
  for (Kind kind : kinds) {
    CEL_RETURN_IF_ERROR(registry.Register(
        FunctionDescriptor(name, /*is_receiver_style=*/false, {kind, kind}),
        std::make_unique<ArithmeticBinaryFunction>(op)));
  }
  return absl::OkStatus();
}

}  // namespace

absl::Status RegisterArithmeticFunctions(FunctionRegistry& registry,
                                         const RuntimeOptions& options) {
  static constexpr Kind kNumericKinds[] = {Kind::kInt, Kind::kUint,
                                           Kind::kDouble};
  static constexpr Kind kIntegralKinds[] = {Kind::kInt, Kind::kUint};

  CEL_RETURN_IF_ERROR(RegisterBinaryOverloads(
      registry, cel::builtin::kAdd, ArithmeticBinaryFunction::Op::kAdd,
      kNumericKinds));
  CEL_RETURN_IF_ERROR(RegisterBinaryOverloads(
      registry, cel::builtin::kSubtract,
      ArithmeticBinaryFunction::Op::kSubtract, kNumericKinds));
  CEL_RETURN_IF_ERROR(RegisterBinaryOverloads(
      registry, cel::builtin::kMultiply,
      ArithmeticBinaryFunction::Op::kMultiply, kNumericKinds));
  CEL_RETURN_IF_ERROR(RegisterBinaryOverloads(
      registry, cel::builtin::kDivide, ArithmeticBinaryFunction::Op::kDivide,
      kNumericKinds));
  CEL_RETURN_IF_ERROR(RegisterBinaryOverloads(
      registry, cel::builtin::kModulo, ArithmeticBinaryFunction::Op::kModulo,
      kIntegralKinds));

  // Negation group
  CEL_RETURN_IF_ERROR(registry.Register(
      FunctionDescriptor(cel::builtin::kNeg, /*is_receiver_style=*/false,
                         {Kind::kInt}),
      std::make_unique<ArithmeticNegateFunction>()));
  return registry.Register(
      FunctionDescriptor(cel::builtin::kNeg, /*is_receiver_style=*/false,
                         {Kind::kDouble}),
      std::make_unique<ArithmeticNegateFunction>());
}

}  // namespace cel
