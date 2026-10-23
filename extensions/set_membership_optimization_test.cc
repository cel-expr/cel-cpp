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

#include "extensions/set_membership_optimization.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "base/attribute.h"
#include "checker/type_checker_builder.h"
#include "checker/validation_result.h"
#include "common/ast.h"
#include "common/decl.h"
#include "common/standard_definitions.h"
#include "common/type.h"
#include "common/value.h"
#include "common/value_kind.h"
#include "common/value_testing.h"
#include "compiler/compiler.h"
#include "compiler/compiler_factory.h"
#include "compiler/standard_library.h"
#include "eval/compiler/constant_folding.h"
#include "eval/compiler/flat_expr_builder.h"
#include "eval/compiler/qualified_reference_resolver.h"
#include "eval/eval/evaluator_core.h"
#include "extensions/bindings_ext.h"
#include "extensions/sets_functions.h"
#include "internal/status_macros.h"
#include "internal/testing.h"
#include "internal/testing_descriptor_pool.h"
#include "runtime/activation.h"
#include "runtime/constant_folding.h"
#include "runtime/function_adapter.h"
#include "runtime/internal/runtime_env.h"
#include "runtime/internal/runtime_env_testing.h"
#include "runtime/reference_resolver.h"
#include "runtime/runtime.h"
#include "runtime/runtime_builder.h"
#include "runtime/runtime_options.h"
#include "runtime/standard_functions.h"
#include "runtime/standard_runtime_builder_factory.h"
#include "cel/expr/conformance/proto2/test_all_types.pb.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"

