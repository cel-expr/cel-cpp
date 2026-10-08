#ifndef THIRD_PARTY_CEL_CPP_EVAL_EVAL_IDENT_STEP_H_
#define THIRD_PARTY_CEL_CPP_EVAL_EVAL_IDENT_STEP_H_

#include "absl/strings/string_view.h"

namespace google::api::expr::runtime {

class ExecutionFrame;

void EvaluateIdentifierStep(absl::string_view identifier,
                            ExecutionFrame& frame);

}  // namespace google::api::expr::runtime

#endif  // THIRD_PARTY_CEL_CPP_EVAL_EVAL_IDENT_STEP_H_
