#include "eval/eval/create_list_step.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include "absl/container/flat_hash_set.h"
#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "common/value.h"
#include "common/values/list_value_builder.h"
#include "eval/eval/attribute_utility.h"
#include "eval/eval/evaluator_core.h"

namespace google::api::expr::runtime {

namespace {

using ::cel::ErrorValue;
using ::cel::ListValueBuilderPtr;
using ::cel::UnknownValue;
using ::cel::Value;
using ::cel::common_internal::NewListValueBuilder;

constexpr size_t kMaxSmallListSize = 58;

template <typename T>
struct StepTraits;

template <>
struct StepTraits<SmallListStepInfo> {
  static bool is_optional(const SmallListStepInfo& step, size_t index) {
    ABSL_DCHECK_LT(index, kMaxSmallListSize);
    return ((step.optional_bit_set >> index) & 1) == 1;
  }

  static size_t num_elements(const SmallListStepInfo& step) {
    return step.list_size;
  }
};

template <>
struct StepTraits<ListStepInfo> {
  static bool is_optional(const ListStepInfo& step, size_t index) {
    return step.is_optional(index);
  }

  static size_t num_elements(const ListStepInfo& step) {
    return step.num_elements();
  }
};

template <typename StepInfo>
void EvaluateListStepImpl(const StepInfo& step, ExecutionFrame& frame) {
  const size_t num_elements = StepTraits<StepInfo>::num_elements(step);

  if (!frame.value_stack().HasEnough(num_elements)) {
    frame.Abort(absl::InternalError("CreateListStep: stack underflow"));
    return;
  }

  absl::Span<const Value> args = frame.value_stack().GetSpan(num_elements);

  for (size_t i = 0; i < num_elements; ++i) {
    if (args[i].IsError()) {
      frame.value_stack().SwapAndPop(num_elements, i);
      return;
    }
  }

  if (frame.enable_unknowns()) {
    absl::optional<UnknownValue> unknown_set =
        frame.attribute_utility().IdentifyAndMergeUnknowns(
            args, frame.value_stack().GetAttributeSpan(num_elements),
            /*use_partial=*/true);
    if (unknown_set.has_value()) {
      frame.value_stack().PopAndPush(num_elements, std::move(*unknown_set));
      return;
    }
  }

  ListValueBuilderPtr builder = NewListValueBuilder(frame.arena());
  builder->Reserve(num_elements);

  for (size_t i = 0; i < num_elements; ++i) {
    const Value& arg = args[i];
    if (StepTraits<StepInfo>::is_optional(step, i)) {
      if (cel::optional_ref<const cel::OptionalValue> optional_arg =
              arg.AsOptional();
          optional_arg.has_value()) {
        if (!optional_arg->HasValue()) {
          continue;
        }
        Value optional_arg_value;
        optional_arg->Value(&optional_arg_value);
        if (optional_arg_value.IsError()) {
          // Error should never be in optional, but better safe than sorry.
          frame.value_stack().PopAndPush(num_elements,
                                         std::move(optional_arg_value));
          return;
        }
        if (absl::Status status = builder->Add(std::move(optional_arg_value));
            !status.ok()) {
          frame.Abort(std::move(status));
          return;
        }
      } else {
        frame.value_stack().PopAndPush(
            num_elements,
            cel::TypeConversionError(arg.GetTypeName(), "optional_type",
                                     frame.arena()));
        return;
      }
    } else {
      if (absl::Status status = builder->Add(arg); !status.ok()) {
        frame.Abort(std::move(status));
        return;
      }
    }
  }

  frame.value_stack().PopAndPush(num_elements, std::move(*builder).Build());
}

}  // namespace

void EvaluateListStep(const ListStepInfo& step, ExecutionFrame& frame) {
  EvaluateListStepImpl(step, frame);
}

void EvaluateSmallListStep(const SmallListStepInfo& step,
                           ExecutionFrame& frame) {
  EvaluateListStepImpl(step, frame);
}

ExpressionStep CreateCreateListStep(
    size_t size, absl::flat_hash_set<size_t> optional_indices,
    int64_t expr_id) {
  if (size <= kMaxSmallListSize) {
    uint64_t optional_bit_set = 0;
    for (size_t index : optional_indices) {
      ABSL_DCHECK_LT(index, size);
      optional_bit_set |= (uint64_t{1} << index);
    }
    return ExpressionStep::MakeCreateSmallListStep(
        SmallListStepInfo{optional_bit_set, size}, expr_id);
  }
  return ExpressionStep::MakeCreateListStep(
      std::make_unique<ListStepInfo>(size, std::move(optional_indices)),
      expr_id);
}

}  // namespace google::api::expr::runtime
