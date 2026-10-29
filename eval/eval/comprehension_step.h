#ifndef THIRD_PARTY_CEL_CPP_EVAL_EVAL_COMPREHENSION_STEP_H_
#define THIRD_PARTY_CEL_CPP_EVAL_EVAL_COMPREHENSION_STEP_H_

#include <cstddef>
#include <cstdint>
#include <limits>

#include "absl/base/macros.h"
#include "eval/eval/expression_step_logic.h"

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

struct ComprehensionNextStepInfo {
  ABSL_DEPRECATE_AND_INLINE()
  void set_jump_offset(int offset) { jump_offset = offset; }

  ABSL_DEPRECATE_AND_INLINE()
  void set_error_jump_offset(int offset) { error_jump_offset = offset; }

  int32_t jump_offset = std::numeric_limits<int32_t>::max();
  int32_t error_jump_offset = std::numeric_limits<int32_t>::max();
};

struct ComprehensionCondStepInfo {
  ABSL_DEPRECATE_AND_INLINE()
  void set_jump_offset(int offset) { jump_offset = offset; }

  ABSL_DEPRECATE_AND_INLINE()
  void set_error_jump_offset(int offset) { error_jump_offset = offset; }

  int32_t jump_offset = std::numeric_limits<int32_t>::max();
  int32_t error_jump_offset = std::numeric_limits<int32_t>::max();
};

}  // namespace google::api::expr::runtime

#endif  // THIRD_PARTY_CEL_CPP_EVAL_EVAL_COMPREHENSION_STEP_H_
