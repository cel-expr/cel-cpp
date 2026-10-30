// Copyright 2026 Google LLC
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

#include "policy/compiler.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "common/ast.h"
#include "common/ast_proto.h"
#include "common/decl.h"
#include "common/navigable_ast.h"
#include "common/source.h"
#include "common/type.h"
#include "common/types/message_type.h"
#include "common/value.h"
#include "common/value_testing.h"
#include "compiler/compiler.h"
#include "compiler/compiler_factory.h"
#include "compiler/optional.h"
#include "compiler/standard_library.h"
#include "extensions/bindings_ext.h"
#include "internal/runfiles.h"
#include "internal/status_macros.h"
#include "internal/testing.h"
#include "internal/testing_descriptor_pool.h"
#include "policy/cel_policy.h"
#include "policy/cel_policy_parse_result.h"
#include "policy/cel_policy_validation_result.h"
#include "policy/yaml_policy_parser.h"
#include "runtime/activation.h"
#include "runtime/optional_types.h"
#include "runtime/runtime.h"
#include "runtime/runtime_builder.h"
#include "runtime/runtime_options.h"
#include "runtime/standard_runtime_builder_factory.h"
#include "tools/cel_unparser.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"

namespace cel {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::cel::test::IntValueIs;
using ::cel::test::OptionalValueIs;
using ::cel::test::OptionalValueIsEmpty;
using ::cel::test::StringValueIs;
using ::cel::test::ValueMatcher;

constexpr absl::string_view kTestPolicyFilePath =
"_main/policy/testdata/cel_policy.yaml";

absl::StatusOr<std::unique_ptr<Compiler>> BuildTestCompiler() {
  CompilerOptions opts;
  opts.adapt_parser_errors = true;
  opts.parser_options.enable_optional_syntax = true;
  CEL_ASSIGN_OR_RETURN(
      auto builder,
      NewCompilerBuilder(internal::GetSharedTestingDescriptorPool(), opts));

  CEL_RETURN_IF_ERROR(builder->AddLibrary(cel::StandardCompilerLibrary()));
  CEL_RETURN_IF_ERROR(builder->AddLibrary(cel::OptionalCompilerLibrary()));
  CEL_RETURN_IF_ERROR(
      builder->AddLibrary(cel::extensions::BindingsCompilerLibrary()));

  CEL_RETURN_IF_ERROR(builder->GetCheckerBuilder().AddVariable(
      cel::MakeVariableDecl("x", IntType())));
  CEL_RETURN_IF_ERROR(builder->GetCheckerBuilder().AddVariable(
      cel::MakeVariableDecl("y", IntType())));

  const google::protobuf::Descriptor* descriptor =
      cel::internal::GetSharedTestingDescriptorPool()->FindMessageTypeByName(
          "cel.expr.conformance.proto3.TestAllTypes");
  if (descriptor == nullptr) {
    return absl::InternalError("Failed to find TestAllTypes descriptor");
  }
  CEL_RETURN_IF_ERROR(builder->GetCheckerBuilder().AddVariable(
      cel::MakeVariableDecl("spec", cel::MessageType(descriptor))));

  return builder->Build();
}

absl::StatusOr<std::unique_ptr<CelPolicy>> ParsePolicyFromYaml(
    absl::string_view yaml_content) {
  CEL_ASSIGN_OR_RETURN(auto source, cel::NewSource(yaml_content, "test.yaml"));

  std::shared_ptr<CelPolicySource> policy_source =
      std::make_shared<CelPolicySource>(std::move(source));
  CEL_ASSIGN_OR_RETURN(auto parse_result,
                       cel::ParseYamlCelPolicy(policy_source));

  if (!parse_result.IsValid()) {
    return absl::InvalidArgumentError("Invalid policy YAML structure");
  }
  return parse_result.ReleasePolicy();
}

[[maybe_unused]]
const Ast* GetAstPointer(const std::unique_ptr<Ast>& ast) {
  return ast.get();
}

MATCHER(MetadataMapValid, negation ? "has stale metadata map entries"
                                   : "has valid metadata maps") {
  const Ast* ast = GetAstPointer(arg);
  if (ast == nullptr) {
    *result_listener << "AST is null";
    return false;
  }
  NavigableAst nav_ast = cel::NavigableAst::Build(ast->root_expr());
  absl::flat_hash_set<int64_t> valid_ids;
  for (const NavigableAstNode& node : nav_ast.Root().DescendantsPostorder()) {
    valid_ids.insert(node.expr()->id());
  }

  bool is_valid = true;
  for (const auto& [id, _] : ast->type_map()) {
    if (!valid_ids.contains(id)) {
      *result_listener << "stale type_map entry for id " << id << "; ";
      is_valid = false;
    }
  }
  for (const auto& [id, _] : ast->reference_map()) {
    if (!valid_ids.contains(id)) {
      *result_listener << "stale reference_map entry for id " << id << "; ";
      is_valid = false;
    }
  }
  for (const auto& [id, _] : ast->source_info().positions()) {
    if (!valid_ids.contains(id)) {
      *result_listener << "stale source_info position for id " << id << "; ";
      is_valid = false;
    }
  }
  for (const auto& [id, _] : ast->source_info().macro_calls()) {
    if (!valid_ids.contains(id)) {
      *result_listener << "stale source_info macro_call for id " << id << "; ";
      is_valid = false;
    }
  }
  return is_valid;
}

TEST(CompilerTest, SmokeTest) {
  std::string contents;
  std::string test_file =
      cel::internal::ResolveRunfilesPath(kTestPolicyFilePath);
  auto read_status = cel::internal::GetFileContents(test_file, &contents);
  ASSERT_THAT(read_status, IsOk());

  auto source_or = cel::NewSource(contents, "cel_policy.yaml");
  ASSERT_THAT(source_or.status(), IsOk());
  auto source = *std::move(source_or);

  std::shared_ptr<CelPolicySource> policy_source =
      std::make_shared<CelPolicySource>(std::move(source));
  auto parse_result_or = cel::ParseYamlCelPolicy(policy_source);
  ASSERT_THAT(parse_result_or.status(), IsOk());
  auto parse_result = *std::move(parse_result_or);

  ASSERT_TRUE(parse_result.IsValid());
  const CelPolicy* policy = parse_result.GetPolicy();

  ASSERT_OK_AND_ASSIGN(auto compiler, BuildTestCompiler());

  ASSERT_OK_AND_ASSIGN(auto result, CompilePolicy(*compiler, *policy));
  ASSERT_TRUE(result.IsValid());
}

TEST(CompilerTest, VariableOutOfScopeReportsError) {
  absl::string_view yaml = R"yaml(
name: cel_policy
rule:
  id: test_rule
  match:
  - condition: variables.non_existent == 10
    output: '"error"'
)yaml";
  ASSERT_OK_AND_ASSIGN(auto policy, ParsePolicyFromYaml(yaml));
  ASSERT_OK_AND_ASSIGN(auto compiler, BuildTestCompiler());
  ASSERT_OK_AND_ASSIGN(auto result, CompilePolicy(*compiler, *policy));
  EXPECT_FALSE(result.IsValid());
  EXPECT_THAT(result.FormatIssues(),
              testing::HasSubstr("undeclared reference"));
}

