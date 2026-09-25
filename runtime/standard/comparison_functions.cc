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

#include <cstdint>

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "base/builtins.h"
#include "base/function_adapter.h"
#include "common/standard_definitions.h"
#include "common/value.h"
#include "internal/number.h"
#include "internal/status_macros.h"
#include "runtime/function_registry.h"
#include "runtime/runtime_options.h"

namespace cel {

namespace {

using ::cel::internal::Number;

// Comparison template functions
template <class Type>
bool LessThan(Type t1, Type t2) {
  return (t1 < t2);
}

template <class Type>
bool LessThanOrEqual(Type t1, Type t2) {
  return (t1 <= t2);
}

template <class Type>
bool GreaterThan(Type t1, Type t2) {
  return LessThan(t2, t1);
}

template <class Type>
bool GreaterThanOrEqual(Type t1, Type t2) {
  return LessThanOrEqual(t2, t1);
}

// String value comparions specializations
template <>
bool LessThan(const StringValue& t1, const StringValue& t2) {
  return t1.Compare(t2) < 0;
}

template <>
bool LessThanOrEqual(const StringValue& t1, const StringValue& t2) {
  return t1.Compare(t2) <= 0;
}

template <>
bool GreaterThan(const StringValue& t1, const StringValue& t2) {
  return t1.Compare(t2) > 0;
}

template <>
bool GreaterThanOrEqual(const StringValue& t1, const StringValue& t2) {
  return t1.Compare(t2) >= 0;
}

// bytes value comparions specializations
template <>
bool LessThan(const BytesValue& t1, const BytesValue& t2) {
  return t1.Compare(t2) < 0;
}

template <>
bool LessThanOrEqual(const BytesValue& t1, const BytesValue& t2) {
  return t1.Compare(t2) <= 0;
}

template <>
bool GreaterThan(const BytesValue& t1, const BytesValue& t2) {
  return t1.Compare(t2) > 0;
}

template <>
bool GreaterThanOrEqual(const BytesValue& t1, const BytesValue& t2) {
  return t1.Compare(t2) >= 0;
}

// Duration comparison specializations
template <>
bool LessThan(absl::Duration t1, absl::Duration t2) {
  return absl::operator<(t1, t2);
}

template <>
bool LessThanOrEqual(absl::Duration t1, absl::Duration t2) {
  return absl::operator<=(t1, t2);
}

template <>
bool GreaterThan(absl::Duration t1, absl::Duration t2) {
  return absl::operator>(t1, t2);
}

template <>
bool GreaterThanOrEqual(absl::Duration t1, absl::Duration t2) {
  return absl::operator>=(t1, t2);
}

// Timestamp comparison specializations
template <>
bool LessThan(absl::Time t1, absl::Time t2) {
  return absl::operator<(t1, t2);
}

template <>
bool LessThanOrEqual(absl::Time t1, absl::Time t2) {
  return absl::operator<=(t1, t2);
}

template <>
bool GreaterThan(absl::Time t1, absl::Time t2) {
  return absl::operator>(t1, t2);
}

template <>
bool GreaterThanOrEqual(absl::Time t1, absl::Time t2) {
  return absl::operator>=(t1, t2);
}

template <typename T, typename U>
bool CrossNumericLessThan(T t, U u) {
  return Number(t) < Number(u);
}

template <typename T, typename U>
bool CrossNumericGreaterThan(T t, U u) {
  return Number(t) > Number(u);
}

template <typename T, typename U>
bool CrossNumericLessOrEqualTo(T t, U u) {
  return Number(t) <= Number(u);
}

template <typename T, typename U>
bool CrossNumericGreaterOrEqualTo(T t, U u) {
  return Number(t) >= Number(u);
}

template <class Type>
absl::Status RegisterComparisonFunctionsForType(
    cel::FunctionRegistry& registry,
    absl::string_view less_overload_id,
    absl::string_view less_or_equal_overload_id,
    absl::string_view greater_overload_id,
    absl::string_view greater_or_equal_overload_id) {
  using FunctionAdapter = BinaryFunctionAdapter<bool, Type, Type>;
  CEL_RETURN_IF_ERROR(
      registry.Register(FunctionAdapter::CreateDescriptor(cel::builtin::kLess,
                                                          less_overload_id,
                                                          false),
                        FunctionAdapter::WrapFunction(LessThan<Type>)));

  CEL_RETURN_IF_ERROR(registry.Register(
      FunctionAdapter::CreateDescriptor(cel::builtin::kLessOrEqual,
                                        less_or_equal_overload_id, false),
      FunctionAdapter::WrapFunction(LessThanOrEqual<Type>)));

  CEL_RETURN_IF_ERROR(registry.Register(
      FunctionAdapter::CreateDescriptor(cel::builtin::kGreater,
                                        greater_overload_id, false),
      FunctionAdapter::WrapFunction(GreaterThan<Type>)));

  CEL_RETURN_IF_ERROR(registry.Register(
      FunctionAdapter::CreateDescriptor(cel::builtin::kGreaterOrEqual,
                                        greater_or_equal_overload_id, false),
      FunctionAdapter::WrapFunction(GreaterThanOrEqual<Type>)));

  return absl::OkStatus();
}

absl::Status RegisterHomogenousComparisonFunctions(
    cel::FunctionRegistry& registry) {
  using cel::StandardOverloadIds;

  CEL_RETURN_IF_ERROR(RegisterComparisonFunctionsForType<bool>(
      registry, StandardOverloadIds::kLessBool,
      StandardOverloadIds::kLessEqualsBool, StandardOverloadIds::kGreaterBool,
      StandardOverloadIds::kGreaterEqualsBool));

  CEL_RETURN_IF_ERROR(RegisterComparisonFunctionsForType<int64_t>(
      registry, StandardOverloadIds::kLessInt,
      StandardOverloadIds::kLessEqualsInt, StandardOverloadIds::kGreaterInt,
      StandardOverloadIds::kGreaterEqualsInt));

  CEL_RETURN_IF_ERROR(RegisterComparisonFunctionsForType<uint64_t>(
      registry, StandardOverloadIds::kLessUint,
      StandardOverloadIds::kLessEqualsUint, StandardOverloadIds::kGreaterUint,
      StandardOverloadIds::kGreaterEqualsUint));

  CEL_RETURN_IF_ERROR(RegisterComparisonFunctionsForType<double>(
      registry, StandardOverloadIds::kLessDouble,
      StandardOverloadIds::kLessEqualsDouble,
      StandardOverloadIds::kGreaterDouble,
      StandardOverloadIds::kGreaterEqualsDouble));

  CEL_RETURN_IF_ERROR(RegisterComparisonFunctionsForType<const StringValue&>(
      registry, StandardOverloadIds::kLessString,
      StandardOverloadIds::kLessEqualsString,
      StandardOverloadIds::kGreaterString,
      StandardOverloadIds::kGreaterEqualsString));

  CEL_RETURN_IF_ERROR(RegisterComparisonFunctionsForType<const BytesValue&>(
      registry, StandardOverloadIds::kLessBytes,
      StandardOverloadIds::kLessEqualsBytes,
      StandardOverloadIds::kGreaterBytes,
      StandardOverloadIds::kGreaterEqualsBytes));

  CEL_RETURN_IF_ERROR(RegisterComparisonFunctionsForType<absl::Duration>(
      registry, StandardOverloadIds::kLessDuration,
      StandardOverloadIds::kLessEqualsDuration,
      StandardOverloadIds::kGreaterDuration,
      StandardOverloadIds::kGreaterEqualsDuration));

  CEL_RETURN_IF_ERROR(RegisterComparisonFunctionsForType<absl::Time>(
      registry, StandardOverloadIds::kLessTimestamp,
      StandardOverloadIds::kLessEqualsTimestamp,
      StandardOverloadIds::kGreaterTimestamp,
      StandardOverloadIds::kGreaterEqualsTimestamp));

  return absl::OkStatus();
}

template <typename T, typename U>
absl::Status RegisterCrossNumericComparisons(cel::FunctionRegistry& registry,
    absl::string_view less_overload_id,
    absl::string_view greater_overload_id,
    absl::string_view greater_or_equal_overload_id,
    absl::string_view less_or_equal_overload_id) {
  using FunctionAdapter = BinaryFunctionAdapter<bool, T, U>;
  CEL_RETURN_IF_ERROR(registry.Register(
      FunctionAdapter::CreateDescriptor(cel::builtin::kLess,
                                        less_overload_id,
                                        /*receiver_style=*/false),
                        FunctionAdapter::WrapFunction(&CrossNumericLessThan<T, U>)));
  CEL_RETURN_IF_ERROR(registry.Register(
      FunctionAdapter::CreateDescriptor(cel::builtin::kGreater,
                                        greater_overload_id,
                                        /*receiver_style=*/false),
      FunctionAdapter::WrapFunction(&CrossNumericGreaterThan<T, U>)));
  CEL_RETURN_IF_ERROR(registry.Register(
      FunctionAdapter::CreateDescriptor(cel::builtin::kGreaterOrEqual,
                                        greater_or_equal_overload_id,
                                        /*receiver_style=*/false),
      FunctionAdapter::WrapFunction(&CrossNumericGreaterOrEqualTo<T, U>)));
  CEL_RETURN_IF_ERROR(registry.Register(
      FunctionAdapter::CreateDescriptor(cel::builtin::kLessOrEqual,
                                        less_or_equal_overload_id,
                                        /*receiver_style=*/false),
      FunctionAdapter::WrapFunction(&CrossNumericLessOrEqualTo<T, U>)));
  return absl::OkStatus();
}

absl::Status RegisterHeterogeneousComparisonFunctions(
    cel::FunctionRegistry& registry) {
  using cel::StandardOverloadIds;

  CEL_RETURN_IF_ERROR(
      (RegisterCrossNumericComparisons<double, int64_t>(registry,
          StandardOverloadIds::kLessDoubleInt,
          StandardOverloadIds::kGreaterDoubleInt,
          StandardOverloadIds::kGreaterEqualsDoubleInt,
          StandardOverloadIds::kLessEqualsDoubleInt)));

  CEL_RETURN_IF_ERROR(
      (RegisterCrossNumericComparisons<double, uint64_t>(registry,
          StandardOverloadIds::kLessDoubleUint,
          StandardOverloadIds::kGreaterDoubleUint,
          StandardOverloadIds::kGreaterEqualsDoubleUint,
          StandardOverloadIds::kLessEqualsDoubleUint)));

  CEL_RETURN_IF_ERROR(
      (RegisterCrossNumericComparisons<uint64_t, double>(registry,
          StandardOverloadIds::kLessUintDouble,
          StandardOverloadIds::kGreaterUintDouble,
          StandardOverloadIds::kGreaterEqualsUintDouble,
          StandardOverloadIds::kLessEqualsUintDouble)));

  CEL_RETURN_IF_ERROR(
      (RegisterCrossNumericComparisons<uint64_t, int64_t>(registry,
          StandardOverloadIds::kLessUintInt,
          StandardOverloadIds::kGreaterUintInt,
          StandardOverloadIds::kGreaterEqualsUintInt,
          StandardOverloadIds::kLessEqualsUintInt)));

  CEL_RETURN_IF_ERROR(
      (RegisterCrossNumericComparisons<int64_t, double>(registry,
          StandardOverloadIds::kLessIntDouble,
          StandardOverloadIds::kGreaterIntDouble,
          StandardOverloadIds::kGreaterEqualsIntDouble,
          StandardOverloadIds::kLessEqualsIntDouble)));

  CEL_RETURN_IF_ERROR(
      (RegisterCrossNumericComparisons<int64_t, uint64_t>(registry,
          StandardOverloadIds::kLessIntUint,
          StandardOverloadIds::kGreaterIntUint,
          StandardOverloadIds::kGreaterEqualsIntUint,
          StandardOverloadIds::kLessEqualsIntUint)));

  CEL_RETURN_IF_ERROR(
      RegisterComparisonFunctionsForType<bool>(registry,
          StandardOverloadIds::kLessBool,
          StandardOverloadIds::kLessEqualsBool,
          StandardOverloadIds::kGreaterBool,
          StandardOverloadIds::kGreaterEqualsBool));

  CEL_RETURN_IF_ERROR(
      RegisterComparisonFunctionsForType<int64_t>(registry,
          StandardOverloadIds::kLessInt,
          StandardOverloadIds::kLessEqualsInt,
          StandardOverloadIds::kGreaterInt,
          StandardOverloadIds::kGreaterEqualsInt));

  CEL_RETURN_IF_ERROR(
      RegisterComparisonFunctionsForType<uint64_t>(registry,
          StandardOverloadIds::kLessUint,
          StandardOverloadIds::kLessEqualsUint,
          StandardOverloadIds::kGreaterUint,
          StandardOverloadIds::kGreaterEqualsUint));

  CEL_RETURN_IF_ERROR(
      RegisterComparisonFunctionsForType<double>(registry,
          StandardOverloadIds::kLessDouble,
          StandardOverloadIds::kLessEqualsDouble,
          StandardOverloadIds::kGreaterDouble,
          StandardOverloadIds::kGreaterEqualsDouble));

  CEL_RETURN_IF_ERROR(
      RegisterComparisonFunctionsForType<const StringValue&>(registry,
          StandardOverloadIds::kLessString,
          StandardOverloadIds::kLessEqualsString,
          StandardOverloadIds::kGreaterString,
          StandardOverloadIds::kGreaterEqualsString));

  CEL_RETURN_IF_ERROR(
      RegisterComparisonFunctionsForType<const BytesValue&>(registry,
          StandardOverloadIds::kLessBytes,
          StandardOverloadIds::kLessEqualsBytes,
          StandardOverloadIds::kGreaterBytes,
          StandardOverloadIds::kGreaterEqualsBytes));

  CEL_RETURN_IF_ERROR(
      RegisterComparisonFunctionsForType<absl::Duration>(registry,
          StandardOverloadIds::kLessDuration,
          StandardOverloadIds::kLessEqualsDuration,
          StandardOverloadIds::kGreaterDuration,
          StandardOverloadIds::kGreaterEqualsDuration));

  CEL_RETURN_IF_ERROR(
      RegisterComparisonFunctionsForType<absl::Time>(registry,
          StandardOverloadIds::kLessTimestamp,
          StandardOverloadIds::kLessEqualsTimestamp,
          StandardOverloadIds::kGreaterTimestamp,
          StandardOverloadIds::kGreaterEqualsTimestamp));

  return absl::OkStatus();
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
