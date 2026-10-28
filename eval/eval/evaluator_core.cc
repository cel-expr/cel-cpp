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

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/log/absl_check.h"
#include "absl/log/absl_log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "base/builtins.h"
#include "common/internal/attribute_trail.h"
#include "common/value.h"
#include "common/value_kind.h"
#include "common/values/list_value_builder.h"
#include "eval/eval/attribute_trail.h"
#include "eval/eval/bitset_steps.h"
#include "eval/eval/comprehension_slots.h"
#include "eval/eval/comprehension_step.h"
#include "eval/eval/create_list_step.h"
#include "eval/eval/equality_steps.h"
#include "eval/eval/ident_step.h"
#include "eval/eval/lazy_init_step.h"
#include "internal/status_macros.h"
#include "runtime/activation_interface.h"
#include "runtime/internal/errors.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"

// ABSL_UNREACHABLE does hardening shenanigans that can mess up compiler
// optimizations. We use this in the interpreter loop which is extremely hot
// and we cannot accept the interference hardening can do to compiler
// optimizations.
#if defined(__cpp_lib_unreachable) && __cpp_lib_unreachable >= 202202L
#define CEL_INTERNAL_UNREACHABLE_IMPL() std::unreachable()
#elif defined(__GNUC__) || ABSL_HAVE_BUILTIN(__builtin_unreachable)
#define CEL_INTERNAL_UNREACHABLE_IMPL() __builtin_unreachable()
#elif ABSL_HAVE_BUILTIN(__builtin_assume)
#define CEL_INTERNAL_UNREACHABLE_IMPL() __builtin_assume(false)
#elif defined(_MSC_VER)
#define CEL_INTERNAL_UNREACHABLE_IMPL() __assume(false)
#else
#define CEL_INTERNAL_UNREACHABLE_IMPL() ((void)0)
#endif

#define CEL_INTERNAL_UNREACHABLE()           \
  do {                                       \
    /* NOLINTNEXTLINE: misc-static-assert */ \
    assert(false && "unreachable");          \
    CEL_INTERNAL_UNREACHABLE_IMPL();         \
  } while (false)

