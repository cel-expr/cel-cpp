#ifndef THIRD_PARTY_CEL_CPP_EVAL_EVAL_LOGIC_STEP_H_
#define THIRD_PARTY_CEL_CPP_EVAL_EVAL_LOGIC_STEP_H_

#include <cstddef>
#include <cstdint>
#include <memory>

#include "eval/eval/direct_expression_step.h"

namespace google::api::expr::runtime {

class ExecutionFrame;

void EvaluateNotStep(ExecutionFrame& frame);

void EvaluateNotStrictlyFalseStep(ExecutionFrame& frame);

enum class BoolLogicKind : uint8_t {
  kAnd = 0,
  kOr = 1,
};

void EvaluateBoolLogicStep(BoolLogicKind kind, size_t num_args,
                           ExecutionFrame& frame);

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

// Factory method for recursive logical not "!" Execution step
std::unique_ptr<DirectExpressionStep> CreateDirectNotStep(
    std::unique_ptr<DirectExpressionStep> operand, int64_t expr_id);

// Factory method for recursive logical "@not_strictly_false" Execution step.
std::unique_ptr<DirectExpressionStep> CreateDirectNotStrictlyFalseStep(
    std::unique_ptr<DirectExpressionStep> operand, int64_t expr_id);

}  // namespace google::api::expr::runtime

#endif  // THIRD_PARTY_CEL_CPP_EVAL_EVAL_LOGIC_STEP_H_
