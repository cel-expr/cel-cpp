#ifndef THIRD_PARTY_CEL_CPP_EVAL_EVAL_CREATE_LIST_STEP_H_
#define THIRD_PARTY_CEL_CPP_EVAL_EVAL_CREATE_LIST_STEP_H_

#include <cstddef>
#include <cstdint>
#include <utility>

#include "absl/container/flat_hash_set.h"

namespace google::api::expr::runtime {

class ExecutionFrame;
class ExpressionStep;

// Small list step info is used for lists with 58 or fewer elements with 58 bits
// used as a bitset to indicate which elements are optional.
// List literals are generally small so this covers most cases.
struct SmallListStepInfo {
  uint64_t optional_bit_set : 58;
  size_t list_size : 6;
};

class ListStepInfo {
 public:
  explicit ListStepInfo(size_t num_elements) : num_elements_(num_elements) {}
  ListStepInfo(size_t num_elements, absl::flat_hash_set<size_t> opt_fields)
      : num_elements_(num_elements), opt_fields_(std::move(opt_fields)) {}

  bool is_optional(size_t index) const { return opt_fields_.contains(index); }

  size_t num_elements() const { return num_elements_; }

 private:
  size_t num_elements_;
  absl::flat_hash_set<size_t> opt_fields_;
};

void EvaluateListStep(const ListStepInfo& step, ExecutionFrame& frame);
void EvaluateSmallListStep(const SmallListStepInfo& step,
                           ExecutionFrame& frame);

// Factory method for CreateList which constructs an immutable list.
// The optional indices are assumed to be valid and in range [0, size).
ExpressionStep CreateCreateListStep(
    size_t size, absl::flat_hash_set<size_t> optional_indices = {},
    int64_t expr_id = -1);

}  // namespace google::api::expr::runtime

#endif  // THIRD_PARTY_CEL_CPP_EVAL_EVAL_CREATE_LIST_STEP_H_