namespace google::api::expr::runtime {

void FlatExpressionEvaluatorState::Reset() {
  if (!dirty_) {
    return;
  }
  value_stack_.Clear();
  iterator_stack_.Clear();
  comprehension_slots_.Reset();
  dirty_ = false;
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

int64_t OneMinusArgCount(size_t count) {
  ABSL_DCHECK(count <=
              static_cast<size_t>(std::numeric_limits<int64_t>::max()));
  return 1 - static_cast<int64_t>(count);
}

absl::Status LookupIdent(absl::string_view name, ExecutionFrame& frame,
                         cel::Value& result, AttributeTrail& attribute) {
  if (frame.attribute_tracking_enabled() &&
      (AttributeLeadMatch)(frame.unknown_attributes(), frame.known_attributes(),
                           frame.missing_attributes(), name, result, attribute,
                           frame.unknown_tree())) {
    return absl::OkStatus();
  }

  CEL_ASSIGN_OR_RETURN(
      bool found, frame.activation().FindVariable(name, frame.descriptor_pool(),
                                                  frame.message_factory(),
                                                  frame.arena(), &result));

  if (found) {
    if (ABSL_PREDICT_FALSE(frame.attribute_tracking_enabled() &&
                           result.IsUnknown())) {
      // This really only happens in our own tests...
      result = cel::common_internal::GraftUnknowns(result.GetUnknown(),
                                                   frame.unknown_tree());
      attribute = AttributeTrail();
    }
    return absl::OkStatus();
  }

  if (frame.attribute_tracking_enabled()) {
    attribute = AttributeTrail();
  }
  result = cel::ErrorValue::From(
      cel::runtime_internal::CreateError(absl::StrCat(
          "No value with name \"", name, "\" found in Activation")),
      frame.arena());

  return absl::OkStatus();
}

ABSL_ATTRIBUTE_ALWAYS_INLINE inline void EvaluateIdentifierStep(
    ExecutionFrame& frame, const IdentStepInfo& step) {
  ComprehensionSlot* slot = frame.comprehension_slots().Get(step.slot_index);
  if (slot->Has()) {
    frame.value_stack().Push(slot->value(), slot->attribute());
    return;
  }
  if (ABSL_PREDICT_FALSE(step.ident_index >= frame.identifiers().size())) {
    frame.Abort(absl::InternalError(
        absl::StrCat("identifier index out of range: ", step.ident_index)));
    return;
  }
  const Identifier& ident = frame.identifiers()[step.ident_index];
  frame.value_stack().Push(cel::NullValue());
  if (absl::Status status =
          LookupIdent(ident.name, frame, frame.value_stack().Peek(),
                      frame.value_stack().PeekAttribute());
      !status.ok()) {
    frame.Abort(std::move(status));
    return;
  }
  slot->Set(frame.value_stack().Peek(), frame.value_stack().PeekAttribute());
}

ABSL_ATTRIBUTE_ALWAYS_INLINE inline void EvaluateReadSlotStep(
    ExecutionFrame& frame, size_t slot_index) {
  const ComprehensionSlots::Slot* slot =
      frame.comprehension_slots().Get(slot_index);
  if (ABSL_PREDICT_FALSE(!slot->Has())) {
    frame.Abort(absl::InternalError(absl::StrCat(
        "Comprehension variable read out of scope: ", slot_index)));
    return;
  }
  frame.value_stack().Push(slot->value(), slot->attribute());
}

ABSL_ATTRIBUTE_ALWAYS_INLINE inline void EvaluateBoolJumpStep(
    ExecutionFrame& frame, const BoolJumpStepInfo& step, bool target) {
  ABSL_DCHECK(step.set) << "BoolJumpStep did not have a value set.";
  if (ABSL_PREDICT_FALSE(!frame.value_stack().HasEnough(step.arg_count))) {
    frame.Abort(
        absl::Status(absl::StatusCode::kInternal, "Value stack underflow"));
    return;
  }
  const cel::Value& value = frame.value_stack().Peek();
  if (value.IsBool() && value.GetBool().NativeValue() == target) {
    frame.value_stack().SwapAndPop(step.arg_count, step.arg_count - 1);
    frame.JumpToOrAbort(step.offset);
  }
  // No-op if the value is not a bool or the value is not the target.
  // Cleanup will happen if we hit a later jump or we fall-through.
}

ABSL_ATTRIBUTE_ALWAYS_INLINE inline void EvaluateTernaryJumpStep(
    ExecutionFrame& frame, const TernaryJumpStepInfo& step) {
  ABSL_DCHECK(step.set) << "TernaryJumpStep did not have a value set.";
  if (ABSL_PREDICT_FALSE(!frame.value_stack().HasEnough(1))) {
    frame.Abort(absl::InternalError("TernaryJumpStep: value stack underflow"));
    return;
  }
  const cel::Value& condition = frame.value_stack().Peek();
  switch (condition.kind()) {
    case cel::ValueKind::kBool:
      if (!condition.GetBool().NativeValue()) {
        frame.JumpToOrAbort(step.jump_to_second_offset);
      }
      frame.value_stack().Pop();
      break;
    default:
      frame.value_stack().PopAndPush(cel::ErrorValue::From(
          cel::runtime_internal::CreateNoMatchingOverloadError(
              "<ternary_condition>"),
          frame.arena()));
      ABSL_FALLTHROUGH_INTENDED;
    case cel::ValueKind::kError:
    case cel::ValueKind::kUnknown:
      // Propagate the error or unknown set.
      frame.JumpToOrAbort(step.error_offset);
      break;
  }
}

ABSL_ATTRIBUTE_ALWAYS_INLINE inline void EvaluateMutableListAppendStep(
    ExecutionFrame& frame) {
  if (ABSL_PREDICT_FALSE(!frame.value_stack().HasEnough(2))) {
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
    if (auto unknown = cel::common_internal::PartiallyIdentifyAndMergeUnknowns(
            args, frame.value_stack().GetAttributeSpan(2),
            frame.unknown_tree());
        unknown.has_value()) {
      frame.value_stack().PopAndPush(2, *unknown);
      return;
    }
  }
  if (const cel::common_internal::MutableListValue* mutable_list_value =
          cel::common_internal::AsMutableListValue(args[0]);
      ABSL_PREDICT_TRUE(mutable_list_value != nullptr)) {
    absl::Status status = mutable_list_value->Append(args[1]);
    if (ABSL_PREDICT_FALSE(!status.ok())) {
      frame.Abort(std::move(status));
      return;
    }
    frame.value_stack().Pop(1);
    return;
  }
  frame.Abort(
      absl::InvalidArgumentError("Unexpected call to runtime list append."));
}

ABSL_ATTRIBUTE_ALWAYS_INLINE inline void EvaluateNotStep(
    ExecutionFrame& frame) {
  if (ABSL_PREDICT_FALSE(!frame.value_stack().HasEnough(1))) {
    frame.Abort(absl::InternalError("Value stack underflow"));
    return;
  }
  const cel::Value& operand = frame.value_stack().Peek();

  switch (operand.kind()) {
    case cel::ValueKind::kBool:
      frame.value_stack().PopAndPush(
          cel::BoolValue{!operand.GetBool().NativeValue()});
      break;
    case cel::ValueKind::kUnknown:
    case cel::ValueKind::kError:
      // just forward.
      break;
    default:
      frame.value_stack().PopAndPush(cel::ErrorValue::From(
          cel::runtime_internal::CreateNoMatchingOverloadError(
              cel::builtin::kNot),
          frame.arena()));
      break;
  }
}

ABSL_ATTRIBUTE_ALWAYS_INLINE inline void EvaluateNotStrictlyFalseStep(
    ExecutionFrame& frame) {
  if (ABSL_PREDICT_FALSE(!frame.value_stack().HasEnough(1))) {
    frame.Abort(absl::InternalError("Value stack underflow"));
    return;
  }
  const cel::Value& operand = frame.value_stack().Peek();

  switch (operand.kind()) {
    case cel::ValueKind::kBool:
      // just forward.
      break;
    case cel::ValueKind::kUnknown:
    case cel::ValueKind::kError:
      frame.value_stack().PopAndPush(cel::TrueValue());
      break;
    default:
      frame.value_stack().PopAndPush(cel::ErrorValue::From(
          cel::runtime_internal::CreateNoMatchingOverloadError(
              cel::builtin::kNot),
          frame.arena()));
      break;
  }
}

enum class BoolLogicKind {
  kAnd,
  kOr,
};

ABSL_ATTRIBUTE_ALWAYS_INLINE inline void EvaluateBoolLogicStep(
    ExecutionFrame& frame, size_t num_args, BoolLogicKind kind) {
  if (ABSL_PREDICT_FALSE(!frame.value_stack().HasEnough(num_args))) {
    frame.Abort(absl::InternalError("Value stack underflow"));
    return;
  }

  const bool shortcircuit = kind == BoolLogicKind::kOr;
  const absl::string_view op_name =
      kind == BoolLogicKind::kOr ? cel::builtin::kOr : cel::builtin::kAnd;
  absl::Span<const cel::Value> args = frame.value_stack().GetSpan(num_args);
  std::optional<size_t> error_pos;

  for (size_t i = 0; i < args.size(); i++) {
    const cel::Value& arg = args[i];
    switch (arg.kind()) {
      case cel::ValueKind::kBool:
        if (arg.GetBool() == shortcircuit) {
          frame.value_stack().PopAndPush(num_args,
                                         cel::BoolValue(shortcircuit));
          return;
        }
        break;
      case cel::ValueKind::kUnknown:
        break;
      case cel::ValueKind::kError:
      default:
        if (!error_pos.has_value()) {
          error_pos = i;
        }
        break;
    }
  }

  // As opposed to regular function, logical operation treat Unknowns with
  // higher precedence than error. This is due to the fact that after Unknown
  // is resolved to actual value, it may short-circuit and thus hide the
  // error.
  if (frame.enable_unknowns()) {
    // Check if unknown?
    UnknownAccumulator accumulator(frame.unknown_tree());
    for (const auto& arg : args) {
      accumulator.MaybeAdd(arg);
    }
    if (auto accumulated = std::move(accumulator).Accumulate();
        accumulated.has_value()) {
      frame.value_stack().PopAndPush(num_args, *accumulated);
      return;
    }
  }

  if (!error_pos.has_value()) {
    frame.value_stack().PopAndPush(num_args, cel::BoolValue(!shortcircuit));
    return;
  }

  cel::Value result = args[error_pos.value()];
  if (!result.IsError()) {
    result = cel::ErrorValue::From(
        cel::runtime_internal::CreateNoMatchingOverloadError(op_name),
        frame.arena());
  }
  frame.value_stack().PopAndPush(num_args, std::move(result));
}

ABSL_ATTRIBUTE_ALWAYS_INLINE inline void EvaluateLazyInitStep(
    ExecutionFrame& frame, const LazyInitStepInfo& step) {
  ComprehensionSlot* slot = frame.comprehension_slots().Get(step.slot_index);
  if (slot->Has()) {
    frame.value_stack().Push(slot->value(), slot->attribute());
  } else {
    frame.Call(step.slot_index, step.subexpression_index);
  }
}

ABSL_ATTRIBUTE_ALWAYS_INLINE inline void EvaluateAssignSlotAndPop(
    ExecutionFrame& frame, size_t slot_index) {
  if (ABSL_PREDICT_FALSE(!frame.value_stack().HasEnough(1))) {
    frame.Abort(absl::InternalError("Stack underflow assigning lazy value"));
    return;
  }
  ComprehensionSlot* slot = frame.comprehension_slots().Get(slot_index);
  slot->Set(frame.value_stack().Peek(), frame.value_stack().PeekAttribute());
  frame.value_stack().Pop(1);
}

ABSL_ATTRIBUTE_ALWAYS_INLINE inline void EvaluateClearSlotStep(
    ExecutionFrame& frame, const ClearSlotStepInfo& step) {
  for (size_t i = 0; i < step.slot_count; ++i) {
    frame.comprehension_slots().ClearSlot(step.slot_index + i);
  }
}

ABSL_ATTRIBUTE_ALWAYS_INLINE inline void EvaluateStep(
    ExecutionFrame& frame, const ExpressionStep& step) {
  switch (step.kind()) {
    case ExpressionStepKind::kGenericLogic:
      step.data().logic->Evaluate(&frame);
      break;
    case ExpressionStepKind::kIntConstant:
      frame.value_stack().Push(cel::IntValue(step.data().int_val));
      break;
    case ExpressionStepKind::kBoolConstant:
      frame.value_stack().Push(cel::BoolValue(step.data().bool_val));
      break;
    case ExpressionStepKind::kDoubleConstant:
      frame.value_stack().Push(cel::DoubleValue(step.data().double_val));
      break;
    case ExpressionStepKind::kNullConstant:
      frame.value_stack().Push(cel::NullValue());
      break;
    case ExpressionStepKind::kUintConstant:
      frame.value_stack().Push(cel::UintValue(step.data().uint_val));
      break;
    case ExpressionStepKind::kOtherConstant:
      frame.value_stack().Push(*step.data().other_val);
      break;
    case ExpressionStepKind::kLazyInit:
      EvaluateLazyInitStep(frame, step.data().lazy_init);
      break;
    case ExpressionStepKind::kAssignSlotAndPop:
      EvaluateAssignSlotAndPop(frame, step.data().slot_index);
      break;
    case ExpressionStepKind::kClearSlots:
      EvaluateClearSlotStep(frame, step.data().clear_slots);
      break;
    case ExpressionStepKind::kBooleanNot:
      EvaluateNotStep(frame);
      break;
    case ExpressionStepKind::kNotStrictlyFalse:
      EvaluateNotStrictlyFalseStep(frame);
      break;
    case ExpressionStepKind::kBooleanOr:
      EvaluateBoolLogicStep(frame, step.data().arg_count, BoolLogicKind::kOr);
      break;
    case ExpressionStepKind::kBooleanAnd:
      EvaluateBoolLogicStep(frame, step.data().arg_count, BoolLogicKind::kAnd);
      break;
    case ExpressionStepKind::kComprehensionFinish:
      EvaluateComprehensionFinishStep(step.data().slot_index, frame);
      break;
    case ExpressionStepKind::kComprehensionNext:
      step.data().next_step.Evaluate1(&frame);
      break;
    case ExpressionStepKind::kComprehensionNext2:
      step.data().next_step.Evaluate2(&frame);
      break;
    case ExpressionStepKind::kComprehensionCond:
      step.data().cond_step.Evaluate1(&frame);
      break;
    case ExpressionStepKind::kComprehensionCond2:
      step.data().cond_step.Evaluate2(&frame);
      break;
    case ExpressionStepKind::kReadSlot:
      EvaluateReadSlotStep(frame, step.data().slot_index);
      break;
    case ExpressionStepKind::kBooleanOrJump:
      EvaluateBoolJumpStep(frame, step.data().bool_jump_step, true);
      break;
    case ExpressionStepKind::kBooleanAndJump:
      EvaluateBoolJumpStep(frame, step.data().bool_jump_step, false);
      break;
    case ExpressionStepKind::kTernaryJump:
      EvaluateTernaryJumpStep(frame, step.data().ternary_jump_step);
      break;
    case ExpressionStepKind::kFixedJump:
      ABSL_DCHECK(step.data().fixed_jump_step.set)
          << "FixedJumpStep did not have a value set.";
      frame.JumpToOrAbort(step.data().fixed_jump_step.offset);
      break;
    case ExpressionStepKind::kIdentifier:
      EvaluateIdentifierStep(frame, step.data().ident_step);
      break;
    case ExpressionStepKind::kEagerFunction:
      step.data().eager_function_step->Evaluate(frame);
      break;
    case ExpressionStepKind::kLazyFunction:
      step.data().lazy_function_step->Evaluate(frame);
      break;
    case ExpressionStepKind::kFastIn:
      EvaluateFastInStep(frame);
      break;
    case ExpressionStepKind::kFastEqual:
      EvaluateFastEqualStep(/*negation=*/false, frame);
      break;
    case ExpressionStepKind::kFastNotEqual:
      EvaluateFastEqualStep(/*negation=*/true, frame);
      break;
    case ExpressionStepKind::kNewMutableList:
      frame.value_stack().Push(cel::CustomListValue(
          cel::common_internal::NewMutableListValue(frame.arena()),
          frame.arena()));
      break;
    case ExpressionStepKind::kMutableListAppend:
      EvaluateMutableListAppendStep(frame);
      break;
    case ExpressionStepKind::kCreateList:
      EvaluateListStep(*step.data().create_list_step, frame);
      break;
    case ExpressionStepKind::kCreateSmallList:
      EvaluateSmallListStep(step.data().create_small_list_step, frame);
      break;
    case ExpressionStepKind::kBitIsSet:
      EvaluateBitIsSetStep(step.data().bitmask, frame);
      break;
    case ExpressionStepKind::kBitAndIsNonZero:
      EvaluateBitAndIsNonZeroStep(step.data().bitmask, frame);
      break;
    case ExpressionStepKind::kBitAndEqual:
      EvaluateBitAndEqualStep(step.data().bitmask, frame);
      break;
    case ExpressionStepKind::kBitEqual:
      EvaluateBitEqualStep(step.data().bitmask, frame);
      break;
    case ExpressionStepKind::kMovedFrom:
      frame.Abort(absl::InternalError(
          "ExpressionStep::Evaluate called on moved-from step"));
      break;
    default:
      CEL_INTERNAL_UNREACHABLE();
  }
}

}  // namespace

void ExecutionFrame::Interpret() {
  for (const ExpressionStep* expr = Next(); ABSL_PREDICT_TRUE(expr != nullptr);
       expr = Next()) {
    EvaluateStep(*this, *expr);
  }
}

void ExecutionFrame::InterpretWithCallback(EvaluationListener& listener) {
  for (const ExpressionStep* expr = Next(); ABSL_PREDICT_TRUE(expr != nullptr);
       expr = Next()) {
    EvaluateStep(*this, *expr);
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
  return FlatExpressionEvaluatorState(
      value_stack_size_, comprehension_slots_size_, iterator_stack_size_,
      type_provider_, descriptor_pool, message_factory, arena);
}

FlatExpressionEvaluatorState FlatExpression::MakeEvaluatorState(
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nullable message_factory) const {
  return FlatExpressionEvaluatorState(
      value_stack_size_, comprehension_slots_size_, iterator_stack_size_,
      type_provider_, descriptor_pool, message_factory);
}

absl::StatusOr<cel::Value> FlatExpression::EvaluateWithCallback(
    const cel::ActivationInterface& activation,
    const cel::EmbedderContext* absl_nullable embedder_context,
    EvaluationListener listener, FlatExpressionEvaluatorState& state) const {
  state.Reset();
  ExecutionFrame frame(subexpressions_, activation, options_, state,
                       std::move(listener), embedder_context, identifiers_);

  return frame.Evaluate(frame.callback());
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
      step.u_.other_val = new cel::Value(value);
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

IdentStepInfo* GetIfIdentStep(ExpressionStep& step) {
  if (step.header_.kind == ExpressionStepKind::kIdentifier) {
    return &step.u_.ident_step;
  }
  return nullptr;
}

const IdentStepInfo* GetIfIdentStep(const ExpressionStep& step) {
  if (step.header_.kind == ExpressionStepKind::kIdentifier) {
    return &step.u_.ident_step;
  }
  return nullptr;
}

std::optional<int64_t> ExpressionStep::StackDelta() const {
  switch (header_.kind) {
    case ExpressionStepKind::kMovedFrom:
      return 0;
    case ExpressionStepKind::kGenericLogic:
      if (header_.stack_delta == std::numeric_limits<int16_t>::max()) {
        // Impractical, but technically allowed.
        return std::nullopt;
      }
      return header_.stack_delta;
    case ExpressionStepKind::kIntConstant:
    case ExpressionStepKind::kBoolConstant:
    case ExpressionStepKind::kDoubleConstant:
    case ExpressionStepKind::kNullConstant:
    case ExpressionStepKind::kUintConstant:
    case ExpressionStepKind::kOtherConstant:
    case ExpressionStepKind::kLazyInit:
    case ExpressionStepKind::kReadSlot:
    case ExpressionStepKind::kIdentifier:
    case ExpressionStepKind::kNewMutableList:
      return 1;
    case ExpressionStepKind::kAssignSlotAndPop:
    case ExpressionStepKind::kComprehensionFinish:
    case ExpressionStepKind::kComprehensionNext:
    case ExpressionStepKind::kComprehensionNext2:
    case ExpressionStepKind::kComprehensionCond:
    case ExpressionStepKind::kComprehensionCond2:
    case ExpressionStepKind::kFastIn:
    case ExpressionStepKind::kFastEqual:
    case ExpressionStepKind::kFastNotEqual:
    case ExpressionStepKind::kMutableListAppend:
      return -1;
    case ExpressionStepKind::kClearSlots:
    case ExpressionStepKind::kBooleanNot:
    case ExpressionStepKind::kNotStrictlyFalse:
    case ExpressionStepKind::kBooleanOrJump:
    case ExpressionStepKind::kBooleanAndJump:
    case ExpressionStepKind::kTernaryJump:
    case ExpressionStepKind::kFixedJump:
    case ExpressionStepKind::kBitIsSet:
    case ExpressionStepKind::kBitAndIsNonZero:
    case ExpressionStepKind::kBitAndEqual:
    case ExpressionStepKind::kBitEqual:
      return 0;
    case ExpressionStepKind::kBooleanOr:
    case ExpressionStepKind::kBooleanAnd:
      return OneMinusArgCount(u_.arg_count);
    case ExpressionStepKind::kEagerFunction:
      return OneMinusArgCount(u_.eager_function_step->num_arguments());
    case ExpressionStepKind::kLazyFunction:
      return OneMinusArgCount(u_.lazy_function_step->num_arguments());
    case ExpressionStepKind::kCreateList:
      return OneMinusArgCount(u_.create_list_step->num_elements());
    case ExpressionStepKind::kCreateSmallList:
      return OneMinusArgCount(u_.create_small_list_step.list_size);
  }
  return std::nullopt;
}
}  // namespace google::api::expr::runtime
