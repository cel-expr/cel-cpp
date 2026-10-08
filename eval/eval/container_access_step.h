#ifndef THIRD_PARTY_CEL_CPP_EVAL_EVAL_CONTAINER_ACCESS_STEP_H_
#define THIRD_PARTY_CEL_CPP_EVAL_EVAL_CONTAINER_ACCESS_STEP_H_

#include <memory>

#include "absl/status/statusor.h"
#include "common/expr.h"
#include "eval/eval/expression_step_logic.h"

namespace google::api::expr::runtime {

// Factory method for Select - based Execution step
absl::StatusOr<std::unique_ptr<ExpressionStepLogic>> CreateContainerAccessStep(
    const cel::CallExpr& call, bool enable_optional_types = false);

}  // namespace google::api::expr::runtime

#endif  // THIRD_PARTY_CEL_CPP_EVAL_EVAL_CONTAINER_ACCESS_STEP_H_
