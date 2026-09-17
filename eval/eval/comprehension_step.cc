#include "eval/eval/comprehension_step.h"

#include <cstddef>
#include <memory>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/casts.h"
#include "absl/base/optimization.h"
#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "base/attribute.h"
#include "common/value.h"
#include "common/value_kind.h"
#include "eval/eval/attribute_trail.h"
#include "eval/eval/comprehension_slots.h"
#include "eval/eval/evaluator_core.h"
#include "eval/eval/iterator_stack.h"
#include "eval/internal/errors.h"

namespace google::api::expr::runtime {
namespace {

enum class IterableKind {
  kList = 1,
  kMap,
};

using ::cel::AttributeQualifier;
using ::cel::Value;
using ::cel::ValueIteratorPtr;
using ::cel::ValueKind;
using ::cel::runtime_internal::CreateNoMatchingOverloadError;
using ::cel::runtime_internal::IteratorStack;

AttributeQualifier AttributeQualifierFromValue(const Value& v) {
  switch (v.kind()) {
    case ValueKind::kString:
      return AttributeQualifier::OfString(v.GetString().ToString());
    case ValueKind::kInt64:
      return AttributeQualifier::OfInt(v.GetInt().NativeValue());
    case ValueKind::kUint64:
      return AttributeQualifier::OfUint(v.GetUint().NativeValue());
    case ValueKind::kBool:
      return AttributeQualifier::OfBool(v.GetBool().NativeValue());
    default:
      // Non-matching qualifier.
      return AttributeQualifier();
  }
}

}  // namespace

void ComprehensionInitStep::Evaluate(ExecutionFrame* frame) const {
  if (!frame->value_stack().HasEnough(1)) {
    frame->Abort(absl::InternalError("Value stack underflow"));
    return;
  }

  const Value& top = frame->value_stack().Peek();
  if (top.IsError() || top.IsUnknown()) {
    frame->JumpToOrAbort(error_jump_offset_);
    return;
  }

  if (frame->enable_unknowns() && top.IsMap()) {
    const AttributeTrail& top_attr = frame->value_stack().PeekAttribute();
    if (auto unknown = top_attr.PartialUnknownMatch(frame->unknown_tree());
        unknown.has_value()) {
      frame->value_stack().PopAndPush(*unknown);
      frame->JumpToOrAbort(error_jump_offset_);
      return;
    }
  }

  absl::StatusOr<cel::ValueIteratorPtr> iterator;
  switch (top.kind()) {
    case ValueKind::kList:
      iterator = top.GetList().NewIterator();
      break;
    case ValueKind::kMap:
      iterator = top.GetMap().NewIterator();
      break;
    default:
      // Replace <iter_range> with an error and jump past
      // ComprehensionFinishStep.
      frame->value_stack().PopAndPush(cel::ErrorValue::From(
          CreateNoMatchingOverloadError("<iter_range>"), frame->arena()));
      frame->JumpToOrAbort(error_jump_offset_);
      return;
  }

  if (!iterator.ok()) {
    frame->Abort(std::move(iterator).status());
    return;
  }
  if (has_iter2_) {
    frame->iterator_stack().Push(*std::move(iterator), iter_slot_, iter2_slot_,
                                 accu_slot_);
  } else {
    frame->iterator_stack().Push(*std::move(iterator), iter_slot_, accu_slot_);
  }
}

void ComprehensionNextStep::Evaluate1(ExecutionFrame* frame) const {
  if (frame->iterator_stack().empty()) {
    frame->Abort(absl::InternalError("Iterator stack underflow"));
    return;
  }
  const IteratorStack::Entry& entry = *frame->iterator_stack().Peek();
  if (!frame->value_stack().HasEnough(2)) {
    frame->Abort(
        absl::Status(absl::StatusCode::kInternal, "Value stack underflow"));
    return;
  }

  {
    Value& accu_var = frame->value_stack().Peek();
    AttributeTrail& accu_var_attr = frame->value_stack().PeekAttribute();
    frame->comprehension_slots().Set(entry.accu_slot, std::move(accu_var),
                                     std::move(accu_var_attr));
    frame->value_stack().Pop(1);
  }

  ComprehensionSlots::Slot* iter_slot =
      frame->comprehension_slots().Get(entry.iter_slot);
  ABSL_DCHECK(iter_slot != nullptr);
  iter_slot->Set();

  if (frame->enable_unknowns()) {
    Value key_or_value;
    Value* key;
    Value* value;
    switch (frame->value_stack().Peek().kind()) {
      case ValueKind::kList:
        key = &key_or_value;
        value = iter_slot->mutable_value();
        break;
      case ValueKind::kMap:
        key = iter_slot->mutable_value();
        value = nullptr;
        break;
      default:
        ABSL_UNREACHABLE();
    }
    absl::StatusOr<bool> ok = entry.iterator->Next2(frame->descriptor_pool(),
                                                    frame->message_factory(),
                                                    frame->arena(), key, value);
    if (!ok.ok()) {
      frame->Abort(std::move(ok).status());
      return;
    }
    if (!*ok) {
      iter_slot->Clear();
      frame->JumpToOrAbort(jump_offset_);
      return;
    }
    absl::Status inc_status = frame->IncrementIterations();
    if (!inc_status.ok()) {
      frame->Abort(std::move(inc_status));
      return;
    }
    static_cast<void>(
        frame->value_stack().PeekAttribute().Match<AttributeTrail::kFull>(
            AttributeQualifierFromValue(*key), *iter_slot->mutable_value(),
            *iter_slot->mutable_attribute(), frame->unknown_tree()));
  } else {
    absl::StatusOr<bool> ok = entry.iterator->Next1(
        frame->descriptor_pool(), frame->message_factory(), frame->arena(),
        iter_slot->mutable_value());
    if (!ok.ok()) {
      frame->Abort(std::move(ok).status());
      return;
    }
    if (!*ok) {
      iter_slot->Clear();
      frame->JumpToOrAbort(jump_offset_);
      return;
    }
    absl::Status inc_status = frame->IncrementIterations();
    if (!inc_status.ok()) {
      frame->Abort(std::move(inc_status));
      return;
    }
  }
}

void ComprehensionNextStep::Evaluate2(ExecutionFrame* frame) const {
  if (frame->iterator_stack().empty()) {
    frame->Abort(absl::InternalError("Iterator stack underflow"));
    return;
  }
  const IteratorStack::Entry& entry = *frame->iterator_stack().Peek();
  if (!frame->value_stack().HasEnough(2)) {
    frame->Abort(
        absl::Status(absl::StatusCode::kInternal, "Value stack underflow"));
    return;
  }

  {
    Value& accu_var = frame->value_stack().Peek();
    AttributeTrail& accu_var_attr = frame->value_stack().PeekAttribute();
    frame->comprehension_slots().Set(entry.accu_slot, std::move(accu_var),
                                     std::move(accu_var_attr));
    frame->value_stack().Pop(1);
  }

  ComprehensionSlots::Slot* iter_slot =
      frame->comprehension_slots().Get(entry.iter_slot);
  ABSL_DCHECK(iter_slot != nullptr);
  iter_slot->Set();

  ComprehensionSlots::Slot* iter2_slot =
      frame->comprehension_slots().Get(entry.iter2_slot);
  ABSL_DCHECK(iter2_slot != nullptr);
  iter2_slot->Set();

  absl::StatusOr<bool> ok = entry.iterator->Next2(
      frame->descriptor_pool(), frame->message_factory(), frame->arena(),
      iter_slot->mutable_value(), iter2_slot->mutable_value());
  if (!ok.ok()) {
    frame->Abort(std::move(ok).status());
    return;
  }
  if (!*ok) {
    iter_slot->Clear();
    iter2_slot->Clear();
    frame->JumpToOrAbort(jump_offset_);
    return;
  }
  absl::Status inc_status = frame->IncrementIterations();
  if (!inc_status.ok()) {
    frame->Abort(std::move(inc_status));
    return;
  }
  if (frame->enable_unknowns()) {
    static_cast<void>(
        frame->value_stack().PeekAttribute().Match<AttributeTrail::kFull>(
            AttributeQualifierFromValue(iter_slot->value()),
            *iter2_slot->mutable_value(), *iter_slot->mutable_attribute(),
            frame->unknown_tree()));
    *iter2_slot->mutable_attribute() = iter_slot->attribute();
  }
}

void ComprehensionCondStep::Evaluate1(ExecutionFrame* frame) const {
  if (frame->iterator_stack().empty()) {
    frame->Abort(absl::InternalError("Iterator stack underflow"));
    return;
  }
  const IteratorStack::Entry& entry = *frame->iterator_stack().Peek();
  if (!frame->value_stack().HasEnough(2)) {
    frame->Abort(
        absl::Status(absl::StatusCode::kInternal, "Value stack underflow"));
    return;
  }
  const Value& top = frame->value_stack().Peek();
  switch (top.kind()) {
    case ValueKind::kBool:
      break;
    case ValueKind::kError:
      ABSL_FALLTHROUGH_INTENDED;
    case ValueKind::kUnknown: {
      frame->value_stack().SwapAndPop(2, 1);
      frame->comprehension_slots().ClearSlot(entry.iter_slot);
      frame->comprehension_slots().ClearSlot(entry.accu_slot);
      frame->iterator_stack().Pop();
      frame->JumpToOrAbort(error_jump_offset_);
      return;
    }
    default: {
      frame->value_stack().PopAndPush(
          2, cel::ErrorValue::From(
                 CreateNoMatchingOverloadError("<loop_condition>"),
                 frame->arena()));
      frame->comprehension_slots().ClearSlot(entry.iter_slot);
      frame->comprehension_slots().ClearSlot(entry.accu_slot);
      frame->iterator_stack().Pop();
      frame->JumpToOrAbort(error_jump_offset_);
      return;
    }
  }
  const bool loop_condition = absl::implicit_cast<bool>(top.GetBool());
  const bool short_circuiting = frame->options().short_circuiting;
  frame->value_stack().Pop(1);  // loop_condition
  if (!loop_condition && short_circuiting) {
    frame->JumpToOrAbort(jump_offset_);
    return;
  }
}

void ComprehensionCondStep::Evaluate2(ExecutionFrame* frame) const {
  if (frame->iterator_stack().empty()) {
    frame->Abort(absl::InternalError("Iterator stack underflow"));
    return;
  }
  const IteratorStack::Entry& entry = *frame->iterator_stack().Peek();
  if (!frame->value_stack().HasEnough(2)) {
    frame->Abort(
        absl::Status(absl::StatusCode::kInternal, "Value stack underflow"));
    return;
  }
  const Value& top = frame->value_stack().Peek();
  switch (top.kind()) {
    case ValueKind::kBool:
      break;
    case ValueKind::kError:
      ABSL_FALLTHROUGH_INTENDED;
    case ValueKind::kUnknown: {
      frame->value_stack().SwapAndPop(2, 1);
      frame->comprehension_slots().ClearSlot(entry.iter_slot);
      frame->comprehension_slots().ClearSlot(entry.iter2_slot);
      frame->comprehension_slots().ClearSlot(entry.accu_slot);
      frame->iterator_stack().Pop();
      frame->JumpToOrAbort(error_jump_offset_);
      return;
    }
    default: {
      frame->value_stack().PopAndPush(
          2, cel::ErrorValue::From(
                 CreateNoMatchingOverloadError("<loop_condition>"),
                 frame->arena()));
      frame->comprehension_slots().ClearSlot(entry.iter_slot);
      frame->comprehension_slots().ClearSlot(entry.iter2_slot);
      frame->comprehension_slots().ClearSlot(entry.accu_slot);
      frame->iterator_stack().Pop();
      frame->JumpToOrAbort(error_jump_offset_);
      return;
    }
  }
  const bool loop_condition = absl::implicit_cast<bool>(top.GetBool());
  const bool short_circuiting = frame->options().short_circuiting;
  frame->value_stack().Pop(1);  // loop_condition
  if (!loop_condition && short_circuiting) {
    frame->JumpToOrAbort(jump_offset_);
    return;
  }
}

void EvaluateComprehensionFinishStep(size_t accu_slot, ExecutionFrame& frame) {
  if (!frame.value_stack().HasEnough(2)) {
    frame.Abort(
        absl::Status(absl::StatusCode::kInternal, "Value stack underflow"));
    return;
  }
  frame.value_stack().SwapAndPop(2, 1);
  frame.comprehension_slots().ClearSlot(accu_slot);
  frame.iterator_stack().Pop();
}

}  // namespace google::api::expr::runtime
