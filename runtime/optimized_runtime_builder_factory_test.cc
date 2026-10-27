// Copyright 2026 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "runtime/optimized_runtime_builder_factory.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "checker/type_checker_builder.h"
#include "checker/validation_result.h"
#include "common/ast.h"
#include "common/decl.h"
#include "common/type.h"
#include "common/value.h"
#include "common/value_testing.h"
#include "compiler/compiler.h"
#include "compiler/compiler_factory.h"
#include "compiler/standard_library.h"
#include "extensions/bindings_ext.h"
#include "extensions/comprehensions_v2.h"
#include "extensions/comprehensions_v2_functions.h"
#include "extensions/protobuf/enum_adapter.h"
#include "extensions/sets_functions.h"
#include "internal/noop_delete.h"
#include "internal/status_macros.h"
#include "internal/testing.h"
#include "internal/testing_descriptor_pool.h"
#include "runtime/activation.h"
#include "runtime/runtime.h"
#include "runtime/runtime_builder.h"
#include "runtime/runtime_options.h"
#include "cel/expr/conformance/proto2/test_all_types.pb.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"

namespace cel {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::cel::expr::conformance::proto2::TestAllTypes;
using ::cel::test::BoolValueIs;

absl::StatusOr<std::unique_ptr<Compiler>> CreateTestCompiler() {
  CEL_ASSIGN_OR_RETURN(
      std::unique_ptr<CompilerBuilder> builder,
      NewCompilerBuilder(internal::GetTestingDescriptorPool()));
  CEL_RETURN_IF_ERROR(builder->AddLibrary(StandardCompilerLibrary()));
  CEL_RETURN_IF_ERROR(
      builder->AddLibrary(extensions::BindingsCompilerLibrary()));
  CEL_RETURN_IF_ERROR(builder->AddLibrary(extensions::SetsCompilerLibrary()));
  CEL_RETURN_IF_ERROR(
      builder->AddLibrary(extensions::ComprehensionsV2CompilerLibrary()));

  TypeCheckerBuilder& checker = builder->GetCheckerBuilder();
  checker.set_container("cel.expr.conformance.proto2");
  CEL_RETURN_IF_ERROR(checker.AddContextDeclaration(
      "cel.expr.conformance.proto2.TestAllTypes"));
  CEL_RETURN_IF_ERROR(checker.AddVariable(
      MakeVariableDecl("msg", MessageType(TestAllTypes::descriptor()))));
  CEL_RETURN_IF_ERROR(checker.AddVariable(MakeVariableDecl("x", IntType())));
  CEL_RETURN_IF_ERROR(checker.AddVariable(MakeVariableDecl("s", StringType())));
  CEL_RETURN_IF_ERROR(checker.AddVariable(
      MakeVariableDecl("int_list", ListType(checker.arena(), IntType()))));
  CEL_RETURN_IF_ERROR(checker.AddVariable(
      MakeVariableDecl("str_list", ListType(checker.arena(), StringType()))));

  return builder->Build();
}

absl::StatusOr<std::unique_ptr<Ast>> CompileTestExpr(absl::string_view expr) {
  static const Compiler* const kCompiler = []() {
    absl::StatusOr<std::unique_ptr<Compiler>> compiler = CreateTestCompiler();
    ABSL_CHECK_OK(compiler.status());
    return compiler->release();
  }();

  CEL_ASSIGN_OR_RETURN(ValidationResult result, kCompiler->Compile(expr));
  if (!result.IsValid()) {
    return absl::InvalidArgumentError(result.FormatError());
  }
  return result.ReleaseAst();
}

absl::StatusOr<std::unique_ptr<const Runtime>> BuildTestRuntime() {
  RuntimeOptions options = OptimizedRuntimeOptions();
  options.container = "cel.expr.conformance.proto2";
  CEL_ASSIGN_OR_RETURN(RuntimeBuilder builder,
                       CreateOptimizedRuntimeBuilder(
                           internal::GetTestingDescriptorPool(), options));
  CEL_RETURN_IF_ERROR(extensions::RegisterProtobufEnum(
      builder.type_registry(), TestAllTypes::NestedEnum_descriptor()));
  CEL_RETURN_IF_ERROR(
      extensions::RegisterSetsFunctions(builder.function_registry(), options));
  CEL_RETURN_IF_ERROR(extensions::RegisterComprehensionsV2Functions(
      builder.function_registry(), options));
  return std::move(builder).Build();
}

TEST(OptimizedRuntimeBuilderFactoryTest, OptimizedRuntimeOptionsDefaults) {
  RuntimeOptions options = OptimizedRuntimeOptions();
  EXPECT_TRUE(options.enable_comprehension_list_append);
  EXPECT_TRUE(options.enable_comprehension_mutable_map);
  EXPECT_TRUE(options.enable_subexpression_inlining);
}

TEST(OptimizedRuntimeBuilderFactoryTest, SharedPtrDescriptorPoolOverload) {
  std::shared_ptr<const google::protobuf::DescriptorPool> pool(
      internal::GetTestingDescriptorPool(),
      internal::NoopDeleteFor<const google::protobuf::DescriptorPool>());
  ASSERT_OK_AND_ASSIGN(RuntimeBuilder builder,
                       CreateOptimizedRuntimeBuilder(std::move(pool)));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<const Runtime> runtime,
                       std::move(builder).Build());

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Ast> ast, CompileTestExpr("1 + 2 == 3"));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Program> program,
                       runtime->CreateProgram(std::move(ast)));

  google::protobuf::Arena arena;
  Activation activation;
  ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));
  EXPECT_THAT(result, BoolValueIs(true));
}

