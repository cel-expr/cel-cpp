#include "eval/eval/ident_step.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "base/type_provider.h"
#include "common/memory.h"
#include "eval/eval/cel_expression_flat_impl.h"
#include "eval/eval/evaluator_core.h"
#include "eval/public/activation.h"
#include "eval/public/cel_attribute.h"
#include "eval/public/cel_value.h"
#include "internal/testing.h"
#include "runtime/internal/runtime_env_testing.h"
#include "runtime/runtime_options.h"
#include "google/protobuf/arena.h"

namespace google::api::expr::runtime {

namespace {

using ::absl_testing::IsOk;
using ::cel::MemoryManagerRef;
using ::cel::RuntimeOptions;
using ::cel::TypeProvider;
using ::cel::runtime_internal::NewTestingRuntimeEnv;
using ::google::protobuf::Arena;
using ::testing::Eq;

TEST(IdentStepTest, TestIdentStep) {
  ExecutionPath path;
  path.push_back(ExpressionStep::MakeIdentifierStep("name0"));

  auto env = NewTestingRuntimeEnv();
  CelExpressionFlatImpl impl(
      env, FlatExpression(std::move(path), /*comprehension_slot_count=*/0,
                          env->type_registry.GetComposedTypeProvider(),
                          cel::RuntimeOptions{}));

  Activation activation;
  Arena arena;
  std::string value("test");

  activation.InsertValue("name0", CelValue::CreateString(&value));
  auto status0 = impl.Evaluate(activation, &arena);
  ASSERT_THAT(status0, IsOk());

  CelValue result = status0.value();

  ASSERT_TRUE(result.IsString());
  EXPECT_THAT(result.StringOrDie().value(), Eq("test"));
}

TEST(IdentStepTest, TestIdentStepNameNotFound) {
  ExecutionPath path;
  path.push_back(ExpressionStep::MakeIdentifierStep("name0"));

  auto env = NewTestingRuntimeEnv();
  CelExpressionFlatImpl impl(
      env, FlatExpression(std::move(path), /*comprehension_slot_count=*/0,
                          env->type_registry.GetComposedTypeProvider(),
                          cel::RuntimeOptions{}));

  Activation activation;
  Arena arena;
  std::string value("test");

  auto status0 = impl.Evaluate(activation, &arena);
  ASSERT_THAT(status0, IsOk());

  CelValue result = status0.value();
  ASSERT_TRUE(result.IsError());
}

TEST(IdentStepTest, DisableMissingAttributeErrorsOK) {
  ExecutionPath path;
  path.push_back(ExpressionStep::MakeIdentifierStep("name0"));
  cel::RuntimeOptions options;
  options.unknown_processing = cel::UnknownProcessingOptions::kDisabled;
  auto env = NewTestingRuntimeEnv();
  CelExpressionFlatImpl impl(
      env,
      FlatExpression(std::move(path),
                     /*comprehension_slot_count=*/0,
                     env->type_registry.GetComposedTypeProvider(), options));

  Activation activation;
  Arena arena;
  std::string value("test");

  activation.InsertValue("name0", CelValue::CreateString(&value));
  auto status0 = impl.Evaluate(activation, &arena);
  ASSERT_THAT(status0, IsOk());

  CelValue result = status0.value();

  ASSERT_TRUE(result.IsString());
  EXPECT_THAT(result.StringOrDie().value(), Eq("test"));

  const CelAttributePattern pattern("name0", {});
  ASSERT_THAT(activation.SetMissingAttributePatterns({pattern}), IsOk());

  status0 = impl.Evaluate(activation, &arena);
  ASSERT_THAT(status0, IsOk());

  EXPECT_THAT(status0->StringOrDie().value(), Eq("test"));
}

TEST(IdentStepTest, TestIdentStepMissingAttributeErrors) {
  ExecutionPath path;
  path.push_back(ExpressionStep::MakeIdentifierStep("name0"));

  cel::RuntimeOptions options;
  options.unknown_processing = cel::UnknownProcessingOptions::kDisabled;
  options.enable_missing_attribute_errors = true;

  auto env = NewTestingRuntimeEnv();
  CelExpressionFlatImpl impl(
      env,
      FlatExpression(std::move(path),
                     /*comprehension_slot_count=*/0,
                     env->type_registry.GetComposedTypeProvider(), options));

  Activation activation;
  Arena arena;
  std::string value("test");

  activation.InsertValue("name0", CelValue::CreateString(&value));
  auto status0 = impl.Evaluate(activation, &arena);
  ASSERT_THAT(status0, IsOk());

  CelValue result = status0.value();

  ASSERT_TRUE(result.IsString());
  EXPECT_THAT(result.StringOrDie().value(), Eq("test"));

  CelAttributePattern pattern("name0", {});
  ASSERT_THAT(activation.SetMissingAttributePatterns({pattern}), IsOk());

  status0 = impl.Evaluate(activation, &arena);
  ASSERT_THAT(status0, IsOk());

  EXPECT_EQ(status0->ErrorOrDie()->code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status0->ErrorOrDie()->message(), "MissingAttributeError: name0");
}

TEST(IdentStepTest, TestIdentStepUnknownAttribute) {
  ExecutionPath path;
  path.push_back(ExpressionStep::MakeIdentifierStep("name0"));

  // Expression with unknowns enabled.
  cel::RuntimeOptions options;
  options.unknown_processing = cel::UnknownProcessingOptions::kAttributeOnly;
  auto env = NewTestingRuntimeEnv();
  CelExpressionFlatImpl impl(
      env,
      FlatExpression(std::move(path),
                     /*comprehension_slot_count=*/0,
                     env->type_registry.GetComposedTypeProvider(), options));

  Activation activation;
  Arena arena;
  std::string value("test");

  activation.InsertValue("name0", CelValue::CreateString(&value));
  std::vector<CelAttributePattern> unknown_patterns;
  unknown_patterns.push_back(CelAttributePattern("name_bad", {}));

  ASSERT_THAT(activation.SetUnknownAttributePatterns(unknown_patterns), IsOk());
  auto status0 = impl.Evaluate(activation, &arena);
  ASSERT_THAT(status0, IsOk());

  CelValue result = status0.value();

  ASSERT_TRUE(result.IsString());
  EXPECT_THAT(result.StringOrDie().value(), Eq("test"));

  unknown_patterns.push_back(CelAttributePattern("name0", {}));

  ASSERT_THAT(activation.SetUnknownAttributePatterns(unknown_patterns), IsOk());
  status0 = impl.Evaluate(activation, &arena);
  ASSERT_THAT(status0, IsOk());

  result = status0.value();

  ASSERT_TRUE(result.IsUnknownSet());
}

}  // namespace

}  // namespace google::api::expr::runtime
