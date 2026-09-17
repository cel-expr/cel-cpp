#include "eval/eval/create_list_step.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/expr.h"
#include "common/internal/attribute_trail.h"
#include "common/value.h"
#include "common/values/list_value_builder.h"
#include "eval/eval/evaluator_core.h"
#include "eval/eval/expression_step_base.h"
#include "eval/eval/expression_step_logic.h"
#include "internal/status_macros.h"

namespace google::api::expr::runtime {

namespace {

using ::cel::ErrorValue;
using ::cel::ListValueBuilderPtr;
using ::cel::Value;
using ::cel::common_internal::NewListValueBuilder;

class CreateListStep : public ExpressionStepBase {
 public:
  CreateListStep(int list_size, absl::flat_hash_set<int> optional_indices)
      : list_size_(list_size), optional_indices_(std::move(optional_indices)) {}

  void Evaluate(ExecutionFrame* frame) const override;

 private:
  absl::Status DoEvaluate(ExecutionFrame* frame, Value* result) const;

  int list_size_;
  absl::flat_hash_set<int32_t> optional_indices_;
};

void CreateListStep::Evaluate(ExecutionFrame* frame) const {
  if (list_size_ < 0) {
    frame->Abort(absl::InternalError("CreateListStep: list size is <0"));
    return;
  }

  if (!frame->value_stack().HasEnough(list_size_)) {
    frame->Abort(absl::InternalError("CreateListStep: stack underflow"));
    return;
  }

  Value result;
  if (absl::Status status = DoEvaluate(frame, &result); !status.ok()) {
    frame->Abort(std::move(status));
    return;
  }

  frame->value_stack().PopAndPush(list_size_, std::move(result));
}

absl::Status CreateListStep::DoEvaluate(ExecutionFrame* frame,
                                        Value* result) const {
  auto args = frame->value_stack().GetSpan(list_size_);

  for (const auto& arg : args) {
    if (arg.IsError()) {
      *result = arg;
      return absl::OkStatus();
    }
  }

  if (frame->enable_unknowns()) {
    if (auto unknown = cel::common_internal::PartiallyIdentityAndMergeUnknowns(
            args, frame->value_stack().GetAttributeSpan(list_size_),
            frame->unknown_tree());
        unknown.has_value()) {
      *result = *unknown;
      return absl::OkStatus();
    }
  }

  ListValueBuilderPtr builder = NewListValueBuilder(frame->arena());
  builder->Reserve(args.size());

  for (size_t i = 0; i < args.size(); ++i) {
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
          *result = std::move(optional_arg_value);
          return absl::OkStatus();
        }
        CEL_RETURN_IF_ERROR(builder->Add(std::move(optional_arg_value)));
      } else {
        *result = cel::TypeConversionError(arg.GetTypeName(), "optional_type",
                                           frame->arena());
        return absl::OkStatus();
      }
    } else {
      CEL_RETURN_IF_ERROR(builder->Add(arg));
    }
  }

  *result = std::move(*builder).Build();
  return absl::OkStatus();
}

absl::flat_hash_set<int32_t> MakeOptionalIndicesSet(
    const cel::ListExpr& create_list_expr) {
  absl::flat_hash_set<int32_t> optional_indices;
  for (size_t i = 0; i < create_list_expr.elements().size(); ++i) {
    if (create_list_expr.elements()[i].optional()) {
      optional_indices.insert(static_cast<int32_t>(i));
    }
  }
  return optional_indices;
}

}  // namespace

absl::StatusOr<std::unique_ptr<ExpressionStepLogic>> CreateCreateListStep(
    const cel::ListExpr& create_list_expr) {
  return std::make_unique<CreateListStep>(
      create_list_expr.elements().size(),
      MakeOptionalIndicesSet(create_list_expr));
}

}  // namespace google::api::expr::runtime
