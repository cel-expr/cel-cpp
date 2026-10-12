#include "eval/eval/ternary_step.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "base/type_provider.h"
#include "eval/eval/cel_expression_flat_impl.h"
#include "eval/eval/evaluator_core.h"
#include "eval/eval/ident_step.h"
#include "eval/public/activation.h"
#include "eval/public/cel_value.h"
#include "internal/testing.h"
#include "runtime/internal/runtime_env.h"
#include "runtime/internal/runtime_env_testing.h"
#include "runtime/runtime_options.h"
#include "google/protobuf/arena.h"

namespace google::api::expr::runtime {

namespace {

using ::absl_testing::IsOk;
using ::cel::RuntimeOptions;
using ::cel::TypeProvider;
using ::cel::runtime_internal::NewTestingRuntimeEnv;
using ::cel::runtime_internal::RuntimeEnv;
using ::google::protobuf::Arena;

class LogicStepTest : public testing::TestWithParam<bool> {
 public:
  LogicStepTest() : env_(NewTestingRuntimeEnv()) {}

  absl::Status EvaluateLogic(CelValue arg0, CelValue arg1, CelValue arg2,
                             CelValue* result, bool enable_unknown) {
    ExecutionPath path;

    path.push_back(ExpressionStep::MakeIdentifierStep(0));
    path.push_back(ExpressionStep::MakeIdentifierStep(1));
    path.push_back(ExpressionStep::MakeIdentifierStep(2));
    path.push_back(ExpressionStep::MakeGenericStep(CreateTernaryStep(), 4));

    cel::RuntimeOptions options;
    if (enable_unknown) {
      options.unknown_processing =
          cel::UnknownProcessingOptions::kAttributeOnly;
    }
    CelExpressionFlatImpl impl(
        env_,
        FlatExpression(std::move(path), /*comprehension_slot_count=*/0,
                       env_->type_registry.GetComposedTypeProvider(), options,
                       /*arena=*/nullptr,
                       IdentifierTable{{"name0"}, {"name1"}, {"name2"}}));

    Activation activation;
    std::string value("test");

    activation.InsertValue("name0", arg0);
    activation.InsertValue("name1", arg1);
    activation.InsertValue("name2", arg2);
    auto status0 = impl.Evaluate(activation, &arena_);
    if (!status0.ok()) return status0.status();

    *result = status0.value();
    return absl::OkStatus();
  }

 private:
  absl_nonnull std::shared_ptr<const RuntimeEnv> env_;
  Arena arena_;
};

TEST_P(LogicStepTest, TestBoolCond) {
  CelValue result;
  absl::Status status =
      EvaluateLogic(CelValue::CreateBool(true), CelValue::CreateBool(true),
                    CelValue::CreateBool(false), &result, GetParam());
  ASSERT_THAT(status, IsOk());
  ASSERT_TRUE(result.IsBool());
  ASSERT_TRUE(result.BoolOrDie());

  status =
      EvaluateLogic(CelValue::CreateBool(false), CelValue::CreateBool(true),
                    CelValue::CreateBool(false), &result, GetParam());
  ASSERT_THAT(status, IsOk());
  ASSERT_TRUE(result.IsBool());
  ASSERT_FALSE(result.BoolOrDie());
}

TEST_P(LogicStepTest, TestErrorHandling) {
  CelValue result;
  CelError error = absl::CancelledError();
  CelValue error_value = CelValue::CreateError(&error);
  ASSERT_THAT(EvaluateLogic(error_value, CelValue::CreateBool(true),
                            CelValue::CreateBool(false), &result, GetParam()),
              IsOk());
  ASSERT_TRUE(result.IsError());

  ASSERT_THAT(EvaluateLogic(CelValue::CreateBool(true), error_value,
                            CelValue::CreateBool(false), &result, GetParam()),
              IsOk());
  ASSERT_TRUE(result.IsError());

  ASSERT_THAT(EvaluateLogic(CelValue::CreateBool(false), error_value,
                            CelValue::CreateBool(false), &result, GetParam()),
              IsOk());
  ASSERT_TRUE(result.IsBool());
  ASSERT_FALSE(result.BoolOrDie());
}

INSTANTIATE_TEST_SUITE_P(LogicStepTest, LogicStepTest, testing::Bool());

}  // namespace

}  // namespace google::api::expr::runtime
