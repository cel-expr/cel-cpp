// Copyright 2017 Google LLC
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

#include "eval/eval/create_struct_step.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "common/value.h"
#include "eval/eval/evaluator_core.h"
#include "eval/eval/expression_step_base.h"
#include "eval/eval/expression_step_logic.h"
#include "internal/status_macros.h"

namespace google::api::expr::runtime {

namespace {

using ::cel::ErrorValue;
using ::cel::StructValueBuilderInterface;
using ::cel::UnknownValue;
using ::cel::Value;

// `CreateStruct` implementation for message/struct.
class CreateStructStepForStruct final : public ExpressionStepBase {
 public:
  CreateStructStepForStruct(std::string name, std::vector<std::string> entries,
                            absl::flat_hash_set<int32_t> optional_indices)
      : ExpressionStepBase(),
        name_(std::move(name)),
        entries_(std::move(entries)),
        optional_indices_(std::move(optional_indices)) {}

  void Evaluate(ExecutionFrame* frame) const override;

 private:
  absl::StatusOr<Value> DoEvaluate(ExecutionFrame* frame) const;

  std::string name_;
  std::vector<std::string> entries_;
  absl::flat_hash_set<int32_t> optional_indices_;
};

absl::StatusOr<Value> CreateStructStepForStruct::DoEvaluate(
    ExecutionFrame* frame) const {
  int entries_size = entries_.size();

  auto args = frame->value_stack().GetSpan(entries_size);

  for (const auto& arg : args) {
    if (arg.IsError()) {
      return arg;
    }
  }

  if (frame->enable_unknowns()) {
    absl::optional<UnknownValue> unknown_set =
        frame->attribute_utility().IdentifyAndMergeUnknowns(
            args, frame->value_stack().GetAttributeSpan(entries_size),
            /*use_partial=*/true);
    if (unknown_set.has_value()) {
      return *unknown_set;
    }
  }

  CEL_ASSIGN_OR_RETURN(auto builder,
                       frame->type_provider().NewValueBuilder(
                           name_, frame->message_factory(), frame->arena()));
  if (builder == nullptr) {
    return ErrorValue::From(
        absl::NotFoundError(absl::StrCat("Unable to find builder: ", name_)),
        frame->arena());
  }

  for (int i = 0; i < entries_size; ++i) {
    const auto& entry = entries_[i];
    const auto& arg = args[i];
    if (optional_indices_.contains(static_cast<int32_t>(i))) {
      if (auto optional_arg = arg.AsOptional(); optional_arg) {
        if (!optional_arg->HasValue()) {
          continue;
        }
        Value optional_arg_value;
        optional_arg->Value(&optional_arg_value);
        if (optional_arg_value.IsError()) {
          // Error should never be in optional, but better safe than sorry.
          return optional_arg_value;
        }
        CEL_ASSIGN_OR_RETURN(
            absl::optional<ErrorValue> error_value,
            builder->SetFieldByName(entry, std::move(optional_arg_value)));
        if (error_value) {
          return std::move(*error_value);
        }
      } else {
        return cel::TypeConversionError(arg.DebugString(), "optional_type",
                                        frame->arena());
      }
    } else {
      CEL_ASSIGN_OR_RETURN(absl::optional<ErrorValue> error_value,
                           builder->SetFieldByName(entry, arg));
      if (error_value) {
        return std::move(*error_value);
      }
    }
  }

  return std::move(*builder).Build();
}

void CreateStructStepForStruct::Evaluate(ExecutionFrame* frame) const {
  if (frame->value_stack().size() < entries_.size()) {
    frame->Abort(
        absl::InternalError("CreateStructStepForStruct: stack underflow"));
    return;
  }
  absl::StatusOr<Value> result = DoEvaluate(frame);
  if (!result.ok()) {
    frame->Abort(std::move(result).status());
    return;
  }
  frame->value_stack().PopAndPush(entries_.size(), *std::move(result));
}

}  // namespace

std::unique_ptr<ExpressionStepLogic> CreateCreateStructStep(
    std::string name, std::vector<std::string> field_keys,
    absl::flat_hash_set<int32_t> optional_indices) {
  // MakeOptionalIndicesSet(create_struct_expr)
  return std::make_unique<CreateStructStepForStruct>(
      std::move(name), std::move(field_keys), std::move(optional_indices));
}
}  // namespace google::api::expr::runtime
