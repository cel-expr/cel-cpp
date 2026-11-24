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

#include <cstdint>
#include <fstream>
#include <ios>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "cel/expr/checked.pb.h"
#include "cel/expr/eval.pb.h"
#include "cel/expr/value.pb.h"
#include "absl/flags/flag.h"
#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/types/span.h"
#include "cel/expr/conformance/proto2/test_all_types.pb.h"
#include "cel/expr/conformance/proto3/test_all_types.pb.h"
#include "cel/expr/conformance/test/env/suite.pb.h"
#include "cel/expr/conformance/test/suite.pb.h"
#include "checker/validation_result.h"
#include "common/ast.h"
#include "common/ast_proto.h"
#include "common/internal/value_conversion.h"
#include "common/value.h"
#include "compiler/compiler.h"
#include "conformance/env/yaml_matcher.h"
#include "conformance/utils.h"
#include "env/config.h"
#include "env/env.h"
#include "env/env_runtime.h"
#include "env/env_std_extensions.h"
#include "env/env_yaml.h"
#include "env/runtime_std_extensions.h"
#include "internal/runfiles.h"
#include "internal/status_macros.h"
#include "internal/testing.h"
#include "internal/testing_descriptor_pool.h"
#include "runtime/activation.h"
#include "runtime/runtime.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "google/protobuf/message.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "google/protobuf/text_format.h"
#include "google/protobuf/util/message_differencer.h"

ABSL_FLAG(std::vector<std::string>, test_suites, {},
          "Comma-separated list of test suite textproto runfiles paths.");
ABSL_FLAG(std::vector<std::string>, skip_tests, {},
          "Comma-separated list of tests to skip.");