namespace cel::extensions {
namespace {

using ::absl_testing::IsOk;
using ::cel::expr::conformance::proto2::TestAllTypes;
using ::cel::runtime_internal::NewTestingRuntimeEnv;
using ::cel::test::BoolValueIs;
using ::cel::test::ValueKindIs;
using ::google::api::expr::runtime::FlatExprBuilder;
using ::google::api::expr::runtime::FlatExpression;
using ::google::api::expr::runtime::NewReferenceResolverExtension;
using ::google::api::expr::runtime::ReferenceResolverOption;
using ::testing::Not;
using ::testing::SizeIs;

absl::StatusOr<std::unique_ptr<Compiler>> CreateTestCompiler() {
  CEL_ASSIGN_OR_RETURN(
      std::unique_ptr<CompilerBuilder> builder,
      NewCompilerBuilder(cel::internal::GetTestingDescriptorPool()));
  CEL_RETURN_IF_ERROR(builder->AddLibrary(StandardCompilerLibrary()));
  CEL_RETURN_IF_ERROR(builder->AddLibrary(SetsCompilerLibrary()));
  CEL_RETURN_IF_ERROR(builder->AddLibrary(BindingsCompilerLibrary()));

  TypeCheckerBuilder& checker = builder->GetCheckerBuilder();
  checker.set_container("cel.expr.conformance.proto2");
  CEL_RETURN_IF_ERROR(checker.AddContextDeclaration(
      "cel.expr.conformance.proto2.TestAllTypes"));
  CEL_RETURN_IF_ERROR(checker.AddVariable(MakeVariableDecl("x", IntType())));
  CEL_RETURN_IF_ERROR(checker.AddVariable(MakeVariableDecl("u", UintType())));
  CEL_RETURN_IF_ERROR(checker.AddVariable(MakeVariableDecl("d", DynType())));
  CEL_RETURN_IF_ERROR(checker.AddVariable(MakeVariableDecl("s", StringType())));
  CEL_RETURN_IF_ERROR(checker.AddVariable(
      MakeVariableDecl("int_list", ListType(checker.arena(), IntType()))));
  CEL_RETURN_IF_ERROR(checker.AddVariable(
      MakeVariableDecl("uint_list", ListType(checker.arena(), UintType()))));
  CEL_RETURN_IF_ERROR(checker.AddVariable(
      MakeVariableDecl("dyn_list", ListType(checker.arena(), DynType()))));
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

absl::StatusOr<std::unique_ptr<const Runtime>> BuildTestRuntime(
    bool enable_constant_folding, UnknownProcessingOptions unknown_processing =
                                      UnknownProcessingOptions::kDisabled) {
  RuntimeOptions options;
  options.container = "cel.expr.conformance.proto2";
  options.enable_heterogeneous_equality = true;
  options.unknown_processing = unknown_processing;

  CEL_ASSIGN_OR_RETURN(RuntimeBuilder builder,
                       CreateStandardRuntimeBuilder(
                           cel::internal::GetTestingDescriptorPool(), options));
  CEL_RETURN_IF_ERROR(
      RegisterSetsFunctions(builder.function_registry(), options));
  CEL_RETURN_IF_ERROR(
      EnableReferenceResolver(builder, ReferenceResolverEnabled::kAlways));
  if (enable_constant_folding) {
    CEL_RETURN_IF_ERROR(EnableConstantFolding(builder));
  }
  CEL_RETURN_IF_ERROR(EnableSetMembershipOptimization(builder));
  return std::move(builder).Build();
}

Value MakeIntList(google::protobuf::Arena* arena, const std::vector<int64_t>& values) {
  ListValueBuilderPtr builder = NewListValueBuilder(arena);
  for (int64_t v : values) {
    ABSL_CHECK_OK(builder->Add(IntValue(v)));
  }
  return std::move(*builder).Build();
}

Value MakeUintList(google::protobuf::Arena* arena, const std::vector<uint64_t>& values) {
  ListValueBuilderPtr builder = NewListValueBuilder(arena);
  for (uint64_t v : values) {
    ABSL_CHECK_OK(builder->Add(UintValue(v)));
  }
  return std::move(*builder).Build();
}

Value MakeValueList(google::protobuf::Arena* arena, const std::vector<Value>& values) {
  ListValueBuilderPtr builder = NewListValueBuilder(arena);
  for (const Value& v : values) {
    ABSL_CHECK_OK(builder->Add(v));
  }
  return std::move(*builder).Build();
}

TEST(SetMembershipOptimizationPlanTest, EmitsCompactBitsetPlan) {
  RuntimeOptions options;
  options.container = "cel.expr.conformance.proto2";
  options.enable_heterogeneous_equality = true;
  std::shared_ptr<cel::runtime_internal::RuntimeEnv> env =
      NewTestingRuntimeEnv();
  ASSERT_THAT(RegisterSetsFunctions(env->function_registry, options), IsOk());
  FlatExprBuilder builder(std::move(env), options);
  builder.set_container(options.container);
  builder.AddAstTransform(
      NewReferenceResolverExtension(ReferenceResolverOption::kAlways));
  builder.AddProgramOptimizer(
      CreateSetMembershipOptimizationProgramOptimizer());

  for (absl::string_view expr_str : {
           "x in [1, 2, 3, 4]",
           "d in []",
           "sets.contains(int_list, [1, 2, 3, 4])",
           "sets.contains(str_list, [])",
           "sets.intersects(int_list, [1, 2, 3, 4])",
           "sets.intersects([1, 2, 3, 4], int_list)",
           "sets.intersects(str_list, [])",
           "sets.equivalent(int_list, [1, 2, 3, 4])",
           "sets.equivalent([1, 2, 3, 4], int_list)",
           "sets.equivalent(str_list, [])",
       }) {
    ASSERT_OK_AND_ASSIGN(std::unique_ptr<Ast> ast, CompileTestExpr(expr_str));
    ASSERT_OK_AND_ASSIGN(FlatExpression plan,
                         builder.CreateExpressionImpl(std::move(ast), nullptr));
    // Optimized plan consists of just 2 steps: reading the variable and
    // evaluating the bitset step.
    EXPECT_THAT(plan.path(), SizeIs(2)) << expr_str;
  }
}

TEST(SetMembershipOptimizationPlanTest,
     EmitsCompactBitsetPlanWithConstantFoldedList) {
  RuntimeOptions options;
  options.container = "cel.expr.conformance.proto2";
  options.enable_heterogeneous_equality = true;
  std::shared_ptr<cel::runtime_internal::RuntimeEnv> env =
      NewTestingRuntimeEnv();
  ASSERT_THAT(RegisterStandardFunctions(env->function_registry, options),
              IsOk());
  ASSERT_THAT(RegisterSetsFunctions(env->function_registry, options), IsOk());
  FlatExprBuilder builder(std::move(env), options);
  builder.set_container(options.container);
  builder.AddAstTransform(
      NewReferenceResolverExtension(ReferenceResolverOption::kAlways));
  builder.AddProgramOptimizer(
      cel::runtime_internal::CreateConstantFoldingOptimizer());
  builder.AddProgramOptimizer(
      CreateSetMembershipOptimizationProgramOptimizer());

  for (absl::string_view expr_str : {
           "x in ([1, 2] + [3, 4])",
           "sets.contains(int_list, [1, 2] + [3, 4])",
           "sets.intersects(int_list, [1, 2] + [3, 4])",
           "sets.equivalent([1, 2] + [3, 4], int_list)",
       }) {
    ASSERT_OK_AND_ASSIGN(std::unique_ptr<Ast> ast, CompileTestExpr(expr_str));
    ASSERT_OK_AND_ASSIGN(FlatExpression plan,
                         builder.CreateExpressionImpl(std::move(ast), nullptr));
    // Constant folding folds `[1, 2] + [3, 4]` into a single constant ListValue
    // step, which SetMembershipOptimization then replaces with a 2-step plan.
    EXPECT_THAT(plan.path(), SizeIs(2)) << expr_str;
  }
}

TEST(SetMembershipOptimizationPlanTest, EmitsCompactStringSetPlan) {
  RuntimeOptions options;
  options.container = "cel.expr.conformance.proto2";
  options.enable_heterogeneous_equality = true;
  std::shared_ptr<cel::runtime_internal::RuntimeEnv> env =
      NewTestingRuntimeEnv();
  ASSERT_THAT(RegisterStandardFunctions(env->function_registry, options),
              IsOk());
  ASSERT_THAT(RegisterSetsFunctions(env->function_registry, options), IsOk());
  FlatExprBuilder builder(std::move(env), options);
  builder.set_container(options.container);
  builder.AddAstTransform(
      NewReferenceResolverExtension(ReferenceResolverOption::kAlways));
  builder.AddProgramOptimizer(
      CreateSetMembershipOptimizationProgramOptimizer());

  for (absl::string_view expr_str : {
           "s in ['a', 'b', 'c', 'd']",
           "d in ['a', 'b', 'c', 'd']",
           "sets.contains(str_list, ['a', 'b', 'c', 'd'])",
           "sets.intersects(str_list, ['a', 'b', 'c', 'd'])",
           "sets.intersects(['a', 'b', 'c', 'd'], str_list)",
           "sets.equivalent(str_list, ['a', 'b', 'c', 'd'])",
           "sets.equivalent(['a', 'b', 'c', 'd'], str_list)",
       }) {
    ASSERT_OK_AND_ASSIGN(std::unique_ptr<Ast> ast, CompileTestExpr(expr_str));
    ASSERT_OK_AND_ASSIGN(FlatExpression plan,
                         builder.CreateExpressionImpl(std::move(ast), nullptr));
    // Optimized plan consists of just 2 steps: reading the variable and
    // evaluating the generic string set step.
    EXPECT_THAT(plan.path(), SizeIs(2)) << expr_str;
  }

  // Constant string lists smaller than default min_set_size (4) are not
  // optimized.
  for (absl::string_view expr_str : {
           "s in ['a', 'b', 'c']",
           "sets.contains(str_list, ['a', 'b', 'c'])",
           "sets.intersects(str_list, ['a', 'b', 'c'])",
           "sets.equivalent(str_list, ['a', 'b', 'c'])",
       }) {
    ASSERT_OK_AND_ASSIGN(std::unique_ptr<Ast> ast, CompileTestExpr(expr_str));
    ASSERT_OK_AND_ASSIGN(FlatExpression plan,
                         builder.CreateExpressionImpl(std::move(ast), nullptr));
    EXPECT_THAT(plan.path(), Not(SizeIs(2))) << expr_str;
  }

  // Constant list<dyn> containing strings is not optimized as a string set.
  for (
      absl::string_view expr_str : {
          "s in [dyn('a'), dyn('b'), dyn('c'), dyn('d')]",
          "sets.contains(str_list, [dyn('a'), dyn('b'), dyn('c'), dyn('d')])",
          "sets.intersects(str_list, [dyn('a'), dyn('b'), dyn('c'), dyn('d')])",
          "sets.equivalent(str_list, [dyn('a'), dyn('b'), dyn('c'), dyn('d')])",
      }) {
    ASSERT_OK_AND_ASSIGN(std::unique_ptr<Ast> ast, CompileTestExpr(expr_str));
    ASSERT_OK_AND_ASSIGN(FlatExpression plan,
                         builder.CreateExpressionImpl(std::move(ast), nullptr));
    EXPECT_THAT(plan.path(), Not(SizeIs(2))) << expr_str;
  }
}

TEST(SetMembershipOptimizationPlanTest,
     RespectsCustomMinSetSizeForConstantStringSets) {
  RuntimeOptions options;
  options.container = "cel.expr.conformance.proto2";
  options.enable_heterogeneous_equality = true;
  std::shared_ptr<cel::runtime_internal::RuntimeEnv> env =
      NewTestingRuntimeEnv();
  ASSERT_THAT(RegisterSetsFunctions(env->function_registry, options), IsOk());
  FlatExprBuilder builder(std::move(env), options);
  builder.set_container(options.container);
  builder.AddAstTransform(
      NewReferenceResolverExtension(ReferenceResolverOption::kAlways));
  builder.AddProgramOptimizer(CreateSetMembershipOptimizationProgramOptimizer(
      SetMembershipOptimizationOptions{.min_set_size = 2}));

  for (absl::string_view expr_str : {
           "s in ['a', 'b']",
           "sets.contains(str_list, ['a', 'b'])",
           "sets.intersects(str_list, ['a', 'b'])",
           "sets.equivalent(str_list, ['a', 'b'])",
       }) {
    ASSERT_OK_AND_ASSIGN(std::unique_ptr<Ast> ast, CompileTestExpr(expr_str));
    ASSERT_OK_AND_ASSIGN(FlatExpression plan,
                         builder.CreateExpressionImpl(std::move(ast), nullptr));
    EXPECT_THAT(plan.path(), SizeIs(2)) << expr_str;
  }

  // Size 1 is still below min_set_size = 2.
  {
    ASSERT_OK_AND_ASSIGN(std::unique_ptr<Ast> ast,
                         CompileTestExpr("s in ['a']"));
    ASSERT_OK_AND_ASSIGN(FlatExpression plan,
                         builder.CreateExpressionImpl(std::move(ast), nullptr));
    EXPECT_THAT(plan.path(), Not(SizeIs(2)));
  }
}

TEST(SetMembershipOptimizationPlanTest,
     EmitsCompactStringSetPlanWithConstantFoldedList) {
  RuntimeOptions options;
  options.container = "cel.expr.conformance.proto2";
  options.enable_heterogeneous_equality = true;
  std::shared_ptr<cel::runtime_internal::RuntimeEnv> env =
      NewTestingRuntimeEnv();
  ASSERT_THAT(RegisterStandardFunctions(env->function_registry, options),
              IsOk());
  ASSERT_THAT(RegisterSetsFunctions(env->function_registry, options), IsOk());
  FlatExprBuilder builder(std::move(env), options);
  builder.set_container(options.container);
  builder.AddAstTransform(
      NewReferenceResolverExtension(ReferenceResolverOption::kAlways));
  builder.AddProgramOptimizer(
      cel::runtime_internal::CreateConstantFoldingOptimizer());
  builder.AddProgramOptimizer(
      CreateSetMembershipOptimizationProgramOptimizer());

  for (absl::string_view expr_str : {
           "s in (['a', 'b'] + ['c', 'd'])",
           "sets.contains(str_list, ['a', 'b'] + ['c', 'd'])",
           "sets.intersects(str_list, ['a', 'b'] + ['c', 'd'])",
           "sets.equivalent(['a', 'b'] + ['c', 'd'], str_list)",
       }) {
    ASSERT_OK_AND_ASSIGN(std::unique_ptr<Ast> ast, CompileTestExpr(expr_str));
    ASSERT_OK_AND_ASSIGN(FlatExpression plan,
                         builder.CreateExpressionImpl(std::move(ast), nullptr));
    EXPECT_THAT(plan.path(), SizeIs(2)) << expr_str;
  }
}

TEST(SetMembershipOptimizationPlanTest,
     EmitsCompactPlanWithInlinedBoundConstantSet) {
  RuntimeOptions options;
  options.container = "cel.expr.conformance.proto2";
  options.enable_heterogeneous_equality = true;
  options.enable_subexpression_inlining = true;
  std::shared_ptr<cel::runtime_internal::RuntimeEnv> env =
      NewTestingRuntimeEnv();
  ASSERT_THAT(RegisterStandardFunctions(env->function_registry, options),
              IsOk());
  ASSERT_THAT(RegisterSetsFunctions(env->function_registry, options), IsOk());
  FlatExprBuilder builder(std::move(env), options);
  builder.set_container(options.container);
  builder.AddAstTransform(
      NewReferenceResolverExtension(ReferenceResolverOption::kAlways));
  builder.AddProgramOptimizer(
      cel::runtime_internal::CreateConstantFoldingOptimizer());
  builder.AddProgramOptimizer(
      CreateSetMembershipOptimizationProgramOptimizer());

  for (absl::string_view expr_str : {
           "cel.bind(set_var, [1, 2, 3, 4], x in set_var)",
           "cel.bind(set_var, ['a', 'b', 'c', 'd'], s in set_var)",
       }) {
    ASSERT_OK_AND_ASSIGN(std::unique_ptr<Ast> ast, CompileTestExpr(expr_str));
    ASSERT_OK_AND_ASSIGN(FlatExpression plan,
                         builder.CreateExpressionImpl(std::move(ast), nullptr));
    ASSERT_FALSE(plan.subexpressions().empty());
    // Main expression has 3 steps: Identifier(x/s), Bitset/StringSet step, and
    // ClearSlot.
    EXPECT_THAT(plan.subexpressions()[0], SizeIs(3)) << expr_str;
  }
}

TEST(SetMembershipOptimizationPlanTest, EmitsCompactBitsetPlanForUncheckedAst) {
  RuntimeOptions options;
  options.container = "cel.expr.conformance.proto2";
  options.enable_heterogeneous_equality = true;
  std::shared_ptr<cel::runtime_internal::RuntimeEnv> env =
      NewTestingRuntimeEnv();
  ASSERT_THAT(RegisterSetsFunctions(env->function_registry, options), IsOk());
  FlatExprBuilder builder(std::move(env), options);
  builder.set_container(options.container);
  builder.AddAstTransform(
      NewReferenceResolverExtension(ReferenceResolverOption::kAlways));
  builder.AddProgramOptimizer(
      CreateSetMembershipOptimizationProgramOptimizer());

  for (absl::string_view expr_str : {
           "x in [1, 2, 3, 4]",
           "sets.contains(int_list, [1, 2, 3, 4])",
           "sets.intersects(int_list, [1, 2, 3, 4])",
           "sets.equivalent(int_list, [1, 2, 3, 4])",
       }) {
    ASSERT_OK_AND_ASSIGN(std::unique_ptr<Ast> ast, CompileTestExpr(expr_str));
    ast->mutable_reference_map().clear();
    ast->mutable_type_map().clear();
    ast->set_is_checked(false);
    ASSERT_OK_AND_ASSIGN(FlatExpression plan,
                         builder.CreateExpressionImpl(std::move(ast), nullptr));
    EXPECT_THAT(plan.path(), SizeIs(2)) << expr_str;
  }
}

TEST(SetMembershipOptimizationPlanTest, DoesNotOptimizeCustomOverload) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<CompilerBuilder> compiler_builder,
      NewCompilerBuilder(cel::internal::GetTestingDescriptorPool()));
  ASSERT_THAT(compiler_builder->AddLibrary(StandardCompilerLibrary()), IsOk());
  ASSERT_THAT(compiler_builder->AddLibrary(SetsCompilerLibrary()), IsOk());

