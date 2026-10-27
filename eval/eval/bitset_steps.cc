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

#include "eval/eval/bitset_steps.h"

#include <cstddef>
#include <cstdint>
#include <utility>

#include "absl/base/optimization.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "base/builtins.h"
#include "common/internal/attribute_trail.h"
#include "common/value.h"
#include "common/value_kind.h"
#include "eval/eval/evaluator_core.h"
#include "internal/number.h"
#include "internal/status_macros.h"
#include "runtime/internal/errors.h"

namespace google::api::expr::runtime {

namespace {

using ::cel::BoolValue;
using ::cel::ListValue;
using ::cel::Value;
using ::cel::ValueKind;
using ::cel::common_internal::AttributeTrail;
using ::cel::internal::Number;

enum class BitSetOp {
  kAndIsNonZero,
  kAndEqual,
  kEqual,
};

}  // namespace

uint64_t ExtractBit(const Value& value) {
  switch (value.kind()) {
    case ValueKind::kInt: {
      int64_t v = value.GetInt().NativeValue();
      if (v >= 0 && v < 64) {
        return uint64_t{1} << v;
      }
      return 0;
    }
    case ValueKind::kUint: {
      uint64_t v = value.GetUint().NativeValue();
      if (v < 64) {
        return uint64_t{1} << v;
      }
      return 0;
    }
    case ValueKind::kDouble: {
      Number number = Number::FromDouble(value.GetDouble().NativeValue());
      if (number.LosslessConvertibleToUint()) {
        uint64_t v = number.AsUint();
        if (v < 64) {
          return uint64_t{1} << v;
        }
      }
      return 0;
    }
    default:
      return 0;
  }
}

namespace {

absl::StatusOr<Value> EvaluateBitAndIsNonZero(ExecutionFrameBase& frame,
                                              const ListValue& list,
                                              uint64_t bitmask) {
  if (bitmask == 0) {
    return BoolValue(false);
  }
  bool intersects = false;
  CEL_RETURN_IF_ERROR(list.ForEach(
      [&](size_t /*index*/, const Value& element) -> absl::StatusOr<bool> {
        uint64_t bit = ExtractBit(element);
        if ((bit & bitmask) != 0) {
          intersects = true;
          return false;
        }
        return true;
      },
      frame.descriptor_pool(), frame.message_factory(), frame.arena()));
  return BoolValue(intersects);
}

absl::StatusOr<Value> EvaluateBitAndEqual(ExecutionFrameBase& frame,
                                          const ListValue& list,
                                          uint64_t bitmask) {
  if (bitmask == 0) {
    return BoolValue(true);
  }
  uint64_t remaining = bitmask;
  CEL_RETURN_IF_ERROR(list.ForEach(
      [&](size_t /*index*/, const Value& element) -> absl::StatusOr<bool> {
        remaining &= ~ExtractBit(element);
        return remaining != 0;
      },
      frame.descriptor_pool(), frame.message_factory(), frame.arena()));
  return BoolValue(remaining == 0);
}

absl::StatusOr<Value> EvaluateBitEqual(ExecutionFrameBase& frame,
                                       const ListValue& list,
                                       uint64_t bitmask) {
  uint64_t list_mask = 0;
  bool has_unexpected = false;
  CEL_RETURN_IF_ERROR(list.ForEach(
      [&](size_t /*index*/, const Value& element) -> absl::StatusOr<bool> {
        uint64_t bit = ExtractBit(element);
        if ((bit & bitmask) == 0) {
          has_unexpected = true;
          return false;
        }
        list_mask |= bit;
        return true;
      },
      frame.descriptor_pool(), frame.message_factory(), frame.arena()));
  return BoolValue(!has_unexpected && list_mask == bitmask);
}

absl::StatusOr<Value> EvaluateBitSetOp(ExecutionFrameBase& frame,
                                       const Value& arg,
                                       const AttributeTrail& attr,
                                       uint64_t bitmask, BitSetOp op) {
  if (arg.IsError()) {
    return arg;
  }

  if (frame.unknown_processing_enabled()) {
    if (auto unknown = cel::common_internal::PartiallyIdentifyAndMergeUnknowns(
            absl::MakeConstSpan(&arg, 1), absl::MakeConstSpan(&attr, 1),
            frame.unknown_tree());
        unknown.has_value()) {
      return *unknown;
    }
  }

  if (!arg.IsList()) {
    return cel::ErrorValue::From(
        cel::runtime_internal::CreateNoMatchingOverloadError(cel::builtin::kIn),
        frame.arena());
  }

  const ListValue& list = arg.GetList();

  switch (op) {
    case BitSetOp::kAndIsNonZero:
      return EvaluateBitAndIsNonZero(frame, list, bitmask);
    case BitSetOp::kAndEqual:
      return EvaluateBitAndEqual(frame, list, bitmask);
    case BitSetOp::kEqual:
      return EvaluateBitEqual(frame, list, bitmask);
  }
  ABSL_UNREACHABLE();
}

void EvaluateBitSetStep(uint64_t bitmask, BitSetOp op, ExecutionFrame& frame) {
  if (!frame.value_stack().HasEnough(1)) {
    frame.Abort(
        absl::Status(absl::StatusCode::kInternal, "Value stack underflow"));
    return;
  }
  const Value& arg = frame.value_stack().Peek();
  const AttributeTrail& attr = frame.value_stack().PeekAttribute();

  absl::StatusOr<Value> result =
      EvaluateBitSetOp(frame, arg, attr, bitmask, op);
  if (!result.ok()) {
    frame.Abort(std::move(result).status());
    return;
  }
  frame.value_stack().PopAndPush(*std::move(result));
}

}  // namespace

void EvaluateBitIsSetStep(uint64_t bitmask, ExecutionFrame& frame) {
  if (!frame.value_stack().HasEnough(1)) {
    frame.Abort(
        absl::Status(absl::StatusCode::kInternal, "Value stack underflow"));
    return;
  }
  const Value& arg = frame.value_stack().Peek();
  if (arg.IsError()) {
    return;
  }

  if (frame.unknown_processing_enabled()) {
    if (auto unknown = cel::common_internal::PartiallyIdentifyAndMergeUnknowns(
            frame.value_stack().GetSpan(1),
            frame.value_stack().GetAttributeSpan(1), frame.unknown_tree());
        unknown.has_value()) {
      frame.value_stack().PopAndPush(1, *unknown);
      return;
    }
  }

  uint64_t bit = ExtractBit(arg);
  frame.value_stack().PopAndPush(BoolValue((bit & bitmask) != 0));
}

void EvaluateBitAndIsNonZeroStep(uint64_t bitmask, ExecutionFrame& frame) {
  EvaluateBitSetStep(bitmask, BitSetOp::kAndIsNonZero, frame);
}

void EvaluateBitAndEqualStep(uint64_t bitmask, ExecutionFrame& frame) {
  EvaluateBitSetStep(bitmask, BitSetOp::kAndEqual, frame);
}

void EvaluateBitEqualStep(uint64_t bitmask, ExecutionFrame& frame) {
  EvaluateBitSetStep(bitmask, BitSetOp::kEqual, frame);
}

}  // namespace google::api::expr::runtime
