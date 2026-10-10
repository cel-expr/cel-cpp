// Copyright 2021 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "eval/compiler/flat_expr_builder.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cel/expr/checked.pb.h"
#include "cel/expr/syntax.pb.h"
#include "google/protobuf/field_mask.pb.h"
#include "google/protobuf/descriptor.pb.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "base/builtins.h"
#include "common/function_descriptor.h"
#include "common/kind.h"
#include "common/value.h"
#include "eval/compiler/cel_expression_builder_flat_impl.h"
#include "eval/compiler/constant_folding.h"
#include "eval/compiler/qualified_reference_resolver.h"
#include "eval/public/activation.h"
#include "eval/public/ast_rewrite.h"
#include "eval/public/builtin_func_registrar.h"
#include "eval/public/cel_attribute.h"
#include "eval/public/cel_expr_builder_factory.h"
#include "eval/public/cel_expression.h"
#include "eval/public/cel_function.h"
#include "eval/public/cel_function_adapter.h"
#include "eval/public/cel_function_registry.h"
#include "eval/public/cel_options.h"
#include "eval/public/cel_value.h"
#include "eval/public/containers/container_backed_map_impl.h"
#include "eval/public/portable_cel_function_adapter.h"
#include "eval/public/structs/cel_proto_descriptor_pool_builder.h"
#include "eval/public/structs/cel_proto_wrapper.h"
#include "eval/public/testing/matchers.h"
#include "eval/public/unknown_attribute_set.h"
#include "eval/public/unknown_set.h"
#include "eval/testutil/test_message.pb.h"
#include "internal/proto_matchers.h"
#include "internal/status_macros.h"
#include "internal/testing.h"
#include "parser/options.h"
#include "parser/parser.h"
#include "runtime/function.h"
#include "runtime/function_adapter.h"
#include "runtime/internal/runtime_env_testing.h"
#include "runtime/runtime_options.h"
#include "runtime/standard_functions.h"
#include "cel/expr/conformance/proto3/test_all_types.pb.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/dynamic_message.h"
#include "google/protobuf/message.h"
#include "google/protobuf/text_format.h"

namespace google::api::expr::runtime {

namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::cel::BytesValue;
using ::cel::Value;
using ::cel::expr::conformance::proto3::TestAllTypes;
using ::cel::internal::test::EqualsProto;
using ::cel::runtime_internal::NewTestingRuntimeEnv;
using ::cel::expr::CheckedExpr;
using ::cel::expr::Expr;
using ::cel::expr::ParsedExpr;
using ::cel::expr::SourceInfo;
using ::testing::_;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::SizeIs;
using ::testing::Truly;

class ConcatFunction : public CelFunction {
 public:
  explicit ConcatFunction() : CelFunction(CreateDescriptor()) {}

  static CelFunctionDescriptor CreateDescriptor() {
    return CelFunctionDescriptor{
        "concat", false, {CelValue::Type::kString, CelValue::Type::kString}};
  }

  absl::Status Evaluate(absl::Span<const CelValue> args, CelValue* result,
                        google::protobuf::Arena* arena) const override {
    if (args.size() != 2) {
      return absl::InvalidArgumentError("Bad arguments number");
    }

    std::string concat = std::string(args[0].StringOrDie().value()) +
                         std::string(args[1].StringOrDie().value());

    auto* concatenated =
        google::protobuf::Arena::Create<std::string>(arena, std::move(concat));

    *result = CelValue::CreateString(concatenated);

    return absl::OkStatus();
  }
};

class RecorderFunction : public CelFunction {
 public:
  explicit RecorderFunction(const std::string& name, int* count)
      : CelFunction(CelFunctionDescriptor{name, false, {}}), count_(count) {}

  absl::Status Evaluate(absl::Span<const CelValue> args, CelValue* result,
                        google::protobuf::Arena* arena) const override {
    if (!args.empty()) {
      return absl::Status(absl::StatusCode::kInvalidArgument,
                          "Bad arguments number");
    }
    (*count_)++;
    *result = CelValue::CreateBool(true);
    return absl::OkStatus();
  }

  int* count_;
};

TEST(FlatExprBuilderTest, SimpleEndToEnd) {
  Expr expr;
  SourceInfo source_info;
  auto call_expr = expr.mutable_call_expr();
  call_expr->set_function("concat");

  auto arg1 = call_expr->add_args();
  arg1->mutable_const_expr()->set_string_value("prefix");

  auto arg2 = call_expr->add_args();
  arg2->mutable_ident_expr()->set_name("value");

  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());

  ASSERT_THAT(
      builder.GetRegistry()->Register(std::make_unique<ConcatFunction>()),
      IsOk());
  ASSERT_OK_AND_ASSIGN(auto cel_expr,
                       builder.CreateExpression(&expr, &source_info));

  std::string variable = "test";

  Activation activation;
  activation.InsertValue("value", CelValue::CreateString(&variable));

  google::protobuf::Arena arena;

  ASSERT_OK_AND_ASSIGN(CelValue result, cel_expr->Evaluate(activation, &arena));
  ASSERT_TRUE(result.IsString());
  EXPECT_THAT(result.StringOrDie().value(), Eq("prefixtest"));
}

TEST(FlatExprBuilderTest, ExprUnset) {
  Expr expr;
  SourceInfo source_info;
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  EXPECT_THAT(builder.CreateExpression(&expr, &source_info).status(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Invalid empty expression")));
}

TEST(FlatExprBuilderTest, RuntimeExtensionsError) {
  Expr expr;
  SourceInfo source_info;
  auto* ext = source_info.add_extensions();
  ext->set_id("ext1");
  ext->add_affected_components(
      cel::expr::SourceInfo_Extension_Component_COMPONENT_RUNTIME);

  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  EXPECT_THAT(builder.CreateExpression(&expr, &source_info).status(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("unsupported CEL extension: ext1")));
}

TEST(FlatExprBuilderTest, ConstValueUnset) {
  Expr expr;
  SourceInfo source_info;
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  // Create an empty constant expression to ensure that it triggers an error.
  expr.mutable_const_expr();

  EXPECT_THAT(builder.CreateExpression(&expr, &source_info).status(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("unspecified constant")));
}

TEST(FlatExprBuilderTest, MapKeyValueUnset) {
  Expr expr;
  SourceInfo source_info;
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());

  // Don't set either the key or the value for the map creation step.
  auto* entry = expr.mutable_struct_expr()->add_entries();
  EXPECT_THAT(builder.CreateExpression(&expr, &source_info).status(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Map entry missing key")));

  // Set the entry key, but not the value.
  entry->mutable_map_key()->mutable_const_expr()->set_bool_value(true);
  EXPECT_THAT(builder.CreateExpression(&expr, &source_info).status(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Map entry missing value")));
}

TEST(FlatExprBuilderTest, MessageFieldValueUnset) {
  Expr expr;
  SourceInfo source_info;
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());

  // Don't set either the field or the value for the message creation step.
  auto* create_message = expr.mutable_struct_expr();
  create_message->set_message_name("google.protobuf.Value");
  auto* entry = create_message->add_entries();
  EXPECT_THAT(builder.CreateExpression(&expr, &source_info).status(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Struct field missing name")));

  // Set the entry field, but not the value.
  entry->set_field_key("bool_value");
  EXPECT_THAT(builder.CreateExpression(&expr, &source_info).status(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Struct field missing value")));
}

TEST(FlatExprBuilderTest, BinaryCallTooManyArguments) {
  Expr expr;
  SourceInfo source_info;
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());

  auto* call = expr.mutable_call_expr();
  call->set_function(cel::builtin::kAnd);
  call->mutable_target()->mutable_const_expr()->set_string_value("random");
  call->add_args()->mutable_const_expr()->set_bool_value(false);
  call->add_args()->mutable_const_expr()->set_bool_value(true);

  EXPECT_THAT(builder.CreateExpression(&expr, &source_info).status(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Invalid argument count")));
}

TEST(FlatExprBuilderTest, TernaryCallTooManyArguments) {
  Expr expr;
  SourceInfo source_info;
  auto* call = expr.mutable_call_expr();
  call->set_function(cel::builtin::kTernary);
  call->mutable_target()->mutable_const_expr()->set_string_value("random");
  call->add_args()->mutable_const_expr()->set_bool_value(false);
  call->add_args()->mutable_const_expr()->set_int64_value(1);
  call->add_args()->mutable_const_expr()->set_int64_value(2);

  {
    cel::RuntimeOptions options;
    options.short_circuiting = true;
    CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv(), options);

    EXPECT_THAT(builder.CreateExpression(&expr, &source_info).status(),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("Invalid argument count")));
  }

  // Disable short-circuiting to ensure that a different visitor is used.
  {
    cel::RuntimeOptions options;
    options.short_circuiting = false;
    CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv(), options);

    EXPECT_THAT(builder.CreateExpression(&expr, &source_info).status(),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("Invalid argument count")));
  }
}

TEST(FlatExprBuilderTest, DelayedFunctionResolutionErrors) {
  Expr expr;
  SourceInfo source_info;
  auto call_expr = expr.mutable_call_expr();
  call_expr->set_function("concat");

  auto arg1 = call_expr->add_args();
  arg1->mutable_const_expr()->set_string_value("prefix");

  auto arg2 = call_expr->add_args();
  arg2->mutable_ident_expr()->set_name("value");

  cel::RuntimeOptions options;
  options.fail_on_warnings = false;
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv(), options);
  std::vector<absl::Status> warnings;

  // Concat function not registered.

  ASSERT_OK_AND_ASSIGN(
      auto cel_expr, builder.CreateExpression(&expr, &source_info, &warnings));

  std::string variable = "test";
  Activation activation;
  activation.InsertValue("value", CelValue::CreateString(&variable));

  google::protobuf::Arena arena;

  ASSERT_OK_AND_ASSIGN(CelValue result, cel_expr->Evaluate(activation, &arena));
  ASSERT_TRUE(result.IsError());
  EXPECT_THAT(result.ErrorOrDie()->message(),
              Eq("No matching overloads found : concat(string, string)"));

  ASSERT_THAT(warnings, testing::SizeIs(1));
  EXPECT_EQ(warnings[0].code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(warnings[0].message()),
              testing::HasSubstr("No overloads provided"));
}

TEST(FlatExprBuilderTest, Shortcircuiting) {
  Expr expr;
  SourceInfo source_info;
  auto call_expr = expr.mutable_call_expr();
  call_expr->set_function("_||_");

  auto arg1 = call_expr->add_args();
  arg1->mutable_call_expr()->set_function("recorder1");

  auto arg2 = call_expr->add_args();
  arg2->mutable_call_expr()->set_function("recorder2");

  Activation activation;
  google::protobuf::Arena arena;

  // Shortcircuiting on
  {
    cel::RuntimeOptions options;
    options.short_circuiting = true;
    CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv(), options);
    auto builtin = RegisterBuiltinFunctions(builder.GetRegistry());

    int count1 = 0;
    int count2 = 0;

    ASSERT_THAT(builder.GetRegistry()->Register(
                    std::make_unique<RecorderFunction>("recorder1", &count1)),
                IsOk());
    ASSERT_THAT(builder.GetRegistry()->Register(
                    std::make_unique<RecorderFunction>("recorder2", &count2)),
                IsOk());

    ASSERT_OK_AND_ASSIGN(auto cel_expr_on,
                         builder.CreateExpression(&expr, &source_info));
    ASSERT_THAT(cel_expr_on->Evaluate(activation, &arena), IsOk());

    EXPECT_THAT(count1, Eq(1));
    EXPECT_THAT(count2, Eq(0));
  }

  // Shortcircuiting off.
  {
    cel::RuntimeOptions options;
    options.short_circuiting = false;
    CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv(), options);
    auto builtin = RegisterBuiltinFunctions(builder.GetRegistry());

    int count1 = 0;
    int count2 = 0;

    ASSERT_THAT(builder.GetRegistry()->Register(
                    std::make_unique<RecorderFunction>("recorder1", &count1)),
                IsOk());
    ASSERT_THAT(builder.GetRegistry()->Register(
                    std::make_unique<RecorderFunction>("recorder2", &count2)),
                IsOk());

    ASSERT_OK_AND_ASSIGN(auto cel_expr_off,
                         builder.CreateExpression(&expr, &source_info));

    ASSERT_THAT(cel_expr_off->Evaluate(activation, &arena), IsOk());
    EXPECT_THAT(count1, Eq(1));
    EXPECT_THAT(count2, Eq(1));
  }
}

TEST(FlatExprBuilderTest, ShortcircuitingComprehension) {
  Expr expr;
  SourceInfo source_info;
  auto comprehension_expr = expr.mutable_comprehension_expr();
  comprehension_expr->set_iter_var("x");
  auto list_expr =
      comprehension_expr->mutable_iter_range()->mutable_list_expr();
  list_expr->add_elements()->mutable_const_expr()->set_int64_value(1);
  list_expr->add_elements()->mutable_const_expr()->set_int64_value(2);
  list_expr->add_elements()->mutable_const_expr()->set_int64_value(3);
  comprehension_expr->set_accu_var("accu");
  comprehension_expr->mutable_accu_init()->mutable_const_expr()->set_bool_value(
      false);
  comprehension_expr->mutable_loop_condition()
      ->mutable_const_expr()
      ->set_bool_value(false);
  comprehension_expr->mutable_loop_step()->mutable_call_expr()->set_function(
      "recorder_function1");
  comprehension_expr->mutable_result()->mutable_const_expr()->set_bool_value(
      false);

  Activation activation;
  google::protobuf::Arena arena;

  // shortcircuiting on
  {
    cel::RuntimeOptions options;
    options.short_circuiting = true;
    CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv(), options);
    auto builtin = RegisterBuiltinFunctions(builder.GetRegistry());

    int count = 0;
    ASSERT_THAT(
        builder.GetRegistry()->Register(
            std::make_unique<RecorderFunction>("recorder_function1", &count)),
        IsOk());

    ASSERT_OK_AND_ASSIGN(auto cel_expr_on,
                         builder.CreateExpression(&expr, &source_info));

    ASSERT_THAT(cel_expr_on->Evaluate(activation, &arena), IsOk());
    EXPECT_THAT(count, Eq(0));
  }

  // shortcircuiting off
  {
    cel::RuntimeOptions options;
    options.short_circuiting = false;
    CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv(), options);
    auto builtin = RegisterBuiltinFunctions(builder.GetRegistry());

    int count = 0;
    ASSERT_THAT(
        builder.GetRegistry()->Register(
            std::make_unique<RecorderFunction>("recorder_function1", &count)),
        IsOk());
    ASSERT_OK_AND_ASSIGN(auto cel_expr_off,
                         builder.CreateExpression(&expr, &source_info));
    ASSERT_THAT(cel_expr_off->Evaluate(activation, &arena), IsOk());
    EXPECT_THAT(count, Eq(3));
  }
}

