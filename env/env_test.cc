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

#include "env/env.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "checker/type_check_issue.h"
#include "checker/type_checker_builder.h"
#include "checker/validation_result.h"
#include "common/ast.h"
#include "common/constant.h"
#include "common/decl.h"
#include "common/expr.h"
#include "common/type.h"
#include "common/value.h"
#include "compiler/compiler.h"
#include "env/config.h"
#include "internal/status_macros.h"
#include "internal/testing.h"
#include "internal/testing_descriptor_pool.h"
#include "parser/macro.h"
#include "parser/macro_expr_factory.h"
#include "parser/parser_interface.h"
#include "runtime/activation.h"
#include "runtime/reference_resolver.h"
#include "runtime/runtime.h"
#include "runtime/runtime_builder.h"
#include "runtime/runtime_options.h"
#include "runtime/standard_runtime_builder_factory.h"
#include "google/protobuf/arena.h"

namespace cel {
namespace {

using ::absl_testing::IsOk;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Property;
using ::testing::UnorderedElementsAre;
using ::testing::Values;
using ::testing::ValuesIn;

Expr TestMacroExpander(MacroExprFactory& factory, absl::Span<Expr> args) {
  return factory.NewStringConst("Hello");
}

class TestLibrary : public CompilerLibrary {
 public:
  explicit TestLibrary(int version)
      : CompilerLibrary(
            "testlib",
            [version](ParserBuilder& builder) {
              absl::Status status;
              CEL_ASSIGN_OR_RETURN(
                  auto macro1,
                  cel::Macro::Global("testMacro1", 0, TestMacroExpander));
              status.Update(builder.AddMacro(macro1));
              if (version == 2) {
                CEL_ASSIGN_OR_RETURN(
                    auto macro2,
                    cel::Macro::Global("testMacro2", 0, TestMacroExpander));
                status.Update(builder.AddMacro(macro2));
              }
              return status;
            },
            [version](TypeCheckerBuilder& builder) {
              absl::Status status;
              CEL_ASSIGN_OR_RETURN(
                  auto func1, cel::MakeFunctionDecl(
                                  "testFunc1", MakeOverloadDecl(StringType())));
              status.Update(builder.AddFunction(func1));
              if (version == 2) {
                CEL_ASSIGN_OR_RETURN(
                    auto func2,
                    cel::MakeFunctionDecl("testFunc2",
                                          MakeOverloadDecl(StringType())));
                status.Update(builder.AddFunction(func2));
              }
              return status;
            }) {};
};

absl::StatusOr<cel::Value> CompileAndEvalExpr(
    Env& env, absl::string_view expr,
    const Activation& activation = Activation()) {
  CEL_ASSIGN_OR_RETURN(std::unique_ptr<Compiler> compiler, env.NewCompiler());
  if (compiler == nullptr) {
    return absl::InternalError("Failed to create compiler");
  }
  CEL_ASSIGN_OR_RETURN(ValidationResult result, compiler->Compile(expr));
  if (!result.GetIssues().empty()) {
    return absl::InvalidArgumentError(result.FormatError());
  }

  cel::RuntimeOptions opts;
  CEL_ASSIGN_OR_RETURN(
      cel::RuntimeBuilder rt_builder,
      cel::CreateStandardRuntimeBuilder(env.GetDescriptorPool(), opts));
  CEL_RETURN_IF_ERROR(cel::EnableReferenceResolver(
      rt_builder, cel::ReferenceResolverEnabled::kAlways));
  CEL_ASSIGN_OR_RETURN(std::unique_ptr<cel::Runtime> runtime,
                       std::move(rt_builder).Build());
  if (runtime == nullptr) {
    return absl::InternalError("Failed to create runtime");
  }

  CEL_ASSIGN_OR_RETURN(std::unique_ptr<Ast> ast, result.ReleaseAst());
  if (ast == nullptr) {
    return absl::InternalError("Failed to create AST");
  }
  google::protobuf::Arena arena;
  CEL_ASSIGN_OR_RETURN(std::unique_ptr<Program> program,
                       runtime->CreateProgram(std::move(ast)));
  if (program == nullptr) {
    return absl::InternalError("Failed to create program");
  }
  CEL_ASSIGN_OR_RETURN(Value value, program->Evaluate(&arena, activation));
  return value;
}

absl::StatusOr<bool> CompileAndEvalBooleanExpr(
    Env& env, absl::string_view expr,
    const Activation& activation = Activation()) {
  CEL_ASSIGN_OR_RETURN(auto value, CompileAndEvalExpr(env, expr, activation));
  return value.GetBool();
}

class LibraryConfigTest : public testing::Test {
 protected:
  void SetUp() override {
    env_.RegisterCompilerLibrary("testlib", "ml", 0,
                                 []() { return TestLibrary(0); });
    env_.RegisterCompilerLibrary("testlib", "ml", 1,
                                 []() { return TestLibrary(1); });
    env_.RegisterCompilerLibrary("testlib", "ml", 2,
                                 []() { return TestLibrary(2); });
    env_.SetDescriptorPool(internal::GetSharedTestingDescriptorPool());
  }