TEST(CompilerTest, ConditionNotBoolReportsError) {
  absl::string_view yaml = R"yaml(
name: cel_policy
rule:
  id: test_rule
  match:
  - condition: 10
    output: '"error"'
)yaml";
  ASSERT_OK_AND_ASSIGN(auto policy, ParsePolicyFromYaml(yaml));
  ASSERT_OK_AND_ASSIGN(auto compiler, BuildTestCompiler());
  ASSERT_OK_AND_ASSIGN(auto result, CompilePolicy(*compiler, *policy));
  EXPECT_FALSE(result.IsValid());
  EXPECT_THAT(result.FormatIssues(),
              testing::HasSubstr("condition must evaluate to bool"));
}

TEST(CompilerTest, InvalidOutputExpressionReportsError) {
  absl::string_view yaml = R"yaml(
name: cel_policy
rule:
  id: test_rule
  match:
  - condition: true
    output: undeclared_var
)yaml";
  ASSERT_OK_AND_ASSIGN(auto policy, ParsePolicyFromYaml(yaml));
  ASSERT_OK_AND_ASSIGN(auto compiler, BuildTestCompiler());
  ASSERT_OK_AND_ASSIGN(auto result, CompilePolicy(*compiler, *policy));
  EXPECT_FALSE(result.IsValid());
  EXPECT_THAT(result.FormatIssues(),
              testing::HasSubstr("undeclared reference"));
}

TEST(CompilerTest, DisplayErrorFormattingForInvalidExpression) {
  absl::string_view yaml = R"yaml(name: cel_policy
rule:
  match:
    - condition: x > 0
      output: undeclared_var
)yaml";
  ASSERT_OK_AND_ASSIGN(auto policy, ParsePolicyFromYaml(yaml));
  ASSERT_OK_AND_ASSIGN(auto compiler, BuildTestCompiler());
  ASSERT_OK_AND_ASSIGN(auto result, CompilePolicy(*compiler, *policy));
  EXPECT_FALSE(result.IsValid());
  EXPECT_THAT(
      result.FormatIssues(),
      testing::HasSubstr(
          R"err(ERROR: test.yaml:5:15: undeclared reference to 'undeclared_var' (in container '')
 |       output: undeclared_var
 | ..............^)err"));
}

TEST(CompilerTest, DisplayErrorFormattingForMultilineBlockLiteral) {
  absl::string_view yaml = R"yaml(name: cel_policy
rule:
  match:
    - condition: x > 0
      output: |
        undeclared_var
)yaml";
  ASSERT_OK_AND_ASSIGN(auto policy, ParsePolicyFromYaml(yaml));
  ASSERT_OK_AND_ASSIGN(auto compiler, BuildTestCompiler());
  ASSERT_OK_AND_ASSIGN(auto result, CompilePolicy(*compiler, *policy));
  EXPECT_FALSE(result.IsValid());
  EXPECT_THAT(
      result.FormatIssues(),
      testing::HasSubstr(
          R"err(ERROR: test.yaml:6:9: undeclared reference to 'undeclared_var' (in container '')
 |         undeclared_var
 | ........^)err"));
}

TEST(CompilerTest, DisplayErrorFormattingForDoubleQuotedExpression) {
  absl::string_view yaml = R"yaml(name: cel_policy
rule:
  match:
    - condition: x > 0
      output: "undeclared_var"
)yaml";
  ASSERT_OK_AND_ASSIGN(auto policy, ParsePolicyFromYaml(yaml));
  ASSERT_OK_AND_ASSIGN(auto compiler, BuildTestCompiler());
  ASSERT_OK_AND_ASSIGN(auto result, CompilePolicy(*compiler, *policy));
  EXPECT_FALSE(result.IsValid());
  EXPECT_THAT(
      result.FormatIssues(),
      testing::HasSubstr(
          R"err(ERROR: test.yaml:5:16: undeclared reference to 'undeclared_var' (in container '')
 |       output: "undeclared_var"
 | ...............^)err"));
}

TEST(CompilerTest, ComposedAstPositionsAreRelativeToMainYaml) {
  absl::string_view yaml = R"yaml(name: cel_policy
rule:
  variables:
    - name: v1
      expression: 10
  match:
    - condition: variables.v1 > 5
      output: "variables.v1 + 20"
)yaml";
  ASSERT_OK_AND_ASSIGN(auto policy, ParsePolicyFromYaml(yaml));
  ASSERT_OK_AND_ASSIGN(auto compiler, BuildTestCompiler());
  ASSERT_OK_AND_ASSIGN(auto result, CompilePolicy(*compiler, *policy));
  ASSERT_TRUE(result.IsValid());
  const cel::Ast* ast = result.GetAst();
  ASSERT_NE(ast, nullptr);
  const cel::Source* source = result.GetSource()->content();
  ASSERT_NE(source, nullptr);

  auto nav_ast = cel::NavigableAst::Build(ast->root_expr());
  const cel::NavigableAstNode* var_node = nullptr;
  const cel::NavigableAstNode* cond_node = nullptr;
  const cel::NavigableAstNode* out_node = nullptr;

  for (const cel::NavigableAstNode& node :
       nav_ast.Root().DescendantsPostorder()) {
    if (node.expr()->has_const_expr() &&
        node.expr()->const_expr().has_int_value() &&
        node.expr()->const_expr().int_value() == 10) {
      var_node = &node;
    } else if (node.expr()->has_call_expr() &&
               node.expr()->call_expr().function() == "_>_") {
      cond_node = &node;
    } else if (node.expr()->has_call_expr() &&
               node.expr()->call_expr().function() == "_+_") {
      out_node = &node;
    }
  }

  ASSERT_NE(var_node, nullptr);
  ASSERT_NE(cond_node, nullptr);
  ASSERT_NE(out_node, nullptr);

  auto var_pos = ast->source_info().positions().find(var_node->expr()->id());
  ASSERT_NE(var_pos, ast->source_info().positions().end());
  auto var_loc = source->GetLocation(var_pos->second);
  ASSERT_TRUE(var_loc.has_value());
  EXPECT_EQ(ast->ComputeSourceLocation(var_node->expr()->id()), *var_loc);
  EXPECT_THAT(
      source->DisplayErrorLocation(*var_loc),
      testing::HasSubstr("      expression: 10\n | ..................^"));

  auto cond_pos = ast->source_info().positions().find(cond_node->expr()->id());
  ASSERT_NE(cond_pos, ast->source_info().positions().end());
  auto cond_loc = source->GetLocation(cond_pos->second);
  ASSERT_TRUE(cond_loc.has_value());
  EXPECT_EQ(ast->ComputeSourceLocation(cond_node->expr()->id()), *cond_loc);
  EXPECT_THAT(source->DisplayErrorLocation(*cond_loc),
              testing::HasSubstr("    - condition: variables.v1 > 5\n | "
                                 "..............................^"));

  auto out_pos = ast->source_info().positions().find(out_node->expr()->id());
  ASSERT_NE(out_pos, ast->source_info().positions().end());
  auto out_loc = source->GetLocation(out_pos->second);
  ASSERT_TRUE(out_loc.has_value());
  EXPECT_EQ(ast->ComputeSourceLocation(out_node->expr()->id()), *out_loc);
  EXPECT_THAT(source->DisplayErrorLocation(*out_loc),
              testing::HasSubstr("      output: \"variables.v1 + 20\"\n | "
                                 "............................^"));
}