  TypeCheckerBuilder& checker = compiler_builder->GetCheckerBuilder();
  ASSERT_THAT(checker.AddVariable(MakeVariableDecl("x", IntType())), IsOk());
  ASSERT_THAT(checker.AddVariable(MakeVariableDecl("s", StringType())), IsOk());
  ListType int_list_type(checker.arena(), IntType());

  ASSERT_OK_AND_ASSIGN(
      FunctionDecl custom_in,
      MakeFunctionDecl(std::string(StandardFunctions::kIn),
                       MakeOverloadDecl("string_in_int_list", BoolType(),
                                        StringType(), int_list_type)));
  ASSERT_THAT(checker.MergeFunction(custom_in), IsOk());

  ASSERT_OK_AND_ASSIGN(
      FunctionDecl custom_contains,
      MakeFunctionDecl("sets.contains",
                       MakeOverloadDecl("int_sets_contains_list", BoolType(),
                                        IntType(), int_list_type)));
  ASSERT_THAT(checker.MergeFunction(custom_contains), IsOk());

  ASSERT_OK_AND_ASSIGN(
      FunctionDecl custom_intersects,
      MakeFunctionDecl("sets.intersects",
                       MakeOverloadDecl("int_sets_intersects_list", BoolType(),
                                        IntType(), int_list_type)));
  ASSERT_THAT(checker.MergeFunction(custom_intersects), IsOk());

