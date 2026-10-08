#ifndef THIRD_PARTY_CEL_CPP_EVAL_EVAL_TERNARY_STEP_H_
#define THIRD_PARTY_CEL_CPP_EVAL_EVAL_TERNARY_STEP_H_

#include <memory>

#include "eval/eval/expression_step_logic.h"

namespace google::api::expr::runtime {

// Factory method for ternary (_?_:_) execution step
std::unique_ptr<ExpressionStepLogic> CreateTernaryStep();

}  // namespace google::api::expr::runtime

#endif  // THIRD_PARTY_CEL_CPP_EVAL_EVAL_TERNARY_STEP_H_