TEST(CompilerTest, UnparseComposedAstWithMacros) {
  absl::string_view yaml = R"yaml(name: macro_policy
rule:
  variables:
    - name: var_inner
      expression: "[1, 2].all(i, i > 0)"
    - name: var_outer
      expression: "[3, 4].exists(j, j > 0 && variables.var_inner)"
  match:
    - condition: "[5, 6].all(k, k > 0)"
      output: "[1].map(m, m > 0 && variables.var_outer)"
)yaml";
  ASSERT_OK_AND_ASSIGN(auto policy, ParsePolicyFromYaml(yaml));
  ASSERT_OK_AND_ASSIGN(auto compiler, BuildTestCompiler());
  ASSERT_OK_AND_ASSIGN(auto result, CompilePolicy(*compiler, *policy));
  ASSERT_TRUE(result.IsValid()) << result.FormatIssues();
  const cel::Ast* ast = result.GetAst();
  ASSERT_NE(ast, nullptr);

  cel::expr::ParsedExpr parsed_expr;
  ASSERT_THAT(cel::AstToParsedExpr(*ast, &parsed_expr), IsOk());
  ASSERT_OK_AND_ASSIGN(std::string unparsed,
                       google::api::expr::Unparse(parsed_expr));

  EXPECT_EQ(
      unparsed,
      "cel.@block([[1, 2].all(i, i > 0), [3, 4].exists(j, j > 0 && @index0)], "
      "[5, 6].all(k, k > 0) ? optional.of([1].map(m, m > 0 && @index1)) : "
      "optional.none())");
}

TEST(CompilerTest, UnreachableMatchAfterTriviallyTrueCondition) {
  absl::string_view yaml = R"yaml(
name: cel_policy
rule:
  id: test_rule
  match:
  - condition: true
    output: '"first"'
  - condition: true
    output: '"second"'
)yaml";
  ASSERT_OK_AND_ASSIGN(auto policy, ParsePolicyFromYaml(yaml));
  ASSERT_OK_AND_ASSIGN(auto compiler, BuildTestCompiler());
  ASSERT_OK_AND_ASSIGN(auto result, CompilePolicy(*compiler, *policy));
  EXPECT_FALSE(result.IsValid());
  EXPECT_THAT(result.FormatIssues(),
              testing::HasSubstr("match creates unreachable outputs"));
}

TEST(CompilerTest, UnreachableMatchAfterUnconditionalExhaustiveSubRule) {
  absl::string_view yaml = R"yaml(
name: dead_branch
rule:
  match:
    - rule:
        match:
          - output: 1
    - output: 2
)yaml";
  ASSERT_OK_AND_ASSIGN(auto policy, ParsePolicyFromYaml(yaml));
  ASSERT_OK_AND_ASSIGN(auto compiler, BuildTestCompiler());
  ASSERT_OK_AND_ASSIGN(auto result, CompilePolicy(*compiler, *policy));
  EXPECT_FALSE(result.IsValid());
  EXPECT_THAT(result.FormatIssues(),
              testing::HasSubstr("match creates unreachable outputs"));
}

TEST(CompilerTest, UnreachableRuleAfterTriviallyTrueCondition) {
  absl::string_view yaml = R"yaml(
name: dead_rule_branch
rule:
  match:
    - condition: true
      output: 1
    - rule:
        match:
          - output: 2
)yaml";
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<CelPolicy> policy,
                       ParsePolicyFromYaml(yaml));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> compiler, BuildTestCompiler());
  ASSERT_OK_AND_ASSIGN(CelPolicyValidationResult result,
                       CompilePolicy(*compiler, *policy));
  EXPECT_FALSE(result.IsValid());
  EXPECT_THAT(result.FormatIssues(),
              testing::HasSubstr("rule creates unreachable outputs"));
}

TEST(CompilerTest, UnreachableMatchAfterUnconditionalAggregateSubRule) {
  absl::string_view yaml = R"yaml(
name: dead_branch_after_aggregate
rule:
  match:
    - rule:
        aggregate:
          - condition: x > 0
            output: 1
    - output: 2
)yaml";
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<CelPolicy> policy,
                       ParsePolicyFromYaml(yaml));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> compiler, BuildTestCompiler());
  ASSERT_OK_AND_ASSIGN(CelPolicyValidationResult result,
                       CompilePolicy(*compiler, *policy));
  EXPECT_FALSE(result.IsValid());
  EXPECT_THAT(result.FormatIssues(),
              testing::HasSubstr("match creates unreachable outputs"));
}

TEST(CompilerTest, LiterallyFalseConditionReportsError) {
  absl::string_view yaml = R"yaml(
name: cel_policy
rule:
  id: test_rule
  match:
  - condition: false
    output: dyn(1)
  - condition: x > 5
    output: 2
  - output: '"default"'
)yaml";
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<CelPolicy> policy,
                       ParsePolicyFromYaml(yaml));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> compiler, BuildTestCompiler());
  ASSERT_OK_AND_ASSIGN(CelPolicyValidationResult result,
                       CompilePolicy(*compiler, *policy));
  EXPECT_FALSE(result.IsValid());
  EXPECT_THAT(result.GetIssues(), testing::SizeIs(1));
  EXPECT_THAT(result.FormatIssues(),
              testing::HasSubstr("condition is always false"));
}

TEST(CompilerTest, AggregateRuleLiterallyFalseConditionReportsError) {
  absl::string_view yaml = R"yaml(
name: aggregate_false_condition_policy
rule:
  aggregate:
  - condition: false
    output: '"unreachable"'
  - output: '"default"'
)yaml";
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<CelPolicy> policy,
                       ParsePolicyFromYaml(yaml));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> compiler, BuildTestCompiler());
  ASSERT_OK_AND_ASSIGN(CelPolicyValidationResult result,
                       CompilePolicy(*compiler, *policy));
  EXPECT_FALSE(result.IsValid());
  EXPECT_THAT(result.FormatIssues(),
              testing::HasSubstr("condition is always false"));
}

TEST(CompilerTest, RuleWithoutMatchesReportsError) {
  absl::string_view yaml = R"yaml(
name: cel_policy
rule:
  id: test_rule
)yaml";
  ASSERT_OK_AND_ASSIGN(auto policy, ParsePolicyFromYaml(yaml));
  ASSERT_OK_AND_ASSIGN(auto compiler, BuildTestCompiler());
  ASSERT_OK_AND_ASSIGN(auto result, CompilePolicy(*compiler, *policy));
  EXPECT_FALSE(result.IsValid());
  EXPECT_THAT(result.FormatIssues(),
              testing::HasSubstr("rule does not specify match conditions"));
}

