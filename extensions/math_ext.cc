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

#include "extensions/math_ext.h"

#include <cmath>
#include <cstdint>
#include <limits>

#include "absl/base/casts.h"
#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "common/casting.h"
#include "common/value.h"
#include "eval/public/cel_function_registry.h"
#include "eval/public/cel_number.h"
#include "eval/public/cel_options.h"
#include "internal/status_macros.h"
#include "runtime/function_adapter.h"
#include "runtime/function_registry.h"
#include "runtime/runtime_options.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"

namespace cel::extensions {

namespace {

using ::google::api::expr::runtime::CelFunctionRegistry;
using ::google::api::expr::runtime::CelNumber;
using ::google::api::expr::runtime::InterpreterOptions;

static constexpr char kMathMin[] = "math.@min";
static constexpr char kMathMax[] = "math.@max";

struct ToValueVisitor {
  Value operator()(uint64_t v) const { return UintValue{v}; }
  Value operator()(int64_t v) const { return IntValue{v}; }
  Value operator()(double v) const { return DoubleValue{v}; }
};

Value NumberToValue(CelNumber number) {
  return number.visit<Value>(ToValueVisitor{});
}

absl::StatusOr<CelNumber> ValueToNumber(const Value& value,
                                        absl::string_view function) {
  if (auto int_value = As<IntValue>(value); int_value) {
    return CelNumber::FromInt64(int_value->NativeValue());
  }
  if (auto uint_value = As<UintValue>(value); uint_value) {
    return CelNumber::FromUint64(uint_value->NativeValue());
  }
  if (auto double_value = As<DoubleValue>(value); double_value) {
    return CelNumber::FromDouble(double_value->NativeValue());
  }
  return absl::InvalidArgumentError(
      absl::StrCat(function, " arguments must be numeric"));
}

CelNumber MinNumber(CelNumber v1, CelNumber v2) {
  if (v2 < v1) {
    return v2;
  }
  return v1;
}

Value MinValue(CelNumber v1, CelNumber v2) {
  return NumberToValue(MinNumber(v1, v2));
}

template <typename T>
Value Identity(T v1) {
  return NumberToValue(CelNumber(v1));
}

template <typename T, typename U>
Value Min(T v1, U v2) {
  return MinValue(CelNumber(v1), CelNumber(v2));
}

absl::StatusOr<Value> MinList(
    const ListValue& values,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena) {
  CEL_ASSIGN_OR_RETURN(auto iterator, values.NewIterator());
  if (!iterator->HasNext()) {
    return ErrorValue(
        absl::InvalidArgumentError("math.@min argument must not be empty"));
  }
  Value value;
  CEL_RETURN_IF_ERROR(
      iterator->Next(descriptor_pool, message_factory, arena, &value));
  absl::StatusOr<CelNumber> current = ValueToNumber(value, kMathMin);
  if (!current.ok()) {
    return ErrorValue{current.status()};
  }
  CelNumber min = *current;
  while (iterator->HasNext()) {
    CEL_RETURN_IF_ERROR(
        iterator->Next(descriptor_pool, message_factory, arena, &value));
    absl::StatusOr<CelNumber> other = ValueToNumber(value, kMathMin);
    if (!other.ok()) {
      return ErrorValue{other.status()};
    }
    min = MinNumber(min, *other);
  }
  return NumberToValue(min);
}

CelNumber MaxNumber(CelNumber v1, CelNumber v2) {
  if (v2 > v1) {
    return v2;
  }
  return v1;
}

Value MaxValue(CelNumber v1, CelNumber v2) {
  return NumberToValue(MaxNumber(v1, v2));
}

template <typename T, typename U>
Value Max(T v1, U v2) {
  return MaxValue(CelNumber(v1), CelNumber(v2));
}

absl::StatusOr<Value> MaxList(
    const ListValue& values,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena) {
  CEL_ASSIGN_OR_RETURN(auto iterator, values.NewIterator());
  if (!iterator->HasNext()) {
    return ErrorValue(
        absl::InvalidArgumentError("math.@max argument must not be empty"));
  }
  Value value;
  CEL_RETURN_IF_ERROR(
      iterator->Next(descriptor_pool, message_factory, arena, &value));
  absl::StatusOr<CelNumber> current = ValueToNumber(value, kMathMax);
  if (!current.ok()) {
    return ErrorValue{current.status()};
  }
  CelNumber min = *current;
  while (iterator->HasNext()) {
    CEL_RETURN_IF_ERROR(
        iterator->Next(descriptor_pool, message_factory, arena, &value));
    absl::StatusOr<CelNumber> other = ValueToNumber(value, kMathMax);
    if (!other.ok()) {
      return ErrorValue{other.status()};
    }
    min = MaxNumber(min, *other);
  }
  return NumberToValue(min);
}

template <typename T, typename U>
absl::Status RegisterCrossNumericMin(absl::string_view overload_id_tu,
                                     absl::string_view overload_id_ut,
                                     FunctionRegistry& registry) {
  CEL_RETURN_IF_ERROR(
      (BinaryFunctionAdapter<Value, T, U>::RegisterGlobalOverload(
          kMathMin, overload_id_tu, Min<T, U>, registry)));

  CEL_RETURN_IF_ERROR(
      (BinaryFunctionAdapter<Value, U, T>::RegisterGlobalOverload(
          kMathMin, overload_id_ut, Min<U, T>, registry)));

  return absl::OkStatus();
}

template <typename T, typename U>
absl::Status RegisterCrossNumericMax(absl::string_view overload_id_tu,
                                     absl::string_view overload_id_ut,
                                     FunctionRegistry& registry) {
  CEL_RETURN_IF_ERROR(
      (BinaryFunctionAdapter<Value, T, U>::RegisterGlobalOverload(
          kMathMax, overload_id_tu, Max<T, U>, registry)));

  CEL_RETURN_IF_ERROR(
      (BinaryFunctionAdapter<Value, U, T>::RegisterGlobalOverload(
          kMathMax, overload_id_ut, Max<U, T>, registry)));

  return absl::OkStatus();
}

double CeilDouble(double value) { return std::ceil(value); }

double FloorDouble(double value) { return std::floor(value); }

double RoundDouble(double value) { return std::round(value); }

double TruncDouble(double value) { return std::trunc(value); }

double SqrtDouble(double value) { return std::sqrt(value); }

double SqrtInt(int64_t value) { return std::sqrt(value); }

double SqrtUint(uint64_t value) { return std::sqrt(value); }

bool IsInfDouble(double value) { return std::isinf(value); }

bool IsNaNDouble(double value) { return std::isnan(value); }

bool IsFiniteDouble(double value) { return std::isfinite(value); }

double AbsDouble(double value) { return std::fabs(value); }

Value AbsInt(int64_t value) {
  if (ABSL_PREDICT_FALSE(value == std::numeric_limits<int64_t>::min())) {
    return ErrorValue(absl::InvalidArgumentError("integer overflow"));
  }
  return IntValue(value < 0 ? -value : value);
}

uint64_t AbsUint(uint64_t value) { return value; }

double SignDouble(double value) {
  if (std::isnan(value)) {
    return value;
  }
  if (value == 0.0) {
    return 0.0;
  }
  return std::signbit(value) ? -1.0 : 1.0;
}

int64_t SignInt(int64_t value) { return value < 0 ? -1 : value > 0 ? 1 : 0; }

uint64_t SignUint(uint64_t value) { return value == 0 ? 0 : 1; }

int64_t BitAndInt(int64_t lhs, int64_t rhs) { return lhs & rhs; }

uint64_t BitAndUint(uint64_t lhs, uint64_t rhs) { return lhs & rhs; }

int64_t BitOrInt(int64_t lhs, int64_t rhs) { return lhs | rhs; }

uint64_t BitOrUint(uint64_t lhs, uint64_t rhs) { return lhs | rhs; }

int64_t BitXorInt(int64_t lhs, int64_t rhs) { return lhs ^ rhs; }

uint64_t BitXorUint(uint64_t lhs, uint64_t rhs) { return lhs ^ rhs; }

int64_t BitNotInt(int64_t value) { return ~value; }

uint64_t BitNotUint(uint64_t value) { return ~value; }

Value BitShiftLeftInt(int64_t lhs, int64_t rhs) {
  if (ABSL_PREDICT_FALSE(rhs < 0)) {
    return ErrorValue(absl::InvalidArgumentError(
        absl::StrCat("math.bitShiftLeft() invalid negative shift: ", rhs)));
  }
  if (rhs > 63) {
    return IntValue(0);
  }
  // Shift in the unsigned domain to avoid undefined behaviour when lhs is
  // negative or the shift moves bits into the sign bit, matching the bit
  // pattern semantics already used by bitShiftRight.
  return IntValue(absl::bit_cast<int64_t>(absl::bit_cast<uint64_t>(lhs)
                                          << static_cast<int>(rhs)));
}

Value BitShiftLeftUint(uint64_t lhs, int64_t rhs) {
  if (ABSL_PREDICT_FALSE(rhs < 0)) {
    return ErrorValue(absl::InvalidArgumentError(
        absl::StrCat("math.bitShiftLeft() invalid negative shift: ", rhs)));
  }
  if (rhs > 63) {
    return UintValue(0);
  }
  return UintValue(lhs << static_cast<int>(rhs));
}

Value BitShiftRightInt(int64_t lhs, int64_t rhs) {
  if (ABSL_PREDICT_FALSE(rhs < 0)) {
    return ErrorValue(absl::InvalidArgumentError(
        absl::StrCat("math.bitShiftRight() invalid negative shift: ", rhs)));
  }
  if (rhs > 63) {
    return IntValue(0);
  }
  // We do not perform a sign extension shift, per the spec we just do the same
  // thing as uint.
  return IntValue(absl::bit_cast<int64_t>(absl::bit_cast<uint64_t>(lhs) >>
                                          static_cast<int>(rhs)));
}

Value BitShiftRightUint(uint64_t lhs, int64_t rhs) {
  if (ABSL_PREDICT_FALSE(rhs < 0)) {
    return ErrorValue(absl::InvalidArgumentError(
        absl::StrCat("math.bitShiftRight() invalid negative shift: ", rhs)));
  }
  if (rhs > 63) {
    return UintValue(0);
  }
  return UintValue(lhs >> static_cast<int>(rhs));
}

}  // namespace

absl::Status RegisterMathExtensionFunctions(FunctionRegistry& registry,
                                            const RuntimeOptions& options,
                                            int version) {
  // Overload IDs matching math_ext_decls.cc
  static constexpr absl::string_view kMathMinInt = "math_@min_int";
  static constexpr absl::string_view kMathMinDouble = "math_@min_double";
  static constexpr absl::string_view kMathMinUint = "math_@min_uint";
  static constexpr absl::string_view kMathMinIntInt = "math_@min_int_int";
  static constexpr absl::string_view kMathMinIntUint = "math_@min_int_uint";
  static constexpr absl::string_view kMathMinIntDouble = "math_@min_int_double";
  static constexpr absl::string_view kMathMinUintInt = "math_@min_uint_int";
  static constexpr absl::string_view kMathMinUintUint = "math_@min_uint_uint";
  static constexpr absl::string_view kMathMinUintDouble =
      "math_@min_uint_double";
  static constexpr absl::string_view kMathMinDoubleInt = "math_@min_double_int";
  static constexpr absl::string_view kMathMinDoubleUint =
      "math_@min_double_uint";
  static constexpr absl::string_view kMathMinDoubleDouble =
      "math_@min_double_double";

  static constexpr absl::string_view kMathMaxInt = "math_@max_int";
  static constexpr absl::string_view kMathMaxDouble = "math_@max_double";
  static constexpr absl::string_view kMathMaxUint = "math_@max_uint";
  static constexpr absl::string_view kMathMaxIntInt = "math_@max_int_int";
  static constexpr absl::string_view kMathMaxIntUint = "math_@max_int_uint";
  static constexpr absl::string_view kMathMaxIntDouble = "math_@max_int_double";
  static constexpr absl::string_view kMathMaxUintInt = "math_@max_uint_int";
  static constexpr absl::string_view kMathMaxUintUint = "math_@max_uint_uint";
  static constexpr absl::string_view kMathMaxUintDouble =
      "math_@max_uint_double";
  static constexpr absl::string_view kMathMaxDoubleInt = "math_@max_double_int";
  static constexpr absl::string_view kMathMaxDoubleUint =
      "math_@max_double_uint";
  static constexpr absl::string_view kMathMaxDoubleDouble =
      "math_@max_double_double";

  static constexpr absl::string_view kMathCeilDouble = "math_ceil_double";
  static constexpr absl::string_view kMathFloorDouble = "math_floor_double";
  static constexpr absl::string_view kMathRoundDouble = "math_round_double";
  static constexpr absl::string_view kMathTruncDouble = "math_trunc_double";
  static constexpr absl::string_view kMathSqrtInt = "math_sqrt_int";
  static constexpr absl::string_view kMathSqrtUint = "math_sqrt_uint";
  static constexpr absl::string_view kMathSqrtDouble = "math_sqrt_double";
  static constexpr absl::string_view kMathIsInfDouble = "math_isInf_double";
  static constexpr absl::string_view kMathIsNaNDouble = "math_isNaN_double";
  static constexpr absl::string_view kMathIsFiniteDouble =
      "math_isFinite_double";
  static constexpr absl::string_view kMathAbsInt = "math_abs_int";
  static constexpr absl::string_view kMathAbsUint = "math_abs_uint";
  static constexpr absl::string_view kMathAbsDouble = "math_abs_double";
  static constexpr absl::string_view kMathSignInt = "math_sign_int";
  static constexpr absl::string_view kMathSignUint = "math_sign_uint";
  static constexpr absl::string_view kMathSignDouble = "math_sign_double";
  static constexpr absl::string_view kMathBitAndIntInt = "math_bitAnd_int_int";
  static constexpr absl::string_view kMathBitAndUintUint =
      "math_bitAnd_uint_uint";
  static constexpr absl::string_view kMathBitOrIntInt = "math_bitOr_int_int";
  static constexpr absl::string_view kMathBitOrUintUint =
      "math_bitOr_uint_uint";
  static constexpr absl::string_view kMathBitXorIntInt = "math_bitXor_int_int";
  static constexpr absl::string_view kMathBitXorUintUint =
      "math_bitXor_uint_uint";
  static constexpr absl::string_view kMathBitNotIntInt = "math_bitNot_int_int";
  static constexpr absl::string_view kMathBitNotUintUint =
      "math_bitNot_uint_uint";
  static constexpr absl::string_view kMathBitShiftLeftIntInt =
      "math_bitShiftLeft_int_int";
  static constexpr absl::string_view kMathBitShiftLeftUintInt =
      "math_bitShiftLeft_uint_int";
  static constexpr absl::string_view kMathBitShiftRightIntInt =
      "math_bitShiftRight_int_int";
  static constexpr absl::string_view kMathBitShiftRightUintInt =
      "math_bitShiftRight_uint_int";

  CEL_RETURN_IF_ERROR(
      (UnaryFunctionAdapter<Value, int64_t>::RegisterGlobalOverload(
          kMathMin, kMathMinInt, Identity<int64_t>, registry)));
  CEL_RETURN_IF_ERROR(
      (UnaryFunctionAdapter<Value, double>::RegisterGlobalOverload(
          kMathMin, kMathMinDouble, Identity<double>, registry)));
  CEL_RETURN_IF_ERROR(
      (UnaryFunctionAdapter<Value, uint64_t>::RegisterGlobalOverload(
          kMathMin, kMathMinUint, Identity<uint64_t>, registry)));
  CEL_RETURN_IF_ERROR(
      (BinaryFunctionAdapter<Value, int64_t, int64_t>::RegisterGlobalOverload(
          kMathMin, kMathMinIntInt, Min<int64_t, int64_t>, registry)));
  CEL_RETURN_IF_ERROR(
      (BinaryFunctionAdapter<Value, double, double>::RegisterGlobalOverload(
          kMathMin, kMathMinDoubleDouble, Min<double, double>, registry)));
  CEL_RETURN_IF_ERROR(
      (BinaryFunctionAdapter<Value, uint64_t, uint64_t>::RegisterGlobalOverload(
          kMathMin, kMathMinUintUint, Min<uint64_t, uint64_t>, registry)));
  CEL_RETURN_IF_ERROR((RegisterCrossNumericMin<int64_t, uint64_t>(
          kMathMinIntUint, kMathMinUintInt, registry)));
  CEL_RETURN_IF_ERROR((RegisterCrossNumericMin<int64_t, double>(
          kMathMinIntDouble, kMathMinDoubleInt, registry)));
  CEL_RETURN_IF_ERROR((RegisterCrossNumericMin<double, uint64_t>(
          kMathMinDoubleUint, kMathMinUintDouble, registry)));
  CEL_RETURN_IF_ERROR((
      UnaryFunctionAdapter<absl::StatusOr<Value>,
                           ListValue>::RegisterGlobalOverload(kMathMin, MinList,
                                                              registry)));

  CEL_RETURN_IF_ERROR(
      (UnaryFunctionAdapter<Value, int64_t>::RegisterGlobalOverload(
          kMathMax, kMathMaxInt, Identity<int64_t>, registry)));
  CEL_RETURN_IF_ERROR(
      (UnaryFunctionAdapter<Value, double>::RegisterGlobalOverload(
          kMathMax, kMathMaxDouble, Identity<double>, registry)));
  CEL_RETURN_IF_ERROR(
      (UnaryFunctionAdapter<Value, uint64_t>::RegisterGlobalOverload(
          kMathMax, kMathMaxUint, Identity<uint64_t>, registry)));
  CEL_RETURN_IF_ERROR(
      (BinaryFunctionAdapter<Value, int64_t, int64_t>::RegisterGlobalOverload(
          kMathMax, kMathMaxIntInt, Max<int64_t, int64_t>, registry)));
  CEL_RETURN_IF_ERROR(
      (BinaryFunctionAdapter<Value, double, double>::RegisterGlobalOverload(
          kMathMax, kMathMaxDoubleDouble, Max<double, double>, registry)));
  CEL_RETURN_IF_ERROR(
      (BinaryFunctionAdapter<Value, uint64_t, uint64_t>::RegisterGlobalOverload(
          kMathMax, kMathMaxUintUint, Max<uint64_t, uint64_t>, registry)));
  CEL_RETURN_IF_ERROR((RegisterCrossNumericMax<int64_t, uint64_t>(
          kMathMaxIntUint, kMathMaxUintInt, registry)));
  CEL_RETURN_IF_ERROR((RegisterCrossNumericMax<int64_t, double>(
          kMathMaxIntDouble, kMathMaxDoubleInt, registry)));
  CEL_RETURN_IF_ERROR((RegisterCrossNumericMax<double, uint64_t>(
          kMathMaxDoubleUint, kMathMaxUintDouble, registry)));
  CEL_RETURN_IF_ERROR((
      UnaryFunctionAdapter<absl::StatusOr<Value>,
                           ListValue>::RegisterGlobalOverload(kMathMax, MaxList,
                                                              registry)));
  if (version == 0) {
    return absl::OkStatus();
  }

  CEL_RETURN_IF_ERROR(
      (UnaryFunctionAdapter<double, double>::RegisterGlobalOverload(
          "math.ceil", kMathCeilDouble, CeilDouble, registry)));
  CEL_RETURN_IF_ERROR(
      (UnaryFunctionAdapter<double, double>::RegisterGlobalOverload(
          "math.floor", kMathFloorDouble, FloorDouble, registry)));
  CEL_RETURN_IF_ERROR(
      (UnaryFunctionAdapter<double, double>::RegisterGlobalOverload(
          "math.round", kMathRoundDouble, RoundDouble, registry)));
  CEL_RETURN_IF_ERROR(
      (UnaryFunctionAdapter<double, double>::RegisterGlobalOverload(
          "math.trunc", kMathTruncDouble, TruncDouble, registry)));
  CEL_RETURN_IF_ERROR(
      (UnaryFunctionAdapter<bool, double>::RegisterGlobalOverload(
          "math.isInf", kMathIsInfDouble, IsInfDouble, registry)));
  CEL_RETURN_IF_ERROR(
      (UnaryFunctionAdapter<bool, double>::RegisterGlobalOverload(
          "math.isNaN", kMathIsNaNDouble, IsNaNDouble, registry)));
  CEL_RETURN_IF_ERROR(
      (UnaryFunctionAdapter<bool, double>::RegisterGlobalOverload(
          "math.isFinite", kMathIsFiniteDouble, IsFiniteDouble, registry)));
  CEL_RETURN_IF_ERROR(
      (UnaryFunctionAdapter<double, double>::RegisterGlobalOverload(
          "math.abs", kMathAbsDouble, AbsDouble, registry)));
  CEL_RETURN_IF_ERROR(
      (UnaryFunctionAdapter<Value, int64_t>::RegisterGlobalOverload(
          "math.abs", kMathAbsInt, AbsInt, registry)));
  CEL_RETURN_IF_ERROR(
      (UnaryFunctionAdapter<uint64_t, uint64_t>::RegisterGlobalOverload(
          "math.abs", kMathAbsUint, AbsUint, registry)));
  CEL_RETURN_IF_ERROR(
      (UnaryFunctionAdapter<double, double>::RegisterGlobalOverload(
          "math.sign", kMathSignDouble, SignDouble, registry)));
  CEL_RETURN_IF_ERROR(
      (UnaryFunctionAdapter<int64_t, int64_t>::RegisterGlobalOverload(
          "math.sign", kMathSignInt, SignInt, registry)));
  CEL_RETURN_IF_ERROR(
      (UnaryFunctionAdapter<uint64_t, uint64_t>::RegisterGlobalOverload(
          "math.sign", kMathSignUint, SignUint, registry)));

  CEL_RETURN_IF_ERROR(
      (BinaryFunctionAdapter<int64_t, int64_t, int64_t>::RegisterGlobalOverload(
          "math.bitAnd", kMathBitAndIntInt, BitAndInt, registry)));
  CEL_RETURN_IF_ERROR(
      (BinaryFunctionAdapter<uint64_t, uint64_t, uint64_t>::
           RegisterGlobalOverload("math.bitAnd", kMathBitAndUintUint,
                                  BitAndUint, registry)));
  CEL_RETURN_IF_ERROR(
      (BinaryFunctionAdapter<int64_t, int64_t, int64_t>::RegisterGlobalOverload(
          "math.bitOr", kMathBitOrIntInt, BitOrInt, registry)));
  CEL_RETURN_IF_ERROR(
      (BinaryFunctionAdapter<uint64_t, uint64_t, uint64_t>::
           RegisterGlobalOverload("math.bitOr", kMathBitOrUintUint, BitOrUint,
                                  registry)));
  CEL_RETURN_IF_ERROR(
      (BinaryFunctionAdapter<int64_t, int64_t, int64_t>::RegisterGlobalOverload(
          "math.bitXor", kMathBitXorIntInt, BitXorInt, registry)));
  CEL_RETURN_IF_ERROR(
      (BinaryFunctionAdapter<uint64_t, uint64_t, uint64_t>::
           RegisterGlobalOverload("math.bitXor", kMathBitXorUintUint,
                                  BitXorUint, registry)));
  CEL_RETURN_IF_ERROR(
      (UnaryFunctionAdapter<int64_t, int64_t>::RegisterGlobalOverload(
          "math.bitNot", kMathBitNotIntInt, BitNotInt, registry)));
  CEL_RETURN_IF_ERROR(
      (UnaryFunctionAdapter<uint64_t, uint64_t>::RegisterGlobalOverload(
          "math.bitNot", kMathBitNotUintUint, BitNotUint, registry)));
  CEL_RETURN_IF_ERROR(
      (BinaryFunctionAdapter<Value, int64_t, int64_t>::RegisterGlobalOverload(
          "math.bitShiftLeft", kMathBitShiftLeftIntInt, BitShiftLeftInt,
          registry)));
  CEL_RETURN_IF_ERROR(
      (BinaryFunctionAdapter<Value, uint64_t, int64_t>::RegisterGlobalOverload(
          "math.bitShiftLeft", kMathBitShiftLeftUintInt, BitShiftLeftUint,
          registry)));
  CEL_RETURN_IF_ERROR(
      (BinaryFunctionAdapter<Value, int64_t, int64_t>::RegisterGlobalOverload(
          "math.bitShiftRight", kMathBitShiftRightIntInt, BitShiftRightInt,
          registry)));
  CEL_RETURN_IF_ERROR(
      (BinaryFunctionAdapter<Value, uint64_t, int64_t>::RegisterGlobalOverload(
          "math.bitShiftRight", kMathBitShiftRightUintInt, BitShiftRightUint,
          registry)));

  if (version == 1) {
    return absl::OkStatus();
  }

  CEL_RETURN_IF_ERROR(
      (UnaryFunctionAdapter<double, double>::RegisterGlobalOverload(
          "math.sqrt", kMathSqrtDouble, SqrtDouble, registry)));
  CEL_RETURN_IF_ERROR(
      (UnaryFunctionAdapter<double, int64_t>::RegisterGlobalOverload(
          "math.sqrt", kMathSqrtInt, SqrtInt, registry)));
  CEL_RETURN_IF_ERROR(
      (UnaryFunctionAdapter<double, uint64_t>::RegisterGlobalOverload(
          "math.sqrt", kMathSqrtUint, SqrtUint, registry)));

  return absl::OkStatus();
}

absl::Status RegisterMathExtensionFunctions(CelFunctionRegistry* registry,
                                            const InterpreterOptions& options) {
  return RegisterMathExtensionFunctions(
      registry->InternalGetRegistry(),
      google::api::expr::runtime::ConvertToRuntimeOptions(options));
}

}  // namespace cel::extensions
