// Copyright 2023 Google LLC
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
//
// Program steps for lazily initialized aliases (e.g. cel.bind).
//
// When used, any reference to variable should be replaced with a conditional
// step that either runs the initialization routine or pushes the already
// initialized variable to the stack.
//
// All references to the variable should be replaced with:
//
// +-----------------+-------------------+--------------------+
// |    stack        |       pc          |    step            |
// +-----------------+-------------------+--------------------+
// |    {}           |       0           | check init slot(i) |
// +-----------------+-------------------+--------------------+
// |    {value}      |       1           | assign slot(i)     |
// +-----------------+-------------------+--------------------+
// |    {value}      |       2           | <expr using value> |
// +-----------------+-------------------+--------------------+
// |                  ....                                    |
// +-----------------+-------------------+--------------------+
// |    {...}        | n (end of scope)  | clear slot(i)      |
// +-----------------+-------------------+--------------------+

#ifndef THIRD_PARTY_CEL_CPP_EVAL_EVAL_LAZY_INIT_STEP_H_
#define THIRD_PARTY_CEL_CPP_EVAL_EVAL_LAZY_INIT_STEP_H_

#include <cstddef>

namespace google::api::expr::runtime {

class ExecutionFrame;

struct LazyInitStepInfo {
  size_t slot_index : 32;
  size_t subexpression_index : 32;
};

void EvaluateLazyInitStep(const LazyInitStepInfo& step, ExecutionFrame& frame);

void EvaluateAssignSlotAndPop(size_t slot_index, ExecutionFrame& frame);

struct ClearSlotStepInfo {
  size_t slot_index : 32;
  size_t slot_count : 32 = 1;
};

void EvaluateClearSlotStep(const ClearSlotStepInfo& step,
                           ExecutionFrame& frame);

}  // namespace google::api::expr::runtime

#endif  // THIRD_PARTY_CEL_CPP_EVAL_EVAL_LAZY_INIT_STEP_H_