TEST(CompilerTest, ExhaustivePolicyCompiles) {
  absl::string_view yaml = R"yaml(
name: cel_policy
rule:
  id: test_rule
  variables:
  - name: test_var
    expression: 10
  match:
  - condition: variables.test_var > 15
    output: '"greater than 15"'
  - condition: variables.test_var > 5
    output: '"greater than 5"'
  - output: '"default"'
)yaml";
  ASSERT_OK_AND_ASSIGN(auto policy, ParsePolicyFromYaml(yaml));
  ASSERT_OK_AND_ASSIGN(auto compiler, BuildTestCompiler());
  ASSERT_OK_AND_ASSIGN(auto result, CompilePolicy(*compiler, *policy));
  ASSERT_TRUE(result.IsValid());
  EXPECT_TRUE(result.GetAst()->is_checked());
}

TEST(CompilerTest, NonExhaustivePolicyCompiles) {
  absl::string_view yaml = R"yaml(
name: cel_policy
rule:
  id: test_rule
  variables:
  - name: test_var
    expression: 10
  match:
  - condition: variables.test_var > 5
    output: '"greater than 5"'
)yaml";
  ASSERT_OK_AND_ASSIGN(auto policy, ParsePolicyFromYaml(yaml));
  ASSERT_OK_AND_ASSIGN(auto compiler, BuildTestCompiler());
  ASSERT_OK_AND_ASSIGN(auto result, CompilePolicy(*compiler, *policy));
  ASSERT_TRUE(result.IsValid());
}

TEST(CompilerTest, PolicyReferencesEnvInput) {
  absl::string_view yaml = R"yaml(
name: cel_policy
rule:
  id: test_rule
  match:
  - condition: spec.single_int32 > 10
    output: '"greater than 10"'
  - output: '"default"'
)yaml";
  ASSERT_OK_AND_ASSIGN(auto policy, ParsePolicyFromYaml(yaml));
  ASSERT_OK_AND_ASSIGN(auto compiler, BuildTestCompiler());
  ASSERT_OK_AND_ASSIGN(auto result, CompilePolicy(*compiler, *policy));
  ASSERT_TRUE(result.IsValid());
  EXPECT_TRUE(result.GetAst()->is_checked());
}

struct EvaluationTestCase {
  std::string name;
  std::string yaml_policy;
  struct Input {
    int64_t x;
    int64_t y;
  } input;
  ValueMatcher expected_result_matcher;
};

class PolicyEvaluationTest : public testing::TestWithParam<EvaluationTestCase> {
};

TEST_P(PolicyEvaluationTest, Evaluate) {
  const auto& test_case = GetParam();

  ASSERT_OK_AND_ASSIGN(auto compiler, BuildTestCompiler());

  ASSERT_OK_AND_ASSIGN(auto policy, ParsePolicyFromYaml(test_case.yaml_policy));
  ASSERT_OK_AND_ASSIGN(auto validation_result,
                       CompilePolicy(*compiler, *policy));
  ASSERT_TRUE(validation_result.IsValid());
  ASSERT_OK_AND_ASSIGN(auto ast, validation_result.ReleaseAst());

  // Set up runtime
  cel::RuntimeOptions opts;
  opts.enable_qualified_type_identifiers = true;
  ASSERT_OK_AND_ASSIGN(
      cel::RuntimeBuilder rt_builder,
      cel::CreateStandardRuntimeBuilder(
          cel::internal::GetSharedTestingDescriptorPool(), opts));
  ASSERT_THAT(cel::extensions::EnableOptionalTypes(rt_builder), IsOk());

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<cel::Runtime> runtime,
                       std::move(rt_builder).Build());

  ASSERT_OK_AND_ASSIGN(auto program, runtime->CreateProgram(std::move(ast)));

  // Set up activation
  cel::Activation activation;
  activation.InsertOrAssignValue("x", cel::IntValue(test_case.input.x));
  activation.InsertOrAssignValue("y", cel::IntValue(test_case.input.y));

  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(cel::Value result,
                       program->Evaluate(&arena, activation));

  EXPECT_THAT(result, test_case.expected_result_matcher);
}

constexpr absl::string_view kEvalPolicyYaml = R"yaml(
name: cel_policy
rule:
  id: test_rule
  match:
  - condition: x > 10 && y > 10
    output: '"both greater than 10"'
  - condition: x > 10
    output: '"x greater than 10"'
  - condition: y > 10
    output: '"y greater than 10"'
  - output: '"default"'
)yaml";

INSTANTIATE_TEST_SUITE_P(
    PolicyEvaluationTest, PolicyEvaluationTest,
    testing::Values(
        EvaluationTestCase{
            .name = "BothGreaterThan10",
            .yaml_policy = std::string(kEvalPolicyYaml),
            .input = {.x = 15, .y = 15},
            .expected_result_matcher = StringValueIs("both greater than 10"),
        },
        EvaluationTestCase{
            .name = "XGreaterThan10",
            .yaml_policy = std::string(kEvalPolicyYaml),
            .input = {.x = 15, .y = 5},
            .expected_result_matcher = StringValueIs("x greater than 10"),
        },
        EvaluationTestCase{
            .name = "YGreaterThan10",
            .yaml_policy = std::string(kEvalPolicyYaml),
            .input = {.x = 5, .y = 15},
            .expected_result_matcher = StringValueIs("y greater than 10"),
        },
        EvaluationTestCase{
            .name = "Default",
            .yaml_policy = std::string(kEvalPolicyYaml),
            .input = {.x = 5, .y = 5},
            .expected_result_matcher = StringValueIs("default"),
        }),
    [](const testing::TestParamInfo<PolicyEvaluationTest::ParamType>& info) {
      return info.param.name;
    });

constexpr absl::string_view kNonExhaustivePolicyYaml = R"yaml(
name: nested_rule4
rule:
  match:
    - condition: x > 0
      rule:
        match:
          - condition: x < 3
            output: 1
          - condition: x < 5
            output: 2
    - condition: x < 0
      rule:
        match:
          - condition: x > -2
            output: 3
          - condition: x > -4
            output: 4
          - output: 5
)yaml";

