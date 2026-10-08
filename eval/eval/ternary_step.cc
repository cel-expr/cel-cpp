#include "eval/eval/ternary_step.h"

#include <cstddef>
#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "base/builtins.h"
#include "common/value.h"
#include "eval/eval/evaluator_core.h"
#include "eval/eval/expression_step_base.h"
#include "eval/eval/expression_step_logic.h"
#include "eval/internal/errors.h"

namespace google::api::expr::runtime {

namespace {

using ::cel::builtin::kTernary;
using ::cel::runtime_internal::CreateNoMatchingOverloadError;

inline constexpr size_t kTernaryStepCondition = 0;
inline constexpr size_t kTernaryStepTrue = 1;
inline constexpr size_t kTernaryStepFalse = 2;

class TernaryStep : public ExpressionStepBase {
 public:
  // Constructs FunctionStep that uses overloads specified.
  TernaryStep() : ExpressionStepBase() {}

  void Evaluate(ExecutionFrame* frame) const override;
};

void TernaryStep::Evaluate(ExecutionFrame* frame) const {
  // Must have 3 or more values on the stack.
  if (!frame->value_stack().HasEnough(3)) {
    frame->Abort(absl::InternalError("Value stack underflow"));
    return;
  }

  // Create Span object that contains input arguments to the function.
  auto args = frame->value_stack().GetSpan(3);

  const auto& condition = args[kTernaryStepCondition];
  // As opposed to regular functions, ternary treats unknowns or errors on the
  // condition (arg0) as blocking. If we get an error or unknown then we
  // ignore the other arguments and forward the condition as the result.
  if (frame->enable_unknowns()) {
    // Check if unknown?
    if (condition.IsUnknown()) {
      frame->value_stack().Pop(2);
      return;
    }
  }

  if (condition.IsError()) {
    frame->value_stack().Pop(2);
    return;
  }

  cel::Value result;
  if (!condition.IsBool()) {
    result = cel::ErrorValue::From(CreateNoMatchingOverloadError(kTernary),
                                   frame->arena());
  } else if (condition.GetBool().NativeValue()) {
    result = args[kTernaryStepTrue];
  } else {
    result = args[kTernaryStepFalse];
  }

  frame->value_stack().PopAndPush(args.size(), std::move(result));
}

}  // namespace

std::unique_ptr<ExpressionStepLogic> CreateTernaryStep() {
  return std::make_unique<TernaryStep>();
}

}  // namespace google::api::expr::runtime
