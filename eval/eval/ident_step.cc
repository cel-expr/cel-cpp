#include "eval/eval/ident_step.h"

#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/optimization.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "common/value.h"
#include "eval/eval/attribute_trail.h"
#include "eval/eval/evaluator_core.h"
#include "eval/eval/expression_step_logic.h"
#include "eval/internal/errors.h"

namespace google::api::expr::runtime {

namespace {

using ::cel::Value;
using ::cel::runtime_internal::CreateError;

ABSL_ATTRIBUTE_ALWAYS_INLINE inline void LookupIdent(
    absl::string_view name, ExecutionFrame& frame, Value& result,
    AttributeTrail& attribute) {
  if (frame.attribute_tracking_enabled() &&
      (AttributeLeadMatch)(frame.unknown_attributes(), frame.known_attributes(),
                           frame.missing_attributes(), name, result, attribute,
                           frame.unknown_tree())) {
    return;
  }

  auto status_or_found = frame.activation().FindVariable(
      name, frame.descriptor_pool(), frame.message_factory(), frame.arena(),
      &result);
  if (ABSL_PREDICT_FALSE(!status_or_found.ok())) {
    frame.Abort(std::move(status_or_found).status());
    return;
  }

  if (*status_or_found) {
    return;
  }

  if (frame.attribute_tracking_enabled()) {
    attribute = AttributeTrail();
  }
  result = cel::ErrorValue::From(
      CreateError(absl::StrCat("No value with name \"", name,
                               "\" found in Activation")),
      frame.arena());
}

}  // namespace

void EvaluateIdentifierStep(absl::string_view identifier,
                            ExecutionFrame& frame) {
  frame.value_stack().Push(cel::NullValue());
  LookupIdent(identifier, frame, frame.value_stack().Peek(),
              frame.value_stack().PeekAttribute());
}

}  // namespace google::api::expr::runtime
