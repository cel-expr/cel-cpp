// Copyright 2021 Google LLC
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

#include "runtime/standard/comparison_functions.h"

#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "base/builtins.h"
#include "common/function_descriptor.h"
#include "common/kind.h"
#include "common/value.h"
#include "common/value_kind.h"
#include "internal/number.h"
#include "internal/status_macros.h"
#include "runtime/function.h"
#include "runtime/function_registry.h"
#include "runtime/runtime_options.h"

namespace cel {

namespace {

using ::cel::internal::Number;

absl::optional<Number> NumberFromValue(const Value& value) {
  switch (value.kind()) {
    case ValueKind::kInt:
      return Number::FromInt64(value.GetInt().NativeValue());
    case ValueKind::kUint:
      return Number::FromUint64(value.GetUint().NativeValue());
    case ValueKind::kDouble:
      return Number::FromDouble(value.GetDouble().NativeValue());
    default:
      return absl::nullopt;
  }
}

class ComparisonFunction final : public Function {
 public:
  enum class Op { kLess, kLessOrEqual, kGreater, kGreaterOrEqual };

  explicit ComparisonFunction(Op op) : op_(op) {}

  absl::StatusOr<Value> Invoke(absl::Span<const Value> args,
                               const InvokeContext&) const override {
    if (args.size() != 2) {
      return absl::InvalidArgumentError(
          "unexpected number of arguments for binary function");
    }
    if (args[0].kind() == args[1].kind()) {
      switch (args[0].kind()) {
        case ValueKind::kBool:
          return BoolValue(CompareOrdered(args[0].GetBool().NativeValue(),
                                          args[1].GetBool().NativeValue()));
        case ValueKind::kInt:
          return BoolValue(CompareOrdered(args[0].GetInt().NativeValue(),
                                          args[1].GetInt().NativeValue()));
        case ValueKind::kUint:
          return BoolValue(CompareOrdered(args[0].GetUint().NativeValue(),
                                          args[1].GetUint().NativeValue()));
        case ValueKind::kDouble:
          return BoolValue(CompareOrdered(args[0].GetDouble().NativeValue(),
                                          args[1].GetDouble().NativeValue()));
        case ValueKind::kString: {
          int cmp = args[0].GetString().Compare(args[1].GetString());
          return BoolValue(CompareThreeWay(cmp));
        }
        case ValueKind::kBytes: {
          int cmp = args[0].GetBytes().Compare(args[1].GetBytes());
          return BoolValue(CompareThreeWay(cmp));
        }
        case ValueKind::kDuration:
          return BoolValue(CompareOrdered(args[0].GetDuration().ToDuration(),
                                          args[1].GetDuration().ToDuration()));
        case ValueKind::kTimestamp:
          return BoolValue(CompareOrdered(args[0].GetTimestamp().ToTime(),
                                          args[1].GetTimestamp().ToTime()));
        default:
          return absl::InvalidArgumentError("expected comparable value");
      }
    }

    absl::optional<Number> lhs = NumberFromValue(args[0]);
    absl::optional<Number> rhs = NumberFromValue(args[1]);
    if (!lhs.has_value() || !rhs.has_value()) {
      return absl::InvalidArgumentError("expected numeric value");
    }
    return BoolValue(CompareOrdered(*lhs, *rhs));
  }

 private:
  template <typename T>
  bool CompareOrdered(const T& lhs, const T& rhs) const {
    switch (op_) {
      case Op::kLess:
        return lhs < rhs;
      case Op::kLessOrEqual:
        return lhs <= rhs;
      case Op::kGreater:
        return lhs > rhs;
      case Op::kGreaterOrEqual:
        return lhs >= rhs;
    }
  }

  bool CompareThreeWay(int cmp) const {
    switch (op_) {
      case Op::kLess:
        return cmp < 0;
      case Op::kLessOrEqual:
        return cmp <= 0;
      case Op::kGreater:
        return cmp > 0;
      case Op::kGreaterOrEqual:
        return cmp >= 0;
    }
  }

  Op op_;
};

absl::Status RegisterComparisonOverloads(FunctionRegistry& registry, Kind lhs,
                                         Kind rhs) {
  static constexpr struct {
    absl::string_view name;
    ComparisonFunction::Op op;
  } kOps[] = {
      {cel::builtin::kLess, ComparisonFunction::Op::kLess},
      {cel::builtin::kLessOrEqual, ComparisonFunction::Op::kLessOrEqual},
      {cel::builtin::kGreater, ComparisonFunction::Op::kGreater},
      {cel::builtin::kGreaterOrEqual, ComparisonFunction::Op::kGreaterOrEqual},
  };
  for (const auto& entry : kOps) {
    CEL_RETURN_IF_ERROR(registry.Register(
        FunctionDescriptor(entry.name, /*is_receiver_style=*/false, {lhs, rhs}),
        std::make_unique<ComparisonFunction>(entry.op)));
  }
  return absl::OkStatus();
}

absl::Status RegisterHomogenousComparisonFunctions(
    cel::FunctionRegistry& registry) {
  static constexpr Kind kOrderableKinds[] = {
      Kind::kBool,   Kind::kInt,   Kind::kUint,     Kind::kDouble,
      Kind::kString, Kind::kBytes, Kind::kDuration, Kind::kTimestamp,
  };
  for (Kind kind : kOrderableKinds) {
    CEL_RETURN_IF_ERROR(RegisterComparisonOverloads(registry, kind, kind));
  }
  return absl::OkStatus();
}

absl::Status RegisterHeterogeneousComparisonFunctions(
    cel::FunctionRegistry& registry) {
  static constexpr std::pair<Kind, Kind> kCrossNumericKinds[] = {
      {Kind::kDouble, Kind::kInt},  {Kind::kDouble, Kind::kUint},
      {Kind::kUint, Kind::kDouble}, {Kind::kUint, Kind::kInt},
      {Kind::kInt, Kind::kDouble},  {Kind::kInt, Kind::kUint},
  };
  for (const auto& pair : kCrossNumericKinds) {
    CEL_RETURN_IF_ERROR(
        RegisterComparisonOverloads(registry, pair.first, pair.second));
  }
  return RegisterHomogenousComparisonFunctions(registry);
}

}  // namespace

absl::Status RegisterComparisonFunctions(FunctionRegistry& registry,
                                         const RuntimeOptions& options) {
  if (options.enable_heterogeneous_equality) {
    CEL_RETURN_IF_ERROR(RegisterHeterogeneousComparisonFunctions(registry));
  } else {
    CEL_RETURN_IF_ERROR(RegisterHomogenousComparisonFunctions(registry));
  }
  return absl::OkStatus();
}

}  // namespace cel
