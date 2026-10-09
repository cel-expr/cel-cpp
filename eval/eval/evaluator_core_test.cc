#include "eval/eval/evaluator_core.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include "cel/expr/syntax.pb.h"
#include "absl/status/status.h"
#include "base/type_provider.h"
#include "common/value.h"
#include "eval/compiler/cel_expression_builder_flat_impl.h"
#include "eval/eval/cel_expression_flat_impl.h"
#include "eval/internal/interop.h"
#include "eval/public/activation.h"
#include "eval/public/builtin_func_registrar.h"
#include "eval/public/cel_value.h"
#include "internal/testing.h"
#include "internal/testing_descriptor_pool.h"
#include "internal/testing_message_factory.h"
#include "runtime/activation.h"
#include "runtime/internal/runtime_env_testing.h"
#include "runtime/internal/runtime_type_provider.h"
#include "runtime/runtime_options.h"
#include "google/protobuf/arena.h"

namespace google::api::expr::runtime {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::cel::IntValue;
using ::cel::TypeProvider;
using ::cel::interop_internal::CreateIntValue;
using ::cel::runtime_internal::NewTestingRuntimeEnv;
using ::cel::expr::Expr;
using ::google::api::expr::runtime::RegisterBuiltinFunctions;
using ::testing::_;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::HasSubstr;

// Fake expression implementation
// Pushes int64(0) on top of value stack.
class FakeConstExpressionStep : public ExpressionStepLogic {
 public:
  FakeConstExpressionStep() = default;

  void Evaluate(ExecutionFrame* frame) const override {
    frame->value_stack().Push(CreateIntValue(0));
  }
};

// Fake expression implementation
// Increments argument on top of the stack.
class FakeIncrementExpressionStep : public ExpressionStepLogic {
 public:
  FakeIncrementExpressionStep() = default;

