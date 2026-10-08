#include "eval/eval/ident_step.h"

#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "common/value.h"
#include "eval/eval/attribute_trail.h"
#include "eval/eval/evaluator_core.h"
#include "eval/eval/expression_step_logic.h"
#include "eval/internal/errors.h"
#include "internal/status_macros.h"

namespace google::api::expr::runtime {

namespace {

using ::cel::Value;
using ::cel::runtime_internal::CreateError;

absl::Status LookupIdent(absl::string_view name, ExecutionFrameBase& frame,
                         Value& result, AttributeTrail& attribute) {
  if (frame.attribute_tracking_enabled()) {
    attribute = AttributeTrail(std::string(name));
    if (frame.missing_attribute_errors_enabled() &&
        frame.attribute_utility().CheckForMissingAttribute(attribute)) {
      CEL_ASSIGN_OR_RETURN(
          result, frame.attribute_utility().CreateMissingAttributeError(
                      attribute.attribute(), frame.arena()));
      return absl::OkStatus();
    }
    if (frame.unknown_processing_enabled() &&
        frame.attribute_utility().CheckForUnknownExact(attribute)) {
      result =
          frame.attribute_utility().CreateUnknownSet(attribute.attribute());
      return absl::OkStatus();
    }
  }

  CEL_ASSIGN_OR_RETURN(
      auto found, frame.activation().FindVariable(name, frame.descriptor_pool(),
                                                  frame.message_factory(),
                                                  frame.arena(), &result));

  if (found) {
    return absl::OkStatus();
  }

  result = cel::ErrorValue::From(
      CreateError(absl::StrCat("No value with name \"", name,
                               "\" found in Activation")),
      frame.arena());

  return absl::OkStatus();
}

}  // namespace

void EvaluateIdentifierStep(absl::string_view identifier,
                            ExecutionFrame& frame) {
  frame.value_stack().Push(cel::NullValue());
  if (absl::Status status =
          LookupIdent(identifier, frame, frame.value_stack().Peek(),
                      frame.value_stack().PeekAttribute());
      !status.ok()) {
    frame.Abort(std::move(status));
    return;
  }
}

}  // namespace google::api::expr::runtime
