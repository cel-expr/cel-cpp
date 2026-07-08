// A collection of tests that confirm that short-circuit and non-short-circuit
// produce expressions with the same outputs.
#include <memory>
#include <string>
#include <tuple>

#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "eval/compiler/cel_expression_builder_flat_impl.h"
#include "eval/public/activation.h"
#include "eval/public/cel_attribute.h"
#include "eval/public/cel_expression.h"
#include "eval/public/cel_value.h"
#include "eval/public/unknown_attribute_set.h"
#include "eval/public/unknown_set.h"
#include "internal/testing.h"
#include "parser/parser.h"
#include "runtime/internal/runtime_env_testing.h"
#include "runtime/runtime_options.h"
#include "google/protobuf/arena.h"

namespace google::api::expr::runtime {

namespace {

using ::cel::runtime_internal::NewTestingRuntimeEnv;
using ::cel::expr::Expr;
using ::google::api::expr::parser::Parse;
using ::testing::Eq;
using ::testing::SizeIs;

void BuildAndEval(CelExpressionBuilder* builder, const Expr& expr,
                  const Activation& activation, google::protobuf::Arena* arena,
                  CelValue* result) {
  ASSERT_OK_AND_ASSIGN(auto expression,
                       builder->CreateExpression(&expr, nullptr));

  auto value = expression->Evaluate(activation, arena);
  ASSERT_OK(value);

  *result = *value;
}

class ShortCircuitingTest
    : public testing::TestWithParam<std::tuple<bool, bool>> {
 public:
  bool short_circuiting() const { return std::get<0>(GetParam()); }
  bool enable_variadic() const { return std::get<1>(GetParam()); }

  std::unique_ptr<CelExpressionBuilder> GetBuilder(
      bool enable_unknowns = false) {
    cel::RuntimeOptions options;
    options.short_circuiting = short_circuiting();
    if (enable_unknowns) {
      options.unknown_processing =
          cel::UnknownProcessingOptions::kAttributeAndFunction;
    }
    auto result = std::make_unique<CelExpressionBuilderFlatImpl>(
        NewTestingRuntimeEnv(), options);
    return result;
  }

  Expr ParseExpr(absl::string_view expression) {
    cel::ParserOptions options;
    options.enable_variadic_logical_operators = enable_variadic();
    auto parsed_expr = Parse(expression, "<input>", options);
    ABSL_CHECK_OK(parsed_expr.status());
    return parsed_expr->expr();
  }
};

TEST_P(ShortCircuitingTest, BasicAnd) {
  Expr expr = ParseExpr("var1 && var2 && var3");
  Activation activation;
  google::protobuf::Arena arena;
  auto builder = GetBuilder();

  activation.InsertValue("var1", CelValue::CreateBool(true));
  activation.InsertValue("var2", CelValue::CreateBool(true));
  activation.InsertValue("var3", CelValue::CreateBool(false));

  CelValue result;
  ASSERT_NO_FATAL_FAILURE(
      BuildAndEval(builder.get(), expr, activation, &arena, &result));

  ASSERT_TRUE(result.IsBool());
  EXPECT_FALSE(result.BoolOrDie());

  ASSERT_TRUE(activation.RemoveValueEntry("var3"));
  activation.InsertValue("var3", CelValue::CreateBool(true));

  ASSERT_NO_FATAL_FAILURE(
      BuildAndEval(builder.get(), expr, activation, &arena, &result));
  ASSERT_TRUE(result.IsBool());
  EXPECT_TRUE(result.BoolOrDie());
}

TEST_P(ShortCircuitingTest, BasicOr) {
  Expr expr = ParseExpr("var1 || var2 || var3");
  Activation activation;
  google::protobuf::Arena arena;
  auto builder = GetBuilder();

  activation.InsertValue("var1", CelValue::CreateBool(false));
  activation.InsertValue("var2", CelValue::CreateBool(false));
  activation.InsertValue("var3", CelValue::CreateBool(true));

  CelValue result;
  ASSERT_NO_FATAL_FAILURE(
      BuildAndEval(builder.get(), expr, activation, &arena, &result));

  ASSERT_TRUE(result.IsBool());
  EXPECT_TRUE(result.BoolOrDie());

  ASSERT_TRUE(activation.RemoveValueEntry("var3"));
  activation.InsertValue("var3", CelValue::CreateBool(false));

  ASSERT_NO_FATAL_FAILURE(
      BuildAndEval(builder.get(), expr, activation, &arena, &result));
  ASSERT_TRUE(result.IsBool());
  EXPECT_FALSE(result.BoolOrDie());
}

TEST_P(ShortCircuitingTest, ErrorAnd) {
  Expr expr = ParseExpr("var1 && var2 && var3");
  Activation activation;
  google::protobuf::Arena arena;
  auto builder = GetBuilder();
  absl::Status error = absl::InternalError("error");

  activation.InsertValue("var1", CelValue::CreateBool(true));
  activation.InsertValue("var2", CelValue::CreateError(&error));
  activation.InsertValue("var3", CelValue::CreateBool(false));

  CelValue result;
  ASSERT_NO_FATAL_FAILURE(
      BuildAndEval(builder.get(), expr, activation, &arena, &result));

  ASSERT_TRUE(result.IsBool());
  EXPECT_FALSE(result.BoolOrDie());

  ASSERT_TRUE(activation.RemoveValueEntry("var3"));
  activation.InsertValue("var3", CelValue::CreateBool(true));

  ASSERT_NO_FATAL_FAILURE(
      BuildAndEval(builder.get(), expr, activation, &arena, &result));
  ASSERT_TRUE(result.IsError());
  EXPECT_THAT(*result.ErrorOrDie(),
              Eq(absl::Status(absl::StatusCode::kInternal, "error")));
}

TEST_P(ShortCircuitingTest, ErrorOr) {
  Expr expr = ParseExpr("var1 || var2 || var3");
  Activation activation;
  google::protobuf::Arena arena;
  auto builder = GetBuilder();
  absl::Status error = absl::InternalError("error");

  activation.InsertValue("var1", CelValue::CreateBool(false));
  activation.InsertValue("var2", CelValue::CreateError(&error));
  activation.InsertValue("var3", CelValue::CreateBool(true));

  CelValue result;
  ASSERT_NO_FATAL_FAILURE(
      BuildAndEval(builder.get(), expr, activation, &arena, &result));

  ASSERT_TRUE(result.IsBool());
  EXPECT_TRUE(result.BoolOrDie());

  ASSERT_TRUE(activation.RemoveValueEntry("var3"));
  activation.InsertValue("var3", CelValue::CreateBool(false));

  ASSERT_NO_FATAL_FAILURE(
      BuildAndEval(builder.get(), expr, activation, &arena, &result));
  ASSERT_TRUE(result.IsError());
  EXPECT_THAT(*result.ErrorOrDie(),
              Eq(absl::Status(absl::StatusCode::kInternal, "error")));
}

TEST_P(ShortCircuitingTest, UnknownAnd) {
  Expr expr = ParseExpr("var1 && var2 && var3");
  Activation activation;
  google::protobuf::Arena arena;
  auto builder = GetBuilder(/* enable_unknowns=*/true);
  absl::Status error = absl::InternalError("error");

  activation.set_unknown_attribute_patterns({CelAttributePattern("var1", {})});
  activation.InsertValue("var2", CelValue::CreateError(&error));
  activation.InsertValue("var3", CelValue::CreateBool(false));

  CelValue result;
  ASSERT_NO_FATAL_FAILURE(
      BuildAndEval(builder.get(), expr, activation, &arena, &result));

  ASSERT_TRUE(result.IsBool());
  EXPECT_FALSE(result.BoolOrDie());

  ASSERT_TRUE(activation.RemoveValueEntry("var3"));
  activation.InsertValue("var3", CelValue::CreateBool(true));

  ASSERT_NO_FATAL_FAILURE(
      BuildAndEval(builder.get(), expr, activation, &arena, &result));
  ASSERT_TRUE(result.IsUnknownSet());
  const UnknownAttributeSet& attrs =
      result.UnknownSetOrDie()->unknown_attributes();
  ASSERT_THAT(attrs, testing::SizeIs(1));
  EXPECT_THAT(attrs.begin()->variable_name(), testing::Eq("var1"));
}

TEST_P(ShortCircuitingTest, UnknownOr) {
  Expr expr = ParseExpr("var1 || var2 || var3");
  Activation activation;
  google::protobuf::Arena arena;
  auto builder = GetBuilder(/* enable_unknowns=*/true);
  absl::Status error = absl::InternalError("error");

  activation.set_unknown_attribute_patterns({CelAttributePattern("var1", {})});
  activation.InsertValue("var2", CelValue::CreateError(&error));
  activation.InsertValue("var3", CelValue::CreateBool(true));

  CelValue result;
  ASSERT_NO_FATAL_FAILURE(
      BuildAndEval(builder.get(), expr, activation, &arena, &result));

  ASSERT_TRUE(result.IsBool());
  EXPECT_TRUE(result.BoolOrDie());

  ASSERT_TRUE(activation.RemoveValueEntry("var3"));
  activation.InsertValue("var3", CelValue::CreateBool(false));

  ASSERT_NO_FATAL_FAILURE(
      BuildAndEval(builder.get(), expr, activation, &arena, &result));
  ASSERT_TRUE(result.IsUnknownSet());
  const UnknownAttributeSet& attrs =
      result.UnknownSetOrDie()->unknown_attributes();
  ASSERT_THAT(attrs, testing::SizeIs(1));
  EXPECT_THAT(attrs.begin()->variable_name(), testing::Eq("var1"));
}

TEST_P(ShortCircuitingTest, BasicTernary) {
  Expr expr = ParseExpr("cond ? arg1 : arg2");
  Activation activation;
  google::protobuf::Arena arena;
  auto builder = GetBuilder();

  activation.InsertValue("cond", CelValue::CreateBool(true));
  activation.InsertValue("arg1", CelValue::CreateUint64(1));
  activation.InsertValue("arg2", CelValue::CreateInt64(-1));

  CelValue result;
  ASSERT_NO_FATAL_FAILURE(
      BuildAndEval(builder.get(), expr, activation, &arena, &result));

  ASSERT_TRUE(result.IsUint64());
  EXPECT_EQ(result.Uint64OrDie(), 1);

  ASSERT_TRUE(activation.RemoveValueEntry("cond"));
  activation.InsertValue("cond", CelValue::CreateBool(false));

  ASSERT_NO_FATAL_FAILURE(
      BuildAndEval(builder.get(), expr, activation, &arena, &result));
  ASSERT_TRUE(result.IsInt64());
  EXPECT_EQ(result.Int64OrDie(), -1);
}

TEST_P(ShortCircuitingTest, TernaryErrorHandling) {
  Expr expr = ParseExpr("cond ? arg1 : arg2");
  Activation activation;
  google::protobuf::Arena arena;
  auto builder = GetBuilder();

  absl::Status error1 = absl::InternalError("error1");
  absl::Status error2 = absl::InternalError("error2");

  activation.InsertValue("cond", CelValue::CreateError(&error1));
  activation.InsertValue("arg1", CelValue::CreateError(&error2));
  activation.InsertValue("arg2", CelValue::CreateInt64(-1));

  CelValue result;
  ASSERT_NO_FATAL_FAILURE(
      BuildAndEval(builder.get(), expr, activation, &arena, &result));

  ASSERT_TRUE(result.IsError());
  EXPECT_EQ(*result.ErrorOrDie(), error1);

  ASSERT_TRUE(activation.RemoveValueEntry("cond"));
  activation.InsertValue("cond", CelValue::CreateBool(false));

  ASSERT_NO_FATAL_FAILURE(
      BuildAndEval(builder.get(), expr, activation, &arena, &result));
  ASSERT_TRUE(result.IsInt64());
  EXPECT_EQ(result.Int64OrDie(), -1);
}

TEST_P(ShortCircuitingTest, TernaryUnknownCondHandling) {
  Expr expr = ParseExpr("cond ? arg1 : arg2");
  Activation activation;
  google::protobuf::Arena arena;
  auto builder = GetBuilder(/*enable_unknowns=*/true);

  absl::Status error = absl::InternalError("error1");

  activation.InsertValue("cond", CelValue::CreateBool(false));
  activation.InsertValue("arg1", CelValue::CreateError(&error));
  activation.InsertValue("arg2", CelValue::CreateInt64(-1));

  activation.set_unknown_attribute_patterns({CelAttributePattern("cond", {})});

  CelValue result;
  ASSERT_NO_FATAL_FAILURE(
      BuildAndEval(builder.get(), expr, activation, &arena, &result));

  ASSERT_TRUE(result.IsUnknownSet());
  const auto& attrs = result.UnknownSetOrDie()->unknown_attributes();
  ASSERT_THAT(attrs, SizeIs(1));
  EXPECT_THAT(attrs.begin()->variable_name(), Eq("cond"));

  // Unknown branches are discarded if condition is unknown
  activation.set_unknown_attribute_patterns({CelAttributePattern("cond", {}),
                                             CelAttributePattern("arg1", {}),
                                             CelAttributePattern("arg2", {})});

  ASSERT_NO_FATAL_FAILURE(
      BuildAndEval(builder.get(), expr, activation, &arena, &result));
  ASSERT_TRUE(result.IsUnknownSet());
  const auto& attrs2 = result.UnknownSetOrDie()->unknown_attributes();
  ASSERT_THAT(attrs2, SizeIs(1));
  EXPECT_THAT(attrs2.begin()->variable_name(), Eq("cond"));
}

TEST_P(ShortCircuitingTest, TernaryUnknownArgsHandling) {
  Expr expr = ParseExpr("cond ? arg1 : arg2");
  Activation activation;
  google::protobuf::Arena arena;
  auto builder = GetBuilder(/*enable_unknowns=*/true);

  absl::Status error = absl::InternalError("error1");

  activation.InsertValue("cond", CelValue::CreateBool(false));
  activation.InsertValue("arg1", CelValue::CreateError(&error));
  activation.InsertValue("arg2", CelValue::CreateInt64(-1));

  // Unknown arg is discarded if condition chooses other branch.
  activation.set_unknown_attribute_patterns({CelAttributePattern("arg1", {})});

  CelValue result;

  ASSERT_NO_FATAL_FAILURE(
      BuildAndEval(builder.get(), expr, activation, &arena, &result));
  ASSERT_TRUE(result.IsInt64());
  EXPECT_EQ(result.Int64OrDie(), -1);

  // Branches won't merge if both are unknown.
  activation.set_unknown_attribute_patterns(
      {CelAttributePattern("arg1", {}), CelAttributePattern("arg2", {})});

  ASSERT_NO_FATAL_FAILURE(
      BuildAndEval(builder.get(), expr, activation, &arena, &result));
  ASSERT_TRUE(result.IsUnknownSet());
  const auto& attrs3 = result.UnknownSetOrDie()->unknown_attributes();
  ASSERT_THAT(attrs3, SizeIs(1));
  EXPECT_EQ(attrs3.begin()->variable_name(), "arg2");
}

TEST_P(ShortCircuitingTest, TernaryUnknownAndErrorHandling) {
  Expr expr = ParseExpr("cond ? arg1 : arg2");
  Activation activation;
  google::protobuf::Arena arena;
  auto builder = GetBuilder(/*enable_unknowns=*/true);

  absl::Status error = absl::InternalError("error1");

  activation.InsertValue("cond", CelValue::CreateError(&error));
  activation.InsertValue("arg1", CelValue::CreateInt64(1));
  activation.InsertValue("arg2", CelValue::CreateInt64(-1));

  // Error cond discards args
  activation.set_unknown_attribute_patterns(
      {CelAttributePattern("arg1", {}), CelAttributePattern("arg2", {})});

  CelValue result;

  ASSERT_NO_FATAL_FAILURE(
      BuildAndEval(builder.get(), expr, activation, &arena, &result));
  ASSERT_TRUE(result.IsError());
  EXPECT_EQ(*result.ErrorOrDie(), error);

  // Error arg discarded if condition unknown
  activation.set_unknown_attribute_patterns({CelAttributePattern("cond", {})});
  ASSERT_TRUE(activation.RemoveValueEntry("arg1"));
  activation.InsertValue("arg1", CelValue::CreateError(&error));

  ASSERT_NO_FATAL_FAILURE(
      BuildAndEval(builder.get(), expr, activation, &arena, &result));
  ASSERT_TRUE(result.IsUnknownSet());
  const auto& attrs = result.UnknownSetOrDie()->unknown_attributes();
  ASSERT_THAT(attrs, SizeIs(1));
  EXPECT_EQ(attrs.begin()->variable_name(), "cond");
}

std::string TestName(testing::TestParamInfo<std::tuple<bool, bool>> info) {
  return absl::StrCat(
      std::get<0>(info.param) ? "short_circuit_enabled"
                              : "short_circuit_disabled",
      "_", std::get<1>(info.param) ? "variadic_enabled" : "variadic_disabled");
}

INSTANTIATE_TEST_SUITE_P(Test, ShortCircuitingTest,
                         testing::Combine(testing::Bool(), testing::Bool()),
                         &TestName);

}  // namespace

}  // namespace google::api::expr::runtime
