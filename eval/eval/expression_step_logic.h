// Copyright 2026 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef THIRD_PARTY_CEL_CPP_EVAL_EVAL_EXPRESSION_STEP_LOGIC_H_
#define THIRD_PARTY_CEL_CPP_EVAL_EVAL_EXPRESSION_STEP_LOGIC_H_

#include "common/native_type.h"

namespace google::api::expr::runtime {

class ExecutionFrame;

// ExpressionStepLogic is the base class for generic expression steps that are
// not implemented directly in the evaluator core.
class ExpressionStepLogic {
 public:
  virtual ~ExpressionStepLogic() = default;

  // Performs actual evaluation.
  // Values are passed between Expression objects via EvaluatorStack, which is
  // supplied with context.
  // Also, Expression gets values supplied by caller though Activation
  // interface.
  // ExpressionStep instances can in specific cases
  // modify execution order(perform jumps).
  //
  // Unrecoverable errors are reported via ExecutionFrame::Abort.
  virtual void Evaluate(ExecutionFrame* frame) const = 0;

  // Return the type of the underlying expression step for special handling in
  // the planning phase. This should only be overridden by special cases, and
  // callers must not make any assumptions about the default case.
  virtual cel::NativeTypeId GetNativeTypeId() const {
    return cel::NativeTypeId();
  }
};

}  // namespace google::api::expr::runtime

#endif  // THIRD_PARTY_CEL_CPP_EVAL_EVAL_EXPRESSION_STEP_LOGIC_H_