TEST(FlatExprBuilderTest, IdentExprUnsetName) {
  Expr expr;
  SourceInfo source_info;
  // An empty ident without the name set should error.
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(R"(ident_expr {})", &expr));

  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  ASSERT_THAT(RegisterBuiltinFunctions(builder.GetRegistry()), IsOk());
  EXPECT_THAT(builder.CreateExpression(&expr, &source_info).status(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("'name' must not be empty")));
}

TEST(FlatExprBuilderTest, SelectExprUnsetField) {
  Expr expr;
  SourceInfo source_info;
  // An empty ident without the name set should error.
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(R"(select_expr{
    operand{ ident_expr {name: 'var'} }
    })",
                                                  &expr));

  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  ASSERT_THAT(RegisterBuiltinFunctions(builder.GetRegistry()), IsOk());
  EXPECT_THAT(builder.CreateExpression(&expr, &source_info).status(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("'field' must not be empty")));
}

TEST(FlatExprBuilderTest, SelectExprUnsetOperand) {
  Expr expr;
  SourceInfo source_info;
  // An empty ident without the name set should error.
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(R"(select_expr{
    field: 'field'
    operand { id: 1 }
    })",
                                                  &expr));

  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  ASSERT_THAT(RegisterBuiltinFunctions(builder.GetRegistry()), IsOk());
  EXPECT_THAT(builder.CreateExpression(&expr, &source_info).status(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("must specify an operand")));
}

TEST(FlatExprBuilderTest, ComprehensionExprUnsetAccuVar) {
  Expr expr;
  SourceInfo source_info;
  // An empty ident without the name set should error.
  ASSERT_TRUE(
      google::protobuf::TextFormat::ParseFromString(R"(comprehension_expr{})", &expr));
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  ASSERT_THAT(RegisterBuiltinFunctions(builder.GetRegistry()), IsOk());
  EXPECT_THAT(builder.CreateExpression(&expr, &source_info).status(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("'accu_var' must not be empty")));
}

TEST(FlatExprBuilderTest, ComprehensionExprUnsetIterVar) {
  Expr expr;
  SourceInfo source_info;
  // An empty ident without the name set should error.
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(R"(
      comprehension_expr{accu_var: "a"}
    )",
                                                  &expr));
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  ASSERT_THAT(RegisterBuiltinFunctions(builder.GetRegistry()), IsOk());
  EXPECT_THAT(builder.CreateExpression(&expr, &source_info).status(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("'iter_var' must not be empty")));
}

TEST(FlatExprBuilderTest, ComprehensionExprUnsetAccuInit) {
  Expr expr;
  SourceInfo source_info;
  // An empty ident without the name set should error.
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(R"(
    comprehension_expr{
      accu_var: "a"
      iter_var: "b"}
    )",
                                                  &expr));
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  ASSERT_THAT(RegisterBuiltinFunctions(builder.GetRegistry()), IsOk());
  EXPECT_THAT(builder.CreateExpression(&expr, &source_info).status(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("'accu_init' must be set")));
}

TEST(FlatExprBuilderTest, ComprehensionExprUnsetLoopCondition) {
  Expr expr;
  SourceInfo source_info;
  // An empty ident without the name set should error.
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(R"(
    comprehension_expr{
      accu_var: 'a'
      iter_var: 'b'
      accu_init {
        const_expr {bool_value: true}
      }}
    )",
                                                  &expr));
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  ASSERT_THAT(RegisterBuiltinFunctions(builder.GetRegistry()), IsOk());
  EXPECT_THAT(builder.CreateExpression(&expr, &source_info).status(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("'loop_condition' must be set")));
}

TEST(FlatExprBuilderTest, ComprehensionExprUnsetLoopStep) {
  Expr expr;
  SourceInfo source_info;
  // An empty ident without the name set should error.
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(R"(
    comprehension_expr{
      accu_var: 'a'
      iter_var: 'b'
      accu_init {
        const_expr {bool_value: true}
      }
      loop_condition {
        const_expr {bool_value: true}
      }}
    )",
                                                  &expr));
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  ASSERT_THAT(RegisterBuiltinFunctions(builder.GetRegistry()), IsOk());
  EXPECT_THAT(builder.CreateExpression(&expr, &source_info).status(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("'loop_step' must be set")));
}

TEST(FlatExprBuilderTest, ComprehensionExprUnsetResult) {
  Expr expr;
  SourceInfo source_info;
  // An empty ident without the name set should error.
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(R"(
    comprehension_expr{
      accu_var: 'a'
      iter_var: 'b'
      accu_init {
        const_expr {bool_value: true}
      }
      loop_condition {
        const_expr {bool_value: true}
      }
      loop_step {
        const_expr {bool_value: false}
      }}
    )",
                                                  &expr));
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  ASSERT_THAT(RegisterBuiltinFunctions(builder.GetRegistry()), IsOk());
  EXPECT_THAT(builder.CreateExpression(&expr, &source_info).status(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("'result' must be set")));
}

TEST(FlatExprBuilderTest, MapComprehension) {
  Expr expr;
  SourceInfo source_info;
  // {1: "", 2: ""}.all(x, x > 0)
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(R"(
    comprehension_expr {
      iter_var: "k"
      accu_var: "accu"
      accu_init {
        const_expr { bool_value: true }
      }
      loop_condition { ident_expr { name: "accu" } }
      result { ident_expr { name: "accu" } }
      loop_step {
        call_expr {
          function: "_&&_"
          args {
            ident_expr { name: "accu" }
          }
          args {
            call_expr {
              function: "_>_"
              args { ident_expr { name: "k" } }
              args { const_expr { int64_value: 0 } }
            }
          }
        }
      }
      iter_range {
        struct_expr {
          entries {
            map_key { const_expr { int64_value: 1 } }
            value { const_expr { string_value: "" } }
          }
          entries {
            map_key { const_expr { int64_value: 2 } }
            value { const_expr { string_value: "" } }
          }
        }
      }
    })",
                                                  &expr));

  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  ASSERT_THAT(RegisterBuiltinFunctions(builder.GetRegistry()), IsOk());
  ASSERT_OK_AND_ASSIGN(auto cel_expr,
                       builder.CreateExpression(&expr, &source_info));

  Activation activation;
  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(CelValue result, cel_expr->Evaluate(activation, &arena));
  ASSERT_TRUE(result.IsBool());
  EXPECT_TRUE(result.BoolOrDie());
}

TEST(FlatExprBuilderTest, InvalidContainer) {
  Expr expr;
  SourceInfo source_info;
  // foo && bar
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(R"(
    call_expr {
      function: "_&&_"
      args {
        ident_expr {
          name: "foo"
        }
      }
      args {
        ident_expr {
          name: "bar"
        }
      }
    })",
                                                  &expr));

  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  ASSERT_THAT(RegisterBuiltinFunctions(builder.GetRegistry()), IsOk());

  builder.set_container(".bad");
  EXPECT_THAT(builder.CreateExpression(&expr, &source_info).status(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("container: '.bad'")));

  builder.set_container("bad.");
  EXPECT_THAT(builder.CreateExpression(&expr, &source_info).status(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("container: 'bad.'")));
}

TEST(FlatExprBuilderTest, ParsedNamespacedFunctionSupport) {
  ASSERT_OK_AND_ASSIGN(ParsedExpr expr, parser::Parse("ext.XOr(a, b)"));
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  builder.flat_expr_builder().AddAstTransform(
      NewReferenceResolverExtension(ReferenceResolverOption::kAlways));
  using FunctionAdapterT = FunctionAdapter<bool, bool, bool>;

  ASSERT_OK(FunctionAdapterT::CreateAndRegister(
      "ext.XOr", /*receiver_style=*/false,
      [](google::protobuf::Arena*, bool a, bool b) { return a != b; },
      builder.GetRegistry()));
  ASSERT_OK_AND_ASSIGN(auto cel_expr, builder.CreateExpression(
                                          &expr.expr(), &expr.source_info()));

  google::protobuf::Arena arena;
  Activation act1;
  act1.InsertValue("a", CelValue::CreateBool(false));
  act1.InsertValue("b", CelValue::CreateBool(true));

  ASSERT_OK_AND_ASSIGN(CelValue result, cel_expr->Evaluate(act1, &arena));
  EXPECT_THAT(result, test::IsCelBool(true));

  Activation act2;
  act2.InsertValue("a", CelValue::CreateBool(true));
  act2.InsertValue("b", CelValue::CreateBool(true));

  ASSERT_OK_AND_ASSIGN(result, cel_expr->Evaluate(act2, &arena));
  EXPECT_THAT(result, test::IsCelBool(false));
}

TEST(FlatExprBuilderTest, ParsedNamespacedFunctionSupportWithContainer) {
  ASSERT_OK_AND_ASSIGN(ParsedExpr expr, parser::Parse("XOr(a, b)"));
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  builder.flat_expr_builder().AddAstTransform(
      NewReferenceResolverExtension(ReferenceResolverOption::kAlways));
  builder.set_container("ext");
  using FunctionAdapterT = FunctionAdapter<bool, bool, bool>;

  ASSERT_OK(FunctionAdapterT::CreateAndRegister(
      "ext.XOr", /*receiver_style=*/false,
      [](google::protobuf::Arena*, bool a, bool b) { return a != b; },
      builder.GetRegistry()));
  ASSERT_OK_AND_ASSIGN(auto cel_expr, builder.CreateExpression(
                                          &expr.expr(), &expr.source_info()));
  google::protobuf::Arena arena;
  Activation act1;
  act1.InsertValue("a", CelValue::CreateBool(false));
  act1.InsertValue("b", CelValue::CreateBool(true));

  ASSERT_OK_AND_ASSIGN(CelValue result, cel_expr->Evaluate(act1, &arena));
  EXPECT_THAT(result, test::IsCelBool(true));

  Activation act2;
  act2.InsertValue("a", CelValue::CreateBool(true));
  act2.InsertValue("b", CelValue::CreateBool(true));

  ASSERT_OK_AND_ASSIGN(result, cel_expr->Evaluate(act2, &arena));
  EXPECT_THAT(result, test::IsCelBool(false));
}

TEST(FlatExprBuilderTest, ParsedNamespacedFunctionResolutionOrder) {
  ASSERT_OK_AND_ASSIGN(ParsedExpr expr, parser::Parse("c.d.Get()"));
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  builder.flat_expr_builder().AddAstTransform(
      NewReferenceResolverExtension(ReferenceResolverOption::kAlways));
  builder.set_container("a.b");
  using FunctionAdapterT = FunctionAdapter<bool>;

  ASSERT_OK(FunctionAdapterT::CreateAndRegister(
      "a.b.c.d.Get", /*receiver_style=*/false,
      [](google::protobuf::Arena*) { return true; }, builder.GetRegistry()));
  ASSERT_OK(FunctionAdapterT::CreateAndRegister(
      "c.d.Get", /*receiver_style=*/false, [](google::protobuf::Arena*) { return false; },
      builder.GetRegistry()));
  ASSERT_OK((FunctionAdapter<bool, bool>::CreateAndRegister(
      "Get",
      /*receiver_style=*/true, [](google::protobuf::Arena*, bool) { return false; },
      builder.GetRegistry())));
  ASSERT_OK_AND_ASSIGN(auto cel_expr, builder.CreateExpression(
                                          &expr.expr(), &expr.source_info()));
  google::protobuf::Arena arena;
  Activation act1;
  ASSERT_OK_AND_ASSIGN(CelValue result, cel_expr->Evaluate(act1, &arena));
  EXPECT_THAT(result, test::IsCelBool(true));
}

TEST(FlatExprBuilderTest,
     ParsedNamespacedFunctionResolutionOrderParentContainer) {
  ASSERT_OK_AND_ASSIGN(ParsedExpr expr, parser::Parse("c.d.Get()"));
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  builder.flat_expr_builder().AddAstTransform(
      NewReferenceResolverExtension(ReferenceResolverOption::kAlways));
  builder.set_container("a.b");
  using FunctionAdapterT = FunctionAdapter<bool>;

  ASSERT_OK(FunctionAdapterT::CreateAndRegister(
      "a.c.d.Get", /*receiver_style=*/false,
      [](google::protobuf::Arena*) { return true; }, builder.GetRegistry()));
  ASSERT_OK(FunctionAdapterT::CreateAndRegister(
      "c.d.Get", /*receiver_style=*/false, [](google::protobuf::Arena*) { return false; },
      builder.GetRegistry()));
  ASSERT_OK((FunctionAdapter<bool, bool>::CreateAndRegister(
      "Get",
      /*receiver_style=*/true, [](google::protobuf::Arena*, bool) { return false; },
      builder.GetRegistry())));
  ASSERT_OK_AND_ASSIGN(auto cel_expr, builder.CreateExpression(
                                          &expr.expr(), &expr.source_info()));
  google::protobuf::Arena arena;
  Activation act1;
  ASSERT_OK_AND_ASSIGN(CelValue result, cel_expr->Evaluate(act1, &arena));
  EXPECT_THAT(result, test::IsCelBool(true));
}

