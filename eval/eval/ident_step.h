#ifndef THIRD_PARTY_CEL_CPP_EVAL_EVAL_IDENT_STEP_H_
#define THIRD_PARTY_CEL_CPP_EVAL_EVAL_IDENT_STEP_H_

#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace google::api::expr::runtime {

class ExecutionFrame;

struct Identifier {
  static constexpr size_t kNoSlot = std::numeric_limits<size_t>::max();

  std::string name;
  size_t slot_index = kNoSlot;
};

using IdentifierTable = std::vector<Identifier>;

void EvaluateIdentifierStep(size_t ident_index, ExecutionFrame& frame);

}  // namespace google::api::expr::runtime

#endif  // THIRD_PARTY_CEL_CPP_EVAL_EVAL_IDENT_STEP_H_
