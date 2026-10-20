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

#ifndef THIRD_PARTY_CEL_CPP_EVAL_EVAL_BITSET_STEPS_H_
#define THIRD_PARTY_CEL_CPP_EVAL_EVAL_BITSET_STEPS_H_

#include <cstdint>

#include "common/value.h"

namespace google::api::expr::runtime {

class ExecutionFrame;

// Extracts a single-bit bitmask (`1ULL << v`) for a `cel::Value` equal to an
// integer `v` in `[0, 63]` under heterogeneous equality.
//
// Return values:
// - `1ULL << v` (non-zero) if `value` is `int`, `uint`, or `double` with an
//   exact integer value `v` in `[0, 63]`.
// - `0` otherwise (if `value` is outside `[0, 63]`, is not an exact integer,
//   or is not a numeric value).
uint64_t ExtractBit(const cel::Value& value);

void EvaluateBitIsSetStep(uint64_t bitmask, ExecutionFrame& frame);
void EvaluateBitAndIsNonZeroStep(uint64_t bitmask, ExecutionFrame& frame);
void EvaluateBitAndEqualStep(uint64_t bitmask, ExecutionFrame& frame);
void EvaluateBitEqualStep(uint64_t bitmask, ExecutionFrame& frame);

}  // namespace google::api::expr::runtime

#endif  // THIRD_PARTY_CEL_CPP_EVAL_EVAL_BITSET_STEPS_H_