  Env env_;
};

TEST_F(LibraryConfigTest, LatestVersion) {
  Config config;
  ASSERT_THAT(
      config.AddExtensionConfig("testlib", Config::ExtensionConfig::kLatest),
      IsOk());

  env_.SetConfig(config);

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> compiler, env_.NewCompiler());
  ASSERT_OK_AND_ASSIGN(ValidationResult result1,
                       compiler->Compile("testMacro1()"));
  ASSERT_OK_AND_ASSIGN(ValidationResult result2,
                       compiler->Compile("testFunc1()"));
  ASSERT_OK_AND_ASSIGN(ValidationResult result3,
                       compiler->Compile("testMacro2()"));
  ASSERT_OK_AND_ASSIGN(ValidationResult result4,
                       compiler->Compile("testFunc2()"));

  EXPECT_THAT(result1.GetIssues(), IsEmpty());
  EXPECT_THAT(result2.GetIssues(), IsEmpty());
  EXPECT_THAT(result3.GetIssues(), IsEmpty());
  EXPECT_THAT(result4.GetIssues(), IsEmpty());
}

TEST_F(LibraryConfigTest, DefaultVersionIsZero) {
  Config config;
  ASSERT_THAT(config.AddExtensionConfig("testlib"), IsOk());

  env_.SetConfig(config);

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> compiler, env_.NewCompiler());
  ASSERT_OK_AND_ASSIGN(ValidationResult result1,
                       compiler->Compile("testMacro1()"));
  ASSERT_OK_AND_ASSIGN(ValidationResult result2,
                       compiler->Compile("testFunc1()"));
  ASSERT_OK_AND_ASSIGN(ValidationResult result3,
                       compiler->Compile("testMacro2()"));
  ASSERT_OK_AND_ASSIGN(ValidationResult result4,
                       compiler->Compile("testFunc2()"));

  EXPECT_THAT(result1.GetIssues(), IsEmpty());
  EXPECT_THAT(result2.GetIssues(), IsEmpty());
  EXPECT_THAT(result3.GetIssues(),
              UnorderedElementsAre(
                  Property(&TypeCheckIssue::message,
                           HasSubstr("undeclared reference to 'testMacro2'"))));
  EXPECT_THAT(result4.GetIssues(),
              UnorderedElementsAre(
                  Property(&TypeCheckIssue::message,
                           HasSubstr("undeclared reference to 'testFunc2'"))));
}

TEST_F(LibraryConfigTest, SpecificVersion) {
  Config config;
  ASSERT_THAT(config.AddExtensionConfig("testlib", 1), IsOk());

  env_.SetConfig(config);

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> compiler, env_.NewCompiler());
  ASSERT_OK_AND_ASSIGN(auto result1, compiler->Compile("testMacro1()"));
  ASSERT_OK_AND_ASSIGN(auto result2, compiler->Compile("testFunc1()"));
  ASSERT_OK_AND_ASSIGN(auto result3, compiler->Compile("testMacro2()"));
  ASSERT_OK_AND_ASSIGN(auto result4, compiler->Compile("testFunc2()"));

  EXPECT_THAT(result1.GetIssues(), IsEmpty());
  EXPECT_THAT(result2.GetIssues(), IsEmpty());
  EXPECT_THAT(result3.GetIssues(),
              UnorderedElementsAre(
                  Property(&TypeCheckIssue::message,
                           HasSubstr("undeclared reference to 'testMacro2'"))));
  EXPECT_THAT(result4.GetIssues(),
              UnorderedElementsAre(
                  Property(&TypeCheckIssue::message,
                           HasSubstr("undeclared reference to 'testFunc2'"))));
}

struct StandardLibraryConfigTestCase {
  Config::StandardLibraryConfig standard_library_config;
  std::vector<std::string> expected_valid_expressions;
  std::vector<std::string> expected_invalid_expressions;
};

