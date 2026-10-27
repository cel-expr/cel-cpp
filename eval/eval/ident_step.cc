#include "eval/eval/ident_step.h"

#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/optimization.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "common/internal/attribute_trail.h"
#include "common/value.h"
#include "eval/eval/attribute_trail.h"
#include "eval/eval/comprehension_slots.h"
#include "eval/eval/evaluator_core.h"
#include "eval/internal/errors.h"

namespace google::api::expr::runtime {

namespace {

using ::cel::Value;
using ::cel::runtime_internal::CreateError;

ABSL_ATTRIBUTE_ALWAYS_INLINE inline absl::Status LookupIdent(
    absl::string_view name, ExecutionFrame& frame, Value& result,
    AttributeTrail& attribute) {
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
      CreateError(absl::StrCat("No value with name \"", name,
                               "\" found in Activation")),
      frame.arena());

  return absl::OkStatus();
}

}  // namespace

void EvaluateIdentifierStep(const IdentStepInfo& step, ExecutionFrame& frame) {
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

}  // namespace google::api::expr::runtime