  void Evaluate(ExecutionFrame* frame) const override {
    auto value = frame->value_stack().Peek();
    frame->value_stack().Pop(1);
    EXPECT_TRUE(value->Is<IntValue>());
    int64_t val = value.GetInt().NativeValue();
    frame->value_stack().Push(CreateIntValue(val + 1));
  }
};

TEST(EvaluatorCoreTest, ExecutionFrameNext) {
  ExecutionPath path;
  google::protobuf::Arena arena;
  cel::runtime_internal::RuntimeTypeProvider type_provider(
      cel::internal::GetTestingDescriptorPool());

  path.push_back(ExpressionStep::MakeGenericStep(
      std::make_unique<FakeConstExpressionStep>()));
  path.push_back(ExpressionStep::MakeGenericStep(
      std::make_unique<FakeIncrementExpressionStep>()));
  path.push_back(ExpressionStep::MakeGenericStep(
      std::make_unique<FakeIncrementExpressionStep>()));

  cel::RuntimeOptions options;
  options.unknown_processing = cel::UnknownProcessingOptions::kDisabled;
  cel::Activation activation;
  FlatExpressionEvaluatorState state(
      path.size(),
      /*comprehension_slots_size=*/0, type_provider,
      cel::internal::GetTestingDescriptorPool(),
      cel::internal::GetTestingMessageFactory(), &arena);
  ExecutionFrame frame(path, activation, options, state);

  EXPECT_THAT(frame.Next(), Eq(&path[0]));
  EXPECT_THAT(frame.Next(), Eq(&path[1]));
  EXPECT_THAT(frame.Next(), Eq(&path[2]));
  EXPECT_THAT(frame.Next(), Eq(nullptr));
}

TEST(EvaluatorCoreTest, JumpOutOfRangeAborts) {
  ExecutionPath path;
  google::protobuf::Arena arena;
  cel::runtime_internal::RuntimeTypeProvider type_provider(
      cel::internal::GetTestingDescriptorPool());

  path.push_back(ExpressionStep::MakeFixedJumpStep());
  FixedJumpStepInfo* jump_info = GetIfFixedJumpStep(path.back());
  ASSERT_NE(jump_info, nullptr);
  jump_info->set = true;
  jump_info->offset = 1;

  cel::RuntimeOptions options;
  cel::Activation activation;
  FlatExpressionEvaluatorState state(
      path.size(),
      /*comprehension_slots_size=*/0, type_provider,
      cel::internal::GetTestingDescriptorPool(),
      cel::internal::GetTestingMessageFactory(), &arena);
  ExecutionFrame frame(path, activation, options, state);

  EXPECT_THAT(frame.Evaluate(),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Jump address out of range")));
}

TEST(EvaluatorCoreTest, TestStepDataAccessors) {
  ExpressionStep fixed_jump = ExpressionStep::MakeFixedJumpStep();
  EXPECT_NE(GetIfFixedJumpStep(fixed_jump), nullptr);
  EXPECT_NE(GetIfFixedJumpStep(&fixed_jump), nullptr);
  EXPECT_EQ(GetIfFixedJumpStep(static_cast<ExpressionStep*>(nullptr)), nullptr);

  ExpressionStep bool_or_jump = ExpressionStep::MakeBooleanOrJumpStep(2);
  EXPECT_NE(GetIfBoolJumpStep(bool_or_jump), nullptr);
  EXPECT_NE(GetIfBoolJumpStep(&bool_or_jump), nullptr);
  EXPECT_EQ(GetIfBoolJumpStep(static_cast<ExpressionStep*>(nullptr)), nullptr);

  ExpressionStep bool_and_jump = ExpressionStep::MakeBooleanAndJumpStep(2);
  EXPECT_NE(GetIfBoolJumpStep(bool_and_jump), nullptr);
  EXPECT_NE(GetIfBoolJumpStep(&bool_and_jump), nullptr);

  ExpressionStep ternary_jump = ExpressionStep::MakeTernaryJumpStep();
  EXPECT_NE(GetIfTernaryJumpStep(ternary_jump), nullptr);
  EXPECT_NE(GetIfTernaryJumpStep(&ternary_jump), nullptr);
  EXPECT_EQ(GetIfTernaryJumpStep(static_cast<ExpressionStep*>(nullptr)),
            nullptr);

  ExpressionStep comp_cond = ExpressionStep::MakeComprehensionCondStep();
  EXPECT_NE(GetIfComprehensionCondStep(comp_cond), nullptr);

  ExpressionStep comp_cond2 = ExpressionStep::MakeComprehensionCond2Step();
  EXPECT_NE(GetIfComprehensionCondStep(comp_cond2), nullptr);

  ExpressionStep comp_next = ExpressionStep::MakeComprehensionNextStep();
  EXPECT_NE(GetIfComprehensionNextStep(comp_next), nullptr);

  ExpressionStep comp_next2 = ExpressionStep::MakeComprehensionNext2Step();
  EXPECT_NE(GetIfComprehensionNextStep(comp_next2), nullptr);

  ExpressionStep const_step = ExpressionStep::MakeConstant(cel::IntValue(42));
  EXPECT_TRUE(IsConstant(const_step));
  cel::Value value;
  EXPECT_TRUE(GetIfConstant(const_step, value));
  EXPECT_TRUE(value.IsInt());
  EXPECT_EQ(value.GetInt().NativeValue(), 42);

  ExpressionStep generic_step = ExpressionStep::MakeGenericStep(
      std::make_unique<FakeConstExpressionStep>());
  EXPECT_TRUE(generic_step.IsGenericStep());
  EXPECT_NE(generic_step.GetGenericStep(), nullptr);

  // Negative checks for mismatched step kinds.
  EXPECT_EQ(GetIfFixedJumpStep(bool_or_jump), nullptr);
  EXPECT_EQ(GetIfBoolJumpStep(fixed_jump), nullptr);
  EXPECT_EQ(GetIfTernaryJumpStep(fixed_jump), nullptr);
  EXPECT_EQ(GetIfComprehensionCondStep(fixed_jump), nullptr);
  EXPECT_EQ(GetIfComprehensionNextStep(fixed_jump), nullptr);
  EXPECT_FALSE(IsConstant(fixed_jump));
  EXPECT_FALSE(GetIfConstant(fixed_jump, value));
  EXPECT_FALSE(fixed_jump.IsGenericStep());
}

TEST(EvaluatorCoreTest, SimpleEvaluatorTest) {
  ExecutionPath path;
  path.push_back(ExpressionStep::MakeGenericStep(
      std::make_unique<FakeConstExpressionStep>()));
  path.push_back(ExpressionStep::MakeGenericStep(
      std::make_unique<FakeIncrementExpressionStep>()));
  path.push_back(ExpressionStep::MakeGenericStep(
      std::make_unique<FakeIncrementExpressionStep>()));

  auto env = NewTestingRuntimeEnv();
  CelExpressionFlatImpl impl(
      env, FlatExpression(std::move(path), 0,
                          env->type_registry.GetComposedTypeProvider(),
                          cel::RuntimeOptions{}));

  Activation activation;
  google::protobuf::Arena arena;

  auto status = impl.Evaluate(activation, &arena);
  ASSERT_THAT(status, IsOk());

  auto value = status.value();
  EXPECT_TRUE(value.IsInt64());
  EXPECT_THAT(value.Int64OrDie(), Eq(2));
}

TEST(EvaluatorCoreTest, MakeConstant) {
  cel::runtime_internal::RuntimeTypeProvider type_provider(
      cel::internal::GetTestingDescriptorPool());
  google::protobuf::Arena arena;
  cel::Activation activation;
  cel::RuntimeOptions options;

  auto evaluate_constant =
      [&](const cel::Value& value) -> absl::StatusOr<cel::Value> {
    ExecutionPath path;
    path.push_back(ExpressionStep::MakeConstant(value));
    FlatExpression expr(std::move(path), 0, type_provider, options);
    auto state = expr.MakeEvaluatorState(
        cel::internal::GetTestingDescriptorPool(),
        cel::internal::GetTestingMessageFactory(), &arena);
    return expr.EvaluateWithCallback(activation, nullptr, nullptr, state);
  };

  ASSERT_OK_AND_ASSIGN(auto bool_val, evaluate_constant(cel::BoolValue(true)));
  EXPECT_TRUE(bool_val.IsBool());
  EXPECT_TRUE(bool_val.GetBool().NativeValue());

  ASSERT_OK_AND_ASSIGN(auto int_val, evaluate_constant(cel::IntValue(42)));
  EXPECT_TRUE(int_val.IsInt());
  EXPECT_EQ(int_val.GetInt().NativeValue(), 42);

  ASSERT_OK_AND_ASSIGN(auto uint_val, evaluate_constant(cel::UintValue(100)));
  EXPECT_TRUE(uint_val.IsUint());
  EXPECT_EQ(uint_val.GetUint().NativeValue(), 100);

  ASSERT_OK_AND_ASSIGN(auto double_val,
                       evaluate_constant(cel::DoubleValue(3.14)));
  EXPECT_TRUE(double_val.IsDouble());
  EXPECT_DOUBLE_EQ(double_val.GetDouble().NativeValue(), 3.14);

  ASSERT_OK_AND_ASSIGN(auto null_val, evaluate_constant(cel::NullValue()));
  EXPECT_TRUE(null_val.IsNull());

  ASSERT_OK_AND_ASSIGN(auto str_val,
                       evaluate_constant(cel::StringValue::Literal("hello")));
  EXPECT_TRUE(str_val.IsString());
  EXPECT_EQ(str_val.GetString().ToString(), "hello");

  auto step_with_id = ExpressionStep::MakeConstant(cel::IntValue(1), 123);
  EXPECT_EQ(step_with_id.id(), 123);
  EXPECT_TRUE(step_with_id.comes_from_ast());
}

class MockTraceCallback {
 public:
  MOCK_METHOD(void, Call,
              (int64_t expr_id, const CelValue& value, google::protobuf::Arena*));
};

TEST(EvaluatorCoreTest, TraceFilterById) {
  ExecutionPath path;
  // Step with ID 0 should trigger trace callback.
  path.push_back(ExpressionStep::MakeGenericStep(
      std::make_unique<FakeConstExpressionStep>(), /*id=*/0));
  // Steps with large IDs (> int32_t max) should not trigger trace callback.
  path.push_back(ExpressionStep::MakeGenericStep(
      std::make_unique<FakeIncrementExpressionStep>(),
      /*id=*/static_cast<int64_t>(std::numeric_limits<int32_t>::max()) + 1));
  path.push_back(ExpressionStep::MakeGenericStep(
      std::make_unique<FakeIncrementExpressionStep>(),
      /*id=*/std::numeric_limits<int64_t>::max()));
  // Steps with negative IDs should not trigger trace callback.
  path.push_back(ExpressionStep::MakeGenericStep(
      std::make_unique<FakeIncrementExpressionStep>(), /*id=*/-1));
  path.push_back(ExpressionStep::MakeGenericStep(
      std::make_unique<FakeIncrementExpressionStep>(), /*id=*/-100));

  auto env = NewTestingRuntimeEnv();
  CelExpressionFlatImpl impl(
      env, FlatExpression(std::move(path), 0,
                          env->type_registry.GetComposedTypeProvider(),
                          cel::RuntimeOptions{}));

  Activation activation;
  google::protobuf::Arena arena;

  std::vector<int64_t> traced_ids;
  auto eval_status = impl.Trace(
      activation, &arena,
      [&](int64_t expr_id, const CelValue& value, google::protobuf::Arena* arena) {
        traced_ids.push_back(expr_id);
        return absl::OkStatus();
      });
  ASSERT_THAT(eval_status, IsOk());
  EXPECT_THAT(traced_ids, ElementsAre(0));
}

TEST(EvaluatorCoreTest, TraceTest) {
  Expr expr;
  cel::expr::SourceInfo source_info;

  // 1 && [1,2,3].all(x, x > 0)

  expr.set_id(1);
  auto and_call = expr.mutable_call_expr();
  and_call->set_function("_&&_");

  auto true_expr = and_call->add_args();
  true_expr->set_id(2);
  true_expr->mutable_const_expr()->set_int64_value(1);

  auto comp_expr = and_call->add_args();
  comp_expr->set_id(3);
  auto comp = comp_expr->mutable_comprehension_expr();
  comp->set_iter_var("x");
  comp->set_accu_var("accu");

  auto list_expr = comp->mutable_iter_range();
  list_expr->set_id(4);
  auto el1_expr = list_expr->mutable_list_expr()->add_elements();
  el1_expr->set_id(11);
  el1_expr->mutable_const_expr()->set_int64_value(1);
  auto el2_expr = list_expr->mutable_list_expr()->add_elements();
  el2_expr->set_id(12);
  el2_expr->mutable_const_expr()->set_int64_value(2);
  auto el3_expr = list_expr->mutable_list_expr()->add_elements();
  el3_expr->set_id(13);
  el3_expr->mutable_const_expr()->set_int64_value(3);

  auto accu_init_expr = comp->mutable_accu_init();
  accu_init_expr->set_id(20);
  accu_init_expr->mutable_const_expr()->set_bool_value(true);

  auto loop_cond_expr = comp->mutable_loop_condition();
  loop_cond_expr->set_id(21);
  loop_cond_expr->mutable_const_expr()->set_bool_value(true);

  auto loop_step_expr = comp->mutable_loop_step();
  loop_step_expr->set_id(22);
  auto condition = loop_step_expr->mutable_call_expr();
  condition->set_function("_>_");

  auto iter_expr = condition->add_args();
  iter_expr->set_id(23);
  iter_expr->mutable_ident_expr()->set_name("x");

  auto zero_expr = condition->add_args();
  zero_expr->set_id(24);
  zero_expr->mutable_const_expr()->set_int64_value(0);

  auto result_expr = comp->mutable_result();
  result_expr->set_id(25);
  result_expr->mutable_const_expr()->set_bool_value(true);

  cel::RuntimeOptions options;
  options.short_circuiting = false;
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv(), options);
  ASSERT_THAT(RegisterBuiltinFunctions(builder.GetRegistry()), IsOk());
  ASSERT_OK_AND_ASSIGN(auto cel_expr,
                       builder.CreateExpression(&expr, &source_info));

  Activation activation;
  google::protobuf::Arena arena;

  MockTraceCallback callback;

  EXPECT_CALL(callback, Call(accu_init_expr->id(), _, &arena));
  EXPECT_CALL(callback, Call(el1_expr->id(), _, &arena));
  EXPECT_CALL(callback, Call(el2_expr->id(), _, &arena));
  EXPECT_CALL(callback, Call(el3_expr->id(), _, &arena));

  EXPECT_CALL(callback, Call(list_expr->id(), _, &arena));

  EXPECT_CALL(callback, Call(loop_cond_expr->id(), _, &arena)).Times(3);
  EXPECT_CALL(callback, Call(iter_expr->id(), _, &arena)).Times(3);
  EXPECT_CALL(callback, Call(zero_expr->id(), _, &arena)).Times(3);
  EXPECT_CALL(callback, Call(loop_step_expr->id(), _, &arena)).Times(3);

  EXPECT_CALL(callback, Call(result_expr->id(), _, &arena));
  EXPECT_CALL(callback, Call(comp_expr->id(), _, &arena));
  EXPECT_CALL(callback, Call(true_expr->id(), _, &arena));
  EXPECT_CALL(callback, Call(expr.id(), _, &arena));

  auto eval_status = cel_expr->Trace(
      activation, &arena,
      [&](int64_t expr_id, const CelValue& value, google::protobuf::Arena* arena) {
        callback.Call(expr_id, value, arena);
        return absl::OkStatus();
      });
  ASSERT_THAT(eval_status, IsOk());
}

TEST(EvaluatorCoreTest, StepStackDelta) {
  EXPECT_EQ(ExpressionStep::MakeConstant(cel::IntValue(1)).StackDelta(), 1);
  EXPECT_EQ(ExpressionStep::MakeConstant(cel::BoolValue(true)).StackDelta(), 1);
  EXPECT_EQ(ExpressionStep::MakeConstant(cel::DoubleValue(1.0)).StackDelta(),
            1);
  EXPECT_EQ(ExpressionStep::MakeConstant(cel::NullValue()).StackDelta(), 1);
  EXPECT_EQ(ExpressionStep::MakeConstant(cel::UintValue(1)).StackDelta(), 1);
  EXPECT_EQ(
      ExpressionStep::MakeConstant(cel::StringValue::Literal("a")).StackDelta(),
      1);

  ExpressionStep moved_step = ExpressionStep::MakeConstant(cel::IntValue(1));
  ExpressionStep dest_step = std::move(moved_step);
  EXPECT_EQ(moved_step.StackDelta(), 0);
  EXPECT_EQ(dest_step.StackDelta(), 1);

  EXPECT_EQ(ExpressionStep::MakeLazyInitStep(0, 1).StackDelta(), 1);
  EXPECT_EQ(ExpressionStep::MakeAssignSlotAndPopStep(0).StackDelta(), -1);
  EXPECT_EQ(ExpressionStep::MakeClearSlotsStep(0, 2).StackDelta(), 0);
  EXPECT_EQ(ExpressionStep::MakeReadSlotStep(0).StackDelta(), 1);

  EXPECT_EQ(ExpressionStep::MakeBooleanNotStep().StackDelta(), 0);
  EXPECT_EQ(ExpressionStep::MakeNotStrictlyFalseStep().StackDelta(), 0);
  EXPECT_EQ(ExpressionStep::MakeBooleanOrStep(3).StackDelta(), -2);
  EXPECT_EQ(ExpressionStep::MakeBooleanAndStep(2).StackDelta(), -1);

  EXPECT_EQ(ExpressionStep::MakeComprehensionFinishStep(0).StackDelta(), -1);
  EXPECT_EQ(ExpressionStep::MakeComprehensionNextStep().StackDelta(), -1);
  EXPECT_EQ(ExpressionStep::MakeComprehensionNext2Step().StackDelta(), -1);
  EXPECT_EQ(ExpressionStep::MakeComprehensionCondStep().StackDelta(), -1);
  EXPECT_EQ(ExpressionStep::MakeComprehensionCond2Step().StackDelta(), -1);

  EXPECT_EQ(ExpressionStep::MakeBooleanOrJumpStep(2).StackDelta(), 0);
  EXPECT_EQ(ExpressionStep::MakeBooleanAndJumpStep(2).StackDelta(), 0);
  EXPECT_EQ(ExpressionStep::MakeTernaryJumpStep().StackDelta(), 0);
  EXPECT_EQ(ExpressionStep::MakeFixedJumpStep().StackDelta(), 0);

  EXPECT_EQ(ExpressionStep::MakeIdentifierStep("x").StackDelta(), 1);
  EXPECT_EQ(ExpressionStep::MakeFastInStep().StackDelta(), -1);
  EXPECT_EQ(ExpressionStep::MakeFastEqualStep().StackDelta(), -1);
  EXPECT_EQ(ExpressionStep::MakeFastNotEqualStep().StackDelta(), -1);
  EXPECT_EQ(ExpressionStep::MakeNewMutableListStep().StackDelta(), 1);
  EXPECT_EQ(ExpressionStep::MakeMutableListAppendStep().StackDelta(), -1);

  EXPECT_EQ(ExpressionStep::MakeCreateSmallListStep(SmallListStepInfo{0, 4})
                .StackDelta(),
            -3);
  EXPECT_EQ(
      ExpressionStep::MakeCreateListStep(
          std::make_unique<ListStepInfo>(5, absl::flat_hash_set<size_t>{}))
          .StackDelta(),
      -4);

  EXPECT_EQ(ExpressionStep::MakeEagerFunctionStep(
                std::make_unique<EagerFunctionStep>(
                    std::vector<cel::FunctionOverloadReference>{}, "fn",
                    /*num_args=*/3, /*receiver_style=*/false, /*expr_id=*/1))
                .StackDelta(),
            -2);
  EXPECT_EQ(ExpressionStep::MakeLazyFunctionStep(
                std::make_unique<LazyFunctionStep>(
                    std::vector<cel::FunctionRegistry::LazyOverload>{}, "fn",
                    /*num_args=*/2, /*receiver_style=*/false, /*expr_id=*/1))
                .StackDelta(),
            -1);

  EXPECT_EQ(ExpressionStep::MakeGenericStep(
                std::make_unique<FakeConstExpressionStep>())
                .StackDelta(),
            1);
  EXPECT_EQ(ExpressionStep::MakeGenericStep(
                std::make_unique<FakeIncrementExpressionStep>(), /*id=*/-1,
                /*stack_delta=*/-2)
                .StackDelta(),
            -2);
  EXPECT_EQ(ExpressionStep::MakeGenericStep(
                std::make_unique<FakeIncrementExpressionStep>(), /*id=*/-1,
                /*stack_delta=*/std::numeric_limits<int16_t>::max())
                .StackDelta(),
            std::nullopt);
  EXPECT_EQ(ExpressionStep::MakeGenericStep(
                std::make_unique<FakeIncrementExpressionStep>(), /*id=*/-1,
                /*stack_delta=*/
                static_cast<int64_t>(std::numeric_limits<int16_t>::min()) - 1)
                .StackDelta(),
            std::nullopt);
}

}  // namespace google::api::expr::runtime
