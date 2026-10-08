// Copyright 2024 Google LLC
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

#include "eval/eval/optional_or_step.h"

#include <memory>
#include <optional>
#include <utility>

#include "absl/base/optimization.h"
#include "absl/status/status.h"
#include "absl/types/span.h"
#include "common/value.h"
#include "eval/eval/attribute_trail.h"
#include "eval/eval/evaluator_core.h"
#include "eval/eval/expression_step_base.h"
#include "eval/eval/expression_step_logic.h"
#include "runtime/internal/errors.h"
#include "google/protobuf/arena.h"

namespace google::api::expr::runtime {

namespace {

using ::cel::ErrorValue;
using ::cel::Value;
using ::cel::runtime_internal::CreateNoMatchingOverloadError;

enum class OptionalOrKind { kOrOptional, kOrValue };

ErrorValue MakeNoOverloadError(OptionalOrKind kind, google::protobuf::Arena* arena) {
  switch (kind) {
    case OptionalOrKind::kOrOptional:
      return ErrorValue::From(CreateNoMatchingOverloadError("or"), arena);
    case OptionalOrKind::kOrValue:
      return ErrorValue::From(CreateNoMatchingOverloadError("orValue"), arena);
  }

  ABSL_UNREACHABLE();
}

class OptionalOrStep : public ExpressionStepBase {
 public:
  explicit OptionalOrStep(OptionalOrKind kind)
      : ExpressionStepBase(), kind_(kind) {}

  void Evaluate(ExecutionFrame* frame) const override;

 private:
  const OptionalOrKind kind_;
};

// Shared implementation for optional or.
//
// If return value is Ok, the result is assigned to the result reference
// argument.
absl::Status EvalOptionalOr(OptionalOrKind kind, const Value& lhs,
                            const Value& rhs, const AttributeTrail& lhs_attr,
                            const AttributeTrail& rhs_attr, Value& result,
                            AttributeTrail& result_attr, google::protobuf::Arena* arena) {
  if (lhs.IsError() || lhs.IsUnknown()) {
    result = lhs;
    result_attr = lhs_attr;
    return absl::OkStatus();
  }

  auto lhs_optional_value = lhs.AsOptional();
  if (!lhs_optional_value.has_value()) {
    result = MakeNoOverloadError(kind, arena);
    result_attr = AttributeTrail();
    return absl::OkStatus();
  }

  if (lhs_optional_value->HasValue()) {
    if (kind == OptionalOrKind::kOrValue) {
      result = lhs_optional_value->Value();
    } else {
      result = lhs;
    }
    result_attr = lhs_attr;
    return absl::OkStatus();
  }

  if (kind == OptionalOrKind::kOrOptional && !rhs.IsError() &&
      !rhs.IsUnknown() && !rhs.IsOptional()) {
    result = MakeNoOverloadError(kind, arena);
    result_attr = AttributeTrail();
    return absl::OkStatus();
  }

  result = rhs;
  result_attr = rhs_attr;
  return absl::OkStatus();
}

void OptionalOrStep::Evaluate(ExecutionFrame* frame) const {
  if (!frame->value_stack().HasEnough(2)) {
    frame->Abort(absl::InternalError("Value stack underflow"));
    return;
  }

  absl::Span<const Value> args = frame->value_stack().GetSpan(2);
  absl::Span<const AttributeTrail> args_attr =
      frame->value_stack().GetAttributeSpan(2);

  Value result;
  AttributeTrail result_attr;
  if (absl::Status status =
          EvalOptionalOr(kind_, args[0], args[1], args_attr[0], args_attr[1],
                         result, result_attr, frame->arena());
      !status.ok()) {
    frame->Abort(std::move(status));
    return;
  }

  frame->value_stack().PopAndPush(2, std::move(result), std::move(result_attr));
}

}  // namespace

void OptionalHasValueJumpStep::Evaluate(ExecutionFrame* frame) const {
  if (!frame->value_stack().HasEnough(1)) {
    frame->Abort(absl::InternalError("Value stack underflow"));
    return;
  }
  const Value& value = frame->value_stack().Peek();
  auto optional_value = value.AsOptional();
  // We jump if the receiver is `optional_type` which has a value or the
  // receiver is an error/unknown. Unlike `_||_` we are not commutative. If
  // we run into an error/unknown, we skip the `else` branch.
  const bool should_jump =
      (optional_value.has_value() && optional_value->HasValue()) ||
      (!optional_value.has_value() && (value.IsError() || value.IsUnknown()));
  if (should_jump) {
    if (is_or_value_ && optional_value.has_value()) {
      frame->value_stack().PopAndPush(optional_value->Value());
    }
    if (!jump_offset_.has_value()) {
      frame->Abort(absl::InternalError("Jump offset not set"));
      return;
    }
    frame->JumpToOrAbort(*jump_offset_);
  }
}

std::unique_ptr<OptionalHasValueJumpStep> CreateOptionalHasValueJumpStep(
    bool or_value) {
  return std::make_unique<OptionalHasValueJumpStep>(or_value);
}

std::unique_ptr<ExpressionStepLogic> CreateOptionalOrStep(bool is_or_value) {
  return std::make_unique<OptionalOrStep>(
      is_or_value ? OptionalOrKind::kOrValue : OptionalOrKind::kOrOptional);
}

}  // namespace google::api::expr::runtime