INSTANTIATE_TEST_SUITE_P(
    NonExhaustivePolicyEvaluation, PolicyEvaluationTest,
    testing::Values(
        EvaluationTestCase{
            .name = "XEquals0_FallthroughTopLevel",
            .yaml_policy = std::string(kNonExhaustivePolicyYaml),
            .input = {.x = 0, .y = 0},
            .expected_result_matcher = OptionalValueIsEmpty(),
        },
        EvaluationTestCase{
            .name = "XEquals2_MatchesFirstNested",
            .yaml_policy = std::string(kNonExhaustivePolicyYaml),
            .input = {.x = 2, .y = 0},
            .expected_result_matcher = OptionalValueIs(IntValueIs(1)),
        },
        EvaluationTestCase{
            .name = "XEquals6_FallthroughNested",
            .yaml_policy = std::string(kNonExhaustivePolicyYaml),
            .input = {.x = 6, .y = 0},
            .expected_result_matcher = OptionalValueIsEmpty(),
        },
        EvaluationTestCase{
            .name = "XEqualsMinus1_MatchesMinus2",
            .yaml_policy = std::string(kNonExhaustivePolicyYaml),
            .input = {.x = -1, .y = 0},
            .expected_result_matcher = OptionalValueIs(IntValueIs(3)),
        },
        EvaluationTestCase{
            .name = "XEqualsMinus3_MatchesMinus4",
            .yaml_policy = std::string(kNonExhaustivePolicyYaml),
            .input = {.x = -3, .y = 0},
            .expected_result_matcher = OptionalValueIs(IntValueIs(4)),
        },
        EvaluationTestCase{
            .name = "XEqualsMinus5_MatchesDefault",
            .yaml_policy = std::string(kNonExhaustivePolicyYaml),
            .input = {.x = -5, .y = 0},
            .expected_result_matcher = OptionalValueIs(IntValueIs(5)),
        }),
    [](const testing::TestParamInfo<PolicyEvaluationTest::ParamType>& info) {
      return info.param.name;
    });

constexpr absl::string_view kNestedVariablePolicyYaml = R"yaml(
name: nested_rule4
rule:
  variables:
    - name: i
      expression: "1"
    - name: j
      expression: "2"
  match:
    - condition: x > 0
      rule:
        variables:
          - name: k
            expression: "3"
        match:
          - output: "variables.i + variables.j + variables.k"
    - condition: x < 0
      rule:
        variables:
          - name: j
            expression: "5"
          - name: k
            expression: "4"
        match:
          - output: "variables.i + variables.j + variables.k"
    - output: "variables.i + variables.j"
)yaml";

INSTANTIATE_TEST_SUITE_P(
    NestedVariablePolicyEvaluation, PolicyEvaluationTest,
    testing::Values(
        EvaluationTestCase{
            .name = "XGreaterThan0",
            .yaml_policy = std::string(kNestedVariablePolicyYaml),
            .input = {.x = 1, .y = 0},
            .expected_result_matcher = IntValueIs(6),
        },
        EvaluationTestCase{
            .name = "XLessThan0",
            .yaml_policy = std::string(kNestedVariablePolicyYaml),
            .input = {.x = -1, .y = 0},
            .expected_result_matcher = IntValueIs(10),
        },
        EvaluationTestCase{
            .name = "XEquals0",
            .yaml_policy = std::string(kNestedVariablePolicyYaml),
            .input = {.x = 0, .y = 0},
            .expected_result_matcher = IntValueIs(3),
        }),
    [](const testing::TestParamInfo<PolicyEvaluationTest::ParamType>& info) {
      return info.param.name;
    });

constexpr absl::string_view
    kOptionalChainingUnconditionalSubRuleOptionalParentYaml = R"yaml(
name: optional_chaining
rule:
  match:
    - rule:
        id: r2
        match:
          - condition: x > 0
            output: 1
    - output: 2
      condition: x < 0
)yaml";

INSTANTIATE_TEST_SUITE_P(
    OptionalChainingUnconditionalSubRuleOptionalParent, PolicyEvaluationTest,
    testing::Values(
        EvaluationTestCase{
            .name = "XEquals1",
            .yaml_policy = std::string(
                kOptionalChainingUnconditionalSubRuleOptionalParentYaml),
            .input = {.x = 1, .y = 0},
            .expected_result_matcher = OptionalValueIs(IntValueIs(1)),
        },
        EvaluationTestCase{
            .name = "XEqualsMinus1",
            .yaml_policy = std::string(
                kOptionalChainingUnconditionalSubRuleOptionalParentYaml),
            .input = {.x = -1, .y = 0},
            .expected_result_matcher = OptionalValueIs(IntValueIs(2)),
        }),
    [](const testing::TestParamInfo<PolicyEvaluationTest::ParamType>& info) {
      return info.param.name;
    });

constexpr absl::string_view kOptionalChainingUnconditionalSubRuleYaml = R"yaml(
name: optional_chaining
rule:
  id: r1
  match:
    - rule:
        id: r2
        match:
          - condition: x > 0
            output: 1
    - output: 2
)yaml";

INSTANTIATE_TEST_SUITE_P(
    OptionalChainingUnconditionalSubRule, PolicyEvaluationTest,
    testing::Values(
        EvaluationTestCase{
            .name = "XEquals1",
            .yaml_policy =
                std::string(kOptionalChainingUnconditionalSubRuleYaml),
            .input = {.x = 1, .y = 0},
            .expected_result_matcher = IntValueIs(1),
        },
        EvaluationTestCase{
            .name = "XEqualsMinus1",
            .yaml_policy =
                std::string(kOptionalChainingUnconditionalSubRuleYaml),
            .input = {.x = -1, .y = 0},
            .expected_result_matcher = IntValueIs(2),
        }),
    [](const testing::TestParamInfo<PolicyEvaluationTest::ParamType>& info) {
      return info.param.name;
    });

constexpr absl::string_view kOptionalChainingUnconditionalComplexYaml = R"yaml(
name: optional_chaining
rule:
  match:
    - condition: x > 0
      rule:
        match:
          - rule:
              match:
                - condition: x == 1
                  output: 1
          - output: 2
    - rule:
        match:
          - condition: x == -1
            output: 3
    - condition: x == -2
      output: 4
)yaml";

INSTANTIATE_TEST_SUITE_P(
    OptionalChainingUnconditionalComplex, PolicyEvaluationTest,
    testing::Values(
        EvaluationTestCase{
            .name = "XEquals1",
            .yaml_policy =
                std::string(kOptionalChainingUnconditionalComplexYaml),
            .input = {.x = 1, .y = 0},
            .expected_result_matcher = OptionalValueIs(IntValueIs(1)),
        },
        EvaluationTestCase{
            .name = "XEqualsMinus1",
            .yaml_policy =
                std::string(kOptionalChainingUnconditionalComplexYaml),
            .input = {.x = -1, .y = 0},
            .expected_result_matcher = OptionalValueIs(IntValueIs(3)),
        },
        EvaluationTestCase{
            .name = "XEqualsMinus2",
            .yaml_policy =
                std::string(kOptionalChainingUnconditionalComplexYaml),
            .input = {.x = -2, .y = 0},
            .expected_result_matcher = OptionalValueIs(IntValueIs(4)),
        },
        EvaluationTestCase{
            .name = "XEqualsMinus3",
            .yaml_policy =
                std::string(kOptionalChainingUnconditionalComplexYaml),
            .input = {.x = -3, .y = 0},
            .expected_result_matcher = OptionalValueIsEmpty(),
        }),
    [](const testing::TestParamInfo<PolicyEvaluationTest::ParamType>& info) {
      return info.param.name;
    });