  ASSERT_OK_AND_ASSIGN(
      FunctionDecl custom_equivalent,
      MakeFunctionDecl("sets.equivalent",
                       MakeOverloadDecl("int_sets_equivalent_list", BoolType(),
                                        IntType(), int_list_type)));
  ASSERT_THAT(checker.MergeFunction(custom_equivalent), IsOk());

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> compiler,
                       compiler_builder->Build());

  RuntimeOptions options;
  options.enable_heterogeneous_equality = true;
  std::shared_ptr<cel::runtime_internal::RuntimeEnv> env =
      NewTestingRuntimeEnv();
  ASSERT_THAT(RegisterSetsFunctions(env->function_registry, options), IsOk());
  for (absl::string_view fn_name :
       {"sets.contains", "sets.intersects", "sets.equivalent"}) {
    ASSERT_THAT(env->function_registry.Register(
                    BinaryFunctionAdapter<bool, int64_t, const ListValue&>::
                        CreateDescriptor(fn_name, /*receiver_style=*/false),
                    BinaryFunctionAdapter<bool, int64_t, const ListValue&>::
                        WrapFunction([](int64_t x, const ListValue&) {
                          return x == 42;
                        })),
                IsOk());
  }
  FlatExprBuilder plan_builder(std::move(env), options);
  plan_builder.AddAstTransform(
      NewReferenceResolverExtension(ReferenceResolverOption::kAlways));
  plan_builder.AddProgramOptimizer(
      CreateSetMembershipOptimizationProgramOptimizer());

