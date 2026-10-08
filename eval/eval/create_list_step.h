#ifndef THIRD_PARTY_CEL_CPP_EVAL_EVAL_CREATE_LIST_STEP_H_
#define THIRD_PARTY_CEL_CPP_EVAL_EVAL_CREATE_LIST_STEP_H_

#include <memory>

#include "absl/status/statusor.h"
#include "common/expr.h"
#include "eval/eval/expression_step_logic.h"

namespace google::api::expr::runtime {

// Factory method for CreateList which constructs an immutable list.
absl::StatusOr<std::unique_ptr<ExpressionStepLogic>> CreateCreateListStep(
    const cel::ListExpr& create_list_expr);

}  // namespace google::api::expr::runtime

#endif  // THIRD_PARTY_CEL_CPP_EVAL_EVAL_CREATE_LIST_STEP_H_