constexpr absl::string_view kUnconditionalExhaustiveSubRuleAsLastMatchYaml =
    R"yaml(
name: exhaustive_unconditional_subrule
rule:
  match:
    - condition: x > 0
      output: 1
    - rule:
        match:
          - condition: y > 0
            output: 2
          - output: 3
)yaml";

INSTANTIATE_TEST_SUITE_P(
    UnconditionalExhaustiveSubRuleAsLastMatch, PolicyEvaluationTest,
    testing::Values(
        EvaluationTestCase{
            .name = "XEquals1",
            .yaml_policy =
                std::string(kUnconditionalExhaustiveSubRuleAsLastMatchYaml),
            .input = {.x = 1, .y = 0},
            .expected_result_matcher = IntValueIs(1),
        },
        EvaluationTestCase{
            .name = "XEquals0_YEquals1",
            .yaml_policy =
                std::string(kUnconditionalExhaustiveSubRuleAsLastMatchYaml),
            .input = {.x = 0, .y = 1},
            .expected_result_matcher = IntValueIs(2),
        },
        EvaluationTestCase{
            .name = "XEquals0_YEquals0",
            .yaml_policy =
                std::string(kUnconditionalExhaustiveSubRuleAsLastMatchYaml),
            .input = {.x = 0, .y = 0},
            .expected_result_matcher = IntValueIs(3),
        }),
    [](const testing::TestParamInfo<PolicyEvaluationTest::ParamType>& info) {
      return info.param.name;
    });

constexpr absl::string_view kUnconditionalNonExhaustiveSubRuleAsLastMatchYaml =
    R"yaml(
name: non_exhaustive_unconditional_subrule
rule:
  match:
    - condition: x > 0
      output: 1
    - rule:
        match:
          - condition: y > 0
            output: 2
)yaml";

INSTANTIATE_TEST_SUITE_P(
    UnconditionalNonExhaustiveSubRuleAsLastMatch, PolicyEvaluationTest,
    testing::Values(
        EvaluationTestCase{
            .name = "XEquals1",
            .yaml_policy =
                std::string(kUnconditionalNonExhaustiveSubRuleAsLastMatchYaml),
            .input = {.x = 1, .y = 0},
            .expected_result_matcher = OptionalValueIs(IntValueIs(1)),
        },
        EvaluationTestCase{
            .name = "XEquals0_YEquals1",
            .yaml_policy =
                std::string(kUnconditionalNonExhaustiveSubRuleAsLastMatchYaml),
            .input = {.x = 0, .y = 1},
            .expected_result_matcher = OptionalValueIs(IntValueIs(2)),
        },
        EvaluationTestCase{
            .name = "XEquals0_YEquals0",
            .yaml_policy =
                std::string(kUnconditionalNonExhaustiveSubRuleAsLastMatchYaml),
            .input = {.x = 0, .y = 0},
            .expected_result_matcher = OptionalValueIsEmpty(),
        }),
    [](const testing::TestParamInfo<PolicyEvaluationTest::ParamType>& info) {
      return info.param.name;
    });

TEST(CompilerTest, ImportsAndAbbreviations) {
  absl::string_view yaml = R"yaml(
name: imports_test
imports:
  - name: cel.expr.conformance.proto3.TestAllTypes
rule:
  match:
    - condition: 'spec == TestAllTypes{single_int32: 10}'
      output: '"matched"'
    - output: '"default"'
)yaml";
  ASSERT_OK_AND_ASSIGN(auto policy, ParsePolicyFromYaml(yaml));
  ASSERT_OK_AND_ASSIGN(auto compiler, BuildTestCompiler());
  auto ast_or = CompilePolicy(*compiler, *policy);
  ASSERT_THAT(ast_or, IsOk());
}

TEST(CompilerTest, MatchWithoutProductionReportsError) {
  absl::string_view yaml = R"yaml(
name: cel_policy
rule:
  id: test_rule
  match:
    - condition: true
)yaml";
  ASSERT_OK_AND_ASSIGN(auto policy, ParsePolicyFromYaml(yaml));
  ASSERT_OK_AND_ASSIGN(auto compiler, BuildTestCompiler());
  ASSERT_OK_AND_ASSIGN(auto result, CompilePolicy(*compiler, *policy));
  EXPECT_FALSE(result.IsValid());
  EXPECT_THAT(result.FormatIssues(),
              testing::HasSubstr("match must specify an output or rule"));
}

int GetAstHeight(const cel::Ast& ast) {
  auto nav_ast = cel::NavigableAst::Build(ast.root_expr());
  return nav_ast.Root().height();
}

TEST(CompilerTest, UnnestHeightValidation) {
  absl::string_view yaml = R"yaml(
name: cel_policy
rule:
  id: test_rule
  match:
  - condition: true
    output: '"ok"'
)yaml";
  ASSERT_OK_AND_ASSIGN(auto policy, ParsePolicyFromYaml(yaml));
  ASSERT_OK_AND_ASSIGN(auto compiler, BuildTestCompiler());

  CompilePolicyOptions options;
  options.unnesting_height_limit = 1;
  auto status_or = CompilePolicy(*compiler, *policy, options);
  EXPECT_THAT(status_or.status(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       testing::HasSubstr(
                           "unnesting_height_limit must be at least 2")));

  options.unnesting_height_limit = 2;
  EXPECT_THAT(CompilePolicy(*compiler, *policy, options), IsOk());
}

constexpr absl::string_view kDeepPolicyYaml = R"yaml(
name: deep_policy
rule:
  match:
    - condition: x > 0
      rule:
        match:
          - condition: x > 1
            rule:
              match:
                - condition: x > 2
                  rule:
                    match:
                      - condition: x > 3
                        rule:
                          match:
                            - condition: x > 4
                              rule:
                                match:
                                  - condition: x > 5
                                    output: 6
                                  - output: 5
                            - output: 4
                      - output: 3
                - output: 2
          - output: 1
    - output: 0
)yaml";

TEST(CompilerTest, UnnestHeightReduction) {
  ASSERT_OK_AND_ASSIGN(auto policy, ParsePolicyFromYaml(kDeepPolicyYaml));
  ASSERT_OK_AND_ASSIGN(auto compiler, BuildTestCompiler());

  // Compile without unnesting
  CompilePolicyOptions options_no_unnest;
  options_no_unnest.unnesting_height_limit = 0;
  ASSERT_OK_AND_ASSIGN(auto result_no_unnest,
                       CompilePolicy(*compiler, *policy, options_no_unnest));
  ASSERT_TRUE(result_no_unnest.IsValid());
  ASSERT_OK_AND_ASSIGN(auto ast_no_unnest, result_no_unnest.ReleaseAst());
  int height_no_unnest = GetAstHeight(*ast_no_unnest);

  CompilePolicyOptions options_unnest;
  options_unnest.unnesting_height_limit = 2;
  ASSERT_OK_AND_ASSIGN(auto result_unnest,
                       CompilePolicy(*compiler, *policy, options_unnest));
  ASSERT_TRUE(result_unnest.IsValid());
  ASSERT_OK_AND_ASSIGN(auto ast_unnest, result_unnest.ReleaseAst());
  int height_unnest = GetAstHeight(*ast_unnest);

  EXPECT_EQ(height_no_unnest, 8);
  EXPECT_EQ(height_unnest, 5);
  EXPECT_LT(height_unnest, height_no_unnest);
}