  for (absl::string_view expr_str : {
           "s in [1, 2, 3, 4]",
           "sets.contains(x, [1, 2, 3, 4])",
           "sets.intersects(x, [1, 2, 3, 4])",
           "sets.equivalent(x, [1, 2, 3, 4])",
       }) {
    ASSERT_OK_AND_ASSIGN(ValidationResult validation,
                         compiler->Compile(expr_str));
    ASSERT_TRUE(validation.IsValid()) << validation.FormatError();
    ASSERT_OK_AND_ASSIGN(std::unique_ptr<Ast> ast, validation.ReleaseAst());
    ASSERT_OK_AND_ASSIGN(FlatExpression plan, plan_builder.CreateExpressionImpl(
                                                  std::move(ast), nullptr));
    EXPECT_THAT(plan.path(), Not(SizeIs(2))) << expr_str;
  }

  ASSERT_OK_AND_ASSIGN(RuntimeBuilder runtime_builder,
                       CreateStandardRuntimeBuilder(
                           cel::internal::GetTestingDescriptorPool(), options));
  ASSERT_THAT(
      RegisterSetsFunctions(runtime_builder.function_registry(), options),
      IsOk());
  for (absl::string_view fn_name :
       {"sets.contains", "sets.intersects", "sets.equivalent"}) {
    ASSERT_THAT(runtime_builder.function_registry().Register(
                    BinaryFunctionAdapter<bool, int64_t, const ListValue&>::
                        CreateDescriptor(fn_name, /*receiver_style=*/false),
                    BinaryFunctionAdapter<bool, int64_t, const ListValue&>::
                        WrapFunction([](int64_t x, const ListValue&) {
                          return x == 42;
                        })),
                IsOk());
  }
  ASSERT_THAT(EnableReferenceResolver(runtime_builder,
                                      ReferenceResolverEnabled::kAlways),
              IsOk());
  ASSERT_THAT(EnableSetMembershipOptimization(runtime_builder), IsOk());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<const Runtime> runtime,
                       std::move(runtime_builder).Build());

  google::protobuf::Arena arena;
  Activation activation;
  activation.InsertOrAssignValue("x", IntValue(42));

  for (absl::string_view expr_str : {
           "sets.contains(x, [1, 2, 3, 4])",
           "sets.intersects(x, [1, 2, 3, 4])",
           "sets.equivalent(x, [1, 2, 3, 4])",
       }) {
    ASSERT_OK_AND_ASSIGN(ValidationResult validation,
                         compiler->Compile(expr_str));
    ASSERT_TRUE(validation.IsValid()) << validation.FormatError();
    ASSERT_OK_AND_ASSIGN(std::unique_ptr<Ast> ast, validation.ReleaseAst());
    ASSERT_OK_AND_ASSIGN(std::unique_ptr<Program> program,
                         runtime->CreateProgram(std::move(ast)));
    ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));
    EXPECT_THAT(result, BoolValueIs(true)) << expr_str;
  }
}