namespace cel {
namespace {

namespace exprpb = cel::expr;

using ExprTestCase = cel::expr::conformance::test::TestCase;
using ExprTestOutput = cel::expr::conformance::test::TestOutput;
using absl_testing::IsOk;
using cel::expr::conformance::test::InputValue;
using cel::expr::conformance::test::env::TestCase;
using cel::expr::conformance::test::env::TestOutput;
using cel::expr::conformance::test::env::TestSuite;
using cel::internal::GetSharedTestingDescriptorPool;
using cel_conformance::DescribeMessage;
using cel_conformance::MatchesConformanceValue;
using testing::HasSubstr;

bool ShouldSkipTest(absl::Span<const std::string> tests_to_skip,
                    absl::string_view name) {
  for (absl::string_view test_to_skip : tests_to_skip) {
    absl::string_view consumed_name = name;
    if (absl::ConsumePrefix(&consumed_name, test_to_skip) &&
        (consumed_name.empty() || absl::StartsWith(consumed_name, "/"))) {
      return true;
    }
  }
  return false;
}

MATCHER_P(MatchesDeducedType, expected, "") {
  static auto* kDifferencer = []() {
    auto* differencer = new google::protobuf::util::MessageDifferencer();
    differencer->set_message_field_comparison(
        google::protobuf::util::MessageDifferencer::EQUIVALENT);
    return differencer;
  }();

  const exprpb::Type& want = expected;
  const exprpb::CheckedExpr& checked_expr = arg;

  int64_t root_id = checked_expr.expr().id();
  auto it = checked_expr.type_map().find(root_id);
  exprpb::Type got;
  if (it != checked_expr.type_map().end()) {
    got = it->second;
  } else {
    // Per CheckedExpr, DYN types are omitted from type_map.
    got.mutable_dyn();
  }

  if (kDifferencer->Compare(got, want)) {
    return true;
  }
  (*result_listener) << "got: " << DescribeMessage(got) << "\n"
                     << "wanted: " << DescribeMessage(want);
  return false;
}

absl::StatusOr<std::unique_ptr<cel::Ast>> CompileExpr(const Compiler& compiler,
                                                      absl::string_view expr) {
  CEL_ASSIGN_OR_RETURN(ValidationResult validation_result,
                       compiler.Compile(expr));
  if (!validation_result.IsValid()) {
    return absl::InvalidArgumentError(validation_result.FormatError());
  }
  return validation_result.ReleaseAst();
}

absl::StatusOr<cel::Value> EvalAst(const Runtime& runtime,
                                   std::unique_ptr<cel::Ast> ast,
                                   const Activation& activation,
                                   google::protobuf::Arena* arena) {
  CEL_ASSIGN_OR_RETURN(std::unique_ptr<cel::Program> program,
                       runtime.CreateProgram(std::move(ast)));
  CEL_ASSIGN_OR_RETURN(cel::Value result, program->Evaluate(arena, activation));
  return result.Clone(arena);
}

absl::StatusOr<cel::Value> CompileAndEval(const Compiler& compiler,
                                          const Runtime& runtime,
                                          absl::string_view expr,
                                          const Activation& activation,
                                          google::protobuf::Arena* arena) {
  CEL_ASSIGN_OR_RETURN(std::unique_ptr<cel::Ast> ast,
                       CompileExpr(compiler, expr));
  return EvalAst(runtime, std::move(ast), activation, arena);
}

absl::StatusOr<cel::Value> ResolveInputValue(const InputValue& input_value,
                                             const Compiler& compiler,
                                             const Runtime& runtime,
                                             google::protobuf::Arena* arena) {
  switch (input_value.kind_case()) {
    case InputValue::kValue:
      return cel::test::FromExprValue(input_value.value(),
                                      runtime.GetDescriptorPool(),
                                      runtime.GetMessageFactory(), arena);
    case InputValue::kExpr: {
      Activation empty_activation;
      CEL_ASSIGN_OR_RETURN(cel::Value value,
                           CompileAndEval(compiler, runtime, input_value.expr(),
                                          empty_activation, arena));
      if (value.IsError()) {
        return absl::InvalidArgumentError(absl::StrCat(
            "failed to evaluate input expr: ", value.DebugString()));
      }
      return value;
    }
    case InputValue::KIND_NOT_SET:
      return absl::InvalidArgumentError("input value kind is not set");
  }
  return absl::InvalidArgumentError("unknown input value kind");
}

void VerifyErrorMessages(
    absl::string_view actual_message,
    const google::protobuf::RepeatedPtrField<std::string>& expected_errors) {
  for (absl::string_view expected_err : expected_errors) {
    if (!expected_err.empty()) {
      EXPECT_THAT(actual_message, HasSubstr(expected_err));
    }
  }
}

bool HasExpectedErrorMessages(const exprpb::ErrorSet& expected_error_set) {
  for (const auto& expected_err : expected_error_set.errors()) {
    if (!expected_err.message().empty()) {
      return true;
    }
  }
  return false;
}

void VerifyEvalErrorMessages(absl::string_view actual_message,
                             const exprpb::ErrorSet& expected_error_set) {
  for (const auto& expected_err : expected_error_set.errors()) {
    if (!expected_err.message().empty()) {
      EXPECT_THAT(actual_message, HasSubstr(expected_err.message()));
    }
  }
}

void VerifyCheckedExpr(const Compiler& compiler, const ExprTestCase& expr_test,
                       absl::string_view sub_test_name,
                       std::unique_ptr<cel::Ast>* ast) {
  const ExprTestOutput& output = expr_test.output();
  const ExprTestOutput::ResultKindCase result_kind = output.result_kind_case();

  std::unique_ptr<cel::Ast> compiled_ast;
  // Only allow eval_error to satisfy a compile-time failure when at least one
  // non-empty expected error message is specified (and deduced_type is unset).
  // An empty eval_error {} must still pass compilation so type-checking errors
  // are not silently bypassed.
  if (result_kind == ExprTestOutput::kEvalError &&
      !expr_test.has_deduced_type() &&
      HasExpectedErrorMessages(output.eval_error())) {
    absl::StatusOr<std::unique_ptr<cel::Ast>> ast_or =
        CompileExpr(compiler, expr_test.expr());
    if (!ast_or.ok()) {
      VerifyEvalErrorMessages(ast_or.status().message(), output.eval_error());
      return;
    }
    compiled_ast = std::move(*ast_or);
  } else {
    ASSERT_OK_AND_ASSIGN(compiled_ast, CompileExpr(compiler, expr_test.expr()));
  }

  if (expr_test.has_deduced_type()) {
    exprpb::CheckedExpr checked_expr;
    ASSERT_THAT(AstToCheckedExpr(*compiled_ast, &checked_expr), IsOk())
        << "Failed to convert AST to CheckedExpr in " << sub_test_name;
    EXPECT_THAT(checked_expr, MatchesDeducedType(expr_test.deduced_type()))
        << "Deduced type mismatch in " << sub_test_name;
  }
  *ast = std::move(compiled_ast);
}

void VerifyExprEval(const ExprTestCase& expr_test,
                    absl::string_view sub_test_name,
                    std::unique_ptr<cel::Ast> ast, const Compiler& compiler,
                    const Runtime& runtime) {
  const ExprTestOutput& output = expr_test.output();
  const ExprTestOutput::ResultKindCase result_kind = output.result_kind_case();

  google::protobuf::Arena arena;
  Activation activation;
  for (const auto& [var_name, input_val] : expr_test.input()) {
    ASSERT_OK_AND_ASSIGN(
        cel::Value resolved,
        ResolveInputValue(input_val, compiler, runtime, &arena));
    activation.InsertOrAssignValue(var_name, std::move(resolved));
  }

  absl::StatusOr<cel::Value> result_or =
      EvalAst(runtime, std::move(ast), activation, &arena);

  switch (result_kind) {
    case ExprTestOutput::kEvalError: {
      if (!result_or.ok()) {
        VerifyEvalErrorMessages(result_or.status().message(),
                                output.eval_error());
      } else if (result_or->IsError()) {
        VerifyEvalErrorMessages(result_or->GetError().ToStatus().message(),
                                output.eval_error());
      } else {
        FAIL() << "Expected error in " << sub_test_name
               << ", but got value: " << result_or->DebugString();
      }
      return;
    }
    case ExprTestOutput::kResultValue:
    case ExprTestOutput::kResultExpr: {
      ASSERT_THAT(result_or, IsOk())
          << "Failed to evaluate expr in " << sub_test_name;
      ASSERT_FALSE(result_or->IsError())
          << "Unexpected evaluation error in " << sub_test_name << ": "
          << result_or->DebugString();

      absl::StatusOr<exprpb::Value> actual_proto_or =
          cel::test::ToExprValue(*result_or, runtime.GetDescriptorPool(),
                                 runtime.GetMessageFactory(), &arena);
      ASSERT_THAT(actual_proto_or, IsOk())
          << "Failed to convert actual value to proto in " << sub_test_name;

      exprpb::Value expected_proto;
      if (result_kind == ExprTestOutput::kResultValue) {
        expected_proto = output.result_value();
      } else {
        Activation empty_activation;
        ASSERT_OK_AND_ASSIGN(
            cel::Value expected_val,
            CompileAndEval(compiler, runtime, output.result_expr(),
                           empty_activation, &arena));
        ASSERT_OK_AND_ASSIGN(
            expected_proto,
            cel::test::ToExprValue(expected_val, runtime.GetDescriptorPool(),
                                   runtime.GetMessageFactory(), &arena));
      }

      exprpb::ExprValue actual_expr_value;
      *actual_expr_value.mutable_value() = std::move(*actual_proto_or);
      EXPECT_THAT(actual_expr_value, MatchesConformanceValue(expected_proto))
          << "Evaluation result mismatch in " << sub_test_name;
      return;
    }
    case ExprTestOutput::kUnknown:
      FAIL() << "Unknown result assertions are not yet supported in "
             << sub_test_name;
    case ExprTestOutput::RESULT_KIND_NOT_SET:
      FAIL() << "Unexpected unset result_kind in " << sub_test_name;
  }
}

void VerifyExprTest(const ExprTestCase& expr_test,
                    absl::string_view sub_test_name, const Compiler& compiler,
                    const Runtime& runtime) {
  SCOPED_TRACE(std::string(sub_test_name));

  if (expr_test.has_env()) {
    FAIL() << "Sub-test env override is not supported in " << sub_test_name;
  }
  if (expr_test.has_input_context()) {
    FAIL() << "Sub-test input_context is not yet supported in "
           << sub_test_name;
  }
  if (expr_test.disable_check()) {
    FAIL() << "Sub-test disable_check is not supported in " << sub_test_name;
  }
  if (expr_test.expr().empty()) {
    FAIL() << "Sub-test expr must not be empty in " << sub_test_name;
  }

  const ExprTestOutput& output = expr_test.output();
  const ExprTestOutput::ResultKindCase result_kind = output.result_kind_case();

  if (!expr_test.has_deduced_type() &&
      result_kind == ExprTestOutput::RESULT_KIND_NOT_SET) {
    FAIL() << "Sub-test must specify output or deduced_type in "
           << sub_test_name;
  }

  std::unique_ptr<cel::Ast> ast;
  ASSERT_NO_FATAL_FAILURE(
      VerifyCheckedExpr(compiler, expr_test, sub_test_name, &ast));
  if (ast == nullptr || result_kind == ExprTestOutput::RESULT_KIND_NOT_SET) {
    return;
  }

  VerifyExprEval(expr_test, sub_test_name, std::move(ast), compiler, runtime);
}

class EnvConformanceTest : public testing::Test {
 public:
  EnvConformanceTest(TestCase test_case, std::string full_test_name,
                     std::vector<std::string> skip_tests, bool skip)
      : test_case_(std::move(test_case)),
        full_test_name_(std::move(full_test_name)),
        skip_tests_(std::move(skip_tests)),
        skip_(skip) {}