class StandardLibraryConfigTest
    : public testing::TestWithParam<StandardLibraryConfigTestCase> {};

TEST_P(StandardLibraryConfigTest, StandardLibraryConfig) {
  const StandardLibraryConfigTestCase& param = GetParam();
  Env env;
  env.SetDescriptorPool(internal::GetSharedTestingDescriptorPool());

  Config config;
  ASSERT_THAT(config.SetStandardLibraryConfig(param.standard_library_config),
              IsOk());
  env.SetConfig(config);

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> compiler, env.NewCompiler());

  for (const std::string& expr : param.expected_valid_expressions) {
    ASSERT_OK_AND_ASSIGN(auto result1, compiler->Compile(expr));
    EXPECT_THAT(result1.GetIssues(), IsEmpty())
        << "With config: " << param.standard_library_config
        << ", expected no issues for expr: " << expr
        << " but got: " << result1.FormatError();
  }
  for (const std::string& expr : param.expected_invalid_expressions) {
    ASSERT_OK_AND_ASSIGN(auto result1, compiler->Compile(expr));
    EXPECT_THAT(result1.GetIssues(), Not(IsEmpty()))
        << "With config: " << param.standard_library_config
        << ", expected compilation error for expr: " << expr << " but got: \'"
        << result1.FormatError() << "\'";
  }
}

INSTANTIATE_TEST_SUITE_P(
    StandardLibraryConfigTest, StandardLibraryConfigTest,
    Values(
        StandardLibraryConfigTestCase{
            .standard_library_config = {},
            .expected_valid_expressions = {"1 + 2",
                                           "[1, 2, 3].exists(x, x == 1)",
                                           "[1, 2, 3].all(x, x == 1)",
                                           "[1, 2, 3].map(x, x)"},
        },
        StandardLibraryConfigTestCase{
            .standard_library_config = {.disable = true},
            .expected_invalid_expressions = {"1 + 2",
                                             "[1, 2, 3].exists(x, x == 1)",
                                             "[1, 2, 3].all(x, x == 1)",
                                             "[1, 2, 3].map(x, x)"},
        },
        StandardLibraryConfigTestCase{
            .standard_library_config = {.disable_macros = true},
            .expected_valid_expressions = {"1 + 2"},
            .expected_invalid_expressions = {"[1, 2, 3].exists(x, x == 1)",
                                             "[1, 2, 3].all(x, x == 1)",
                                             "[1, 2, 3].map(x, x)"},
        },
        StandardLibraryConfigTestCase{
            .standard_library_config = {.excluded_macros = {"map", "all"}},
            .expected_valid_expressions = {"[1, 2, 3].exists(x, x == 1)"},
            .expected_invalid_expressions = {"[1, 2, 3].all(x, x == 1)",
                                             "[1, 2, 3].map(x, x)"},
        },
        StandardLibraryConfigTestCase{
            .standard_library_config = {.included_macros = {"map", "all"}},
            .expected_valid_expressions = {"[1, 2, 3].all(x, x == 1)",
                                           "[1, 2, 3].map(x, x)"},
            .expected_invalid_expressions = {"[1, 2, 3].exists(x, x == 1)"},
        },
        StandardLibraryConfigTestCase{
            .standard_library_config = {.excluded_functions = {{"_+_", ""}}},
            .expected_invalid_expressions = {"1 + 2", "[1, 2, 3] + [4, 5, 6]",
                                             "'hello' + 'world'"},
        },
        StandardLibraryConfigTestCase{
            .standard_library_config =
                {.excluded_functions = {{"_+_", "_+_(bytes,bytes)"},
                                        {"_+_", "_+_(list<~A>,list<~A>)"},
                                        {"_+_", "_+_(string,string)"}}},
            .expected_valid_expressions = {"1 + 2"},
            .expected_invalid_expressions = {"[1, 2, 3] + [4, 5, 6]",
                                             "'hello' + 'world'"},
        },
        StandardLibraryConfigTestCase{
            .standard_library_config =
                {.excluded_functions = {{"_+_", "add_bytes"},
                                        {"_+_", "add_list"},
                                        {"_+_", "add_string"}}},
            .expected_valid_expressions = {"1 + 2"},
            .expected_invalid_expressions = {"[1, 2, 3] + [4, 5, 6]",
                                             "'hello' + 'world'"},
        },
        StandardLibraryConfigTestCase{
            .standard_library_config = {.included_functions = {{"_+_", ""}}},
            .expected_valid_expressions = {"1 + 2", "[1, 2, 3] + [4, 5, 6]",
                                           "'hello' + 'world'"},
        },
        StandardLibraryConfigTestCase{
            .standard_library_config =
                {.included_functions = {{"_+_", "_+_(int,int)"},
                                        {"_+_", "_+_(list<~A>,list<~A>)"}}},
            .expected_valid_expressions = {"1 + 2", "[1, 2, 3] + [4, 5, 6]"},
            .expected_invalid_expressions = {"'hello' + 'world'"},
        },
        StandardLibraryConfigTestCase{
            .standard_library_config =
                {.included_functions = {{"_+_", "add_int64"},
                                        {"_+_", "add_list"}}},
            .expected_valid_expressions = {"1 + 2", "[1, 2, 3] + [4, 5, 6]"},
            .expected_invalid_expressions = {"'hello' + 'world'"},
        }));

