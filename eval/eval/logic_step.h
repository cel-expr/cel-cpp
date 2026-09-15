#ifndef THIRD_PARTY_CEL_CPP_EVAL_EVAL_LOGIC_STEP_H_
#define THIRD_PARTY_CEL_CPP_EVAL_EVAL_LOGIC_STEP_H_

#include <cstdint>
#include <memory>

#include "absl/status/statusor.h"
#include "eval/eval/direct_expression_step.h"
#include "eval/eval/evaluator_core.h"

namespace google::api::expr::runtime {

// Factory method for "And" Execution step
std::unique_ptr<DirectExpressionStep> CreateDirectAndStep(
    std::unique_ptr<DirectExpressionStep> lhs,
    std::unique_ptr<DirectExpressionStep> rhs, int64_t expr_id,
    bool shortcircuiting);

// Factory method for "Or" Execution step
std::unique_ptr<DirectExpressionStep> CreateDirectOrStep(
    std::unique_ptr<DirectExpressionStep> lhs,
    std::unique_ptr<DirectExpressionStep> rhs, int64_t expr_id,
    bool shortcircuiting);

// Factory method for "And" Execution step
std::unique_ptr<ExpressionStepLogic> CreateAndStep(size_t num_args);

// Factory method for "Or" Execution step
std::unique_ptr<ExpressionStepLogic> CreateOrStep(size_t num_args);

// Factory method for recursive logical not "!" Execution step
std::unique_ptr<DirectExpressionStep> CreateDirectNotStep(
    std::unique_ptr<DirectExpressionStep> operand, int64_t expr_id);

// Factory method for iterative logical not "!" Execution step
std::unique_ptr<ExpressionStepLogic> CreateNotStep();

// Factory method for recursive logical "@not_strictly_false" Execution step.
std::unique_ptr<DirectExpressionStep> CreateDirectNotStrictlyFalseStep(
    std::unique_ptr<DirectExpressionStep> operand, int64_t expr_id);

// Factory method for iterative logical "@not_strictly_false" Execution step.
std::unique_ptr<ExpressionStepLogic> CreateNotStrictlyFalseStep();

}  // namespace google::api::expr::runtime

#endif  // THIRD_PARTY_CEL_CPP_EVAL_EVAL_LOGIC_STEP_H_