TEST(FlatExprBuilderTest,
     ParsedNamespacedFunctionResolutionOrderExplicitGlobal) {
  ASSERT_OK_AND_ASSIGN(ParsedExpr expr, parser::Parse(".c.d.Get()"));
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  builder.flat_expr_builder().AddAstTransform(
      NewReferenceResolverExtension(ReferenceResolverOption::kAlways));
  builder.set_container("a.b");
  using FunctionAdapterT = FunctionAdapter<bool>;

  ASSERT_OK(FunctionAdapterT::CreateAndRegister(
      "a.c.d.Get", /*receiver_style=*/false,
      [](google::protobuf::Arena*) { return false; }, builder.GetRegistry()));
  ASSERT_OK(FunctionAdapterT::CreateAndRegister(
      "c.d.Get", /*receiver_style=*/false, [](google::protobuf::Arena*) { return true; },
      builder.GetRegistry()));
  ASSERT_OK((FunctionAdapter<bool, bool>::CreateAndRegister(
      "Get",
      /*receiver_style=*/true, [](google::protobuf::Arena*, bool) { return false; },
      builder.GetRegistry())));
  ASSERT_OK_AND_ASSIGN(auto cel_expr, builder.CreateExpression(
                                          &expr.expr(), &expr.source_info()));
  google::protobuf::Arena arena;
  Activation act1;
  ASSERT_OK_AND_ASSIGN(CelValue result, cel_expr->Evaluate(act1, &arena));
  EXPECT_THAT(result, test::IsCelBool(true));
}

TEST(FlatExprBuilderTest, ParsedNamespacedFunctionResolutionOrderReceiverCall) {
  ASSERT_OK_AND_ASSIGN(ParsedExpr expr, parser::Parse("e.Get()"));
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  builder.flat_expr_builder().AddAstTransform(
      NewReferenceResolverExtension(ReferenceResolverOption::kAlways));
  builder.set_container("a.b");
  using FunctionAdapterT = FunctionAdapter<bool>;

  ASSERT_OK(FunctionAdapterT::CreateAndRegister(
      "a.c.d.Get", /*receiver_style=*/false,
      [](google::protobuf::Arena*) { return false; }, builder.GetRegistry()));
  ASSERT_OK(FunctionAdapterT::CreateAndRegister(
      "c.d.Get", /*receiver_style=*/false, [](google::protobuf::Arena*) { return false; },
      builder.GetRegistry()));
  ASSERT_OK((FunctionAdapter<bool, bool>::CreateAndRegister(
      "Get",
      /*receiver_style=*/true, [](google::protobuf::Arena*, bool) { return true; },
      builder.GetRegistry())));
  ASSERT_OK_AND_ASSIGN(auto cel_expr, builder.CreateExpression(
                                          &expr.expr(), &expr.source_info()));
  google::protobuf::Arena arena;
  Activation act1;
  act1.InsertValue("e", CelValue::CreateBool(false));
  ASSERT_OK_AND_ASSIGN(CelValue result, cel_expr->Evaluate(act1, &arena));
  EXPECT_THAT(result, test::IsCelBool(true));
}

TEST(FlatExprBuilderTest, ParsedNamespacedFunctionSupportDisabled) {
  ASSERT_OK_AND_ASSIGN(ParsedExpr expr, parser::Parse("ext.XOr(a, b)"));
  cel::RuntimeOptions options;
  options.fail_on_warnings = false;
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv(), options);
  std::vector<absl::Status> build_warnings;
  builder.set_container("ext");
  using FunctionAdapterT = FunctionAdapter<bool, bool, bool>;

  ASSERT_OK(FunctionAdapterT::CreateAndRegister(
      "ext.XOr", /*receiver_style=*/false,
      [](google::protobuf::Arena*, bool a, bool b) { return a != b; },
      builder.GetRegistry()));
  ASSERT_OK_AND_ASSIGN(
      auto cel_expr, builder.CreateExpression(&expr.expr(), &expr.source_info(),
                                              &build_warnings));
  google::protobuf::Arena arena;
  Activation act1;
  act1.InsertValue("a", CelValue::CreateBool(false));
  act1.InsertValue("b", CelValue::CreateBool(true));

  ASSERT_OK_AND_ASSIGN(CelValue result, cel_expr->Evaluate(act1, &arena));
  EXPECT_THAT(result, test::IsCelError(StatusIs(absl::StatusCode::kUnknown,
                                                HasSubstr("ext"))));
}

TEST(FlatExprBuilderTest, BasicCheckedExprSupport) {
  CheckedExpr expr;
  // foo && bar
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(R"(
    expr {
      id: 1
      call_expr {
        function: "_&&_"
        args {
          id: 2
          ident_expr {
            name: "foo"
          }
        }
        args {
          id: 3
          ident_expr {
            name: "bar"
          }
        }
      }
    })",
                                                  &expr));

  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  ASSERT_THAT(RegisterBuiltinFunctions(builder.GetRegistry()), IsOk());
  ASSERT_OK_AND_ASSIGN(auto cel_expr, builder.CreateExpression(&expr));

  Activation activation;
  activation.InsertValue("foo", CelValue::CreateBool(true));
  activation.InsertValue("bar", CelValue::CreateBool(true));
  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(CelValue result, cel_expr->Evaluate(activation, &arena));
  ASSERT_TRUE(result.IsBool());
  EXPECT_TRUE(result.BoolOrDie());
}

TEST(FlatExprBuilderTest, CheckedExprWithReferenceMap) {
  CheckedExpr expr;
  // `foo.var1` && `bar.var2`
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(R"(
    reference_map {
      key: 2
      value {
        name: "foo.var1"
      }
    }
    reference_map {
      key: 4
      value {
        name: "bar.var2"
      }
    }
    expr {
      id: 1
      call_expr {
        function: "_&&_"
        args {
          id: 2
          select_expr {
            field: "var1"
            operand {
              id: 3
              ident_expr {
                name: "foo"
              }
            }
          }
        }
        args {
          id: 4
          select_expr {
            field: "var2"
            operand {
              ident_expr {
                name: "bar"
              }
            }
          }
        }
      }
    })",
                                                  &expr));

  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  builder.flat_expr_builder().AddAstTransform(
      NewReferenceResolverExtension(ReferenceResolverOption::kCheckedOnly));
  ASSERT_THAT(RegisterBuiltinFunctions(builder.GetRegistry()), IsOk());
  ASSERT_OK_AND_ASSIGN(auto cel_expr, builder.CreateExpression(&expr));

  Activation activation;
  activation.InsertValue("foo.var1", CelValue::CreateBool(true));
  activation.InsertValue("bar.var2", CelValue::CreateBool(true));
  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(CelValue result, cel_expr->Evaluate(activation, &arena));
  ASSERT_TRUE(result.IsBool());
  EXPECT_TRUE(result.BoolOrDie());
}

TEST(FlatExprBuilderTest, CheckedExprWithReferenceMapFunction) {
  CheckedExpr expr;
  // ext.and(var1, bar.var2)
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(R"(
    reference_map {
      key: 1
      value {
        overload_id: "com.foo.ext.and"
      }
    }
    reference_map {
      key: 3
      value {
        name: "com.foo.var1"
      }
    }
    reference_map {
      key: 4
      value {
        name: "bar.var2"
      }
    }
    expr {
      id: 1
      call_expr {
        function: "and"
        target {
          id: 2
          ident_expr {
            name: "ext"
          }
        }
        args {
          id: 3
          ident_expr {
            name: "var1"
          }
        }
        args {
          id: 4
          select_expr {
            field: "var2"
            operand {
              id: 5
              ident_expr {
                name: "bar"
              }
            }
          }
        }
      }
    })",
                                                  &expr));

  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  builder.flat_expr_builder().AddAstTransform(
      NewReferenceResolverExtension(ReferenceResolverOption::kCheckedOnly));
  builder.set_container("com.foo");
  ASSERT_THAT(RegisterBuiltinFunctions(builder.GetRegistry()), IsOk());
  ASSERT_OK((FunctionAdapter<bool, bool, bool>::CreateAndRegister(
      "com.foo.ext.and", false,
      [](google::protobuf::Arena*, bool lhs, bool rhs) { return lhs && rhs; },
      builder.GetRegistry())));
  ASSERT_OK_AND_ASSIGN(auto cel_expr, builder.CreateExpression(&expr));

  Activation activation;
  activation.InsertValue("com.foo.var1", CelValue::CreateBool(true));
  activation.InsertValue("bar.var2", CelValue::CreateBool(true));
  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(CelValue result, cel_expr->Evaluate(activation, &arena));
  ASSERT_TRUE(result.IsBool());
  EXPECT_TRUE(result.BoolOrDie());
}

TEST(FlatExprBuilderTest, CheckedExprActivationMissesReferences) {
  CheckedExpr expr;
  // <foo.var1> && <bar>.<var2>
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(R"(
    reference_map {
      key: 2
      value {
        name: "foo.var1"
      }
    }
    reference_map {
      key: 5
      value {
        name: "bar"
      }
    }
    expr {
      id: 1
      call_expr {
        function: "_&&_"
        args {
          id: 2
          select_expr {
            field: "var1"
            operand {
              id: 3
              ident_expr {
                name: "foo"
              }
            }
          }
        }
        args {
          id: 4
          select_expr {
            field: "var2"
            operand {
              id: 5
              ident_expr {
                name: "bar"
              }
            }
          }
        }
      }
    })",
                                                  &expr));

  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  builder.flat_expr_builder().AddAstTransform(
      NewReferenceResolverExtension(ReferenceResolverOption::kCheckedOnly));
  ASSERT_THAT(RegisterBuiltinFunctions(builder.GetRegistry()), IsOk());
  ASSERT_OK_AND_ASSIGN(auto cel_expr, builder.CreateExpression(&expr));

  Activation activation;
  activation.InsertValue("foo.var1", CelValue::CreateBool(true));
  // Activation tries to bind a namespaced variable but the reference map refers
  // to the container 'bar'.
  activation.InsertValue("bar.var2", CelValue::CreateBool(true));
  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(CelValue result, cel_expr->Evaluate(activation, &arena));
  ASSERT_TRUE(result.IsError());
  EXPECT_THAT(*(result.ErrorOrDie()),
              StatusIs(absl::StatusCode::kUnknown,
                       HasSubstr("No value with name \"bar\" found")));

  // Re-run with the expected interpretation of `bar`.`var2`
  std::vector<std::pair<CelValue, CelValue>> map_pairs{
      {CelValue::CreateStringView("var2"), CelValue::CreateBool(false)}};

  std::unique_ptr<CelMap> map_value =
      *CreateContainerBackedMap(absl::MakeSpan(map_pairs));
  activation.InsertValue("bar", CelValue::CreateMap(map_value.get()));
  ASSERT_OK_AND_ASSIGN(result, cel_expr->Evaluate(activation, &arena));
  ASSERT_TRUE(result.IsBool());
  EXPECT_FALSE(result.BoolOrDie());
}

TEST(FlatExprBuilderTest, CheckedExprWithReferenceMapAndConstantFolding) {
  CheckedExpr expr;
  // {`var1`: 'hello'}
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(R"(
    reference_map {
      key: 3
      value {
        name: "var1"
        value {
          int64_value: 1
        }
      }
    }
    expr {
      id: 1
      struct_expr {
        entries {
          id: 2
          map_key {
            id: 3
            ident_expr {
              name: "var1"
            }
          }
          value {
            id: 4
            const_expr {
              string_value: "hello"
            }
          }
        }
      }
    })",
                                                  &expr));

  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  builder.flat_expr_builder().AddAstTransform(
      NewReferenceResolverExtension(ReferenceResolverOption::kCheckedOnly));
  google::protobuf::Arena arena;
  builder.flat_expr_builder().AddProgramOptimizer(
      cel::runtime_internal::CreateConstantFoldingOptimizer());
  ASSERT_THAT(RegisterBuiltinFunctions(builder.GetRegistry()), IsOk());
  ASSERT_OK_AND_ASSIGN(auto cel_expr, builder.CreateExpression(&expr));

  Activation activation;
  ASSERT_OK_AND_ASSIGN(CelValue result, cel_expr->Evaluate(activation, &arena));
  ASSERT_TRUE(result.IsMap());
  auto m = result.MapOrDie();
  auto v = m->Get(&arena, CelValue::CreateInt64(1L));
  EXPECT_THAT(v->StringOrDie().value(), Eq("hello"));
}

TEST(FlatExprBuilderTest, ComprehensionWorksForError) {
  Expr expr;
  SourceInfo source_info;
  // {}[0].all(x, x) should evaluate OK but return an error value
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(R"(
    id: 4
    comprehension_expr {
      iter_var: "x"
      iter_range {
        id: 2
        call_expr {
          function: "_[_]"
          args {
            id: 1
            struct_expr {
            }
          }
          args {
            id: 3
            const_expr {
              int64_value: 0
            }
          }
        }
      }
      accu_var: "__result__"
      accu_init {
        id: 7
        const_expr {
          bool_value: true
        }
      }
      loop_condition {
        id: 8
        call_expr {
          function: "__not_strictly_false__"
          args {
            id: 9
            ident_expr {
              name: "__result__"
            }
          }
        }
      }
      loop_step {
        id: 10
        call_expr {
          function: "_&&_"
          args {
            id: 11
            ident_expr {
              name: "__result__"
            }
          }
          args {
            id: 6
            ident_expr {
              name: "x"
            }
          }
        }
      }
      result {
        id: 12
        ident_expr {
          name: "__result__"
        }
      }
    })",
                                                  &expr));

  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  ASSERT_THAT(RegisterBuiltinFunctions(builder.GetRegistry()), IsOk());
  ASSERT_OK_AND_ASSIGN(auto cel_expr,
                       builder.CreateExpression(&expr, &source_info));

  Activation activation;
  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(CelValue result, cel_expr->Evaluate(activation, &arena));
  ASSERT_TRUE(result.IsError());
}

