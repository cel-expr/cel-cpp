#include "eval/eval/logic_step.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/string_view.h"
#include "base/attribute.h"
#include "base/attribute_set.h"
#include "base/type_provider.h"
#include "common/expr.h"
#include "eval/eval/cel_expression_flat_impl.h"
#include "eval/eval/evaluator_core.h"
#include "eval/public/activation.h"
#include "eval/public/cel_attribute.h"
#include "eval/public/cel_value.h"
#include "eval/public/unknown_attribute_set.h"
#include "eval/public/unknown_set.h"
#include "internal/status_macros.h"
#include "internal/testing.h"
#include "runtime/internal/runtime_env.h"
#include "runtime/internal/runtime_env_testing.h"
#include "runtime/runtime_options.h"
#include "google/protobuf/arena.h"

namespace google::api::expr::runtime {

namespace {

using ::absl_testing::IsOk;
using ::cel::Attribute;
using ::cel::AttributeSet;
using ::cel::Expr;
using ::cel::TypeProvider;
using ::cel::runtime_internal::NewTestingRuntimeEnv;
using ::cel::runtime_internal::RuntimeEnv;
using ::google::protobuf::Arena;
using ::testing::Eq;

class LogicStepTest : public testing::TestWithParam<bool> {
 public:
  LogicStepTest() : env_(NewTestingRuntimeEnv()) {}

  absl::Status EvaluateLogic(CelValue arg0, CelValue arg1, bool is_or,
                             CelValue* result, bool enable_unknown) {
    ExecutionPath path;
    path.push_back(ExpressionStep::MakeIdentifierStep("name0"));
    path.push_back(ExpressionStep::MakeIdentifierStep("name1"));
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
        env_,
        FlatExpression(std::move(path), /*comprehension_slot_count=*/0,
                       env_->type_registry.GetComposedTypeProvider(), options));

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

TEST_F(LogicStepTest, TestAndLogicUnknownHandling) {
  CelValue result;
  UnknownSet unknown_set;
  CelError cel_error = absl::CancelledError();
  CelValue unknown_value = CelValue::CreateUnknownSet(&unknown_set);
  CelValue error_value = CelValue::CreateError(&cel_error);
  absl::Status status = EvaluateLogic(unknown_value, CelValue::CreateBool(true),
                                      false, &result, true);
  ASSERT_THAT(status, IsOk());
  ASSERT_TRUE(result.IsUnknownSet());

  status = EvaluateLogic(CelValue::CreateBool(true), unknown_value, false,
                         &result, true);
  ASSERT_THAT(status, IsOk());
  ASSERT_TRUE(result.IsUnknownSet());

  status = EvaluateLogic(CelValue::CreateBool(false), unknown_value, false,
                         &result, true);
  ASSERT_THAT(status, IsOk());
  ASSERT_TRUE(result.IsBool());
  ASSERT_FALSE(result.BoolOrDie());

  status = EvaluateLogic(unknown_value, CelValue::CreateBool(false), false,
                         &result, true);
  ASSERT_THAT(status, IsOk());
  ASSERT_TRUE(result.IsBool());
  ASSERT_FALSE(result.BoolOrDie());

  status = EvaluateLogic(error_value, unknown_value, false, &result, true);
  ASSERT_THAT(status, IsOk());
  ASSERT_TRUE(result.IsUnknownSet());

  status = EvaluateLogic(unknown_value, error_value, false, &result, true);
  ASSERT_THAT(status, IsOk());
  ASSERT_TRUE(result.IsUnknownSet());

  CelAttribute attr0("name0", {}), attr1("name1", {});
  UnknownAttributeSet unknown_attr_set0({attr0});
  UnknownAttributeSet unknown_attr_set1({attr1});
  UnknownSet unknown_set0(unknown_attr_set0);
  UnknownSet unknown_set1(unknown_attr_set1);

  EXPECT_THAT(unknown_attr_set0.size(), Eq(1));
  EXPECT_THAT(unknown_attr_set1.size(), Eq(1));

  status = EvaluateLogic(CelValue::CreateUnknownSet(&unknown_set0),
                         CelValue::CreateUnknownSet(&unknown_set1), false,
                         &result, true);
  ASSERT_THAT(status, IsOk());
  ASSERT_TRUE(result.IsUnknownSet());
  ASSERT_THAT(result.UnknownSetOrDie()->unknown_attributes().size(), Eq(2));
}

TEST_F(LogicStepTest, TestOrLogicUnknownHandling) {
  CelValue result;
  UnknownSet unknown_set;
  CelError cel_error = absl::CancelledError();
  CelValue unknown_value = CelValue::CreateUnknownSet(&unknown_set);
  CelValue error_value = CelValue::CreateError(&cel_error);
  absl::Status status = EvaluateLogic(
      unknown_value, CelValue::CreateBool(false), true, &result, true);
  ASSERT_THAT(status, IsOk());
  ASSERT_TRUE(result.IsUnknownSet());

  status = EvaluateLogic(CelValue::CreateBool(false), unknown_value, true,
                         &result, true);
  ASSERT_THAT(status, IsOk());
  ASSERT_TRUE(result.IsUnknownSet());

  status = EvaluateLogic(CelValue::CreateBool(true), unknown_value, true,
                         &result, true);
  ASSERT_THAT(status, IsOk());
  ASSERT_TRUE(result.IsBool());
  ASSERT_TRUE(result.BoolOrDie());

  status = EvaluateLogic(unknown_value, CelValue::CreateBool(true), true,
                         &result, true);
  ASSERT_THAT(status, IsOk());
  ASSERT_TRUE(result.IsBool());
  ASSERT_TRUE(result.BoolOrDie());

  status = EvaluateLogic(unknown_value, error_value, true, &result, true);
  ASSERT_THAT(status, IsOk());
  ASSERT_TRUE(result.IsUnknownSet());

  status = EvaluateLogic(error_value, unknown_value, true, &result, true);
  ASSERT_THAT(status, IsOk());
  ASSERT_TRUE(result.IsUnknownSet());

  CelAttribute attr0("name0", {}), attr1("name1", {});
  UnknownAttributeSet unknown_attr_set0({attr0});
  UnknownAttributeSet unknown_attr_set1({attr1});

  UnknownSet unknown_set0(unknown_attr_set0);
  UnknownSet unknown_set1(unknown_attr_set1);

  EXPECT_THAT(unknown_attr_set0.size(), Eq(1));
  EXPECT_THAT(unknown_attr_set1.size(), Eq(1));

  status = EvaluateLogic(CelValue::CreateUnknownSet(&unknown_set0),
                         CelValue::CreateUnknownSet(&unknown_set1), true,
                         &result, true);
  ASSERT_THAT(status, IsOk());
  ASSERT_TRUE(result.IsUnknownSet());
  ASSERT_THAT(result.UnknownSetOrDie()->unknown_attributes().size(), Eq(2));
}

INSTANTIATE_TEST_SUITE_P(LogicStepTest, LogicStepTest, testing::Bool());

}  // namespace

}  // namespace google::api::expr::runtime
