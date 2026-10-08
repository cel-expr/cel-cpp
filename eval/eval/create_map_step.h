// Copyright 2024 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef THIRD_PARTY_CEL_CPP_EVAL_EVAL_CREATE_MAP_STEP_H_
#define THIRD_PARTY_CEL_CPP_EVAL_EVAL_CREATE_MAP_STEP_H_

#include <cstddef>
#include <cstdint>
#include <memory>

#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "eval/eval/expression_step_logic.h"

namespace google::api::expr::runtime {

// Creates an `ExpressionStep` which performs `CreateStruct` for a map.
absl::StatusOr<std::unique_ptr<ExpressionStepLogic>>
CreateCreateStructStepForMap(size_t entry_count,
                             absl::flat_hash_set<int32_t> optional_indices);

// Factory method for CreateMap which constructs a mutable map.
//
// This is intended for the map construction step is generated for a
// map-building comprehension (rather than a user authored expression).
std::unique_ptr<ExpressionStepLogic> CreateMutableMapStep();

}  // namespace google::api::expr::runtime

#endif  // THIRD_PARTY_CEL_CPP_EVAL_EVAL_CREATE_MAP_STEP_H_
