#ifndef THIRD_PARTY_CEL_CPP_EVAL_EVAL_COMPREHENSION_STEP_H_
#define THIRD_PARTY_CEL_CPP_EVAL_EVAL_COMPREHENSION_STEP_H_

#include <cstdint>
#include <limits>

#include "absl/base/macros.h"

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

enum class ComprehensionVersion {
  kV1 = 0,
  kV2,
};

struct ComprehensionInitStepInfo {
  ABSL_DEPRECATE_AND_INLINE()
  void set_error_jump_offset(int offset) { error_jump_offset = offset; }

  [[nodiscard]]
  uint32_t iter_slot() const {
    return first_slot;
  }

  [[nodiscard]]
  uint32_t iter2_slot() const {
    ABSL_ASSERT(v2);
    return first_slot + 1;
  }

  [[nodiscard]]
  uint32_t accu_slot() const {
    return first_slot + v2 + 1;
  }

  // First slot.
  uint32_t first_slot : 31;
  uint32_t v2 : 1;
  int32_t error_jump_offset = std::numeric_limits<int>::max();
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
