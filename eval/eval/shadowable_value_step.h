#ifndef THIRD_PARTY_CEL_CPP_EVAL_EVAL_SHADOWABLE_VALUE_STEP_H_
#define THIRD_PARTY_CEL_CPP_EVAL_EVAL_SHADOWABLE_VALUE_STEP_H_

#include <memory>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "common/value.h"
#include "eval/eval/expression_step_logic.h"

namespace google::api::expr::runtime {

// Create an identifier resolution step with a default value that may be
// shadowed by an identifier of the same name within the runtime-provided
// Activation.
absl::StatusOr<std::unique_ptr<ExpressionStepLogic>> CreateShadowableValueStep(
    absl::string_view name, cel::Value value);

}  // namespace google::api::expr::runtime

#endif  // THIRD_PARTY_CEL_CPP_EVAL_EVAL_SHADOWABLE_VALUE_STEP_H_
