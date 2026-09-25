// Copyright 2017 Google LLC
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

#include "eval/eval/evaluator_core.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/log/absl_check.h"
#include "absl/log/absl_log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "common/value.h"
#include "common/value_kind.h"
#include "common/values/list_value_builder.h"
#include "eval/eval/attribute_trail.h"
#include "eval/eval/comprehension_slots.h"
#include "eval/eval/comprehension_step.h"
#include "eval/eval/equality_steps.h"
#include "eval/eval/lazy_init_step.h"
#include "eval/eval/logic_step.h"
#include "internal/status_macros.h"
#include "runtime/activation_interface.h"
#include "runtime/internal/errors.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"

namespace google::api::expr::runtime {

void FlatExpressionEvaluatorState::Reset() {
  value_stack_.Clear();
  iterator_stack_.Clear();
  comprehension_slots_.Reset();
}

const ExpressionStep* ExecutionFrame::NextSlow() {
  while (true) {
    const size_t end_pos = execution_path_.size();

    if (ABSL_PREDICT_TRUE(pc_ < end_pos)) {
      const auto* step = &execution_path_[pc_++];
      ABSL_ASSUME(step != nullptr);
      return step;
    }
    if (ABSL_PREDICT_TRUE(pc_ == end_pos)) {
      if (!call_stack_.empty()) {
        SubFrame& subframe = call_stack_.back();
        pc_ = subframe.return_pc;
        execution_path_ = subframe.return_expression;
        ABSL_DCHECK_EQ(value_stack().size(), subframe.expected_stack_size);
        comprehension_slots().Set(subframe.slot_index, value_stack().Peek(),
                                  value_stack().PeekAttribute());
        call_stack_.pop_back();
        continue;
      }
    } else {
      ABSL_LOG(ERROR) << "Attempting to step beyond the end of execution path.";
    }
    return nullptr;
  }
}

void ExecutionFrame::AbortJumpOutOfRange(int offset) {
  Abort(absl::Status(
      absl::StatusCode::kInternal,
      absl::StrCat("Jump address out of range: position: ", pc_,
                   ", offset: ", offset, ", range: ", execution_path_.size())));
}

namespace {

// This class abuses the fact that `absl::Status` is trivially destructible when
// `absl::Status::ok()` is `true`. If the implementation of `absl::Status` every
// changes, LSan and ASan should catch it. We cannot deal with the cost of extra
// move assignment and destructor calls.
//
// This is useful only in the evaluation loop and is a direct replacement for
// `RETURN_IF_ERROR`. It yields the most improvements on benchmarks with lots of
// steps which never return non-OK `absl::Status`.
class EvaluationStatus final {
 public:
  explicit EvaluationStatus(absl::Status&& status) {
    ::new (static_cast<void*>(&status_[0])) absl::Status(std::move(status));
  }

  EvaluationStatus() = delete;
  EvaluationStatus(const EvaluationStatus&) = delete;
  EvaluationStatus(EvaluationStatus&&) = delete;
  EvaluationStatus& operator=(const EvaluationStatus&) = delete;
  EvaluationStatus& operator=(EvaluationStatus&&) = delete;

  absl::Status Consume() && {
    return std::move(*reinterpret_cast<absl::Status*>(&status_[0]));
  }

  bool ok() const {
    return ABSL_PREDICT_TRUE(
        reinterpret_cast<const absl::Status*>(&status_[0])->ok());
  }

