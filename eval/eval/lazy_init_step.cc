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

#include "eval/eval/lazy_init_step.h"

#include <cstddef>

#include "cel/expr/value.pb.h"
#include "absl/status/status.h"
#include "eval/eval/comprehension_slots.h"
#include "eval/eval/evaluator_core.h"

namespace google::api::expr::runtime {

void EvaluateLazyInitStep(const LazyInitStepInfo& step, ExecutionFrame& frame) {
  ComprehensionSlot* slot = frame.comprehension_slots().Get(step.slot_index);
  if (slot->Has()) {
    frame.value_stack().Push(slot->value(), slot->attribute());
  } else {
    frame.Call(step.slot_index, step.subexpression_index);
  }
}

void EvaluateAssignSlotAndPop(size_t slot_index, ExecutionFrame& frame) {
  if (!frame.value_stack().HasEnough(1)) {
    frame.Abort(absl::InternalError("Stack underflow assigning lazy value"));
    return;
  }
  ComprehensionSlot* slot = frame.comprehension_slots().Get(slot_index);
  slot->Set(frame.value_stack().Peek(), frame.value_stack().PeekAttribute());
  frame.value_stack().Pop(1);
}

void EvaluateClearSlotStep(const ClearSlotStepInfo& step,
                           ExecutionFrame& frame) {
  for (size_t i = 0; i < step.slot_count; ++i) {
    frame.comprehension_slots().ClearSlot(step.slot_index + i);
  }
}

}  // namespace google::api::expr::runtime