TEST(FlatExprBuilderTest, ComprehensionWorksForNonContainer) {
  Expr expr;
  SourceInfo source_info;
  // 0.all(x, x) should evaluate OK but return an error value.
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(R"(
    id: 4
    comprehension_expr {
      iter_var: "x"
      iter_range {
        id: 2
        const_expr {
          int64_value: 0
        }
      }
      accu_var: "__result__"
      accu_init {
        id: 7
        const_expr {
          bool_value: true
        }
      }
      loop_condition {
        id: 8
        call_expr {
          function: "__not_strictly_false__"
          args {
            id: 9
            ident_expr {
              name: "__result__"
            }
          }
        }
      }
      loop_step {
        id: 10
        call_expr {
          function: "_&&_"
          args {
            id: 11
            ident_expr {
              name: "__result__"
            }
          }
          args {
            id: 6
            ident_expr {
              name: "x"
            }
          }
        }
      }
      result {
        id: 12
        ident_expr {
          name: "__result__"
        }
      }
    })",
                                                  &expr));

  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  ASSERT_THAT(RegisterBuiltinFunctions(builder.GetRegistry()), IsOk());
  ASSERT_OK_AND_ASSIGN(auto cel_expr,
                       builder.CreateExpression(&expr, &source_info));

  Activation activation;
  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(CelValue result, cel_expr->Evaluate(activation, &arena));
  ASSERT_TRUE(result.IsError());
  EXPECT_THAT(result.ErrorOrDie()->message(),
              Eq("No matching overloads found : <iter_range>"));
}

TEST(FlatExprBuilderTest, ComprehensionBudget) {
  Expr expr;
  SourceInfo source_info;
  // [1, 2].all(x, x > 0)
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(R"(
    comprehension_expr {
      iter_var: "k"
      accu_var: "accu"
      accu_init {
        const_expr { bool_value: true }
      }
      loop_condition { ident_expr { name: "accu" } }
      result { ident_expr { name: "accu" } }
      loop_step {
        call_expr {
          function: "_&&_"
          args {
            ident_expr { name: "accu" }
          }
          args {
            call_expr {
              function: "_>_"
              args { ident_expr { name: "k" } }
              args { const_expr { int64_value: 0 } }
            }
          }
        }
      }
      iter_range {
        list_expr {
          elements { const_expr { int64_value: 1 } }
          elements { const_expr { int64_value: 2 } }
        }
      }
    })",
                                                  &expr));

  cel::RuntimeOptions options;
  options.comprehension_max_iterations = 1;
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv(), options);
  ASSERT_THAT(RegisterBuiltinFunctions(builder.GetRegistry()), IsOk());
  ASSERT_OK_AND_ASSIGN(auto cel_expr,
                       builder.CreateExpression(&expr, &source_info));

  Activation activation;
  google::protobuf::Arena arena;
  EXPECT_THAT(cel_expr->Evaluate(activation, &arena).status(),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Iteration budget exceeded")));
}

TEST(FlatExprBuilderTest, SimpleEnumTest) {
  TestMessage message;
  Expr expr;
  SourceInfo source_info;
  constexpr char enum_name[] =
      "google.api.expr.runtime.TestMessage.TestEnum.TEST_ENUM_1";

  std::vector<std::string> enum_name_parts = absl::StrSplit(enum_name, '.');
  Expr* cur_expr = &expr;

  for (int i = enum_name_parts.size() - 1; i > 0; i--) {
    auto select_expr = cur_expr->mutable_select_expr();
    select_expr->set_field(enum_name_parts[i]);
    cur_expr = select_expr->mutable_operand();
  }

  cur_expr->mutable_ident_expr()->set_name(enum_name_parts[0]);

  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  builder.GetTypeRegistry()->Register(TestMessage::TestEnum_descriptor());
  ASSERT_OK_AND_ASSIGN(auto cel_expr,
                       builder.CreateExpression(&expr, &source_info));

  google::protobuf::Arena arena;
  Activation activation;
  ASSERT_OK_AND_ASSIGN(CelValue result, cel_expr->Evaluate(activation, &arena));
  ASSERT_TRUE(result.IsInt64());
  EXPECT_THAT(result.Int64OrDie(), Eq(TestMessage::TEST_ENUM_1));
}

TEST(FlatExprBuilderTest, SimpleEnumIdentTest) {
  TestMessage message;
  Expr expr;
  SourceInfo source_info;
  constexpr char enum_name[] =
      "google.api.expr.runtime.TestMessage.TestEnum.TEST_ENUM_1";

  Expr* cur_expr = &expr;
  cur_expr->mutable_ident_expr()->set_name(enum_name);

  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  builder.GetTypeRegistry()->Register(TestMessage::TestEnum_descriptor());
  ASSERT_OK_AND_ASSIGN(auto cel_expr,
                       builder.CreateExpression(&expr, &source_info));

  google::protobuf::Arena arena;
  Activation activation;
  ASSERT_OK_AND_ASSIGN(CelValue result, cel_expr->Evaluate(activation, &arena));
  ASSERT_TRUE(result.IsInt64());
  EXPECT_THAT(result.Int64OrDie(), Eq(TestMessage::TEST_ENUM_1));
}

TEST(FlatExprBuilderTest, ContainerStringFormat) {
  Expr expr;
  SourceInfo source_info;
  expr.mutable_ident_expr()->set_name("ident");

  {
    CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
    builder.set_container("");
    ASSERT_THAT(builder.CreateExpression(&expr, &source_info), IsOk());
  }

  {
    CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
    builder.set_container("random.namespace");
    ASSERT_THAT(builder.CreateExpression(&expr, &source_info), IsOk());
  }
  {
    CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
    // Leading '.'
    builder.set_container(".random.namespace");
    EXPECT_THAT(builder.CreateExpression(&expr, &source_info).status(),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("Invalid expression container")));
  }
  {
    CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
    // Trailing '.'
    builder.set_container("random.namespace.");
    EXPECT_THAT(builder.CreateExpression(&expr, &source_info).status(),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("Invalid expression container")));
  }
}

// Builder with google.api.expr.runtime.TestMessage and TestEnum types
// linked in and the standard functions registered.
CelExpressionBuilderFlatImpl BuilderForNameResolutionTest(
    absl::string_view container) {
  cel::RuntimeOptions options;
  options.enable_qualified_type_identifiers = true;

  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv(), options);
  builder.GetTypeRegistry()->Register(TestMessage::TestEnum_descriptor());
  builder.GetTypeRegistry()->Register(TestEnum_descriptor());
  builder.set_container(std::string(container));
  ABSL_CHECK_OK(cel::RegisterStandardFunctions(
      builder.GetRegistry()->InternalGetRegistry(), options));
  return builder;
}

TEST(FlatExprBuilderTest, ShortEnumResolution) {
  google::protobuf::Arena arena;
  CelExpressionBuilderFlatImpl builder =
      BuilderForNameResolutionTest("google.api.expr.runtime.TestMessage");

  ASSERT_OK_AND_ASSIGN(ParsedExpr expr,
                       parser::Parse("TestMessage.TestEnum.TEST_ENUM_1"));
  ASSERT_OK_AND_ASSIGN(auto cel_expr, builder.CreateExpression(
                                          &expr.expr(), &expr.source_info()));

  Activation activation;

  ASSERT_OK_AND_ASSIGN(CelValue result, cel_expr->Evaluate(activation, &arena));

  ASSERT_TRUE(result.IsInt64());
  EXPECT_THAT(result.Int64OrDie(), Eq(TestMessage::TEST_ENUM_1));
}

TEST(FlatExprBuilderTest, EnumResolutionHonorsLeadingDot) {
  google::protobuf::Arena arena;
  CelExpressionBuilderFlatImpl builder =
      BuilderForNameResolutionTest("google.api.expr.runtime");

  // Leading dot disables container resolution.
  ASSERT_OK_AND_ASSIGN(ParsedExpr expr,
                       parser::Parse(".TestMessage.TestEnum.TEST_ENUM_1"));
  ASSERT_OK_AND_ASSIGN(auto cel_expr, builder.CreateExpression(
                                          &expr.expr(), &expr.source_info()));

  Activation activation;
  ASSERT_OK_AND_ASSIGN(CelValue result, cel_expr->Evaluate(activation, &arena));
  ASSERT_TRUE(result.IsError());
  EXPECT_THAT(
      result.ErrorOrDie()->message(),
      HasSubstr("No value with name \"TestMessage\" found in Activation"));
}

TEST(FlatExprBuilderTest, EnumResolutionComprehensionShadowing) {
  google::protobuf::Arena arena;
  CelExpressionBuilderFlatImpl builder =
      BuilderForNameResolutionTest("google.api.expr.runtime");

  // Prefer the interpretation that it's a comprehension var if there's a
  // collision.
  ASSERT_OK_AND_ASSIGN(
      ParsedExpr expr,
      parser::Parse("[{'TestEnum': {'TEST_ENUM_1': 42}}].map(TestMessage, "
                    "TestMessage.TestEnum.TEST_ENUM_1)[0] == 42"));
  ASSERT_OK_AND_ASSIGN(auto cel_expr, builder.CreateExpression(
                                          &expr.expr(), &expr.source_info()));

  Activation activation;
  ASSERT_OK_AND_ASSIGN(CelValue result, cel_expr->Evaluate(activation, &arena));
  ASSERT_TRUE(result.IsBool());
  EXPECT_TRUE(result.BoolOrDie());
}

TEST(FlatExprBuilderTest, EnumResolutionComprehensionShadowingLeadingDot) {
  google::protobuf::Arena arena;
  CelExpressionBuilderFlatImpl builder =
      BuilderForNameResolutionTest("google.api.expr.runtime");

  // Prefer the interpretation that it's a comprehension var if there's a
  // collision.
  ASSERT_OK_AND_ASSIGN(
      ParsedExpr expr,
      parser::Parse("[0].map(google, "
                    ".google.api.expr.runtime.TestMessage.TestEnum.TEST_ENUM_1)"
                    "[0] == TestMessage.TestEnum.TEST_ENUM_1"));
  ASSERT_OK_AND_ASSIGN(auto cel_expr, builder.CreateExpression(
                                          &expr.expr(), &expr.source_info()));

  Activation activation;
  ASSERT_OK_AND_ASSIGN(CelValue result, cel_expr->Evaluate(activation, &arena));
  ASSERT_TRUE(result.IsBool());
  EXPECT_TRUE(result.BoolOrDie());
}

TEST(FlatExprBuilderTest, FullEnumNameWithContainerResolution) {
  google::protobuf::Arena arena;
  CelExpressionBuilderFlatImpl builder =
      BuilderForNameResolutionTest("very.random.Namespace");

  // Fully qualified name should work.
  ASSERT_OK_AND_ASSIGN(
      ParsedExpr expr,
      parser::Parse(
          "google.api.expr.runtime.TestMessage.TestEnum.TEST_ENUM_1"));
  ASSERT_OK_AND_ASSIGN(auto cel_expr, builder.CreateExpression(
                                          &expr.expr(), &expr.source_info()));

  Activation activation;
  ASSERT_OK_AND_ASSIGN(CelValue result, cel_expr->Evaluate(activation, &arena));
  ASSERT_TRUE(result.IsInt64());
  EXPECT_THAT(result.Int64OrDie(), Eq(TestMessage::TEST_ENUM_1));
}

TEST(FlatExprBuilderTest, SameShortNameEnumResolution) {
  google::protobuf::Arena arena;

  // This precondition validates that
  // TestMessage::TestEnum::TEST_ENUM1 and TestEnum::TEST_ENUM1 are compiled and
  // linked in and their values are different.
  ASSERT_TRUE(static_cast<int>(TestEnum::TEST_ENUM_1) !=
              static_cast<int>(TestMessage::TEST_ENUM_1));

  {
    CelExpressionBuilderFlatImpl builder =
        BuilderForNameResolutionTest("google.api.expr.runtime.TestMessage");
    ASSERT_OK_AND_ASSIGN(ParsedExpr expr,
                         parser::Parse("TestEnum.TEST_ENUM_1"));
    ASSERT_OK_AND_ASSIGN(auto cel_expr, builder.CreateExpression(
                                            &expr.expr(), &expr.source_info()));
    Activation activation;
    ASSERT_OK_AND_ASSIGN(CelValue result,
                         cel_expr->Evaluate(activation, &arena));
    ASSERT_TRUE(result.IsInt64());
    EXPECT_THAT(result.Int64OrDie(), Eq(TestMessage::TEST_ENUM_1));
  }

  // TEST_ENUM3 is present in google.api.expr.runtime.TestEnum, is absent in
  // google.api.expr.runtime.TestMessage.TestEnum.
  {
    CelExpressionBuilderFlatImpl builder =
        BuilderForNameResolutionTest("google.api.expr.runtime.TestMessage");
    ASSERT_OK_AND_ASSIGN(ParsedExpr expr,
                         parser::Parse("TestEnum.TEST_ENUM_3"));
    ASSERT_OK_AND_ASSIGN(auto cel_expr, builder.CreateExpression(
                                            &expr.expr(), &expr.source_info()));
    Activation activation;
    ASSERT_OK_AND_ASSIGN(CelValue result,
                         cel_expr->Evaluate(activation, &arena));
    ASSERT_TRUE(result.IsInt64());
    EXPECT_THAT(result.Int64OrDie(), Eq(TestEnum::TEST_ENUM_3));
  }

  {
    CelExpressionBuilderFlatImpl builder =
        BuilderForNameResolutionTest("google.api.expr.runtime");
    ASSERT_OK_AND_ASSIGN(ParsedExpr expr,
                         parser::Parse("TestEnum.TEST_ENUM_1"));
    ASSERT_OK_AND_ASSIGN(auto cel_expr, builder.CreateExpression(
                                            &expr.expr(), &expr.source_info()));
    Activation activation;
    ASSERT_OK_AND_ASSIGN(CelValue result,
                         cel_expr->Evaluate(activation, &arena));
    ASSERT_TRUE(result.IsInt64());
    EXPECT_THAT(result.Int64OrDie(), Eq(TestEnum::TEST_ENUM_1));
  }
}

