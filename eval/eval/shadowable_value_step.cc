#include "eval/eval/shadowable_value_step.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "common/value.h"
#include "eval/eval/evaluator_core.h"
#include "eval/eval/expression_step_base.h"
#include "eval/eval/expression_step_logic.h"

namespace google::api::expr::runtime {

namespace {

using ::cel::Value;

class ShadowableValueStep : public ExpressionStepBase {
 public:
  ShadowableValueStep(std::string identifier, cel::Value value)
      : ExpressionStepBase(),
        identifier_(std::move(identifier)),
        value_(std::move(value)) {}

  void Evaluate(ExecutionFrame* frame) const override;

 private:
  std::string identifier_;
  Value value_;
};

void ShadowableValueStep::Evaluate(ExecutionFrame* frame) const {
  cel::Value result;
  absl::StatusOr<bool> found = frame->modern_activation().FindVariable(
      identifier_, frame->descriptor_pool(), frame->message_factory(),
      frame->arena(), &result);
  if (!found.ok()) {
    frame->Abort(std::move(found).status());
    return;
  }
  if (*found) {
    frame->value_stack().Push(std::move(result));
  } else {
    frame->value_stack().Push(value_);
  }
}

}  // namespace

absl::StatusOr<std::unique_ptr<ExpressionStepLogic>> CreateShadowableValueStep(
    absl::string_view name, cel::Value value) {
  return std::make_unique<ShadowableValueStep>(std::string(name),
                                               std::move(value));
}

}  // namespace google::api::expr::runtime
