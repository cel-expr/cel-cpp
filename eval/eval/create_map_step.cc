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

#include "eval/eval/create_map_step.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "common/value.h"
#include "common/values/map_value_builder.h"
#include "eval/eval/evaluator_core.h"
#include "eval/eval/expression_step_base.h"
#include "eval/eval/expression_step_logic.h"
#include "internal/status_macros.h"

namespace google::api::expr::runtime {

namespace {

using ::cel::ErrorValueReturn;
using ::cel::MapValueBuilderPtr;
using ::cel::UnknownValue;
using ::cel::Value;
using ::cel::common_internal::NewMapValueBuilder;
using ::cel::common_internal::NewMutableMapValue;

// `CreateStruct` implementation for map.
class CreateStructStepForMap final : public ExpressionStepBase {
 public:
  CreateStructStepForMap(size_t entry_count,
                         absl::flat_hash_set<int32_t> optional_indices)
      : entry_count_(entry_count),
        optional_indices_(std::move(optional_indices)) {}

  void Evaluate(ExecutionFrame* frame) const override;

 private:
  absl::StatusOr<Value> DoEvaluate(ExecutionFrame* frame) const;

  size_t entry_count_;
  absl::flat_hash_set<int32_t> optional_indices_;
};

absl::StatusOr<Value> CreateStructStepForMap::DoEvaluate(
    ExecutionFrame* frame) const {
  auto args = frame->value_stack().GetSpan(2 * entry_count_);

  for (const auto& arg : args) {
    if (arg.IsError()) {
      return arg;
    }
  }

  if (frame->enable_unknowns()) {
    absl::optional<UnknownValue> unknown_set =
        frame->attribute_utility().IdentifyAndMergeUnknowns(
            args, frame->value_stack().GetAttributeSpan(args.size()), true);
    if (unknown_set.has_value()) {
      return *unknown_set;
    }
  }

  MapValueBuilderPtr builder = NewMapValueBuilder(frame->arena());
  builder->Reserve(entry_count_);

  for (size_t i = 0; i < entry_count_; i += 1) {
    const auto& map_key = args[2 * i];
    CEL_RETURN_IF_ERROR(cel::CheckMapKey(map_key))
        .With(ErrorValueReturn(frame->arena()));
    const auto& map_value = args[(2 * i) + 1];
    if (optional_indices_.contains(static_cast<int32_t>(i))) {
      if (auto optional_map_value = map_value.AsOptional();
          optional_map_value) {
        if (!optional_map_value->HasValue()) {
          continue;
        }
        Value optional_map_value_value;
        optional_map_value->Value(&optional_map_value_value);
        if (optional_map_value_value.IsError()) {
          // Error should never be in optional, but better safe than sorry.
          return optional_map_value_value;
        }
        CEL_RETURN_IF_ERROR(
            builder->Put(map_key, std::move(optional_map_value_value)));
      } else {
        return cel::TypeConversionError(map_value.DebugString(),
                                        "optional_type", frame->arena());
      }
    } else {
      CEL_RETURN_IF_ERROR(builder->Put(map_key, map_value));
    }
  }

  return std::move(*builder).Build();
}

void CreateStructStepForMap::Evaluate(ExecutionFrame* frame) const {
  if (frame->value_stack().size() < 2 * entry_count_) {
    frame->Abort(
        absl::InternalError("CreateStructStepForMap: stack underflow"));
    return;
  }

  absl::StatusOr<Value> result = DoEvaluate(frame);
  if (!result.ok()) {
    frame->Abort(std::move(result).status());
    return;
  }

  frame->value_stack().PopAndPush(2 * entry_count_, *std::move(result));
}

class MutableMapStep final : public ExpressionStepBase {
 public:
  MutableMapStep() = default;

  void Evaluate(ExecutionFrame* frame) const override {
    frame->value_stack().Push(cel::CustomMapValue(
        NewMutableMapValue(frame->arena()), frame->arena()));
  }
};

}  // namespace

absl::StatusOr<std::unique_ptr<ExpressionStepLogic>>
CreateCreateStructStepForMap(size_t entry_count,
                             absl::flat_hash_set<int32_t> optional_indices) {
  // Make map-creating step.
  return std::make_unique<CreateStructStepForMap>(entry_count,
                                                  std::move(optional_indices));
}

std::unique_ptr<ExpressionStepLogic> CreateMutableMapStep() {
  return std::make_unique<MutableMapStep>();
}

}  // namespace google::api::expr::runtime
