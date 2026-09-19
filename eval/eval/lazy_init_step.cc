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

#include "eval/eval/lazy_init_step.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include "cel/expr/value.pb.h"
#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "common/value.h"
#include "eval/eval/attribute_trail.h"
#include "eval/eval/comprehension_slots.h"
#include "eval/eval/direct_expression_step.h"
#include "eval/eval/evaluator_core.h"
#include "internal/status_macros.h"

namespace google::api::expr::runtime {

namespace {

using ::cel::Value;

class DirectLazyInitStep final : public DirectExpressionStep {
 public:
  DirectLazyInitStep(size_t slot_index,
                     const DirectExpressionStep* subexpression, int64_t expr_id)
      : DirectExpressionStep(expr_id),
        slot_index_(slot_index),
        subexpression_(subexpression) {}

  absl::Status Evaluate(ExecutionFrameBase& frame, Value& result,
                        AttributeTrail& attribute) const override {
    ComprehensionSlot* slot = frame.comprehension_slots().Get(slot_index_);
    if (slot->Has()) {
      result = slot->value();
      attribute = slot->attribute();
    } else {
      CEL_RETURN_IF_ERROR(subexpression_->Evaluate(frame, result, attribute));
      slot->Set(result, attribute);
    }
    return absl::OkStatus();
  }

 private:
  const size_t slot_index_;
  const DirectExpressionStep* absl_nonnull const subexpression_;
};

class BindStep : public DirectExpressionStep {
 public:
  BindStep(size_t slot_index,
           std::unique_ptr<DirectExpressionStep> subexpression, int64_t expr_id)
      : DirectExpressionStep(expr_id),
        slot_index_(slot_index),
        subexpression_(std::move(subexpression)) {}

  absl::Status Evaluate(ExecutionFrameBase& frame, Value& result,
                        AttributeTrail& attribute) const override {
    CEL_RETURN_IF_ERROR(subexpression_->Evaluate(frame, result, attribute));

    frame.comprehension_slots().ClearSlot(slot_index_);

    return absl::OkStatus();
  }

 private:
  size_t slot_index_;
  std::unique_ptr<DirectExpressionStep> subexpression_;
};

class BlockStep : public DirectExpressionStep {
 public:
  BlockStep(size_t slot_index, size_t slot_count,
            std::unique_ptr<DirectExpressionStep> subexpression,
            int64_t expr_id)
      : DirectExpressionStep(expr_id),
        slot_index_(slot_index),
        slot_count_(slot_count),
        subexpression_(std::move(subexpression)) {}

  absl::Status Evaluate(ExecutionFrameBase& frame, Value& result,
                        AttributeTrail& attribute) const override {
    CEL_RETURN_IF_ERROR(subexpression_->Evaluate(frame, result, attribute));

    for (size_t i = 0; i < slot_count_; ++i) {
      frame.comprehension_slots().ClearSlot(slot_index_ + i);
    }

    return absl::OkStatus();
  }

 private:
  size_t slot_index_;
  size_t slot_count_;
  std::unique_ptr<DirectExpressionStep> subexpression_;
};

}  // namespace

void EvaluateLazyInitStep(const LazyInitStepInfo& step, ExecutionFrame& frame) {
  ComprehensionSlot* slot = frame.comprehension_slots().Get(step.slot_index);
  if (slot->Has()) {
    frame.value_stack().Push(slot->value(), slot->attribute());
  } else {
    frame.Call(step.slot_index, step.subexpression_index);
  }
}

void EvaluateAssignSlotAndPop(size_t slot_index, ExecutionFrame& frame) {
  if (!frame.value_stack().HasEnough(1)) {
    frame.Abort(absl::InternalError("Stack underflow assigning lazy value"));
    return;
  }
  ComprehensionSlot* slot = frame.comprehension_slots().Get(slot_index);
  slot->Set(frame.value_stack().Peek(), frame.value_stack().PeekAttribute());
  frame.value_stack().Pop(1);
}

void EvaluateClearSlotStep(const ClearSlotStepInfo& step,
                           ExecutionFrame& frame) {
  for (size_t i = 0; i < step.slot_count; ++i) {
    frame.comprehension_slots().ClearSlot(step.slot_index + i);
  }
}

std::unique_ptr<DirectExpressionStep> CreateDirectBindStep(
    size_t slot_index, std::unique_ptr<DirectExpressionStep> expression,
    int64_t expr_id) {
  return std::make_unique<BindStep>(slot_index, std::move(expression), expr_id);
}

std::unique_ptr<DirectExpressionStep> CreateDirectBlockStep(
    size_t slot_index, size_t slot_count,
    std::unique_ptr<DirectExpressionStep> expression, int64_t expr_id) {
  return std::make_unique<BlockStep>(slot_index, slot_count,
                                     std::move(expression), expr_id);
}

std::unique_ptr<DirectExpressionStep> CreateDirectLazyInitStep(
    size_t slot_index, const DirectExpressionStep* absl_nonnull subexpression,
    int64_t expr_id) {
  return std::make_unique<DirectLazyInitStep>(slot_index, subexpression,
                                              expr_id);
}

}  // namespace google::api::expr::runtime