 private:
  alignas(absl::Status) char status_[sizeof(absl::Status)];
};

}  // namespace

void ExpressionStep::EvaluateMutableListAppendStep(ExecutionFrame& frame) {
  if (!frame.value_stack().HasEnough(2)) {
    frame.Abort(
        absl::Status(absl::StatusCode::kInternal, "Value stack underflow"));
    return;
  }
  absl::Span<const cel::Value> args = frame.value_stack().GetSpan(2);
  if (args[0].IsError()) {
    frame.value_stack().Pop(1);
    return;
  }
  if (args[1].IsError()) {
    frame.value_stack().SwapAndPop(2, 1);
    return;
  }
  if (frame.unknown_processing_enabled()) {
    std::optional<cel::UnknownValue> unknown_set =
        frame.attribute_utility().IdentifyAndMergeUnknowns(
            args, frame.value_stack().GetAttributeSpan(2),
            /*use_partial=*/true);
    if (unknown_set.has_value()) {
      frame.value_stack().PopAndPush(2, std::move(*unknown_set));
      return;
    }
  }
  if (const cel::common_internal::MutableListValue* mutable_list_value =
          cel::common_internal::AsMutableListValue(args[0]);
      mutable_list_value != nullptr) {
    absl::Status status = mutable_list_value->Append(args[1]);
    if (!status.ok()) {
      frame.Abort(std::move(status));
      return;
    }
    frame.value_stack().Pop(1);
    return;
  }
  frame.Abort(
      absl::InvalidArgumentError("Unexpected call to runtime list append."));
}

void ExecutionFrame::Interpret() {
  for (const ExpressionStep* expr = Next(); expr != nullptr; expr = Next()) {
    expr->Evaluate(*this);
  }
}

void ExecutionFrame::InterpretWithCallback(EvaluationListener& listener) {
  for (const ExpressionStep* expr = Next(); expr != nullptr; expr = Next()) {
    expr->Evaluate(*this);
    if (pc_ == 0 || !expr->comes_from_ast() || !abort_status().ok()) {
      // Skip if we just started a Call, if the step doesn't map to an AST
      // id, or if evaluation was aborted.
      continue;
    }

    if (ABSL_PREDICT_FALSE(value_stack().empty())) {
      ABSL_LOG(ERROR) << "Stack is empty after a ExpressionStep.Evaluate. "
                         "Try to disable short-circuiting.";
      continue;
    }
    if (EvaluationStatus status(listener(expr->id(), value_stack().Peek(),
                                         descriptor_pool(), message_factory(),
                                         arena()));
        !status.ok()) {
      Abort(std::move(status).Consume());
      return;
    }
  }
}

absl::StatusOr<cel::Value> ExecutionFrame::Evaluate(
    EvaluationListener& listener) {
  const size_t initial_stack_size = value_stack().size();

  if (!listener) {
    Interpret();
  } else {
    InterpretWithCallback(listener);
  }

  if (!abort_status().ok()) {
    return std::move(abort_status());
  }

  const size_t final_stack_size = value_stack().size();
  if (ABSL_PREDICT_FALSE(final_stack_size != initial_stack_size + 1 ||
                         final_stack_size == 0)) {
    return absl::InternalError(absl::StrCat(
        "Stack error during evaluation: expected=", initial_stack_size + 1,
        ", actual=", final_stack_size));
  }

  cel::Value value = std::move(value_stack().Peek());
  value_stack().Pop(1);
  return value;
}

FlatExpressionEvaluatorState FlatExpression::MakeEvaluatorState(
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena) const {
  return FlatExpressionEvaluatorState(path_.size(), comprehension_slots_size_,
                                      type_provider_, descriptor_pool,
                                      message_factory, arena);
}

absl::StatusOr<cel::Value> FlatExpression::EvaluateWithCallback(
    const cel::ActivationInterface& activation,
    const cel::EmbedderContext* absl_nullable embedder_context,
    EvaluationListener listener, FlatExpressionEvaluatorState& state) const {
  state.Reset();

  ExecutionFrame frame(subexpressions_, activation, options_, state,
                       std::move(listener), embedder_context);

  return frame.Evaluate(frame.callback());
}

void ExpressionStep::SwapToEmpty(ExpressionStep& step,
                                 ExpressionStep& empty_step) {
  ABSL_DCHECK(empty_step.header_.kind == ExpressionStepKind::kMovedFrom);
  using std::swap;
  swap(step.header_, empty_step.header_);
  switch (empty_step.header_.kind) {
    case ExpressionStepKind::kGenericLogic:
      empty_step.u_.logic = std::move(step.u_.logic);
      break;
    case ExpressionStepKind::kBoolConstant:
      empty_step.u_.bool_val = step.u_.bool_val;
      break;
    case ExpressionStepKind::kIntConstant:
      empty_step.u_.int_val = step.u_.int_val;
      break;
    case ExpressionStepKind::kUintConstant:
      empty_step.u_.uint_val = step.u_.uint_val;
      break;
    case ExpressionStepKind::kDoubleConstant:
      empty_step.u_.double_val = step.u_.double_val;
      break;
    case ExpressionStepKind::kOtherConstant:
      empty_step.u_.other_val = std::move(step.u_.other_val);
      break;
    case ExpressionStepKind::kIdentifier:
      empty_step.u_.identifier = std::move(step.u_.identifier);
      break;
    case ExpressionStepKind::kEagerFunction:
      empty_step.u_.eager_function_step =
          std::move(step.u_.eager_function_step);
      break;
    case ExpressionStepKind::kLazyFunction:
      empty_step.u_.lazy_function_step = std::move(step.u_.lazy_function_step);
      break;
    case ExpressionStepKind::kLazyInit:
      empty_step.u_.lazy_init = step.u_.lazy_init;
      break;
    case ExpressionStepKind::kAssignSlotAndPop:
    case ExpressionStepKind::kComprehensionFinish:
    case ExpressionStepKind::kReadSlot:
      empty_step.u_.slot_index = step.u_.slot_index;
      break;
    case ExpressionStepKind::kClearSlots:
      empty_step.u_.clear_slots = step.u_.clear_slots;
      break;
    case ExpressionStepKind::kBooleanOr:
    case ExpressionStepKind::kBooleanAnd:
      empty_step.u_.arg_count = step.u_.arg_count;
      break;
    case ExpressionStepKind::kComprehensionCond:
    case ExpressionStepKind::kComprehensionCond2:
      empty_step.u_.cond_step = step.u_.cond_step;
      break;
    case ExpressionStepKind::kComprehensionNext:
    case ExpressionStepKind::kComprehensionNext2:
      empty_step.u_.next_step = step.u_.next_step;
      break;
    case ExpressionStepKind::kBooleanOrJump:
    case ExpressionStepKind::kBooleanAndJump:
      empty_step.u_.bool_jump_step = step.u_.bool_jump_step;
      break;
    case ExpressionStepKind::kTernaryJump:
      empty_step.u_.ternary_jump_step = step.u_.ternary_jump_step;
      break;
    case ExpressionStepKind::kFixedJump:
      empty_step.u_.fixed_jump_step = step.u_.fixed_jump_step;
      break;
    case ExpressionStepKind::kNullConstant:
    case ExpressionStepKind::kBooleanNot:
    case ExpressionStepKind::kNotStrictlyFalse:
    case ExpressionStepKind::kFastIn:
    case ExpressionStepKind::kFastEqual:
    case ExpressionStepKind::kFastNotEqual:
    case ExpressionStepKind::kNewMutableList:
    case ExpressionStepKind::kMutableListAppend:
    case ExpressionStepKind::kMovedFrom:
      break;
    default:
      ABSL_UNREACHABLE();
  }
  step.u_.empty = nullptr;
}

ExpressionStep ExpressionStep::MakeConstant(const cel::Value& value,
                                            int64_t id) {
  if (id < 0 || id > std::numeric_limits<int32_t>::max()) {
    id = -1;
  }
  int32_t id32 = static_cast<int32_t>(id);
  switch (value.kind()) {
    case cel::ValueKind::kBool: {
      ExpressionStep step(ExpressionStepKind::kBoolConstant, id32);
      step.u_.bool_val = value.GetBool().NativeValue();
      return step;
    }
    case cel::ValueKind::kInt: {
      ExpressionStep step(ExpressionStepKind::kIntConstant, id32);
      step.u_.int_val = value.GetInt().NativeValue();
      return step;
    }
    case cel::ValueKind::kUint: {
      ExpressionStep step(ExpressionStepKind::kUintConstant, id32);
      step.u_.uint_val = value.GetUint().NativeValue();
      return step;
    }
    case cel::ValueKind::kDouble: {
      ExpressionStep step(ExpressionStepKind::kDoubleConstant, id32);
      step.u_.double_val = value.GetDouble().NativeValue();
      return step;
    }
    case cel::ValueKind::kNull:
      return ExpressionStep(ExpressionStepKind::kNullConstant, id32);
    default: {
      ExpressionStep step(ExpressionStepKind::kOtherConstant, id32);
      step.u_.other_val = std::make_unique<cel::Value>(value);
      return step;
    }
  }
}

bool GetIfConstant(const ExpressionStep& step, cel::Value& out) {
  switch (step.header_.kind) {
    case ExpressionStepKind::kIntConstant:
      out = cel::IntValue(step.u_.int_val);
      return true;
    case ExpressionStepKind::kBoolConstant:
      out = cel::BoolValue(step.u_.bool_val);
      return true;
    case ExpressionStepKind::kDoubleConstant:
      out = cel::DoubleValue(step.u_.double_val);
      return true;
    case ExpressionStepKind::kNullConstant:
      out = cel::NullValue();
      return true;
    case ExpressionStepKind::kUintConstant:
      out = cel::UintValue(step.u_.uint_val);
      return true;
    case ExpressionStepKind::kOtherConstant:
      out = *step.u_.other_val;
      return true;
    default:
      return false;
  }
}

bool IsConstant(const ExpressionStep& step) {
  switch (step.header_.kind) {
    case ExpressionStepKind::kIntConstant:
    case ExpressionStepKind::kBoolConstant:
    case ExpressionStepKind::kDoubleConstant:
    case ExpressionStepKind::kNullConstant:
    case ExpressionStepKind::kUintConstant:
    case ExpressionStepKind::kOtherConstant:
      return true;
    default:
      return false;
  }
}

ComprehensionCondStep* GetIfComprehensionCondStep(ExpressionStep& step) {
  if (step.header_.kind == ExpressionStepKind::kComprehensionCond ||
      step.header_.kind == ExpressionStepKind::kComprehensionCond2) {
    return &step.u_.cond_step;
  }
  return nullptr;
}

ComprehensionNextStep* GetIfComprehensionNextStep(ExpressionStep& step) {
  if (step.header_.kind == ExpressionStepKind::kComprehensionNext ||
      step.header_.kind == ExpressionStepKind::kComprehensionNext2) {
    return &step.u_.next_step;
  }
  return nullptr;
}

BoolJumpStepInfo* GetIfBoolJumpStep(ExpressionStep& step) {
  if (step.header_.kind == ExpressionStepKind::kBooleanOrJump ||
      step.header_.kind == ExpressionStepKind::kBooleanAndJump) {
    return &step.u_.bool_jump_step;
  }
  return nullptr;
}

TernaryJumpStepInfo* GetIfTernaryJumpStep(ExpressionStep& step) {
  if (step.header_.kind == ExpressionStepKind::kTernaryJump) {
    return &step.u_.ternary_jump_step;
  }
  return nullptr;
}

FixedJumpStepInfo* GetIfFixedJumpStep(ExpressionStep& step) {
  if (step.header_.kind == ExpressionStepKind::kFixedJump) {
    return &step.u_.fixed_jump_step;
  }
  return nullptr;
}

void WrappedDirectStep::Evaluate(ExecutionFrame* frame) const {
  cel::Value result;
  AttributeTrail attribute_trail;
  if (absl::Status status = impl_->Evaluate(*frame, result, attribute_trail);
      !status.ok()) {
    frame->Abort(std::move(status));
    return;
  }
  frame->value_stack().Push(std::move(result), std::move(attribute_trail));
}

}  // namespace google::api::expr::runtime