TEST(OptimizedRuntimeBuilderFactoryTest, RegexPrecompilation) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<const Runtime> runtime,
                       BuildTestRuntime());

  // Valid constant regex succeeds.
  {
    ASSERT_OK_AND_ASSIGN(std::unique_ptr<Ast> ast,
                         CompileTestExpr("s.matches('^a+b+$')"));
    ASSERT_OK_AND_ASSIGN(std::unique_ptr<Program> program,
                         runtime->CreateProgram(std::move(ast)));

    google::protobuf::Arena arena;
    Activation activation;
    activation.InsertOrAssignValue("s", StringValue::From("aaabbb", &arena));
    ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));
    EXPECT_THAT(result, BoolValueIs(true));
  }

  // Invalid constant regex fails at plan time when regex precompilation is
  // enabled.
  {
    ASSERT_OK_AND_ASSIGN(std::unique_ptr<Ast> ast,
                         CompileTestExpr("s.matches('[invalid')"));
    EXPECT_THAT(runtime->CreateProgram(std::move(ast)),
                StatusIs(absl::StatusCode::kInvalidArgument));
  }
}

struct EvaluateResultTestCase {
  std::string name;
  std::string expression;
  bool expected_result;

  template <typename S>
  friend void AbslStringify(S& sink, const EvaluateResultTestCase& tc) {
    sink.Append(tc.name);
  }
};

class OptimizedRuntimeEvalTest
    : public ::testing::TestWithParam<EvaluateResultTestCase> {};

TEST_P(OptimizedRuntimeEvalTest, EvaluatesCorrectly) {
  const EvaluateResultTestCase& test_case = GetParam();
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<const Runtime> runtime,
                       BuildTestRuntime());

  TestAllTypes msg;
  msg.mutable_single_nested_message()->set_bb(42);
  msg.set_standalone_enum(TestAllTypes::BAR);

  google::protobuf::Arena arena;
  Activation activation;
  activation.InsertOrAssignValue(
      "msg",
      Value::WrapMessage(&msg, internal::GetTestingDescriptorPool(),
                         google::protobuf::MessageFactory::generated_factory(), &arena));
  activation.InsertOrAssignValue("x", IntValue(3));
  activation.InsertOrAssignValue("s", StringValue::From("delta", &arena));

  ListValueBuilderPtr int_list_builder = NewListValueBuilder(&arena);
  ASSERT_THAT(int_list_builder->Add(IntValue(1)), IsOk());
  ASSERT_THAT(int_list_builder->Add(IntValue(3)), IsOk());
  activation.InsertOrAssignValue("int_list",
                                 std::move(*int_list_builder).Build());

  ListValueBuilderPtr str_list_builder = NewListValueBuilder(&arena);
  ASSERT_THAT(str_list_builder->Add(StringValue::From("alpha", &arena)),
              IsOk());
  ASSERT_THAT(str_list_builder->Add(StringValue::From("delta", &arena)),
              IsOk());
  activation.InsertOrAssignValue("str_list",
                                 std::move(*str_list_builder).Build());

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Ast> ast,
                       CompileTestExpr(test_case.expression));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Program> program,
                       runtime->CreateProgram(std::move(ast)));
  ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));
  EXPECT_THAT(result, BoolValueIs(test_case.expected_result))
      << test_case.expression;
}

INSTANTIATE_TEST_SUITE_P(
    ReferenceResolverAndSelectOptimization, OptimizedRuntimeEvalTest,
    testing::ValuesIn(std::vector<EvaluateResultTestCase>{
        {"nested_message_presence_and_select",
         "has(msg.single_nested_message) && msg.single_nested_message.bb == 42",
         true},
        {"unset_message_presence", "!has(msg.standalone_message)", true},
        {"qualified_enum_reference",
         "msg.standalone_enum == TestAllTypes.NestedEnum.BAR", true},
    }));

INSTANTIATE_TEST_SUITE_P(
    ComprehensionListAndMapOptimizations, OptimizedRuntimeEvalTest,
    testing::ValuesIn(std::vector<EvaluateResultTestCase>{
        {"filter_and_map_list_append",
         "[1, 2, 3, 4].filter(i, i % 2 == 0).map(i, i * 10) == [20, 40]", true},
        {"transform_map_mutable_map",
         "{'a': 1, 'b': 2}.transformMap(k, v, v + 10)['b'] == 12", true},
        {"bind_subexpression_inlining", "cel.bind(a, 1 + 2, a + x == 6)", true},
    }));

INSTANTIATE_TEST_SUITE_P(
    ConstantFoldingAndSetMembershipOptimization, OptimizedRuntimeEvalTest,
    testing::ValuesIn(std::vector<EvaluateResultTestCase>{
        {"int_in_constant_list_true", "x in [1, 2, 3, 4]", true},
        {"int_in_folded_list_true", "x in ([1, 2] + [3, 4])", true},
        {"sets_contains_int_true", "sets.contains([1, 2, 3, 4], int_list)",
         true},
        {"sets_intersects_int_true", "sets.intersects(int_list, [3, 4, 5])",
         true},
        {"sets_equivalent_int_true", "sets.equivalent(int_list, [3, 1])", true},
        {"string_in_constant_list_true",
         "s in ['alpha', 'beta', 'gamma', 'delta']", true},
        {"sets_contains_string_true",
         "sets.contains(['alpha', 'beta', 'gamma', 'delta'], str_list)", true},
        {"sets_intersects_string_true",
         "sets.intersects(str_list, ['gamma', 'delta', 'epsilon', 'zeta'])",
         true},
        {"int_in_constant_list_false", "x in [10, 20, 30]", false},
        {"sets_contains_int_false", "sets.contains(int_list, [1, 2, 3])",
         false},
        {"string_in_constant_list_false",
         "s in ['one', 'two', 'three', 'four']", false},
    }));

}  // namespace
}  // namespace cel