TEST(FlatExprBuilderTest, PartialQualifiedEnumResolution) {
  google::protobuf::Arena arena;
  CelExpressionBuilderFlatImpl builder =
      BuilderForNameResolutionTest("google.api.expr");

  ASSERT_OK_AND_ASSIGN(
      ParsedExpr expr,
      parser::Parse("runtime.TestMessage.TestEnum.TEST_ENUM_1"));
  ASSERT_OK_AND_ASSIGN(auto cel_expr, builder.CreateExpression(
                                          &expr.expr(), &expr.source_info()));

  Activation activation;
  ASSERT_OK_AND_ASSIGN(CelValue result, cel_expr->Evaluate(activation, &arena));

  ASSERT_TRUE(result.IsInt64());
  EXPECT_THAT(result.Int64OrDie(), Eq(TestMessage::TEST_ENUM_1));
}

TEST(FlatExprBuilderTest, NameCollisionWithComprehensionVar) {
  google::protobuf::Arena arena;
  CelExpressionBuilderFlatImpl builder = BuilderForNameResolutionTest("google");

  ASSERT_OK_AND_ASSIGN(ParsedExpr expr, parser::Parse("[0].map(x, x)[0]"));
  ASSERT_OK_AND_ASSIGN(auto cel_expr, builder.CreateExpression(
                                          &expr.expr(), &expr.source_info()));

  Activation activation;
  activation.InsertValue("x", CelValue::CreateInt64(1));
  ASSERT_OK_AND_ASSIGN(CelValue result, cel_expr->Evaluate(activation, &arena));

  ASSERT_TRUE(result.IsInt64());
  EXPECT_THAT(result.Int64OrDie(), Eq(0));
}

TEST(FlatExprBuilderTest, NameCollisionWithComprehensionVarLeadingDot) {
  google::protobuf::Arena arena;
  CelExpressionBuilderFlatImpl builder = BuilderForNameResolutionTest("google");

  ASSERT_OK_AND_ASSIGN(ParsedExpr expr, parser::Parse("[0].map(x, .x)[0]"));
  ASSERT_OK_AND_ASSIGN(auto cel_expr, builder.CreateExpression(
                                          &expr.expr(), &expr.source_info()));

  Activation activation;
  activation.InsertValue("x", CelValue::CreateInt64(1));
  ASSERT_OK_AND_ASSIGN(CelValue result, cel_expr->Evaluate(activation, &arena));

  ASSERT_TRUE(result.IsInt64());
  EXPECT_THAT(result.Int64OrDie(), Eq(1));
}

TEST(FlatExprBuilderTest, MapFieldPresence) {
  Expr expr;
  SourceInfo source_info;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(R"(
    id: 1,
    select_expr{
      operand {
        id: 2
        ident_expr{ name: "msg" }
      }
      field: "string_int32_map"
      test_only: true
    })",
                                                  &expr));

  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  ASSERT_OK_AND_ASSIGN(auto cel_expr,
                       builder.CreateExpression(&expr, &source_info));

  google::protobuf::Arena arena;
  {
    TestMessage message;
    auto strMap = message.mutable_string_int32_map();
    strMap->insert({"key", 1});
    Activation activation;
    activation.InsertValue("msg",
                           CelProtoWrapper::CreateMessage(&message, &arena));
    ASSERT_OK_AND_ASSIGN(CelValue result,
                         cel_expr->Evaluate(activation, &arena));
    ASSERT_TRUE(result.IsBool());
    ASSERT_TRUE(result.BoolOrDie());
  }
  {
    TestMessage message;
    Activation activation;
    activation.InsertValue("msg",
                           CelProtoWrapper::CreateMessage(&message, &arena));
    ASSERT_OK_AND_ASSIGN(CelValue result,
                         cel_expr->Evaluate(activation, &arena));
    ASSERT_TRUE(result.IsBool());
    ASSERT_FALSE(result.BoolOrDie());
  }
}

TEST(FlatExprBuilderTest, RepeatedFieldPresence) {
  Expr expr;
  SourceInfo source_info;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(R"(
    id: 1,
    select_expr{
      operand {
        id: 2
        ident_expr{ name: "msg" }
      }
      field: "int32_list"
      test_only: true
    })",
                                                  &expr));

  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  ASSERT_OK_AND_ASSIGN(auto cel_expr,
                       builder.CreateExpression(&expr, &source_info));

  google::protobuf::Arena arena;
  {
    TestMessage message;
    message.add_int32_list(1);
    Activation activation;
    activation.InsertValue("msg",
                           CelProtoWrapper::CreateMessage(&message, &arena));
    ASSERT_OK_AND_ASSIGN(CelValue result,
                         cel_expr->Evaluate(activation, &arena));
    ASSERT_TRUE(result.IsBool());
    ASSERT_TRUE(result.BoolOrDie());
  }
  {
    TestMessage message;
    Activation activation;
    activation.InsertValue("msg",
                           CelProtoWrapper::CreateMessage(&message, &arena));
    ASSERT_OK_AND_ASSIGN(CelValue result,
                         cel_expr->Evaluate(activation, &arena));
    ASSERT_TRUE(result.IsBool());
    ASSERT_FALSE(result.BoolOrDie());
  }
}

absl::Status RunTernaryExpression(CelValue selector, CelValue value1,
                                  CelValue value2, google::protobuf::Arena* arena,
                                  CelValue* result) {
  Expr expr;
  SourceInfo source_info;
  auto call_expr = expr.mutable_call_expr();
  call_expr->set_function(cel::builtin::kTernary);

  auto arg0 = call_expr->add_args();
  arg0->mutable_ident_expr()->set_name("selector");
  auto arg1 = call_expr->add_args();
  arg1->mutable_ident_expr()->set_name("value1");
  auto arg2 = call_expr->add_args();
  arg2->mutable_ident_expr()->set_name("value2");

  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  CEL_ASSIGN_OR_RETURN(auto cel_expr,
                       builder.CreateExpression(&expr, &source_info));

  std::string variable = "test";

  Activation activation;
  activation.InsertValue("selector", selector);
  activation.InsertValue("value1", value1);
  activation.InsertValue("value2", value2);

  CEL_ASSIGN_OR_RETURN(auto eval, cel_expr->Evaluate(activation, arena));
  *result = eval;
  return absl::OkStatus();
}

TEST(FlatExprBuilderTest, Ternary) {
  Expr expr;
  SourceInfo source_info;
  auto call_expr = expr.mutable_call_expr();
  call_expr->set_function(cel::builtin::kTernary);

  auto arg0 = call_expr->add_args();
  arg0->mutable_ident_expr()->set_name("selector");
  auto arg1 = call_expr->add_args();
  arg1->mutable_ident_expr()->set_name("value1");
  auto arg2 = call_expr->add_args();
  arg2->mutable_ident_expr()->set_name("value1");

  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  ASSERT_OK_AND_ASSIGN(auto cel_expr,
                       builder.CreateExpression(&expr, &source_info));

  google::protobuf::Arena arena;

  // On True, value 1
  {
    CelValue result;
    ASSERT_THAT(RunTernaryExpression(CelValue::CreateBool(true),
                                     CelValue::CreateInt64(1),
                                     CelValue::CreateInt64(2), &arena, &result),
                IsOk());
    ASSERT_TRUE(result.IsInt64());
    EXPECT_THAT(result.Int64OrDie(), Eq(1));

    // Unknown handling
    UnknownSet unknown_set;
    ASSERT_THAT(RunTernaryExpression(CelValue::CreateBool(true),
                                     CelValue::CreateUnknownSet(&unknown_set),
                                     CelValue::CreateInt64(2), &arena, &result),
                IsOk());
    ASSERT_TRUE(result.IsUnknownSet());

    ASSERT_THAT(RunTernaryExpression(
                    CelValue::CreateBool(true), CelValue::CreateInt64(1),
                    CelValue::CreateUnknownSet(&unknown_set), &arena, &result),
                IsOk());
    ASSERT_TRUE(result.IsInt64());
    EXPECT_THAT(result.Int64OrDie(), Eq(1));
  }

  // On False, value 2
  {
    CelValue result;
    ASSERT_THAT(RunTernaryExpression(CelValue::CreateBool(false),
                                     CelValue::CreateInt64(1),
                                     CelValue::CreateInt64(2), &arena, &result),
                IsOk());
    ASSERT_TRUE(result.IsInt64());
    EXPECT_THAT(result.Int64OrDie(), Eq(2));

    // Unknown handling
    UnknownSet unknown_set;
    ASSERT_THAT(RunTernaryExpression(CelValue::CreateBool(false),
                                     CelValue::CreateUnknownSet(&unknown_set),
                                     CelValue::CreateInt64(2), &arena, &result),
                IsOk());
    ASSERT_TRUE(result.IsInt64());
    EXPECT_THAT(result.Int64OrDie(), Eq(2));

    ASSERT_THAT(RunTernaryExpression(
                    CelValue::CreateBool(false), CelValue::CreateInt64(1),
                    CelValue::CreateUnknownSet(&unknown_set), &arena, &result),
                IsOk());
    ASSERT_TRUE(result.IsUnknownSet());
  }
  // On Error, surface error
  {
    CelValue result;
    ASSERT_THAT(RunTernaryExpression(CreateErrorValue(&arena, "error"),
                                     CelValue::CreateInt64(1),
                                     CelValue::CreateInt64(2), &arena, &result),
                IsOk());
    ASSERT_TRUE(result.IsError());
  }
  // On Unknown, surface Unknown
  {
    UnknownSet unknown_set;
    CelValue result;
    ASSERT_THAT(RunTernaryExpression(CelValue::CreateUnknownSet(&unknown_set),
                                     CelValue::CreateInt64(1),
                                     CelValue::CreateInt64(2), &arena, &result),
                IsOk());
    ASSERT_TRUE(result.IsUnknownSet());
    EXPECT_THAT(unknown_set, Eq(*result.UnknownSetOrDie()));
  }
  // We should not merge unknowns
  {
    CelAttribute selector_attr("selector", {});

    CelAttribute value1_attr("value1", {});

    CelAttribute value2_attr("value2", {});

    UnknownSet unknown_selector(UnknownAttributeSet({selector_attr}));
    UnknownSet unknown_value1(UnknownAttributeSet({value1_attr}));
    UnknownSet unknown_value2(UnknownAttributeSet({value2_attr}));
    CelValue result;
    ASSERT_THAT(
        RunTernaryExpression(CelValue::CreateUnknownSet(&unknown_selector),
                             CelValue::CreateUnknownSet(&unknown_value1),
                             CelValue::CreateUnknownSet(&unknown_value2),
                             &arena, &result),
        IsOk());
    ASSERT_TRUE(result.IsUnknownSet());
    const UnknownSet* result_set = result.UnknownSetOrDie();
    EXPECT_THAT(result_set->unknown_attributes().size(), Eq(1));
    EXPECT_THAT(result_set->unknown_attributes().begin()->variable_name(),
                Eq("selector"));
  }
}

TEST(FlatExprBuilderTest, EmptyCallList) {
  std::vector<std::string> operators = {"_&&_", "_||_", "_?_:_"};
  for (const auto& op : operators) {
    Expr expr;
    SourceInfo source_info;
    auto call_expr = expr.mutable_call_expr();
    call_expr->set_function(op);
    CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
    ASSERT_THAT(RegisterBuiltinFunctions(builder.GetRegistry()), IsOk());
    auto build = builder.CreateExpression(&expr, &source_info);
    ASSERT_FALSE(build.ok());
  }
}

// Note: this should not be allowed by default, but updating is a breaking
// change.
TEST(FlatExprBuilderTest, HeterogeneousListsAllowed) {
  ASSERT_OK_AND_ASSIGN(ParsedExpr parsed_expr,
                       parser::Parse("[17, 'seventeen']"));

  cel::RuntimeOptions options;
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv(), options);

  ASSERT_OK_AND_ASSIGN(auto expression,
                       builder.CreateExpression(&parsed_expr.expr(),
                                                &parsed_expr.source_info()));

  Activation activation;
  google::protobuf::Arena arena;

  ASSERT_OK_AND_ASSIGN(CelValue result,
                       expression->Evaluate(activation, &arena));

  ASSERT_TRUE(result.IsList()) << result.DebugString();

  const auto& list = *result.ListOrDie();
  ASSERT_EQ(list.size(), 2);

  CelValue elem0 = list.Get(&arena, 0);
  CelValue elem1 = list.Get(&arena, 1);

  EXPECT_THAT(elem0, test::IsCelInt64(17));
  EXPECT_THAT(elem1, test::IsCelString("seventeen"));
}

TEST(FlatExprBuilderTest, NullUnboxingEnabled) {
  TestMessage message;
  ASSERT_OK_AND_ASSIGN(ParsedExpr parsed_expr,
                       parser::Parse("message.int32_wrapper_value"));
  cel::RuntimeOptions options;
  options.enable_empty_wrapper_null_unboxing = true;
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv(), options);
  ASSERT_OK_AND_ASSIGN(auto expression,
                       builder.CreateExpression(&parsed_expr.expr(),
                                                &parsed_expr.source_info()));

  Activation activation;
  google::protobuf::Arena arena;
  activation.InsertValue("message",
                         CelProtoWrapper::CreateMessage(&message, &arena));
  ASSERT_OK_AND_ASSIGN(CelValue result,
                       expression->Evaluate(activation, &arena));

  EXPECT_TRUE(result.IsNull());
}