  void TestBody() override {
    if (skip_) {
      GTEST_SKIP() << "Skipping test: " << full_test_name_;
    }

    absl::StatusOr<Config> config_or;
    switch (test_case_.env_config_case()) {
      case TestCase::kEnvYaml:
        config_or = EnvConfigFromYaml(test_case_.env_yaml());
        break;
      case TestCase::kEnv:
        FAIL() << "Proto Environment input is not yet supported in "
               << full_test_name_;
      case TestCase::ENV_CONFIG_NOT_SET:
        FAIL() << "No env_config specified in " << full_test_name_;
    }

    const TestOutput& output = test_case_.output();
    const bool has_exported_yaml = !output.exported_env_yaml().empty();
    const bool has_expected_errors = !output.errors().empty();
    const bool has_expr_tests = !test_case_.expr_tests().empty();

    if (!has_exported_yaml && !has_expected_errors && !has_expr_tests) {
      FAIL() << "No expected output or expr_tests specified in "
             << full_test_name_;
    }

    if (has_expected_errors) {
      if (has_exported_yaml || has_expr_tests) {
        FAIL() << "Cannot specify env-level errors together with "
                  "exported_env_yaml or expr_tests in "
               << full_test_name_;
      }
      if (!config_or.ok()) {
        VerifyErrorMessages(config_or.status().message(), output.errors());
        return;
      }
      VerifyEnvBuildError(*config_or, output.errors());
      return;
    }

    ASSERT_THAT(config_or, IsOk())
        << "Failed to parse env_yaml in " << full_test_name_;

    if (has_exported_yaml) {
      std::ostringstream actual_stream;
      EnvConfigToYamlOptions options;
      options.use_type_signatures = true;
      EnvConfigToYaml(*config_or, actual_stream, options);
      std::string actual_yaml = actual_stream.str();

      EXPECT_THAT(actual_yaml, EqualsYaml(output.exported_env_yaml()))
          << "Env YAML mismatch in " << full_test_name_;
    }

    if (has_expr_tests) {
      VerifyExprTests(*config_or);
    }
  }

