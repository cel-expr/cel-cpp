#ifndef THIRD_PARTY_CEL_CPP_EVAL_EVAL_COMPREHENSION_STEP_H_
#define THIRD_PARTY_CEL_CPP_EVAL_EVAL_COMPREHENSION_STEP_H_

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

#include "absl/status/status.h"
#include "eval/eval/direct_expression_step.h"
#include "eval/eval/expression_step_logic.h"
#include "eval/eval/iterator_stack.h"

namespace google::api::expr::runtime {

// Comprehension Evaluation
//
// 0: <iter_range>              1 -> 1
// 1: ComprehensionInitStep     1 -> 1
// 2: <accu_init>               1 -> 2
// 3: ComprehensionNextStep     2 -> 1
// 4: <loop_condition>          1 -> 2
// 5: ComprehensionCondStep     2 -> 1
// 6: <loop_step>               1 -> 2
// 8: <result>                  1 -> 2
// 9: ComprehensionFinishStep   2 -> 1

class ComprehensionInitStep final : public ExpressionStepLogic {
 public:
  ComprehensionInitStep(size_t iter_slot, size_t iter2_slot, size_t accu_slot)
      : iter_slot_(iter_slot),
        iter2_slot_(iter2_slot),
        accu_slot_(accu_slot),
        has_iter2_(true) {}
  ComprehensionInitStep(size_t iter_slot, size_t accu_slot)
      : iter_slot_(iter_slot),
        iter2_slot_(0),
        accu_slot_(accu_slot),
        has_iter2_(false) {}

  void set_error_jump_offset(int offset) { error_jump_offset_ = offset; }

  void Evaluate(ExecutionFrame* frame) const override;

 private:
  const size_t iter_slot_;
  const size_t iter2_slot_;
  const size_t accu_slot_;
  bool has_iter2_ = false;
  int error_jump_offset_ = std::numeric_limits<int>::max();
};

class ComprehensionNextStep final {
 public:
  ComprehensionNextStep() = default;

  void set_jump_offset(int offset) { jump_offset_ = offset; }

  void set_error_jump_offset(int offset) { error_jump_offset_ = offset; }

  void Evaluate1(ExecutionFrame* frame) const;

  void Evaluate2(ExecutionFrame* frame) const;

 private:
  int32_t jump_offset_ = std::numeric_limits<int32_t>::max();
  int32_t error_jump_offset_ = std::numeric_limits<int32_t>::max();
};

class ComprehensionCondStep final {
 public:
  ComprehensionCondStep() = default;

  void set_jump_offset(int offset) { jump_offset_ = offset; }

  void set_error_jump_offset(int offset) { error_jump_offset_ = offset; }

  void Evaluate1(ExecutionFrame* frame) const;

  void Evaluate2(ExecutionFrame* frame) const;

 private:
  int32_t jump_offset_ = std::numeric_limits<int32_t>::max();
  int32_t error_jump_offset_ = std::numeric_limits<int32_t>::max();
};

// Creates a step for executing a comprehension.
std::unique_ptr<DirectExpressionStep> CreateDirectComprehensionStep(
    size_t iter_slot, size_t iter2_slot, size_t accu_slot,
    std::unique_ptr<DirectExpressionStep> range,
    std::unique_ptr<DirectExpressionStep> accu_init,
    std::unique_ptr<DirectExpressionStep> loop_step,
    std::unique_ptr<DirectExpressionStep> condition_step,
    std::unique_ptr<DirectExpressionStep> result_step, bool shortcircuiting,
    int64_t expr_id);

// Runs a cleanup step for the comprehension.
// Removes the comprehension context then pushes the 'result' sub expression to
// the top of the stack.
void EvaluateComprehensionFinishStep(size_t accu_slot, ExecutionFrame& frame);

}  // namespace google::api::expr::runtime

#endif  // THIRD_PARTY_CEL_CPP_EVAL_EVAL_COMPREHENSION_STEP_H_
