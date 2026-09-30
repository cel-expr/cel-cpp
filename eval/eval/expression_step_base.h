#ifndef THIRD_PARTY_CEL_CPP_EVAL_EVAL_EXPRESSION_STEP_BASE_H_
#define THIRD_PARTY_CEL_CPP_EVAL_EVAL_EXPRESSION_STEP_BASE_H_

#include <cstdint>

#include "eval/eval/expression_step_logic.h"

namespace google::api::expr::runtime {

class ExpressionStepBase : public ExpressionStepLogic {
 public:
  ExpressionStepBase() = default;
  explicit ExpressionStepBase(int64_t /*expr_id*/,
                              bool /*comes_from_ast*/ = true) {}
};

}  // namespace google::api::expr::runtime

#endif  // THIRD_PARTY_CEL_CPP_EVAL_EVAL_EXPRESSION_STEP_BASE_H_
