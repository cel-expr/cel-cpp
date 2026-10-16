#ifndef THIRD_PARTY_CEL_CPP_EVAL_EVAL_IDENT_STEP_H_
#define THIRD_PARTY_CEL_CPP_EVAL_EVAL_IDENT_STEP_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace google::api::expr::runtime {

class ExecutionFrame;

struct Identifier {
  std::string name;
  size_t slot_index = 0;
};

using IdentifierTable = std::vector<Identifier>;

struct IdentStepInfo {
  uint32_t ident_index;
  uint32_t slot_index;
};

void EvaluateIdentifierStep(const IdentStepInfo& step, ExecutionFrame& frame);

}  // namespace google::api::expr::runtime

#endif  // THIRD_PARTY_CEL_CPP_EVAL_EVAL_IDENT_STEP_H_