 private:
  void VerifyEnvBuildError(
      const Config& config,
      const google::protobuf::RepeatedPtrField<std::string>& expected_errors) {
    std::shared_ptr<const google::protobuf::DescriptorPool> descriptor_pool =
        GetSharedTestingDescriptorPool();

    Env env;
    env.SetDescriptorPool(descriptor_pool);
    RegisterStandardExtensions(env);
    env.SetConfig(config);

    absl::StatusOr<std::unique_ptr<Compiler>> compiler_or = env.NewCompiler();
    if (!compiler_or.ok()) {
      VerifyErrorMessages(compiler_or.status().message(), expected_errors);
      return;
    }

    EnvRuntime env_runtime;
    env_runtime.SetDescriptorPool(descriptor_pool);
    RegisterStandardExtensions(env_runtime);
    env_runtime.SetConfig(config);

    absl::StatusOr<std::unique_ptr<Runtime>> runtime_or =
        env_runtime.NewRuntime();
    if (!runtime_or.ok()) {
      VerifyErrorMessages(runtime_or.status().message(), expected_errors);
      return;
    }

    FAIL() << "Expected error in " << full_test_name_ << ", but succeeded.";
  }

  void VerifyExprTests(const Config& config) {
    std::shared_ptr<const google::protobuf::DescriptorPool> descriptor_pool =
        GetSharedTestingDescriptorPool();

    Env env;
    env.SetDescriptorPool(descriptor_pool);
    RegisterStandardExtensions(env);
    env.SetConfig(config);

    EnvRuntime env_runtime;
    env_runtime.SetDescriptorPool(descriptor_pool);
    RegisterStandardExtensions(env_runtime);
    env_runtime.SetConfig(config);

    absl::StatusOr<std::unique_ptr<Compiler>> compiler_or = env.NewCompiler();
    ASSERT_THAT(compiler_or, IsOk())
        << "Failed to build compiler in " << full_test_name_;

    absl::StatusOr<std::unique_ptr<Runtime>> runtime_or =
        env_runtime.NewRuntime();
    ASSERT_THAT(runtime_or, IsOk())
        << "Failed to build runtime in " << full_test_name_;

    for (int i = 0; i < test_case_.expr_tests_size(); ++i) {
      const ExprTestCase& expr_test = test_case_.expr_tests(i);
      std::string sub_test_name = absl::StrCat(
          full_test_name_, "/",
          expr_test.name().empty() ? absl::StrCat("#", i) : expr_test.name());
      if (ShouldSkipTest(skip_tests_, sub_test_name)) {
        continue;
      }
      VerifyExprTest(expr_test, sub_test_name, **compiler_or, **runtime_or);
    }
  }