TEST(CompilerTest, UnnestComprehensionFailure) {
  absl::string_view yaml = R"yaml(
name: comprehension_policy
rule:
  match:
    - condition: x > 0
      rule:
        match:
          - condition: "[1, 2].all(i, i > x)"
            output: 1
          - output: 2
    - output: 0
)yaml";
  ASSERT_OK_AND_ASSIGN(auto policy, ParsePolicyFromYaml(yaml));
  ASSERT_OK_AND_ASSIGN(auto compiler, BuildTestCompiler());

  CompilePolicyOptions options;
  options.unnesting_height_limit = 2;
  ASSERT_OK_AND_ASSIGN(auto result, CompilePolicy(*compiler, *policy, options));
  EXPECT_FALSE(result.IsValid());
  EXPECT_THAT(result.FormatIssues(),
              testing::HasSubstr("cannot unnest AST due to comprehension"));
}

struct UnnestEvaluationTestCase {
  std::string name;
  int64_t x;
  ValueMatcher expected;
};

class UnnestedDeepPolicyEvaluationTest
    : public testing::TestWithParam<UnnestEvaluationTestCase> {};

TEST_P(UnnestedDeepPolicyEvaluationTest, Evaluate) {
  const auto& tc = GetParam();
  ASSERT_OK_AND_ASSIGN(auto policy, ParsePolicyFromYaml(kDeepPolicyYaml));
  ASSERT_OK_AND_ASSIGN(auto compiler, BuildTestCompiler());

  CompilePolicyOptions options;
  options.unnesting_height_limit = 2;
  ASSERT_OK_AND_ASSIGN(auto result, CompilePolicy(*compiler, *policy, options));
  ASSERT_TRUE(result.IsValid());
  ASSERT_OK_AND_ASSIGN(auto ast, result.ReleaseAst());

  // Set up runtime
  cel::RuntimeOptions opts;
  opts.enable_qualified_type_identifiers = true;
  ASSERT_OK_AND_ASSIGN(
      cel::RuntimeBuilder rt_builder,
      cel::CreateStandardRuntimeBuilder(
          cel::internal::GetSharedTestingDescriptorPool(), opts));
  ASSERT_THAT(cel::extensions::EnableOptionalTypes(rt_builder), IsOk());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<cel::Runtime> runtime,
                       std::move(rt_builder).Build());

  ASSERT_OK_AND_ASSIGN(auto program, runtime->CreateProgram(std::move(ast)));

  cel::Activation activation;
  activation.InsertOrAssignValue("x", cel::IntValue(tc.x));
  google::protobuf::Arena arena;

  ASSERT_OK_AND_ASSIGN(cel::Value res, program->Evaluate(&arena, activation));

  EXPECT_THAT(res, tc.expected);
}

INSTANTIATE_TEST_SUITE_P(
    UnnestedDeepPolicyEvaluation, UnnestedDeepPolicyEvaluationTest,
    testing::Values(UnnestEvaluationTestCase{"XEquals6", 6, IntValueIs(6)},
                    UnnestEvaluationTestCase{"XEquals5", 5, IntValueIs(5)},
                    UnnestEvaluationTestCase{"XEquals4", 4, IntValueIs(4)},
                    UnnestEvaluationTestCase{"XEquals3", 3, IntValueIs(3)},
                    UnnestEvaluationTestCase{"XEquals2", 2, IntValueIs(2)},
                    UnnestEvaluationTestCase{"XEquals1", 1, IntValueIs(1)},
                    UnnestEvaluationTestCase{"XEquals0", 0, IntValueIs(0)},
                    UnnestEvaluationTestCase{"XEqualsMinus1", -1,
                                             IntValueIs(0)}),
    [](const testing::TestParamInfo<
        UnnestedDeepPolicyEvaluationTest::ParamType>& info) {
      return info.param.name;
    });

TEST(CompilerTest, UnnestCleanupRunsWhenDisabled) {
  // A policy without variables and without nesting.
  absl::string_view yaml = R"yaml(
name: cel_policy
rule:
  id: test_rule
  match:
  - condition: true
    output: '"ok"'
)yaml";
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<CelPolicy> policy,
                       ParsePolicyFromYaml(yaml));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> compiler, BuildTestCompiler());

  CompilePolicyOptions options;
  options.unnesting_height_limit = 0;  // Disabled
  ASSERT_OK_AND_ASSIGN(CelPolicyValidationResult result,
                       CompilePolicy(*compiler, *policy, options));
  ASSERT_TRUE(result.IsValid());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Ast> ast, result.ReleaseAst());

  // If cleanup ran, it should have optimized away the trivial `cel.@block`.
  // So the root expression should NOT be a call to `cel.@block`.
  // It should be just the constant `"ok"`.
  NavigableAst nav_ast = cel::NavigableAst::Build(ast->root_expr());
  EXPECT_FALSE(nav_ast.Root().expr()->has_call_expr() &&
               nav_ast.Root().expr()->call_expr().function() == "cel.@block");
  EXPECT_TRUE(nav_ast.Root().expr()->has_const_expr());

  // Stale metadata for cel.@block and empty list must be pruned.
  for (const auto& [id, ref] : ast->reference_map()) {
    EXPECT_NE(ref.name(), "cel.@block");
  }

  EXPECT_THAT(ast, MetadataMapValid());
}

TEST(CompilerTest, UnnestCleanupRemovesStaleMetadata) {
  absl::string_view yaml = R"yaml(
name: cel_policy
rule:
  id: test_rule
  match:
  - condition: x > 0
    output: 1
  - output: 2
)yaml";
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<CelPolicy> policy,
                       ParsePolicyFromYaml(yaml));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> compiler, BuildTestCompiler());

  ASSERT_OK_AND_ASSIGN(CelPolicyValidationResult result,
                       CompilePolicy(*compiler, *policy));
  ASSERT_TRUE(result.IsValid());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Ast> ast, result.ReleaseAst());

  NavigableAst nav_ast = cel::NavigableAst::Build(ast->root_expr());
  EXPECT_FALSE(nav_ast.Root().expr()->has_call_expr() &&
               nav_ast.Root().expr()->call_expr().function() == "cel.@block");

  for (const auto& [id, ref] : ast->reference_map()) {
    EXPECT_NE(ref.name(), "cel.@block");
  }

  EXPECT_THAT(ast, MetadataMapValid());
}