TEST(FlatExprBuilderTest, TypeResolve) {
  TestMessage message;
  ASSERT_OK_AND_ASSIGN(ParsedExpr parsed_expr,
                       parser::Parse("type(message) == runtime.TestMessage"));
  cel::RuntimeOptions options;
  options.enable_qualified_type_identifiers = true;
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv(), options);
  builder.set_container("google.api.expr");
  ASSERT_THAT(RegisterBuiltinFunctions(builder.GetRegistry()), IsOk());
  ASSERT_OK_AND_ASSIGN(auto expression,
                       builder.CreateExpression(&parsed_expr.expr(),
                                                &parsed_expr.source_info()));

  Activation activation;
  google::protobuf::Arena arena;
  activation.InsertValue("message",
                         CelProtoWrapper::CreateMessage(&message, &arena));
  ASSERT_OK_AND_ASSIGN(CelValue result,
                       expression->Evaluate(activation, &arena));

  ASSERT_TRUE(result.IsBool()) << result.DebugString();
  EXPECT_TRUE(result.BoolOrDie());
}

TEST(FlatExprBuilderTest, FastEquality) {
  TestMessage message;
  ASSERT_OK_AND_ASSIGN(ParsedExpr parsed_expr, parser::Parse("'foo' == 'bar'"));
  cel::RuntimeOptions options;
  options.enable_fast_builtins = true;
  InterpreterOptions legacy_options;
  legacy_options.enable_fast_builtins = true;
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv(), options);
  ASSERT_THAT(RegisterBuiltinFunctions(builder.GetRegistry(), legacy_options),
              IsOk());
  ASSERT_OK_AND_ASSIGN(auto expression,
                       builder.CreateExpression(&parsed_expr.expr(),
                                                &parsed_expr.source_info()));

  Activation activation;
  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(CelValue result,
                       expression->Evaluate(activation, &arena));

  ASSERT_TRUE(result.IsBool()) << result.DebugString();
  EXPECT_FALSE(result.BoolOrDie());
}

TEST(FlatExprBuilderTest, FastEqualityFiltersBadCalls) {
  ASSERT_OK_AND_ASSIGN(ParsedExpr parsed_expr, parser::Parse("'foo' == 'bar'"));
  parsed_expr.mutable_expr()
      ->mutable_call_expr()
      ->mutable_target()
      ->mutable_const_expr()
      ->set_string_value("foo");
  cel::RuntimeOptions options;
  options.enable_fast_builtins = true;
  InterpreterOptions legacy_options;
  legacy_options.enable_fast_builtins = true;
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv(), options);
  ASSERT_THAT(RegisterBuiltinFunctions(builder.GetRegistry(), legacy_options),
              IsOk());
  ASSERT_THAT(
      builder.CreateExpression(&parsed_expr.expr(), &parsed_expr.source_info()),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr(
                   "unexpected number of args for builtin equality operator")));
}

TEST(FlatExprBuilderTest, FastInequalityFiltersBadCalls) {
  ASSERT_OK_AND_ASSIGN(ParsedExpr parsed_expr, parser::Parse("'foo' != 'bar'"));
  parsed_expr.mutable_expr()
      ->mutable_call_expr()
      ->mutable_target()
      ->mutable_const_expr()
      ->set_string_value("foo");
  cel::RuntimeOptions options;
  options.enable_fast_builtins = true;
  InterpreterOptions legacy_options;
  legacy_options.enable_fast_builtins = true;
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv(), options);
  ASSERT_THAT(RegisterBuiltinFunctions(builder.GetRegistry(), legacy_options),
              IsOk());
  ASSERT_THAT(
      builder.CreateExpression(&parsed_expr.expr(), &parsed_expr.source_info()),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr(
                   "unexpected number of args for builtin equality operator")));
}

TEST(FlatExprBuilderTest, FastInFiltersBadCalls) {
  ASSERT_OK_AND_ASSIGN(ParsedExpr parsed_expr, parser::Parse("a in b"));
  parsed_expr.mutable_expr()
      ->mutable_call_expr()
      ->mutable_target()
      ->mutable_const_expr()
      ->set_string_value("foo");
  cel::RuntimeOptions options;
  options.enable_fast_builtins = true;
  InterpreterOptions legacy_options;
  legacy_options.enable_fast_builtins = true;
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv(), options);
  ASSERT_THAT(RegisterBuiltinFunctions(builder.GetRegistry(), legacy_options),
              IsOk());
  ASSERT_THAT(
      builder.CreateExpression(&parsed_expr.expr(), &parsed_expr.source_info()),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr("unexpected number of args for builtin 'in' operator")));
}

TEST(FlatExprBuilderTest, IndexFiltersBadCalls) {
  ASSERT_OK_AND_ASSIGN(ParsedExpr parsed_expr, parser::Parse("a[b]"));
  parsed_expr.mutable_expr()
      ->mutable_call_expr()
      ->mutable_target()
      ->mutable_const_expr()
      ->set_string_value("foo");
  cel::RuntimeOptions options;
  options.enable_fast_builtins = true;
  InterpreterOptions legacy_options;
  legacy_options.enable_fast_builtins = true;
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv(), options);
  ASSERT_THAT(RegisterBuiltinFunctions(builder.GetRegistry(), legacy_options),
              IsOk());
  ASSERT_THAT(
      builder.CreateExpression(&parsed_expr.expr(), &parsed_expr.source_info()),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr("unexpected number of args for builtin index operator")));
}

// TODO(uncreated-issue/79): temporarily allow index operator with a target.
TEST(FlatExprBuilderTest, IndexWithTarget) {
  ASSERT_OK_AND_ASSIGN(ParsedExpr parsed_expr, parser::Parse("a[b]"));
  parsed_expr.mutable_expr()
      ->mutable_call_expr()
      ->mutable_target()
      ->mutable_ident_expr()
      ->set_name("a");
  parsed_expr.mutable_expr()
      ->mutable_call_expr()
      ->mutable_args()
      ->DeleteSubrange(0, 1);

  cel::RuntimeOptions options;
  options.enable_fast_builtins = true;
  InterpreterOptions legacy_options;
  legacy_options.enable_fast_builtins = true;
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv(), options);
  ASSERT_THAT(RegisterBuiltinFunctions(builder.GetRegistry(), legacy_options),
              IsOk());
  ASSERT_THAT(
      builder.CreateExpression(&parsed_expr.expr(), &parsed_expr.source_info()),
      IsOk());
}

TEST(FlatExprBuilderTest, NotFiltersBadCalls) {
  ASSERT_OK_AND_ASSIGN(ParsedExpr parsed_expr, parser::Parse("!a"));
  parsed_expr.mutable_expr()
      ->mutable_call_expr()
      ->mutable_target()
      ->mutable_const_expr()
      ->set_string_value("foo");
  cel::RuntimeOptions options;
  options.enable_fast_builtins = true;
  InterpreterOptions legacy_options;
  legacy_options.enable_fast_builtins = true;
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv(), options);
  ASSERT_THAT(RegisterBuiltinFunctions(builder.GetRegistry(), legacy_options),
              IsOk());
  ASSERT_THAT(
      builder.CreateExpression(&parsed_expr.expr(), &parsed_expr.source_info()),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr("unexpected number of args for builtin not operator")));
}

TEST(FlatExprBuilderTest, NotStrictlyFalseFiltersBadCalls) {
  ASSERT_OK_AND_ASSIGN(ParsedExpr parsed_expr, parser::Parse("!a"));
  auto* call = parsed_expr.mutable_expr()->mutable_call_expr();
  call->mutable_target()->mutable_const_expr()->set_string_value("foo");
  call->set_function("@not_strictly_false");
  cel::RuntimeOptions options;
  options.enable_fast_builtins = true;
  InterpreterOptions legacy_options;
  legacy_options.enable_fast_builtins = true;
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv(), options);
  ASSERT_THAT(RegisterBuiltinFunctions(builder.GetRegistry(), legacy_options),
              IsOk());
  ASSERT_THAT(
      builder.CreateExpression(&parsed_expr.expr(), &parsed_expr.source_info()),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("unexpected number of args for builtin "
                         "not_strictly_false operator")));
}

TEST(FlatExprBuilderTest, FastEqualityDisabledWithCustomEquality) {
  TestMessage message;
  ASSERT_OK_AND_ASSIGN(ParsedExpr parsed_expr, parser::Parse("1 == b'\001'"));
  cel::RuntimeOptions options;
  options.enable_fast_builtins = true;
  InterpreterOptions legacy_options;
  legacy_options.enable_fast_builtins = true;
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv(), options);
  ASSERT_THAT(RegisterBuiltinFunctions(builder.GetRegistry(), legacy_options),
              IsOk());

  auto& registry = builder.GetRegistry()->InternalGetRegistry();

  auto status = cel::BinaryFunctionAdapter<bool, int64_t, const BytesValue&>::
      RegisterGlobalOverload(
          "_==_",
          [](int64_t lhs, const cel::BytesValue& rhs) -> bool { return true; },
          registry);
  ASSERT_THAT(status, IsOk());

  ASSERT_OK_AND_ASSIGN(auto expression,
                       builder.CreateExpression(&parsed_expr.expr(),
                                                &parsed_expr.source_info()));

  Activation activation;
  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(CelValue result,
                       expression->Evaluate(activation, &arena));

  ASSERT_TRUE(result.IsBool()) << result.DebugString();
  EXPECT_TRUE(result.BoolOrDie());
}

TEST(FlatExprBuilderTest, AnyPackingList) {
  google::protobuf::LinkMessageReflection<TestAllTypes>();
  ASSERT_OK_AND_ASSIGN(ParsedExpr parsed_expr,
                       parser::Parse("TestAllTypes{single_any: [1, 2, 3]}"));

  cel::RuntimeOptions options;
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv(), options);
  builder.set_container("cel.expr.conformance.proto3");

  ASSERT_OK_AND_ASSIGN(auto expression,
                       builder.CreateExpression(&parsed_expr.expr(),
                                                &parsed_expr.source_info()));

  Activation activation;
  google::protobuf::Arena arena;

  ASSERT_OK_AND_ASSIGN(CelValue result,
                       expression->Evaluate(activation, &arena));

  EXPECT_THAT(result,
              test::IsCelMessage(EqualsProto(
                  R"pb(single_any {
                         [type.googleapis.com/google.protobuf.ListValue] {
                           values { number_value: 1 }
                           values { number_value: 2 }
                           values { number_value: 3 }
                         }
                       })pb")))
      << result.DebugString();
}

TEST(FlatExprBuilderTest, AnyPackingNestedNumbers) {
  google::protobuf::LinkMessageReflection<TestAllTypes>();
  ASSERT_OK_AND_ASSIGN(ParsedExpr parsed_expr,
                       parser::Parse("TestAllTypes{single_any: [1, 2.3]}"));

  cel::RuntimeOptions options;
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv(), options);
  builder.set_container("cel.expr.conformance.proto3");

  ASSERT_OK_AND_ASSIGN(auto expression,
                       builder.CreateExpression(&parsed_expr.expr(),
                                                &parsed_expr.source_info()));

  Activation activation;
  google::protobuf::Arena arena;

  ASSERT_OK_AND_ASSIGN(CelValue result,
                       expression->Evaluate(activation, &arena));

  EXPECT_THAT(result,
              test::IsCelMessage(EqualsProto(
                  R"pb(single_any {
                         [type.googleapis.com/google.protobuf.ListValue] {
                           values { number_value: 1 }
                           values { number_value: 2.3 }
                         }
                       })pb")))
      << result.DebugString();
}

TEST(FlatExprBuilderTest, AnyPackingInt) {
  ASSERT_OK_AND_ASSIGN(ParsedExpr parsed_expr,
                       parser::Parse("TestAllTypes{single_any: 1}"));

  cel::RuntimeOptions options;
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv(), options);
  builder.set_container("cel.expr.conformance.proto3");

  ASSERT_OK_AND_ASSIGN(auto expression,
                       builder.CreateExpression(&parsed_expr.expr(),
                                                &parsed_expr.source_info()));

  Activation activation;
  google::protobuf::Arena arena;

  ASSERT_OK_AND_ASSIGN(CelValue result,
                       expression->Evaluate(activation, &arena));

  EXPECT_THAT(
      result,
      test::IsCelMessage(EqualsProto(
          R"pb(single_any {
                 [type.googleapis.com/google.protobuf.Int64Value] { value: 1 }
               })pb")))
      << result.DebugString();
}

TEST(FlatExprBuilderTest, AnyPackingMap) {
  ASSERT_OK_AND_ASSIGN(
      ParsedExpr parsed_expr,
      parser::Parse("TestAllTypes{single_any: {'key': 'value'}}"));

  cel::RuntimeOptions options;
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv(), options);
  builder.set_container("cel.expr.conformance.proto3");

  ASSERT_OK_AND_ASSIGN(auto expression,
                       builder.CreateExpression(&parsed_expr.expr(),
                                                &parsed_expr.source_info()));

  Activation activation;
  google::protobuf::Arena arena;

  ASSERT_OK_AND_ASSIGN(CelValue result,
                       expression->Evaluate(activation, &arena));

  EXPECT_THAT(result, test::IsCelMessage(EqualsProto(
                          R"pb(single_any {
                                 [type.googleapis.com/google.protobuf.Struct] {
                                   fields {
                                     key: "key"
                                     value { string_value: "value" }
                                   }
                                 }
                               })pb")))
      << result.DebugString();
}

TEST(FlatExprBuilderTest, NullUnboxingDisabled) {
  TestMessage message;
  ASSERT_OK_AND_ASSIGN(ParsedExpr parsed_expr,
                       parser::Parse("message.int32_wrapper_value"));
  cel::RuntimeOptions options;
  options.enable_empty_wrapper_null_unboxing = false;
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv(), options);
  ASSERT_OK_AND_ASSIGN(auto expression,
                       builder.CreateExpression(&parsed_expr.expr(),
                                                &parsed_expr.source_info()));

  Activation activation;
  google::protobuf::Arena arena;
  activation.InsertValue("message",
                         CelProtoWrapper::CreateMessage(&message, &arena));
  ASSERT_OK_AND_ASSIGN(CelValue result,
                       expression->Evaluate(activation, &arena));

  EXPECT_THAT(result, test::IsCelInt64(0));
}

