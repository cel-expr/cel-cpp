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

#include <fstream>
#include <ios>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

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
#include "cel/expr/conformance/test/env/suite.pb.h"
#include "cel/expr/conformance/test/suite.pb.h"
#include "common/ast.h"
#include "common/internal/value_conversion.h"
#include "common/value.h"
#include "compiler/compiler.h"
#include "conformance/env/normalize_yaml.h"
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
#include "google/protobuf/dynamic_message.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "google/protobuf/message.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "google/protobuf/text_format.h"

ABSL_FLAG(std::vector<std::string>, test_suites, {},
          "Comma-separated list of test suite textproto runfiles paths.");
ABSL_FLAG(std::vector<std::string>, skip_tests, {},
          "Comma-separated list of tests to skip.");

namespace cel {
namespace {

namespace exprpb = cel::expr;

using absl_testing::IsOk;
using cel::expr::conformance::test::InputValue;
using cel::expr::conformance::test::env::TestCase;
using cel::expr::conformance::test::env::TestOutput;
using cel::expr::conformance::test::env::TestSuite;
using cel::internal::GetSharedTestingDescriptorPool;
using cel_conformance::MatchesConformanceValue;
using testing::HasSubstr;
using testing::Not;

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

absl::StatusOr<cel::Value> CompileAndEval(
    const Compiler& compiler, const Runtime& runtime, absl::string_view expr,
    const Activation& activation, google::protobuf::MessageFactory* message_factory,
    google::protobuf::Arena* arena) {
  CEL_ASSIGN_OR_RETURN(auto validation_result, compiler.Compile(expr));
  if (!validation_result.IsValid()) {
    return absl::InvalidArgumentError(validation_result.FormatError());
  }
  CEL_ASSIGN_OR_RETURN(std::unique_ptr<cel::Ast> ast,
                       validation_result.ReleaseAst());
  CEL_ASSIGN_OR_RETURN(std::unique_ptr<cel::Program> program,
                       runtime.CreateProgram(std::move(ast)));
  EvaluateOptions options;
  options.message_factory = message_factory;
  CEL_ASSIGN_OR_RETURN(cel::Value result,
                       program->Evaluate(arena, activation, options));
  return result.Clone(arena);
}

absl::StatusOr<cel::Value> ResolveInputValue(
    const InputValue& input_value, const Compiler& compiler,
    const Runtime& runtime, const google::protobuf::DescriptorPool* descriptor_pool,
    google::protobuf::MessageFactory* message_factory, google::protobuf::Arena* arena) {
  switch (input_value.kind_case()) {
    case InputValue::kValue:
      return cel::test::FromExprValue(input_value.value(), descriptor_pool,
                                      message_factory, arena);
    case InputValue::kExpr: {
      Activation empty_activation;
      return CompileAndEval(compiler, runtime, input_value.expr(),
                            empty_activation, message_factory, arena);
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

class EnvConformanceTest : public testing::Test {
 public:
  EnvConformanceTest(TestCase test_case, std::string full_test_name, bool skip)
      : test_case_(std::move(test_case)),
        full_test_name_(std::move(full_test_name)),
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
    const bool has_expr = !test_case_.expr().empty();
    const bool has_result_value = output.has_result_value();
    const bool has_expected_errors = !output.errors().empty();

    if (!has_exported_yaml && !has_result_value && !has_expected_errors) {
      FAIL() << "No expected output specified in " << full_test_name_;
    }

    if (!has_expr && has_expected_errors) {
      ASSERT_THAT(config_or, Not(IsOk()))
          << "Expected error in " << full_test_name_ << ", but succeeded.";
      VerifyErrorMessages(config_or.status().message(), output.errors());
      return;
    }

    if (!config_or.ok() && has_expected_errors) {
      VerifyErrorMessages(config_or.status().message(), output.errors());
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

      EXPECT_EQ(NormalizeYaml(actual_yaml),
                NormalizeYaml(output.exported_env_yaml()))
          << "Env YAML mismatch in " << full_test_name_ << "\nExpected:\n"
          << output.exported_env_yaml() << "\nActual:\n"
          << actual_yaml;
    }

    if (has_expr) {
      VerifyExpression(*config_or, output);
    } else if (has_result_value) {
      FAIL() << "Expected result_value specified without expr in "
             << full_test_name_;
    }
  }

 private:
  void VerifyExpression(const Config& config, const TestOutput& output) {
    std::shared_ptr<const google::protobuf::DescriptorPool> descriptor_pool =
        GetSharedTestingDescriptorPool();
    google::protobuf::DynamicMessageFactory message_factory(descriptor_pool.get());
    google::protobuf::Arena arena;

    Env env;
    env.SetDescriptorPool(descriptor_pool);
    RegisterStandardExtensions(env);
    env.SetConfig(config);

    EnvRuntime env_runtime;
    env_runtime.SetDescriptorPool(descriptor_pool);
    RegisterStandardExtensions(env_runtime);
    env_runtime.SetConfig(config);

    const bool has_expected_errors = !output.errors().empty();
    if (has_expected_errors && output.has_result_value()) {
      FAIL() << "Cannot specify both errors and result_value in "
             << full_test_name_;
    }

    absl::StatusOr<std::unique_ptr<Compiler>> compiler_or = env.NewCompiler();
    if (!compiler_or.ok()) {
      if (has_expected_errors) {
        VerifyErrorMessages(compiler_or.status().message(), output.errors());
        return;
      }
      ASSERT_THAT(compiler_or, IsOk())
          << "Failed to build compiler in " << full_test_name_;
    }

    absl::StatusOr<std::unique_ptr<Runtime>> runtime_or =
        env_runtime.NewRuntime();
    if (!runtime_or.ok()) {
      if (has_expected_errors) {
        VerifyErrorMessages(runtime_or.status().message(), output.errors());
        return;
      }
      ASSERT_THAT(runtime_or, IsOk())
          << "Failed to build runtime in " << full_test_name_;
    }

    Activation activation;
    for (const auto& [var_name, input_val] : test_case_.input()) {
      absl::StatusOr<cel::Value> resolved_or =
          ResolveInputValue(input_val, **compiler_or, **runtime_or,
                            descriptor_pool.get(), &message_factory, &arena);
      ASSERT_THAT(resolved_or, IsOk())
          << "Failed to resolve input '" << var_name << "' in "
          << full_test_name_;
      ASSERT_FALSE(resolved_or->IsError())
          << "Unexpected error resolving input '" << var_name << "' in "
          << full_test_name_ << ": " << resolved_or->DebugString();
      activation.InsertOrAssignValue(var_name, std::move(*resolved_or));
    }

    absl::StatusOr<cel::Value> result_or =
        CompileAndEval(**compiler_or, **runtime_or, test_case_.expr(),
                       activation, &message_factory, &arena);

    if (has_expected_errors) {
      if (!result_or.ok()) {
        VerifyErrorMessages(result_or.status().message(), output.errors());
      } else if (result_or->IsError()) {
        VerifyErrorMessages(result_or->GetError().ToStatus().message(),
                            output.errors());
      } else {
        FAIL() << "Expected error in " << full_test_name_
               << ", but got value: " << result_or->DebugString();
      }
      return;
    }

    if (!output.has_result_value()) {
      FAIL() << "expr is specified without expected result_value or errors in "
             << full_test_name_;
    }

    ASSERT_THAT(result_or, IsOk())
        << "Failed to compile/eval expr in " << full_test_name_;
    ASSERT_FALSE(result_or->IsError())
        << "Unexpected evaluation error in " << full_test_name_ << ": "
        << result_or->DebugString();

    absl::StatusOr<exprpb::Value> actual_proto_or = cel::test::ToExprValue(
        *result_or, descriptor_pool.get(), &message_factory, &arena);
    ASSERT_THAT(actual_proto_or, IsOk())
        << "Failed to convert actual value to proto in " << full_test_name_;

    exprpb::ExprValue actual_expr_value;
    *actual_expr_value.mutable_value() = std::move(*actual_proto_or);
    EXPECT_THAT(actual_expr_value,
                MatchesConformanceValue(output.result_value()))
        << "Evaluation result mismatch in " << full_test_name_;
  }

  TestCase test_case_;
  std::string full_test_name_;
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

  const std::string& suite_name = suite.name();
  for (const auto& section : suite.sections()) {
    const std::string& section_name = section.name();
    for (const auto& test : section.tests()) {
      const std::string& test_name = test.name();
      std::string full_test_name =
          absl::StrCat(suite_name, "/", section_name, "/", test_name);
      bool skip = ShouldSkipTest(skip_tests, full_test_name);

      testing::RegisterTest(
          suite_name.c_str(),
          absl::StrCat(section_name, "/", test_name).c_str(), nullptr, nullptr,
          __FILE__, __LINE__, [test, full_test_name, skip]() {
            return new EnvConformanceTest(test, full_test_name, skip);
          });
    }
  }
  return absl::OkStatus();
}

void RegisterAllTests() {
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