struct EvalTestCase {
  std::string expr;
  bool expected;
};

class SetMembershipOptimizationEvalTest
    : public ::testing::TestWithParam<bool> {};

TEST_P(SetMembershipOptimizationEvalTest, EvaluatesCorrectly) {
  const bool enable_constant_folding = GetParam();
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<const Runtime> runtime,
                       BuildTestRuntime(enable_constant_folding));

  google::protobuf::Arena arena;
  Activation activation;
  activation.InsertOrAssignValue("x", IntValue(2));
  activation.InsertOrAssignValue("u", UintValue(63));
  activation.InsertOrAssignValue("d", DoubleValue(2.0));
  activation.InsertOrAssignValue("s", StringValue::From("b", &arena));
  activation.InsertOrAssignValue("int_list", MakeIntList(&arena, {0, 2, 63}));
  activation.InsertOrAssignValue("uint_list", MakeUintList(&arena, {0, 2}));
  activation.InsertOrAssignValue(
      "dyn_list", MakeValueList(&arena, {IntValue(0), DoubleValue(2.0)}));
  activation.InsertOrAssignValue(
      "str_list", MakeValueList(&arena, {StringValue::From("a", &arena),
                                         StringValue::From("b", &arena),
                                         StringValue::From("c", &arena),
                                         StringValue::From("d", &arena)}));

  const std::vector<EvalTestCase> test_cases = {
      // @in with int, uint, dyn(double), and protobuf enums
      {"x in [0, 2, 63]", true},
      {"x in [0, 1, 63]", false},
      {"u in [0u, 2u, 63u]", true},
      {"u in [0u, 2u]", false},
      {"d in [0, 2, 63]", true},
      {"d in [0, 1, 63]", false},
      {"dyn(2.5) in [0, 2, 63]", false},
      {"-1 in [0, 2, 63]", false},
      {"64 in [0, 2, 63]", false},
      {"x in [TestAllTypes.NestedEnum.FOO, TestAllTypes.NestedEnum.BAR, "
       "TestAllTypes.NestedEnum.BAZ]",
       true},
      {"x in [TestAllTypes.NestedEnum.FOO, TestAllTypes.NestedEnum.BAR]",
       false},

      // sets.contains
      {"sets.contains(int_list, [0, 2])", true},
      {"sets.contains(int_list, [0, 2, 63])", true},
      {"sets.contains(int_list, [0, 1, 2])", false},
      {"sets.contains(uint_list, [0u, 2u])", true},
      {"sets.contains(uint_list, [0u, 1u])", false},
      {"sets.contains(dyn_list, [0, 2])", true},
      {"sets.contains(int_list, [TestAllTypes.NestedEnum.FOO, "
       "TestAllTypes.NestedEnum.BAZ])",
       true},
      // Non-optimizable 1st arg constant list still evaluates via standard fn
      {"sets.contains([0, 2, 63], int_list)", true},
      {"sets.contains([0, 2], int_list)", false},

      // sets.intersects (both argument orders)
      {"sets.intersects(int_list, [1, 2])", true},
      {"sets.intersects([1, 2], int_list)", true},
      {"sets.intersects(int_list, [1, 3])", false},
      {"sets.intersects([1, 3], int_list)", false},
      {"sets.intersects(uint_list, [2u, 5u])", true},
      {"sets.intersects([2u, 5u], uint_list)", true},
      {"sets.intersects(dyn_list, [2, 5])", true},

      // sets.equivalent (both argument orders)
      {"sets.equivalent(int_list, [0, 2, 63])", true},
      {"sets.equivalent([0, 2, 63], int_list)", true},
      {"sets.equivalent(int_list, [0, 2, 2, 63])", true},
      {"sets.equivalent(int_list, [0, 2])", false},
      {"sets.equivalent([0, 2], int_list)", false},
      {"sets.equivalent(int_list, [0, 1, 2, 63])", false},
      {"sets.equivalent(dyn_list, [0, 2])", true},
      {"sets.equivalent([0, 2], dyn_list)", true},
      {"sets.equivalent([0, 2, 64], [0, 2])", false},
      {"sets.equivalent([0, 2, -1], [0, 2])", false},
      {"sets.equivalent([dyn(0), dyn(2), dyn(1.5)], [0, 2])", false},

      // Empty constant lists
      {"x in []", false},
      {"u in []", false},
      {"d in []", false},
      {"'hello' in []", false},
      {"sets.contains(int_list, [])", true},
      {"sets.contains(str_list, [])", true},
      {"sets.intersects(int_list, [])", false},
      {"sets.intersects([], int_list)", false},
      {"sets.intersects(str_list, [])", false},
      {"sets.equivalent(int_list, [])", false},
      {"sets.equivalent([], int_list)", false},
      {"sets.equivalent(str_list, [])", false},
      {"sets.equivalent(int_list.filter(i, i < 0), [])", true},
      {"sets.equivalent(str_list.filter(s, s == 'none'), [])", true},

      // Constant string sets (size >= 4)
      {"s in ['a', 'b', 'c', 'd']", true},
      {"s in ['a', 'c', 'd', 'e']", false},
      {"d in ['a', 'b', 'c', 'd']", false},
      {"sets.contains(str_list, ['a', 'b', 'c', 'd'])", true},
      {"sets.contains(str_list, ['a', 'b', 'b', 'c'])", true},
      {"sets.contains(str_list, ['a', 'b', 'c', 'e'])", false},
      {"sets.contains(['a', 'b', 'c'], ['a', 'b', 'c', 'd'])", false},
      {"sets.contains([dyn('a'), dyn('b'), dyn('c'), dyn('d'), dyn(1)], "
       "['a', 'b', 'c', 'd'])",
       true},
      {"sets.intersects(str_list, ['b', 'e', 'f', 'g'])", true},
      {"sets.intersects(['b', 'e', 'f', 'g'], str_list)", true},
      {"sets.intersects(str_list, ['e', 'f', 'g', 'h'])", false},
      {"sets.intersects(['e', 'f', 'g', 'h'], str_list)", false},
      {"sets.intersects([dyn(1), dyn('d')], ['a', 'b', 'c', 'd'])", true},
      {"sets.equivalent(str_list, ['d', 'c', 'b', 'a'])", true},
      {"sets.equivalent(['d', 'c', 'b', 'a'], str_list)", true},
      {"sets.equivalent(str_list, ['a', 'b', 'b', 'c', 'd'])", true},
      {"sets.equivalent(['a', 'a', 'b', 'c', 'd'], "
       "['a', 'b', 'c', 'd'])",
       true},
      {"sets.equivalent(str_list, ['a', 'b', 'c', 'e'])", false},
      {"sets.equivalent(['a', 'b', 'c', 'e'], str_list)", false},
      {"sets.equivalent(['a', 'b', 'c'], ['a', 'b', 'c', 'd'])", false},
      {"sets.equivalent([dyn('a'), dyn('b'), dyn('c'), dyn('d'), dyn(1)], "
       "['a', 'b', 'c', 'd'])",
       false},

      // Fallback when constant list has out-of-range values, mixed types, or
      // small string lists (< 4 elements)
      {"x in [-1, 2, 100]", true},
      {"100 in [-1, 2, 100]", true},
      {"-1 in [-1, 2, 100]", true},
      {"x in [1, 64]", false},
      {"d in [dyn(1), dyn(2u), dyn(3.5)]", true},
      {"'a' in ['a', 'b', 'c']", true},
      {"sets.contains(str_list, ['a', 'b'])", true},
      {"sets.intersects(str_list, ['b', 'e'])", true},
      {"sets.equivalent(['a', 'b'], ['b', 'a'])", true},

      // Heterogeneous non-numeric dynamic elements with constant int sets
      {"dyn('1') in [0, 1, 2]", false},
      {"sets.contains([dyn('1'), dyn(1)], [1])", true},
      {"sets.intersects([1], [dyn('1'), dyn(2)])", false},
      {"sets.equivalent([1], [dyn('1'), dyn(1)])", false},

      // Constant-folded list expression when constant folding is enabled
      {"x in ([0, 1] + [2, 3])", true},
      {"sets.contains(int_list, [0] + [2])", true},
      {"s in (['a', 'b'] + ['c', 'd'])", true},
      {"sets.contains(str_list, ['a', 'b'] + ['c', 'd'])", true},
      {"sets.intersects(str_list, ['e', 'f'] + ['g', 'd'])", true},
      {"sets.equivalent(str_list, ['d', 'c'] + ['b', 'a'])", true},
  };

  for (const EvalTestCase& tc : test_cases) {
    ASSERT_OK_AND_ASSIGN(std::unique_ptr<Ast> ast, CompileTestExpr(tc.expr));
    ASSERT_OK_AND_ASSIGN(std::unique_ptr<Program> program,
                         runtime->CreateProgram(std::move(ast)));
    ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));
    EXPECT_THAT(result, BoolValueIs(tc.expected)) << "Expression: " << tc.expr;
  }
}