TEST(FlatExprBuilderTest, HeterogeneousEqualityEnabled) {
  ASSERT_OK_AND_ASSIGN(ParsedExpr parsed_expr,
                       parser::Parse("{1: 2, 2u: 3}[1.0]"));
  cel::RuntimeOptions options;
  options.enable_heterogeneous_equality = true;
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv(), options);
  ASSERT_OK_AND_ASSIGN(auto expression,
                       builder.CreateExpression(&parsed_expr.expr(),
                                                &parsed_expr.source_info()));

  Activation activation;
  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(CelValue result,
                       expression->Evaluate(activation, &arena));

  EXPECT_THAT(result, test::IsCelInt64(2));
}

TEST(FlatExprBuilderTest, HeterogeneousEqualityDisabled) {
  ASSERT_OK_AND_ASSIGN(ParsedExpr parsed_expr,
                       parser::Parse("{1: 2, 2u: 3}[1.0]"));
  cel::RuntimeOptions options;
  options.enable_heterogeneous_equality = false;
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv(), options);
  ASSERT_OK_AND_ASSIGN(auto expression,
                       builder.CreateExpression(&parsed_expr.expr(),
                                                &parsed_expr.source_info()));

  Activation activation;
  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(CelValue result,
                       expression->Evaluate(activation, &arena));

  EXPECT_THAT(result,
              test::IsCelError(StatusIs(absl::StatusCode::kInvalidArgument,
                                        HasSubstr("Invalid map key type"))));
}

std::pair<google::protobuf::Message*, const google::protobuf::Reflection*> CreateTestMessage(
    const google::protobuf::DescriptorPool& descriptor_pool,
    google::protobuf::MessageFactory& message_factory, absl::string_view name) {
  const google::protobuf::Descriptor* desc = descriptor_pool.FindMessageTypeByName(name);
  const google::protobuf::Message* message_prototype = message_factory.GetPrototype(desc);
  google::protobuf::Message* message = message_prototype->New();
  const google::protobuf::Reflection* refl = message->GetReflection();
  return std::make_pair(message, refl);
}

struct CustomDescriptorPoolTestParam final {
  using SetterFunction =
      std::function<void(google::protobuf::Message*, const google::protobuf::Reflection*,
                         const google::protobuf::FieldDescriptor*)>;
  std::string message_type;
  std::string field_name;
  SetterFunction setter;
  test::CelValueMatcher matcher;
};

class CustomDescriptorPoolTest
    : public ::testing::TestWithParam<CustomDescriptorPoolTestParam> {};

// This test in particular checks for conversion errors in cel_proto_wrapper.cc.
TEST_P(CustomDescriptorPoolTest, TestType) {
  const CustomDescriptorPoolTestParam& p = GetParam();

  google::protobuf::DescriptorPool descriptor_pool;
  google::protobuf::Arena arena;

  // Setup descriptor pool and builder
  ASSERT_THAT(AddStandardMessageTypesToDescriptorPool(descriptor_pool), IsOk());
  google::protobuf::DynamicMessageFactory message_factory(&descriptor_pool);
  ASSERT_OK_AND_ASSIGN(ParsedExpr parsed_expr, parser::Parse("m"));
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  ASSERT_THAT(RegisterBuiltinFunctions(builder.GetRegistry()), IsOk());

  // Create test subject, invoke custom setter for message
  auto [message, reflection] =
      CreateTestMessage(descriptor_pool, message_factory, p.message_type);
  const google::protobuf::FieldDescriptor* field =
      message->GetDescriptor()->FindFieldByName(p.field_name);

  p.setter(message, reflection, field);
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<CelExpression> expression,
                       builder.CreateExpression(&parsed_expr.expr(),
                                                &parsed_expr.source_info()));

  // Evaluate expression, verify expectation with custom matcher
  Activation activation;
  activation.InsertValue("m", CelProtoWrapper::CreateMessage(message, &arena));
  ASSERT_OK_AND_ASSIGN(CelValue result,
                       expression->Evaluate(activation, &arena));
  EXPECT_THAT(result, p.matcher);

  delete message;
}

INSTANTIATE_TEST_SUITE_P(
    ValueTypes, CustomDescriptorPoolTest,
    ::testing::ValuesIn(std::vector<CustomDescriptorPoolTestParam>{
        {"google.protobuf.Duration", "seconds",
         [](google::protobuf::Message* message, const google::protobuf::Reflection* reflection,
            const google::protobuf::FieldDescriptor* field) {
           reflection->SetInt64(message, field, 10);
         },
         test::IsCelDuration(absl::Seconds(10))},
        {"google.protobuf.DoubleValue", "value",
         [](google::protobuf::Message* message, const google::protobuf::Reflection* reflection,
            const google::protobuf::FieldDescriptor* field) {
           reflection->SetDouble(message, field, 1.2);
         },
         test::IsCelDouble(1.2)},
        {"google.protobuf.Int64Value", "value",
         [](google::protobuf::Message* message, const google::protobuf::Reflection* reflection,
            const google::protobuf::FieldDescriptor* field) {
           reflection->SetInt64(message, field, -23);
         },
         test::IsCelInt64(-23)},
        {"google.protobuf.UInt64Value", "value",
         [](google::protobuf::Message* message, const google::protobuf::Reflection* reflection,
            const google::protobuf::FieldDescriptor* field) {
           reflection->SetUInt64(message, field, 42);
         },
         test::IsCelUint64(42)},
        {"google.protobuf.BoolValue", "value",
         [](google::protobuf::Message* message, const google::protobuf::Reflection* reflection,
            const google::protobuf::FieldDescriptor* field) {
           reflection->SetBool(message, field, true);
         },
         test::IsCelBool(true)},
        {"google.protobuf.StringValue", "value",
         [](google::protobuf::Message* message, const google::protobuf::Reflection* reflection,
            const google::protobuf::FieldDescriptor* field) {
           reflection->SetString(message, field, "foo");
         },
         test::IsCelString("foo")},
        {"google.protobuf.BytesValue", "value",
         [](google::protobuf::Message* message, const google::protobuf::Reflection* reflection,
            const google::protobuf::FieldDescriptor* field) {
           reflection->SetString(message, field, "bar");
         },
         test::IsCelBytes("bar")},
        {"google.protobuf.Timestamp", "seconds",
         [](google::protobuf::Message* message, const google::protobuf::Reflection* reflection,
            const google::protobuf::FieldDescriptor* field) {
           reflection->SetInt64(message, field, 20);
         },
         test::IsCelTimestamp(absl::FromUnixSeconds(20))}}));

struct ConstantFoldingTestCase {
  std::string test_name;
  std::string expr;
  test::CelValueMatcher matcher;
  absl::flat_hash_map<std::string, int64_t> values;
};

class UnknownFunctionImpl : public cel::Function {
  absl::StatusOr<Value> Invoke(absl::Span<const Value> args,
                               const InvokeContext& context) const override {
    return cel::UnknownValue();
  }
};

absl::StatusOr<std::unique_ptr<CelExpressionBuilder>>
CreateConstantFoldingConformanceTestExprBuilder(
    const InterpreterOptions& options) {
  auto builder =
      google::api::expr::runtime::CreateCelExpressionBuilder(options);
  CEL_RETURN_IF_ERROR(
      RegisterBuiltinFunctions(builder->GetRegistry(), options));
  CEL_RETURN_IF_ERROR(builder->GetRegistry()->RegisterLazyFunction(
      cel::FunctionDescriptor("LazyFunction", false, {})));
  CEL_RETURN_IF_ERROR(builder->GetRegistry()->RegisterLazyFunction(
      cel::FunctionDescriptor("LazyFunction", false, {cel::Kind::kBool})));
  CEL_RETURN_IF_ERROR(builder->GetRegistry()->Register(
      cel::FunctionDescriptor("UnknownFunction", false, {}),
      std::make_unique<UnknownFunctionImpl>()));
  return builder;
}

class ConstantFoldingConformanceTest
    : public ::testing::TestWithParam<ConstantFoldingTestCase> {
 protected:
  google::protobuf::Arena arena_;
};

TEST_P(ConstantFoldingConformanceTest, Updated) {
  InterpreterOptions options;
  options.constant_folding = true;
  options.constant_arena = &arena_;
  // Check interaction between const folding and list append optimizations.
  options.enable_comprehension_list_append = true;

  const ConstantFoldingTestCase& p = GetParam();
  ASSERT_OK_AND_ASSIGN(
      auto builder, CreateConstantFoldingConformanceTestExprBuilder(options));

  ASSERT_OK_AND_ASSIGN(ParsedExpr expr, parser::Parse(p.expr));

  ASSERT_OK_AND_ASSIGN(
      auto plan, builder->CreateExpression(&expr.expr(), &expr.source_info()));

  Activation activation;
  ASSERT_OK(activation.InsertFunction(
      PortableUnaryFunctionAdapter<bool, bool>::Create(
          "LazyFunction", false,
          [](google::protobuf::Arena* arena, bool val) { return val; })));

  for (auto iter = p.values.begin(); iter != p.values.end(); ++iter) {
    activation.InsertValue(iter->first, CelValue::CreateInt64(iter->second));
  }
  ASSERT_OK_AND_ASSIGN(CelValue result, plan->Evaluate(activation, &arena_));
  // Check that none of the memoized constants are being mutated.
  ASSERT_OK_AND_ASSIGN(result, plan->Evaluate(activation, &arena_));
  EXPECT_THAT(result, p.matcher);
}

INSTANTIATE_TEST_SUITE_P(
    Exprs, ConstantFoldingConformanceTest,
    ::testing::ValuesIn(std::vector<ConstantFoldingTestCase>{
        {"simple_add", "1 + 2 + 3", test::IsCelInt64(6)},
        {"add_with_var",
         "1 + (2 + (3 + id))",
         test::IsCelInt64(10),
         {{"id", 4}}},
        {"const_list", "[1, 2, 3, 4]", test::IsCelList(_)},
        {"mixed_const_list",
         "[1, 2, 3, 4] + [id]",
         test::IsCelList(_),
         {{"id", 5}}},
        {"create_struct", "{'abc': 'def', 'def': 'efg', 'efg': 'hij'}",
         Truly([](const CelValue& v) { return v.IsMap(); })},
        {"field_selection", "{'abc': 123}.abc == 123", test::IsCelBool(true)},
        {"type_coverage",
         // coverage for constant literals, type() is used to make the list
         // homogenous.
         R"cel(
            [type(bool),
             type(123),
             type(123u),
             type(12.3),
             type(b'123'),
             type('123'),
             type(null),
             type(timestamp(0)),
             type(duration('1h'))
             ])cel",
         test::IsCelList(SizeIs(9))},
        {"lazy_function", "true || LazyFunction()", test::IsCelBool(true)},
        {"lazy_function_called", "LazyFunction(true) || false",
         test::IsCelBool(true)},
        {"unknown_function", "UnknownFunction() && false",
         test::IsCelBool(false)},
        {"nested_comprehension",
         "[1, 2, 3, 4].all(x, [5, 6, 7, 8].all(y, x < y))",
         test::IsCelBool(true)},
        // Implementation detail: map and filter use replace the accu_init
        // expr with a special mutable list to avoid quadratic memory usage
        // building the projected list.
        {"map", "[1, 2, 3, 4].map(x, x * 2).size() == 4",
         test::IsCelBool(true)},
        {"str_cat",
         "'1234567890' + '1234567890' + '1234567890' + '1234567890' + "
         "'1234567890'",
         test::IsCelString(
             "12345678901234567890123456789012345678901234567890")}}));

// Check that list literals are pre-computed
TEST(UpdatedConstantFolding, FoldsLists) {
  InterpreterOptions options;
  google::protobuf::Arena arena;
  options.constant_folding = true;
  options.constant_arena = &arena;

  ASSERT_OK_AND_ASSIGN(
      auto builder, CreateConstantFoldingConformanceTestExprBuilder(options));
  ASSERT_OK_AND_ASSIGN(ParsedExpr expr,
                       parser::Parse("[1] + [2] + [3] + [4] + [5] + [6] + [7] "
                                     "+ [8] + [9] + [10] + [11] + [12]"));

  ASSERT_OK_AND_ASSIGN(
      auto plan, builder->CreateExpression(&expr.expr(), &expr.source_info()));
  Activation activation;
  int before_size = arena.SpaceUsed();
  ASSERT_OK_AND_ASSIGN(CelValue result, plan->Evaluate(activation, &arena));
  // Some incidental allocations are expected related to interop.
  // 128 is less than the expected allocations for allocating the list terms and
  // any intermediates in the unoptimized case.
  EXPECT_LE(arena.SpaceUsed() - before_size, 512);
  EXPECT_THAT(result, test::IsCelList(SizeIs(12)));
}

TEST(FlatExprBuilderTest, BlockBadIndex) {
  ParsedExpr parsed_expr;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        expr: {
          call_expr: {
            function: "cel.@block"
            args {
              list_expr: { elements { const_expr: { string_value: "foo" } } }
            }
            args { ident_expr: { name: "@index-1" } }
          }
        }
      )pb",
      &parsed_expr));

  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  EXPECT_THAT(
      builder.CreateExpression(&parsed_expr.expr(), &parsed_expr.source_info()),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("bad @index")));
}

TEST(FlatExprBuilderTest, OutOfRangeBlockIndex) {
  ParsedExpr parsed_expr;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        expr: {
          call_expr: {
            function: "cel.@block"
            args {
              list_expr: { elements { const_expr: { string_value: "foo" } } }
            }
            args { ident_expr: { name: "@index1" } }
          }
        }
      )pb",
      &parsed_expr));

  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  EXPECT_THAT(
      builder.CreateExpression(&parsed_expr.expr(), &parsed_expr.source_info()),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("invalid @index greater than number of bindings:")));
}

