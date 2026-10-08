#ifndef THIRD_PARTY_CEL_CPP_EVAL_EVAL_LOGIC_STEP_H_
#define THIRD_PARTY_CEL_CPP_EVAL_EVAL_LOGIC_STEP_H_

#include <cstddef>

namespace google::api::expr::runtime {

class ExecutionFrame;

void EvaluateNotStep(ExecutionFrame& frame);

void EvaluateNotStrictlyFalseStep(ExecutionFrame& frame);

enum class BoolLogicKind {
  kAnd,
  kOr,
};

void EvaluateBoolLogicStep(BoolLogicKind kind, size_t num_args,
                           ExecutionFrame& frame);

}  // namespace google::api::expr::runtime

#endif  // THIRD_PARTY_CEL_CPP_EVAL_EVAL_LOGIC_STEP_H_