TEST(CompilerTest, UnnestPreservesVariablesBlock) {
  absl::string_view yaml = R"yaml(
name: cel_policy
rule:
  id: test_rule
  variables:
  - name: my_var
    expression: 10
  match:
  - condition: x > variables.my_var
    output: 1
  - output: 2
)yaml";
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<CelPolicy> policy,
                       ParsePolicyFromYaml(yaml));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> compiler, BuildTestCompiler());

  CompilePolicyOptions options;
  options.unnesting_height_limit = 0;
  ASSERT_OK_AND_ASSIGN(CelPolicyValidationResult result,
                       CompilePolicy(*compiler, *policy, options));
  ASSERT_TRUE(result.IsValid());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Ast> ast, result.ReleaseAst());

  NavigableAst nav_ast = cel::NavigableAst::Build(ast->root_expr());
  EXPECT_TRUE(nav_ast.Root().expr()->has_call_expr() &&
              nav_ast.Root().expr()->call_expr().function() == "cel.@block");

  EXPECT_THAT(ast, MetadataMapValid());
}

TEST(CompilerTest, UnnestRequiredLongConditionalChain) {
  absl::string_view yaml = R"yaml(
name: conditional_chain_policy
rule:
  match:
  - condition: x == 1
    output: 10
  - condition: x == 2
    output: 20
  - condition: x == 3
    output: 30
  - condition: x == 4
    output: 40
  - condition: x == 5
    output: 50
  - condition: x == 6
    output: 60
  - condition: x == 7
    output: 70
  - output: 0
)yaml";
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<CelPolicy> policy,
                       ParsePolicyFromYaml(yaml));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> compiler, BuildTestCompiler());

  CompilePolicyOptions options;
  options.unnesting_height_limit = 2;
  ASSERT_OK_AND_ASSIGN(CelPolicyValidationResult result,
                       CompilePolicy(*compiler, *policy, options));
  ASSERT_TRUE(result.IsValid());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Ast> ast, result.ReleaseAst());

  // Root should be cel.@block because unnesting introduced @index variables.
  NavigableAst nav_ast = cel::NavigableAst::Build(ast->root_expr());
  EXPECT_TRUE(nav_ast.Root().expr()->has_call_expr() &&
              nav_ast.Root().expr()->call_expr().function() == "cel.@block");

  bool found_index_var = false;
  for (const NavigableAstNode& node : nav_ast.Root().DescendantsPostorder()) {
    if (node.expr()->has_ident_expr() &&
        absl::StartsWith(node.expr()->ident_expr().name(), "@index")) {
      found_index_var = true;
      EXPECT_TRUE(ast->type_map().contains(node.expr()->id()));
      EXPECT_TRUE(ast->reference_map().contains(node.expr()->id()));
      break;
    }
  }
  EXPECT_TRUE(found_index_var);

  EXPECT_THAT(ast, MetadataMapValid());

  // Also verify that the compiled unnested AST evaluates correctly.
  cel::RuntimeOptions opts;
  opts.enable_qualified_type_identifiers = true;
  ASSERT_OK_AND_ASSIGN(
      cel::RuntimeBuilder rt_builder,
      cel::CreateStandardRuntimeBuilder(
          cel::internal::GetSharedTestingDescriptorPool(), opts));
  ASSERT_THAT(cel::extensions::EnableOptionalTypes(rt_builder),
              absl_testing::IsOk());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<cel::Runtime> runtime,
                       std::move(rt_builder).Build());

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<cel::Program> program,
                       runtime->CreateProgram(std::move(ast)));

  google::protobuf::Arena arena;
  for (int64_t test_x : {1, 3, 5, 7, 99}) {
    cel::Activation activation;
    activation.InsertOrAssignValue("x", cel::IntValue(test_x));
    ASSERT_OK_AND_ASSIGN(cel::Value res, program->Evaluate(&arena, activation));
    int64_t expected = (test_x >= 1 && test_x <= 7) ? test_x * 10 : 0;
    EXPECT_THAT(res, IntValueIs(expected));
  }
}

TEST(CompilerTest, CompositionTypeCheckFailureReportsError) {
  absl::string_view yaml = R"yaml(
name: cel_policy
rule:
  id: test_rule
  match:
  - condition: x > 10
    output: dyn(1)
  - condition: x > 5
    output: 2
  - output: '"three"'
)yaml";
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<CelPolicy> policy,
                       ParsePolicyFromYaml(yaml));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> compiler, BuildTestCompiler());
  ASSERT_OK_AND_ASSIGN(CelPolicyValidationResult result,
                       CompilePolicy(*compiler, *policy));
  EXPECT_FALSE(result.IsValid());
  EXPECT_THAT(result.FormatIssues(),
              testing::HasSubstr("no matching overload for '_?_:_'"));
}

TEST(CompilerTest, AggregateRuleReturnsUnimplementedError) {
  absl::string_view yaml = R"yaml(
name: aggregate_policy
rule:
  aggregate:
  - output: '"first"'
  - condition: true
    output: '"second"'
  - condition: y > 5
    output: '"third"'
)yaml";
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<CelPolicy> policy,
                       ParsePolicyFromYaml(yaml));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> compiler, BuildTestCompiler());
  EXPECT_THAT(CompilePolicy(*compiler, *policy),
              StatusIs(absl::StatusCode::kUnimplemented));
}

TEST(CompilerTest, NestedAggregateRuleReturnsUnimplementedError) {
  absl::string_view yaml = R"yaml(
name: nested_aggregate_policy
rule:
  match:
  - condition: x > 10
    rule:
      aggregate:
      - output: '"first"'
)yaml";
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<CelPolicy> policy,
                       ParsePolicyFromYaml(yaml));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> compiler, BuildTestCompiler());
  EXPECT_THAT(CompilePolicy(*compiler, *policy),
              StatusIs(absl::StatusCode::kUnimplemented));
}

TEST(CompilerTest, NestedAggregateUnderAggregateReportsError) {
  absl::string_view yaml = R"yaml(
name: nested_aggregate_policy
rule:
  aggregate:
  - condition: x > 10
    rule:
      aggregate:
      - output: '"first"'
)yaml";
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<CelPolicy> policy,
                       ParsePolicyFromYaml(yaml));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> compiler, BuildTestCompiler());
  ASSERT_OK_AND_ASSIGN(CelPolicyValidationResult result,
                       CompilePolicy(*compiler, *policy));
  EXPECT_FALSE(result.IsValid());
  EXPECT_THAT(result.FormatIssues(),
              testing::HasSubstr("nested aggregate rules are not allowed"));
}

TEST(CompilerTest, NestedAggregateUnderMatchUnderAggregateReportsError) {
  absl::string_view yaml = R"yaml(
name: nested_aggregate_mixed_policy
rule:
  aggregate:
  - condition: x > 10
    rule:
      match:
      - condition: y > 5
        rule:
          aggregate:
          - output: '"first"'
)yaml";
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<CelPolicy> policy,
                       ParsePolicyFromYaml(yaml));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> compiler, BuildTestCompiler());
  ASSERT_OK_AND_ASSIGN(CelPolicyValidationResult result,
                       CompilePolicy(*compiler, *policy));
  EXPECT_FALSE(result.IsValid());
  EXPECT_THAT(result.FormatIssues(),
              testing::HasSubstr("nested aggregate rules are not allowed"));
}

}  // namespace
}  // namespace cel