TEST(FlatExprBuilderTest, EarlyBlockIndex) {
  ParsedExpr parsed_expr;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        expr: {
          call_expr: {
            function: "cel.@block"
            args { list_expr: { elements { ident_expr: { name: "@index0" } } } }
            args { ident_expr: { name: "@index0" } }
          }
        }
      )pb",
      &parsed_expr));

  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  EXPECT_THAT(
      builder.CreateExpression(&parsed_expr.expr(), &parsed_expr.source_info()),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("@index references current or future binding:")));
}

TEST(FlatExprBuilderTest, OutOfScopeCSE) {
  ParsedExpr parsed_expr;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        expr: { ident_expr: { name: "@ac:0:0" } }
      )pb",
      &parsed_expr));

  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  EXPECT_THAT(
      builder.CreateExpression(&parsed_expr.expr(), &parsed_expr.source_info()),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("out of scope reference to CSE generated "
                         "comprehension variable")));
}

TEST(FlatExprBuilderTest, BlockMissingBindings) {
  ParsedExpr parsed_expr;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        expr: { call_expr: { function: "cel.@block" } }
      )pb",
      &parsed_expr));

  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  EXPECT_THAT(
      builder.CreateExpression(&parsed_expr.expr(), &parsed_expr.source_info()),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr(
                   "malformed cel.@block: missing list of bound expressions")));
}

TEST(FlatExprBuilderTest, BlockMissingExpression) {
  ParsedExpr parsed_expr;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        expr: {
          call_expr: {
            function: "cel.@block"
            args { list_expr: {} }
          }
        }
      )pb",
      &parsed_expr));

  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  EXPECT_THAT(
      builder.CreateExpression(&parsed_expr.expr(), &parsed_expr.source_info()),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("malformed cel.@block: missing bound expression")));
}

TEST(FlatExprBuilderTest, BlockNotListOfBoundExpressions) {
  ParsedExpr parsed_expr;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        expr: {
          call_expr: {
            function: "cel.@block"
            args { ident_expr: { name: "@index0" } }
            args { ident_expr: { name: "@index0" } }
          }
        }
      )pb",
      &parsed_expr));

  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  EXPECT_THAT(
      builder.CreateExpression(&parsed_expr.expr(), &parsed_expr.source_info()),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("malformed cel.@block: first argument is not a list "
                         "of bound expressions")));
}

TEST(FlatExprBuilderTest, BlockEmptyListOfBoundExpressions) {
  ParsedExpr parsed_expr;
  // Allowed, but degenerate case.
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        expr: {
          call_expr: {
            function: "cel.@block"
            args { list_expr: {} }
            args { ident_expr: { name: "@index0" } }
          }
        }
      )pb",
      &parsed_expr));

  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  EXPECT_THAT(
      builder.CreateExpression(&parsed_expr.expr(), &parsed_expr.source_info()),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("invalid @index greater than number of bindings:")));
}

TEST(FlatExprBuilderTest, BlockOptionalListOfBoundExpressions) {
  ParsedExpr parsed_expr;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        expr: {
          call_expr: {
            function: "cel.@block"
            args {
              list_expr: {
                elements { const_expr: { string_value: "foo" } }
                optional_indices: [ 0 ]
              }
            }
            args { ident_expr: { name: "@index0" } }
          }
        }
      )pb",
      &parsed_expr));

  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  EXPECT_THAT(
      builder.CreateExpression(&parsed_expr.expr(), &parsed_expr.source_info()),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("malformed cel.@block: list of bound expressions "
                         "contains an optional")));
}

TEST(FlatExprBuilderTest, BlockNested) {
  ParsedExpr parsed_expr;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        expr: {
          call_expr: {
            function: "cel.@block"
            args {
              list_expr: { elements { const_expr: { string_value: "foo" } } }
            }
            args {
              call_expr: {
                function: "cel.@block"
                args {
                  list_expr: {
                    elements { const_expr: { string_value: "foo" } }
                  }
                }
                args { ident_expr: { name: "@index1" } }
              }
            }
          }
        }
      )pb",
      &parsed_expr));

  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv());
  EXPECT_THAT(
      builder.CreateExpression(&parsed_expr.expr(), &parsed_expr.source_info()),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("multiple cel.@block are not allowed")));
}

struct VariadicLogicalEvalTestCase {
  std::string label;
  std::string expr;
  std::string a_val;
  std::string b_val;
  std::string c_val;
  std::string expected_type;  // "bool", "error", "unknown"
  bool expected_bool = false;
};

class FlatExprBuilderVariadicLogicalTest
    : public testing::TestWithParam<VariadicLogicalEvalTestCase> {};

TEST_P(FlatExprBuilderVariadicLogicalTest, Evaluate) {
  const auto& test_case = GetParam();
  parser::ParserOptions parser_options;
  parser_options.enable_variadic_logical_operators = true;
  ASSERT_OK_AND_ASSIGN(
      ParsedExpr parsed_expr,
      parser::Parse(test_case.expr, test_case.label, parser_options));

  cel::RuntimeOptions options;
  options.unknown_processing =
      cel::UnknownProcessingOptions::kAttributeAndFunction;
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv(), options);
  ASSERT_OK_AND_ASSIGN(auto cel_expr,
                       builder.CreateExpression(&parsed_expr.expr(),
                                                &parsed_expr.source_info()));

  Activation activation;
  google::protobuf::Arena arena;
  std::vector<CelAttributePattern> unknown_patterns;

  // Set up variables:
  auto insert_value = [&](absl::string_view name, const std::string& val) {
    if (val == "true") {
      activation.InsertValue(name, CelValue::CreateBool(true));
    } else if (val == "false") {
      activation.InsertValue(name, CelValue::CreateBool(false));
    } else if (val == "error") {
      activation.InsertValue(name, CreateErrorValue(&arena, "test error"));
    } else if (val == "unknown1" || val == "unknown2") {
      activation.InsertValue(name, CelValue::CreateBool(true));
      unknown_patterns.push_back(CreateCelAttributePattern(name, {}));
    }
  };

  insert_value("a", test_case.a_val);
  insert_value("b", test_case.b_val);
  insert_value("c", test_case.c_val);

  if (!unknown_patterns.empty()) {
    ASSERT_THAT(
        activation.SetUnknownAttributePatterns(std::move(unknown_patterns)),
        IsOk());
  }

  ASSERT_OK_AND_ASSIGN(CelValue result, cel_expr->Evaluate(activation, &arena));

  if (test_case.expected_type == "bool") {
    ASSERT_TRUE(result.IsBool()) << result.DebugString();
    EXPECT_EQ(result.BoolOrDie(), test_case.expected_bool);
  } else if (test_case.expected_type == "error") {
    EXPECT_TRUE(result.IsError()) << result.DebugString();
  } else if (test_case.expected_type == "unknown") {
    EXPECT_TRUE(result.IsUnknownSet()) << result.DebugString();
  }
}

INSTANTIATE_TEST_SUITE_P(
    FlatExprBuilderVariadicLogicalTest, FlatExprBuilderVariadicLogicalTest,
    testing::Values(
        VariadicLogicalEvalTestCase{"AND_AllTrue", "a && b && c", "true",
                                    "true", "true", "bool", true},
        VariadicLogicalEvalTestCase{"AND_ShortCircuitFalse", "a && b && c",
                                    "true", "false", "unset", "bool", false},
        VariadicLogicalEvalTestCase{"AND_ShortCircuitFirstFalse", "a && b && c",
                                    "false", "unset", "unset", "bool", false},
        VariadicLogicalEvalTestCase{"OR_AllFalse", "a || b || c", "false",
                                    "false", "false", "bool", false},
        VariadicLogicalEvalTestCase{"OR_ShortCircuitTrue", "a || b || c",
                                    "false", "true", "unset", "bool", true},
        VariadicLogicalEvalTestCase{"OR_ShortCircuitFirstTrue", "a || b || c",
                                    "true", "unset", "unset", "bool", true},
        VariadicLogicalEvalTestCase{"AND_Error", "a && b && c", "true", "error",
                                    "true", "error"},
        VariadicLogicalEvalTestCase{"AND_ShortCircuitBeforeError",
                                    "a && b && c", "false", "error", "unset",
                                    "bool", false},
        VariadicLogicalEvalTestCase{"OR_Error", "a || b || c", "false", "error",
                                    "false", "error"},
        VariadicLogicalEvalTestCase{"OR_ShortCircuitBeforeError", "a || b || c",
                                    "true", "error", "unset", "bool", true},
        VariadicLogicalEvalTestCase{"AND_Unknown", "a && b && c", "true",
                                    "unknown1", "true", "unknown"},
        VariadicLogicalEvalTestCase{"AND_ShortCircuitBeforeUnknown",
                                    "a && b && c", "false", "unknown1", "unset",
                                    "bool", false},
        VariadicLogicalEvalTestCase{"OR_Unknown", "a || b || c", "false",
                                    "unknown1", "false", "unknown"},
        VariadicLogicalEvalTestCase{"OR_ShortCircuitBeforeUnknown",
                                    "a || b || c", "true", "unknown1", "unset",
                                    "bool", true},
        VariadicLogicalEvalTestCase{"AND_UnknownAggregation", "a && b && c",
                                    "unknown1", "unknown2", "true", "unknown"},
        VariadicLogicalEvalTestCase{"OR_UnknownAggregation", "a || b || c",
                                    "unknown1", "unknown2", "false", "unknown"},
        VariadicLogicalEvalTestCase{"Exists_True", "[a, b, c].exists(x, x)",
                                    "false", "false", "true", "bool", true},
        VariadicLogicalEvalTestCase{"Exists_Unknown", "[a, b, c].exists(x, x)",
                                    "false", "unknown1", "false", "unknown"},
        VariadicLogicalEvalTestCase{"All_False", "[a, b, c].all(x, x)", "true",
                                    "true", "false", "bool", false},
        VariadicLogicalEvalTestCase{"All_Unknown", "[a, b, c].all(x, x)",
                                    "true", "unknown1", "true", "unknown"}));

void ReplaceResultAccumulatorWithLegacyName(Expr* expr) {
  class LegacyAccumulatorRewriter : public AstRewriterBase {
   public:
    bool PostVisitRewrite(Expr* expr, const SourcePosition*) override {
      if (expr->has_ident_expr() && expr->ident_expr().name() == "@result") {
        expr->mutable_ident_expr()->set_name("__result__");
        return true;
      }
      if (expr->has_comprehension_expr() &&
          expr->comprehension_expr().accu_var() == "@result") {
        expr->mutable_comprehension_expr()->set_accu_var("__result__");
        return true;
      }
      return false;
    }
  } rewriter;
  AstRewrite(expr, /*source_info=*/nullptr, &rewriter);
}

TEST(FlatExprBuilderTest, LegacyAccumulatorReferenceDoesNotMutateInPlace) {
  cel::RuntimeOptions options;
  options.enable_comprehension_list_append = true;
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv(), options);
  ASSERT_THAT(RegisterBuiltinFunctions(builder.GetRegistry()), IsOk());

  for (absl::string_view expr_str : {
           "[1, 2].map(x, (__result__ + [x]).size()) == [1, 2]",
           "[1, 2, 3].filter(x, (__result__ + [x]).size() > 1) == []",
           "[1, 2, 3].filter(x, (__result__ + [x]).size() == 1) == [1]",
       }) {
    ASSERT_OK_AND_ASSIGN(ParsedExpr parsed_expr, parser::Parse(expr_str));
    ReplaceResultAccumulatorWithLegacyName(parsed_expr.mutable_expr());

    ASSERT_OK_AND_ASSIGN(std::unique_ptr<CelExpression> cel_expr,
                         builder.CreateExpression(&parsed_expr.expr(),
                                                  &parsed_expr.source_info()));

    Activation activation;
    google::protobuf::Arena arena;
    ASSERT_OK_AND_ASSIGN(CelValue result,
                         cel_expr->Evaluate(activation, &arena));
    EXPECT_THAT(result, test::IsCelBool(true)) << expr_str;
  }
}

TEST(FlatExprBuilderTest,
     ComprehensionTernaryNonIdentFalseBranchSkipsMutableListAppend) {
  // Hand-crafted comprehension where the false branch of the ternary loop_step
  // is not the accumulator variable:
  //   accu_var = "__result__", accu_init = []
  //   loop_step = x > 1 ? (__result__ + [x]) : [0]
  // For iter_range = [1, 2, 3]:
  //   x = 1 -> false branch -> __result__ becomes [0]
  //   x = 2 -> true branch  -> __result__ becomes [0, 2]
  //   x = 3 -> true branch  -> __result__ becomes [0, 2, 3]
  ASSERT_OK_AND_ASSIGN(
      ParsedExpr parsed_expr,
      parser::Parse("[1, 2, 3].filter(x, x > 1) == [0, 2, 3]"));
  Expr* filter_expr =
      parsed_expr.mutable_expr()->mutable_call_expr()->mutable_args(0);
  ASSERT_TRUE(filter_expr->has_comprehension_expr());
  Expr* false_branch = filter_expr->mutable_comprehension_expr()
                           ->mutable_loop_step()
                           ->mutable_call_expr()
                           ->mutable_args(2);
  false_branch->Clear();
  false_branch->mutable_list_expr()
      ->add_elements()
      ->mutable_const_expr()
      ->set_int64_value(0);

  cel::RuntimeOptions options;
  options.enable_comprehension_list_append = true;
  CelExpressionBuilderFlatImpl builder(NewTestingRuntimeEnv(), options);
  ASSERT_THAT(RegisterBuiltinFunctions(builder.GetRegistry()), IsOk());

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<CelExpression> cel_expr,
                       builder.CreateExpression(&parsed_expr.expr(),
                                                &parsed_expr.source_info()));

  Activation activation;
  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(CelValue result, cel_expr->Evaluate(activation, &arena));
  EXPECT_THAT(result, test::IsCelBool(true));
}

}  // namespace

}  // namespace google::api::expr::runtime