TEST(ContainerConfigTest, ContainerConfig) {
  Env env;
  env.SetDescriptorPool(internal::GetSharedTestingDescriptorPool());
  Config config;
  config.SetContainerConfig({.name = "cel.expr.conformance.proto2"});
  env.SetConfig(config);
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> compiler, env.NewCompiler());
  ASSERT_OK_AND_ASSIGN(auto result, compiler->Compile("TestAllTypes{}"));

  EXPECT_THAT(result.GetIssues(), IsEmpty()) << result.FormatError();
}

TEST(ContainerConfigTest, ContainerConfigWithAbbreviations) {
  Env env;
  env.SetDescriptorPool(internal::GetSharedTestingDescriptorPool());
  Config config;
  config.SetContainerConfig(
      {.name = "cel.expr.conformance",
       .abbreviations = {"cel.expr.conformance.proto2.TestAllTypes"}});
  env.SetConfig(config);
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> compiler, env.NewCompiler());
  ASSERT_OK_AND_ASSIGN(auto result, compiler->Compile("TestAllTypes{}"));

  EXPECT_THAT(result.GetIssues(), IsEmpty()) << result.FormatError();
}

TEST(ContainerConfigTest, ContainerConfigWithAliases) {
  Env env;
  env.SetDescriptorPool(internal::GetSharedTestingDescriptorPool());
  Config config;
  config.SetContainerConfig(
      {.name = "cel.expr.conformance",
       .aliases = {
           {.alias = "MyTestType",
            .qualified_name = "cel.expr.conformance.proto2.TestAllTypes"}}});
  env.SetConfig(config);
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> compiler, env.NewCompiler());
  ASSERT_OK_AND_ASSIGN(auto result, compiler->Compile("MyTestType{}"));

  EXPECT_THAT(result.GetIssues(), IsEmpty()) << result.FormatError();
}

TEST(ContextVariableConfigTest, Basic) {
  Env env;
  env.SetDescriptorPool(internal::GetSharedTestingDescriptorPool());
  Config config;
  config.SetContextType("cel.expr.conformance.proto3.TestAllTypes");
  env.SetConfig(config);

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> compiler, env.NewCompiler());

  // Top-level fields of TestAllTypes like "single_int32" should resolve
  // successfully.
  ASSERT_OK_AND_ASSIGN(auto result, compiler->Compile("single_int32 > 10"));
  EXPECT_THAT(result.GetIssues(), IsEmpty());

  ASSERT_OK_AND_ASSIGN(auto result_invalid,
                       compiler->Compile("non_existent_field > 10"));
  EXPECT_THAT(result_invalid.GetIssues(), Not(IsEmpty()));
}

struct VariableConfigWithValueTestCase {
  Config::VariableConfig variable_config;
  std::string validate_type_expr;
  std::string validate_value_expr;
};

class VariableConfigWithValueTest
    : public testing::TestWithParam<VariableConfigWithValueTestCase> {};

TEST_P(VariableConfigWithValueTest, VariableConfigWithValue) {
  const VariableConfigWithValueTestCase& param = GetParam();

  Env env;
  env.SetDescriptorPool(internal::GetSharedTestingDescriptorPool());
  Config config;
  ASSERT_THAT(config.AddVariableConfig(param.variable_config), IsOk());
  env.SetConfig(config);
  ASSERT_OK_AND_ASSIGN(
      bool type_as_expected,
      CompileAndEvalBooleanExpr(env, param.validate_type_expr));
  ASSERT_TRUE(type_as_expected) << " expr: " << param.validate_type_expr;
  if (!param.validate_value_expr.empty()) {
    ASSERT_OK_AND_ASSIGN(
        bool value_as_expected,
        CompileAndEvalBooleanExpr(env, param.validate_value_expr));
    ASSERT_TRUE(value_as_expected) << " expr: " << param.validate_value_expr;
  }
}

