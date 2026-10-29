#include "eval/eval/comprehension_step.h"

#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/value.h"
#include "common/value_kind.h"
#include "eval/eval/attribute_trail.h"
#include "eval/eval/evaluator_core.h"
#include "eval/eval/iterator_stack.h"
#include "eval/internal/errors.h"

namespace google::api::expr::runtime {
namespace {

using ::cel::Value;
using ::cel::ValueIteratorPtr;
using ::cel::ValueKind;
using ::cel::runtime_internal::CreateNoMatchingOverloadError;

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

}  // namespace google::api::expr::runtime