INSTANTIATE_TEST_SUITE_P(SetMembershipOptimizationEvalTest,
                         SetMembershipOptimizationEvalTest, testing::Bool());

TEST(SetMembershipOptimizationUnknownAndErrorTest,
     PropagatesUnknownsAndErrors) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const Runtime> runtime,
      BuildTestRuntime(/*enable_constant_folding=*/true,
                       UnknownProcessingOptions::kAttributeAndFunction));

  google::protobuf::Arena arena;
  Activation activation;
  ASSERT_THAT(
      activation.SetUnknownPatterns(
          {AttributePattern("x", {}), AttributePattern("s", {}),
           AttributePattern("int_list", {}), AttributePattern("str_list", {})}),
      IsOk());

  for (absl::string_view expr_str : {
           "x in [1, 2, 3]",
           "sets.contains(int_list, [1, 2])",
           "sets.intersects(int_list, [1, 2])",
           "sets.equivalent(int_list, [1, 2])",
           "s in ['a', 'b', 'c', 'd']",
           "sets.contains(str_list, ['a', 'b', 'c', 'd'])",
           "sets.intersects(str_list, ['a', 'b', 'c', 'd'])",
           "sets.equivalent(str_list, ['a', 'b', 'c', 'd'])",
       }) {
    ASSERT_OK_AND_ASSIGN(std::unique_ptr<Ast> ast, CompileTestExpr(expr_str));
    ASSERT_OK_AND_ASSIGN(std::unique_ptr<Program> program,
                         runtime->CreateProgram(std::move(ast)));
    ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));
    EXPECT_THAT(result, ValueKindIs(ValueKind::kUnknown)) << expr_str;
  }

  // Runtime error in operand propagates as ErrorValue.
  for (absl::string_view expr_str : {
           "(1 / 0) in [1, 2, 3]",
           "dyn(1 / 0) in ['a', 'b', 'c', 'd']",
           "sets.contains(dyn(1 / 0), ['a', 'b', 'c', 'd'])",
           "sets.intersects(dyn(1 / 0), ['a', 'b', 'c', 'd'])",
           "sets.equivalent(dyn(1 / 0), ['a', 'b', 'c', 'd'])",
       }) {
    ASSERT_OK_AND_ASSIGN(std::unique_ptr<Ast> ast, CompileTestExpr(expr_str));
    ASSERT_OK_AND_ASSIGN(std::unique_ptr<Program> program,
                         runtime->CreateProgram(std::move(ast)));
    ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));
    EXPECT_THAT(result, ValueKindIs(ValueKind::kError)) << expr_str;
  }
}

}  // namespace
}  // namespace cel::extensions