Config::VariableConfig MakeConstant(
    absl::string_view variable_name, absl::string_view type_name,
    absl::AnyInvocable<void(Constant&)> setter) {
  Config::VariableConfig variable_config;
  variable_config.name = variable_name;
  Constant c;
  setter(c);
  variable_config.type_info.name = type_name;
  variable_config.value = c;
  return variable_config;
}

std::vector<VariableConfigWithValueTestCase>
GetVariableConfigWithValueTestCases() {
  return {
      VariableConfigWithValueTestCase{
          .variable_config = MakeConstant(
              "x", "null", [](auto& c) { c.set_null_value(nullptr); }),
          .validate_type_expr = "type(x) == type(null)",
      },
      VariableConfigWithValueTestCase{
          .variable_config = MakeConstant(
              "x", "bool", [](auto& c) { c.set_bool_value(true); }),
          .validate_type_expr = "type(x) == bool",
          .validate_value_expr = "x == true",
      },
      VariableConfigWithValueTestCase{
          .variable_config = MakeConstant(
              "x", "int", [](Constant& c) { c.set_int_value(42); }),
          .validate_type_expr = "type(x) == int",
          .validate_value_expr = "x == 42",
      },
      VariableConfigWithValueTestCase{
          .variable_config = MakeConstant(
              "x", "uint", [](Constant& c) { c.set_uint_value(777); }),
          .validate_type_expr = "type(x) == uint",
          .validate_value_expr = "x == 777u",
      },
      VariableConfigWithValueTestCase{
          .variable_config =
              MakeConstant("x", "double",
                           [](Constant& c) { c.set_double_value(1.0 / 3.0); }),
          .validate_type_expr = "type(x) == double",
          .validate_value_expr = "x > 0.333 && x < 0.334",
      },
      VariableConfigWithValueTestCase{
          .variable_config = MakeConstant("x", "bytes",
                                          [](Constant& c) {
                                            c.set_bytes_value(absl::string_view(
                                                "\xff\x00\x01", 3));
                                          }),
          .validate_type_expr = "type(x) == bytes",
          .validate_value_expr = "x == b'\\xff\\x00\\x01'",
      },
      VariableConfigWithValueTestCase{
          .variable_config = MakeConstant(
              "x", "string", [](Constant& c) { c.set_string_value("hello"); }),
          .validate_type_expr = "type(x) == string",
          .validate_value_expr = "x == 'hello'",
      },
      VariableConfigWithValueTestCase{
          .variable_config = MakeConstant(
              "x", "timestamp",
              [](Constant& c) {
                // NOLINTNEXTLINE(clang-diagnostic-deprecated-declarations)
                c.set_timestamp_value(absl::FromUnixSeconds(1767323045));
              }),
          .validate_type_expr =
              "type(x) == type(timestamp('2026-01-02T03:04:05Z'))",
          .validate_value_expr = "x == timestamp('2026-01-02T03:04:05Z')",
      },
      VariableConfigWithValueTestCase{
          .variable_config = MakeConstant(
              "x", "duration",
              [](Constant& c) {
                // NOLINTNEXTLINE(clang-diagnostic-deprecated-declarations)
                c.set_duration_value(absl::Hours(1) + absl::Minutes(2) +
                                     absl::Seconds(3));
              }),
          .validate_type_expr = "type(x) == type(duration('1h2m3s'))",
          .validate_value_expr = "x == duration('1h2m3s')",
      },
  };
}

INSTANTIATE_TEST_SUITE_P(VariableConfigTest, VariableConfigWithValueTest,
                         ValuesIn(GetVariableConfigWithValueTestCases()));

struct FunctionConfigTestCase {
  Config::FunctionConfig function_config;
  std::vector<Config::VariableConfig> variable_configs;
  std::string expr;
  std::string expected_error;
};

class FunctionConfigTest
    : public testing::TestWithParam<FunctionConfigTestCase> {};

