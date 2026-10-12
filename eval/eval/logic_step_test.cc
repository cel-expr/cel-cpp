#include "eval/eval/logic_step.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/string_view.h"
#include "base/type_provider.h"
#include "common/expr.h"
#include "eval/eval/cel_expression_flat_impl.h"
#include "eval/eval/evaluator_core.h"
#include "eval/public/activation.h"
#include "eval/public/cel_value.h"
#include "internal/status_macros.h"
#include "internal/testing.h"
#include "runtime/internal/runtime_env.h"
#include "runtime/internal/runtime_env_testing.h"
#include "runtime/runtime_options.h"
#include "google/protobuf/arena.h"

namespace google::api::expr::runtime {

namespace {

using ::absl_testing::IsOk;
using ::cel::Expr;
using ::cel::TypeProvider;
using ::cel::runtime_internal::NewTestingRuntimeEnv;
using ::cel::runtime_internal::RuntimeEnv;
using ::google::protobuf::Arena;

class LogicStepTest : public testing::TestWithParam<bool> {
 public:
  LogicStepTest() : env_(NewTestingRuntimeEnv()) {}

  absl::Status EvaluateLogic(CelValue arg0, CelValue arg1, bool is_or,
                             CelValue* result, bool enable_unknown) {
    ExecutionPath path;
    path.push_back(ExpressionStep::MakeIdentifierStep(0));
    path.push_back(ExpressionStep::MakeIdentifierStep(1));
    path.push_back(
        (is_or) ? ExpressionStep::MakeBooleanOrStep(/*num_args=*/2, /*id=*/2)
                : ExpressionStep::MakeBooleanAndStep(/*num_args=*/2, /*id=*/2));

    auto dummy_expr = std::make_unique<Expr>();
    cel::RuntimeOptions options;
    if (enable_unknown) {
      options.unknown_processing =
          cel::UnknownProcessingOptions::kAttributeOnly;
    }
    CelExpressionFlatImpl impl(
        env_, FlatExpression(
                  std::move(path), /*comprehension_slot_count=*/0,
                  env_->type_registry.GetComposedTypeProvider(), options,
                  /*arena=*/nullptr, IdentifierTable{{"name0"}, {"name1"}}));

    Activation activation;
    activation.InsertValue("name0", arg0);
    activation.InsertValue("name1", arg1);
    CEL_ASSIGN_OR_RETURN(CelValue value, impl.Evaluate(activation, &arena_));
    *result = value;
    return absl::OkStatus();
  }

 private:
  absl_nonnull std::shared_ptr<const RuntimeEnv> env_;
  Arena arena_;
};

TEST_P(LogicStepTest, TestAndLogic) {
  CelValue result;
  absl::Status status =
      EvaluateLogic(CelValue::CreateBool(true), CelValue::CreateBool(true),
                    false, &result, GetParam());
  ASSERT_THAT(status, IsOk());
  ASSERT_TRUE(result.IsBool());
  ASSERT_TRUE(result.BoolOrDie());

  status =
      EvaluateLogic(CelValue::CreateBool(true), CelValue::CreateBool(false),
                    false, &result, GetParam());
  ASSERT_THAT(status, IsOk());
  ASSERT_TRUE(result.IsBool());
  ASSERT_FALSE(result.BoolOrDie());

  status =
      EvaluateLogic(CelValue::CreateBool(false), CelValue::CreateBool(true),
                    false, &result, GetParam());
  ASSERT_THAT(status, IsOk());
  ASSERT_TRUE(result.IsBool());
  ASSERT_FALSE(result.BoolOrDie());

  status =
      EvaluateLogic(CelValue::CreateBool(false), CelValue::CreateBool(false),
                    false, &result, GetParam());
  ASSERT_THAT(status, IsOk());
  ASSERT_TRUE(result.IsBool());
  ASSERT_FALSE(result.BoolOrDie());
}

TEST_P(LogicStepTest, TestOrLogic) {
  CelValue result;
  absl::Status status =
      EvaluateLogic(CelValue::CreateBool(true), CelValue::CreateBool(true),
                    true, &result, GetParam());
  ASSERT_THAT(status, IsOk());
  ASSERT_TRUE(result.IsBool());
  ASSERT_TRUE(result.BoolOrDie());

  status =
      EvaluateLogic(CelValue::CreateBool(true), CelValue::CreateBool(false),
                    true, &result, GetParam());
  ASSERT_THAT(status, IsOk());
  ASSERT_TRUE(result.IsBool());
  ASSERT_TRUE(result.BoolOrDie());

  status = EvaluateLogic(CelValue::CreateBool(false),
                         CelValue::CreateBool(true), true, &result, GetParam());
  ASSERT_THAT(status, IsOk());
  ASSERT_TRUE(result.IsBool());
  ASSERT_TRUE(result.BoolOrDie());

  status =
      EvaluateLogic(CelValue::CreateBool(false), CelValue::CreateBool(false),
                    true, &result, GetParam());
  ASSERT_THAT(status, IsOk());
  ASSERT_TRUE(result.IsBool());
  ASSERT_FALSE(result.BoolOrDie());
}

TEST_P(LogicStepTest, TestAndLogicErrorHandling) {
  CelValue result;
  CelError error = absl::CancelledError();
  CelValue error_value = CelValue::CreateError(&error);
  absl::Status status = EvaluateLogic(error_value, CelValue::CreateBool(true),
                                      false, &result, GetParam());
  ASSERT_THAT(status, IsOk());
  ASSERT_TRUE(result.IsError());

  status = EvaluateLogic(CelValue::CreateBool(true), error_value, false,
                         &result, GetParam());
  ASSERT_THAT(status, IsOk());
  ASSERT_TRUE(result.IsError());

  status = EvaluateLogic(CelValue::CreateBool(false), error_value, false,
                         &result, GetParam());
  ASSERT_THAT(status, IsOk());
  ASSERT_TRUE(result.IsBool());
  ASSERT_FALSE(result.BoolOrDie());

  status = EvaluateLogic(error_value, CelValue::CreateBool(false), false,
                         &result, GetParam());
  ASSERT_THAT(status, IsOk());
  ASSERT_TRUE(result.IsBool());
  ASSERT_FALSE(result.BoolOrDie());
}

TEST_P(LogicStepTest, TestOrLogicErrorHandling) {
  CelValue result;
  CelError error = absl::CancelledError();
  CelValue error_value = CelValue::CreateError(&error);
  absl::Status status = EvaluateLogic(error_value, CelValue::CreateBool(false),
                                      true, &result, GetParam());
  ASSERT_THAT(status, IsOk());
  ASSERT_TRUE(result.IsError());

  status = EvaluateLogic(CelValue::CreateBool(false), error_value, true,
                         &result, GetParam());
  ASSERT_THAT(status, IsOk());
  ASSERT_TRUE(result.IsError());

  status = EvaluateLogic(CelValue::CreateBool(true), error_value, true, &result,
                         GetParam());
  ASSERT_THAT(status, IsOk());
  ASSERT_TRUE(result.IsBool());
  ASSERT_TRUE(result.BoolOrDie());

  status = EvaluateLogic(error_value, CelValue::CreateBool(true), true, &result,
                         GetParam());
  ASSERT_THAT(status, IsOk());
  ASSERT_TRUE(result.IsBool());
  ASSERT_TRUE(result.BoolOrDie());
}

INSTANTIATE_TEST_SUITE_P(LogicStepTest, LogicStepTest, testing::Bool());

}  // namespace

}  // namespace google::api::expr::runtime
