#include "eval/eval/logic_step.h"

#include <cstddef>
#include <optional>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "base/builtins.h"
#include "common/value.h"
#include "common/value_kind.h"
#include "eval/eval/attribute_trail.h"
#include "eval/eval/evaluator_core.h"
#include "eval/internal/errors.h"
#include "runtime/internal/errors.h"

namespace google::api::expr::runtime {

namespace {

using ::cel::BoolValue;
using ::cel::ErrorValue;
using ::cel::UnknownValue;
using ::cel::Value;
using ::cel::ValueKind;
using ::cel::runtime_internal::CreateNoMatchingOverloadError;

}  // namespace

void EvaluateNotStep(ExecutionFrame& frame) {
  if (!frame.value_stack().HasEnough(1)) {
    frame.Abort(absl::InternalError("Value stack underflow"));
    return;
  }
  const Value& operand = frame.value_stack().Peek();

  if (frame.unknown_processing_enabled()) {
    const AttributeTrail& attribute_trail = frame.value_stack().PeekAttribute();
    if (frame.attribute_utility().CheckForUnknownPartial(attribute_trail)) {
      frame.value_stack().PopAndPush(frame.attribute_utility().CreateUnknownSet(
          attribute_trail.attribute()));
      return;
    }
  }

  switch (operand.kind()) {
    case ValueKind::kBool:
      frame.value_stack().PopAndPush(
          BoolValue{!operand.GetBool().NativeValue()});
      break;
    case ValueKind::kUnknown:
    case ValueKind::kError:
      // just forward.
      break;
    default:
      frame.value_stack().PopAndPush(cel::ErrorValue::From(
          CreateNoMatchingOverloadError(cel::builtin::kNot), frame.arena()));
      break;
  }
}

void EvaluateNotStrictlyFalseStep(ExecutionFrame& frame) {
  if (!frame.value_stack().HasEnough(1)) {
    frame.Abort(absl::InternalError("Value stack underflow"));
    return;
  }
  const Value& operand = frame.value_stack().Peek();

  switch (operand.kind()) {
    case ValueKind::kBool:
      // just forward.
      break;
    case ValueKind::kUnknown:
    case ValueKind::kError:
      frame.value_stack().PopAndPush(BoolValue(true));
      break;
    default:
      frame.value_stack().PopAndPush(cel::ErrorValue::From(
          CreateNoMatchingOverloadError(cel::builtin::kNot), frame.arena()));
      break;
  }
}

void EvaluateBoolLogicStep(BoolLogicKind kind, size_t num_args,
                           ExecutionFrame& frame) {
  if (!frame.value_stack().HasEnough(num_args)) {
    frame.Abort(absl::InternalError("Value stack underflow"));
    return;
  }

  const bool shortcircuit = kind == BoolLogicKind::kOr;
  const absl::string_view op_name =
      kind == BoolLogicKind::kOr ? cel::builtin::kOr : cel::builtin::kAnd;
  absl::Span<const Value> args = frame.value_stack().GetSpan(num_args);
  std::optional<size_t> error_pos;

  for (size_t i = 0; i < args.size(); i++) {
    const Value& arg = args[i];
    switch (arg.kind()) {
      case ValueKind::kBool:
        if (arg.GetBool() == shortcircuit) {
          frame.value_stack().PopAndPush(num_args,
                                         cel::BoolValue(shortcircuit));
          return;
        }
        break;
      case ValueKind::kUnknown:
        break;
      case ValueKind::kError:
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
    std::optional<cel::UnknownValue> unknown_set =
        frame.attribute_utility().MergeUnknowns(args);
    if (unknown_set.has_value()) {
      frame.value_stack().PopAndPush(num_args, *std::move(unknown_set));
      return;
    }
  }

  if (!error_pos.has_value()) {
    frame.value_stack().PopAndPush(num_args, cel::BoolValue(!shortcircuit));
    return;
  }

  cel::Value result = args[error_pos.value()];
  if (!result.IsError()) {
    result = cel::ErrorValue::From(CreateNoMatchingOverloadError(op_name),
                                   frame.arena());
  }
  frame.value_stack().PopAndPush(num_args, std::move(result));
}

}  // namespace google::api::expr::runtime