TEST_P(FunctionConfigTest, FunctionConfig) {
  const FunctionConfigTestCase& param = GetParam();

  Env env;
  env.SetDescriptorPool(internal::GetSharedTestingDescriptorPool());
  Config config;
  for (const Config::VariableConfig& variable_config : param.variable_configs) {
    ASSERT_THAT(config.AddVariableConfig(variable_config), IsOk());
  }
  ASSERT_THAT(config.AddFunctionConfig(param.function_config), IsOk());
  env.SetConfig(config);

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> compiler, env.NewCompiler());
  ASSERT_OK_AND_ASSIGN(ValidationResult result, compiler->Compile(param.expr));
  if (param.expected_error.empty()) {
    EXPECT_TRUE(result.GetIssues().empty())
        << " expr: " << param.expr << " error: " << result.FormatError();
  } else {
    EXPECT_THAT(result.GetIssues(),
                UnorderedElementsAre(Property(&TypeCheckIssue::message,
                                              HasSubstr(param.expected_error))))
        << " expr: " << param.expr << " error: " << result.FormatError();
  }
}

std::vector<FunctionConfigTestCase> GetFunctionConfigTestCases() {
  return {{
      FunctionConfigTestCase{
          .function_config =
              {
                  .name = "add",
                  .overload_configs =
                      {
                          {
                              .overload_id = "plus(int,int)",
                              .examples = {"add(1, 2) -> 3"},
                              .parameters = {{.name = "int"}, {.name = "int"}},
                              .return_type = {.name = "int"},
                          },
                      },
              },
          .expr = "add(1, 2)",
      },
      FunctionConfigTestCase{
          .function_config =
              {
                  .name = "add",
                  .overload_configs =
                      {
                          {
                              .overload_id = "int.plus(int)",
                              .examples = {"1.add(2) -> 3"},
                              .is_member_function = true,
                              .parameters = {{.name = "int"}, {.name = "int"}},
                              .return_type = {.name = "int"},
                          },
                      },
              },
          .expr = "1.add(2) == 3",
      },
      FunctionConfigTestCase{
          .function_config =
              {
                  .name = "add",
                  .overload_configs =
                      {
                          {
                              .overload_id = "plus(string,string)",
                              .examples =
                                  {"add('hello', 'world') -> 'hello world'"},
                              .parameters = {{.name = "int"}, {.name = "int"}},
                              .return_type = {.name = "string"},
                          },
                      },
              },
          .expr = "add('hello', 'world')",
          .expected_error = "found no matching overload for 'add' applied to "
                            "'(string, string)'",
      },
      FunctionConfigTestCase{
          .function_config =
              {
                  .name = "add",
                  .overload_configs =
                      {
                          {
                              .overload_id = "int.plus(int)",
                              .examples = {"1.add(2) -> 'three'"},
                              .is_member_function = true,
                              .parameters = {{.name = "int"}, {.name = "int"}},
                              .return_type = {.name = "string"},
                          },
                      },
              },
          .expr = "1.add(2) == 3",
          .expected_error = "found no matching overload for '_==_' applied to "
                            "'(string, int)'",
      },
      FunctionConfigTestCase{
          .function_config =
              {
                  .name = "sum",
                  .description = "Sum a collection, which is an opaque type.",
                  .overload_configs =
                      {
                          {
                              .overload_id = "sum(collection<double>)",
                              .examples = {"sum(my_collection) -> 100"},
                              .parameters = {{.name = "collection",
                                              .params = {{.name = "double"}}}},
                              .return_type = {.name = "double"},
                          },
                      },
              },
          .variable_configs =
              {
                  {.name = "my_collection",
                   .description = "Matching opaque type.",
                   .type_info = {.name = "collection",
                                 .params = {{.name = "double"}}}},
              },
          .expr = "sum(my_collection) / 3.0",
      },
      FunctionConfigTestCase{
          .function_config =
              {
                  .name = "sum",
                  .description = "Sum a collection, which is an opaque type.",
                  .overload_configs =
                      {
                          {
                              .overload_id = "sum(collection<int>)",
                              .examples = {"sum(my_collection) -> 100"},
                              .parameters = {{.name = "collection",
                                              .params = {{.name = "int"}}}},
                              .return_type = {.name = "double"},
                          },
                      },
              },
          .variable_configs =
              {
                  {.name = "my_collection",
                   .description = "Mismatched opaque type.",
                   .type_info = {.name = "collection",
                                 .params = {{.name = "double"}}}},
              },
          .expr = "sum(my_collection) / 3.0",
          .expected_error = "found no matching overload for 'sum' applied to "
                            "'(collection(double))'",
      },
  }};
}

INSTANTIATE_TEST_SUITE_P(FunctionConfigTest, FunctionConfigTest,
                         ::testing::ValuesIn(GetFunctionConfigTestCases()));

}  // namespace
}  // namespace cel
