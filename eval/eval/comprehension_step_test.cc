#include "eval/eval/comprehension_step.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cel/expr/syntax.pb.h"
#include "google/protobuf/struct.pb.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "base/type_provider.h"
#include "common/expr.h"
#include "eval/eval/cel_expression_flat_impl.h"
#include "eval/eval/evaluator_core.h"
#include "eval/eval/expression_step_base.h"
#include "eval/eval/ident_step.h"
#include "eval/public/activation.h"
#include "eval/public/cel_attribute.h"
#include "eval/public/cel_value.h"
#include "eval/public/structs/cel_proto_wrapper.h"
#include "internal/testing.h"
#include "runtime/internal/runtime_env_testing.h"
#include "runtime/runtime_options.h"
#include "google/protobuf/arena.h"

namespace google::api::expr::runtime {
namespace {

using ::absl_testing::IsOk;
using ::cel::Expr;
using ::cel::TypeProvider;
using ::cel::runtime_internal::NewTestingRuntimeEnv;
using ::google::protobuf::Struct;
using ::google::protobuf::Arena;
using ::testing::Eq;
using ::testing::SizeIs;

class ListKeysStepTest : public testing::Test {
 public:
  ListKeysStepTest() = default;

  std::unique_ptr<CelExpressionFlatImpl> MakeExpression(
      ExecutionPath&& path, bool unknown_attributes = false) {
    cel::RuntimeOptions options;
    if (unknown_attributes) {
      options.unknown_processing =
          cel::UnknownProcessingOptions::kAttributeAndFunction;
    }
    auto env = NewTestingRuntimeEnv();
    return std::make_unique<CelExpressionFlatImpl>(
        env,
        FlatExpression(std::move(path), /*comprehension_slot_count=*/0,
                       env->type_registry.GetComposedTypeProvider(), options));
  }

 private:
  Expr dummy_expr_;
};

class GetListKeysResultStep : public ExpressionStepBase {
 public:
  GetListKeysResultStep() : ExpressionStepBase() {}

  void Evaluate(ExecutionFrame* frame) const override {
    frame->value_stack().Pop(1);
  }
};

MATCHER_P(CelStringValue, val, "") {
  const CelValue& to_match = arg;
  absl::string_view value = val;
  return to_match.IsString() && to_match.StringOrDie().value() == value;
}

TEST_F(ListKeysStepTest, MapPartiallyUnknown) {
  ExecutionPath path;
  path.push_back(ExpressionStep::MakeIdentifierStep("var"));
  auto init_step =
      std::make_unique<ComprehensionInitStep>(/*iter_slot=*/0, /*accu_slot=*/0);
  init_step->set_error_jump_offset(1);
  path.push_back(ExpressionStep::MakeGenericStep(std::move(init_step)));
  path.push_back(ExpressionStep::MakeGenericStep(
      std::make_unique<GetListKeysResultStep>()));

  auto expression =
      MakeExpression(std::move(path), /*unknown_attributes=*/true);

  Activation activation;
  Arena arena;
  Struct value;
  (*value.mutable_fields())["key1"].set_number_value(1.0);
  (*value.mutable_fields())["key2"].set_number_value(2.0);
  (*value.mutable_fields())["key3"].set_number_value(3.0);

  activation.InsertValue("var", CelProtoWrapper::CreateMessage(&value, &arena));
  ASSERT_THAT(activation.SetUnknownAttributePatterns({CelAttributePattern(
                  "var", {CreateCelAttributeQualifierPattern(
                              CelValue::CreateStringView("key2")),
                          CreateCelAttributeQualifierPattern(
                              CelValue::CreateStringView("foo")),
                          CelAttributeQualifierPattern::CreateWildcard()})}),
              IsOk());

  auto eval_result = expression->Evaluate(activation, &arena);

  ASSERT_THAT(eval_result, IsOk());
  ASSERT_TRUE(eval_result->IsUnknownSet());
  const auto& attrs = eval_result->UnknownSetOrDie()->unknown_attributes();

  EXPECT_THAT(attrs, SizeIs(1));
  EXPECT_THAT(attrs.begin()->variable_name(), Eq("var"));
  EXPECT_THAT(attrs.begin()->qualifier_path(), SizeIs(0));
}

TEST_F(ListKeysStepTest, ErrorPassedThrough) {
  ExecutionPath path;
  path.push_back(ExpressionStep::MakeIdentifierStep("var"));
  auto init_step =
      std::make_unique<ComprehensionInitStep>(/*iter_slot=*/0, /*accu_slot=*/0);
  init_step->set_error_jump_offset(1);
  path.push_back(ExpressionStep::MakeGenericStep(std::move(init_step)));
  path.push_back(ExpressionStep::MakeGenericStep(
      std::make_unique<GetListKeysResultStep>()));

  auto expression = MakeExpression(std::move(path));

  Activation activation;
  Arena arena;

  // Var not in activation, turns into cel error at eval time.
  auto eval_result = expression->Evaluate(activation, &arena);

  ASSERT_THAT(eval_result, IsOk());
  ASSERT_TRUE(eval_result->IsError());
  EXPECT_THAT(eval_result->ErrorOrDie()->message(),
              testing::HasSubstr("\"var\""));
  EXPECT_EQ(eval_result->ErrorOrDie()->code(), absl::StatusCode::kUnknown);
}

TEST_F(ListKeysStepTest, UnknownSetPassedThrough) {
  ExecutionPath path;
  path.push_back(ExpressionStep::MakeIdentifierStep("var"));
  auto init_step =
      std::make_unique<ComprehensionInitStep>(/*iter_slot=*/0, /*accu_slot=*/0);
  init_step->set_error_jump_offset(1);
  path.push_back(ExpressionStep::MakeGenericStep(std::move(init_step)));
  path.push_back(ExpressionStep::MakeGenericStep(
      std::make_unique<GetListKeysResultStep>()));

  auto expression =
      MakeExpression(std::move(path), /*unknown_attributes=*/true);

  Activation activation;
  Arena arena;

  ASSERT_THAT(
      activation.SetUnknownAttributePatterns({CelAttributePattern("var", {})}),
      IsOk());

  auto eval_result = expression->Evaluate(activation, &arena);

  ASSERT_THAT(eval_result, IsOk());
  ASSERT_TRUE(eval_result->IsUnknownSet());
  EXPECT_THAT(eval_result->UnknownSetOrDie()->unknown_attributes(), SizeIs(1));
}

}  // namespace
}  // namespace google::api::expr::runtime