  TestCase test_case_;
  std::string full_test_name_;
  std::vector<std::string> skip_tests_;
  bool skip_;
};

absl::Status RegisterTestsFromFile(absl::Span<const std::string> skip_tests,
                                   absl::string_view runfile_path) {
  std::string abs_path = cel::internal::ResolveRunfilesPath(runfile_path);
  if (abs_path.empty()) {
    return absl::NotFoundError(
        absl::StrCat("could not resolve runfiles path: ", runfile_path));
  }

  TestSuite suite;
  {
    std::ifstream in(abs_path, std::ios_base::in | std::ios_base::binary);
    if (!in.is_open()) {
      return absl::UnknownError(
          absl::StrCat("failed to open test suite file: ", abs_path));
    }
    google::protobuf::io::IstreamInputStream stream(&in);
    if (!google::protobuf::TextFormat::Parse(&stream, &suite)) {
      return absl::InvalidArgumentError(
          absl::StrCat("failed to parse test suite textproto: ", abs_path));
    }
  }

  std::vector<std::string> skip_tests_vec(skip_tests.begin(), skip_tests.end());
  const std::string& suite_name = suite.name();
  for (const auto& section : suite.sections()) {
    const std::string& section_name = section.name();
    for (const auto& test : section.tests()) {
      const std::string& test_name = test.name();
      std::string full_test_name =
          absl::StrCat(suite_name, "/", section_name, "/", test_name);
      bool skip = ShouldSkipTest(skip_tests, full_test_name);

      testing::RegisterTest(suite_name.c_str(),
                            absl::StrCat(section_name, "/", test_name).c_str(),
                            nullptr, nullptr, __FILE__, __LINE__,
                            [test, full_test_name, skip_tests_vec, skip]() {
                              return new EnvConformanceTest(
                                  test, full_test_name, skip_tests_vec, skip);
                            });
    }
  }
  return absl::OkStatus();
}

void RegisterAllTests() {
  google::protobuf::LinkMessageReflection<cel::expr::conformance::proto2::TestAllTypes>();
  google::protobuf::LinkMessageReflection<cel::expr::conformance::proto3::TestAllTypes>();
  google::protobuf::LinkMessageReflection<
      cel::expr::conformance::proto2::NestedTestAllTypes>();
  google::protobuf::LinkMessageReflection<
      cel::expr::conformance::proto3::NestedTestAllTypes>();

  std::vector<std::string> suite_paths = absl::GetFlag(FLAGS_test_suites);
  std::vector<std::string> skip_tests = absl::GetFlag(FLAGS_skip_tests);

  ABSL_CHECK(!suite_paths.empty())
      << "No test suites specified in --test_suites flag.";

  for (const std::string& suite_path : suite_paths) {
    ABSL_CHECK_OK(RegisterTestsFromFile(skip_tests, suite_path));
  }
}

}  // namespace
}  // namespace cel

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);

  cel::RegisterAllTests();
  return RUN_ALL_TESTS();
}
