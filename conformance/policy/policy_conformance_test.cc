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

#include <cstddef>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cel/expr/eval.pb.h"
#include "absl/flags/flag.h"
#include "absl/log/absl_check.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "common/ast.h"
#include "common/internal/value_conversion.h"
#include "common/source.h"
#include "common/value.h"
#include "compiler/compiler.h"
#include "env/config.h"
#include "env/env.h"
#include "env/env_runtime.h"
#include "env/env_std_extensions.h"
#include "env/env_yaml.h"
#include "env/runtime_std_extensions.h"
#include "extensions/protobuf/enum_adapter.h"
#include "internal/runfiles.h"
#include "internal/status_macros.h"
#include "internal/testing.h"
#include "internal/testing_descriptor_pool.h"
#include "policy/cel_policy.h"
#include "policy/cel_policy_parse_result.h"
#include "policy/cel_policy_validation_result.h"
#include "policy/compiler.h"
#include "policy/test_util.h"
#include "policy/yaml_policy_parser.h"
#include "runtime/activation.h"
#include "runtime/bind_proto_to_activation.h"
#include "runtime/function_adapter.h"
#include "runtime/runtime.h"
#include "cel/expr/conformance/test/suite.pb.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/dynamic_message.h"
#include "google/protobuf/message.h"

ABSL_FLAG(std::vector<std::string>, test_bundles, {},
          "Space or comma separated list of test bundle runfiles paths.");
ABSL_FLAG(std::vector<std::string>, skip_tests, {},
          "Comma-separated list of tests to skip.");

namespace cel {
namespace {

using ::absl_testing::IsOk;
using ::cel::expr::conformance::test::TestSuite;
using ::cel::internal::GetSharedTestingDescriptorPool;
using ::testing::HasSubstr;

struct BundleSections {
  absl::string_view config_content;
  absl::string_view policy_content;
  absl::string_view tests_content;
};

absl::string_view TrimDoc(absl::string_view doc) {
  absl::ConsumeSuffix(&doc, "\n");
  absl::ConsumeSuffix(&doc, "\r");
  return doc;
}

absl::StatusOr<BundleSections> ParseYamlBundle(
    absl::string_view bundle_content) {
  BundleSections sections;
  std::vector<absl::string_view> docs;
  absl::string_view remaining = bundle_content;

  size_t next_line = remaining.find('\n');
  while (next_line != absl::string_view::npos) {
    if (absl::StartsWith(remaining.substr(next_line), "\n---\r\n")) {
      docs.push_back(TrimDoc(remaining.substr(0, next_line)));
      remaining = remaining.substr(next_line + 5);
      next_line = remaining.find('\n');
      continue;
    }

    if (absl::StartsWith(remaining.substr(next_line), "\n---\n")) {
      docs.push_back(TrimDoc(remaining.substr(0, next_line)));
      remaining = remaining.substr(next_line + 4);
      next_line = remaining.find('\n');
      continue;
    }

    next_line = remaining.find('\n', next_line + 1);
  }

  if (remaining.empty()) {
    return absl::InvalidArgumentError("Empty document in yaml bundle");
  }
  docs.push_back(remaining);

  if (docs.size() == 3) {
    sections.config_content = docs[0];
    sections.policy_content = docs[1];
    sections.tests_content = docs[2];
  } else if (docs.size() == 2) {
    sections.policy_content = docs[0];
    sections.tests_content = docs[1];
  } else {
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid number of sections: ", docs.size()));
  }

  return sections;
}

// Implementations for extension functions referenced in conformance tests.
cel::Value LocationCode(const cel::StringValue& ip,
                        const google::protobuf::DescriptorPool* pool,
                        google::protobuf::MessageFactory* factory, google::protobuf::Arena* arena) {
  std::string ip_str = ip.ToString();
  if (ip_str == "10.0.0.1") return cel::StringValue::WrapUnsafe("us");
  if (ip_str == "10.0.0.2") return cel::StringValue::WrapUnsafe("de");
  return cel::StringValue::WrapUnsafe("ir");
}

// TODO(uncreated-issue/92): This should be migrated to use the testrunner utility
// after adding support for reading the yaml specification for envs/tests.
class InputEvaluator {
 public:
  static absl::StatusOr<std::unique_ptr<InputEvaluator>> Create(
      const std::shared_ptr<const google::protobuf::DescriptorPool>& pool) {
    cel::Env env;
    env.SetDescriptorPool(pool);
    cel::RegisterStandardExtensions(env);

    cel::EnvRuntime env_runtime;
    env_runtime.SetDescriptorPool(pool);
    cel::RegisterStandardExtensions(env_runtime);
    env_runtime.mutable_runtime_options().enable_qualified_type_identifiers =
        true;

    // Enable default extensions (optional, bindings)
    cel::Config config;
    CEL_RETURN_IF_ERROR(config.AddExtensionConfig(
        "optional", cel::Config::ExtensionConfig::kLatest));
    CEL_RETURN_IF_ERROR(config.AddExtensionConfig(
        "bindings", cel::Config::ExtensionConfig::kLatest));
    env.SetConfig(config);
    env_runtime.SetConfig(config);

    auto compiler_builder_or = env.NewCompilerBuilder();
    CEL_ASSIGN_OR_RETURN(auto compiler_builder, std::move(compiler_builder_or));
    compiler_builder->GetParserBuilder().GetOptions().enable_optional_syntax =
        true;
    CEL_ASSIGN_OR_RETURN(auto compiler, compiler_builder->Build());

    auto runtime_builder_or = env_runtime.CreateRuntimeBuilder();
    CEL_ASSIGN_OR_RETURN(auto runtime_builder, std::move(runtime_builder_or));

    // Register conformance enums
    for (const auto& enum_name :
         {"cel.expr.conformance.proto2.GlobalEnum",
          "cel.expr.conformance.proto3.GlobalEnum",
          "cel.expr.conformance.proto2.TestAllTypes.NestedEnum",
          "cel.expr.conformance.proto3.TestAllTypes.NestedEnum"}) {
      auto* enum_desc = pool->FindEnumTypeByName(enum_name);
      if (enum_desc != nullptr) {
        CEL_RETURN_IF_ERROR(cel::extensions::RegisterProtobufEnum(
            runtime_builder.type_registry(), enum_desc));
      }
    }

    CEL_ASSIGN_OR_RETURN(auto runtime, std::move(runtime_builder).Build());

    return absl::WrapUnique(
        new InputEvaluator(std::move(compiler), std::move(runtime)));
  }

  absl::StatusOr<cel::Value> Evaluate(
      absl::string_view expr_str, google::protobuf::Arena* arena,
      google::protobuf::MessageFactory* message_factory) const {
    CEL_ASSIGN_OR_RETURN(auto validation_result, compiler_->Compile(expr_str));
    if (!validation_result.IsValid()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Failed to compile input expr: ", expr_str));
    }
    CEL_ASSIGN_OR_RETURN(auto ast, validation_result.ReleaseAst());
    CEL_ASSIGN_OR_RETURN(
        auto program,
        runtime_->CreateProgram(std::make_unique<cel::Ast>(std::move(*ast))));
    cel::Activation activation;
    EvaluateOptions options;
    options.message_factory = message_factory;
    CEL_ASSIGN_OR_RETURN(auto result,
                         program->Evaluate(arena, activation, options));
    return result.Clone(arena);
  }

 private:
  InputEvaluator(std::unique_ptr<cel::Compiler> compiler,
                 std::unique_ptr<cel::Runtime> runtime)
      : compiler_(std::move(compiler)), runtime_(std::move(runtime)) {}

  std::unique_ptr<cel::Compiler> compiler_;
  std::unique_ptr<cel::Runtime> runtime_;
};

absl::StatusOr<cel::Value> EvaluateInputValue(
    const cel::expr::conformance::test::InputValue& input_val,
    const InputEvaluator& evaluator,
    const google::protobuf::DescriptorPool* descriptor_pool,
    google::protobuf::MessageFactory* message_factory, google::protobuf::Arena* arena) {
  if (input_val.has_expr()) {
    return evaluator.Evaluate(input_val.expr(), arena, message_factory);
  }
  if (input_val.has_value()) {
    return cel::test::FromExprValue(input_val.value(), descriptor_pool,
                                    message_factory, arena);
  }
  return absl::InvalidArgumentError("Empty InputValue");
}

class CelValueMatcherImpl
    : public testing::MatcherInterface<const cel::Value&> {
 public:
  CelValueMatcherImpl(cel::Value expected_val,
                      const google::protobuf::DescriptorPool* pool,
                      google::protobuf::MessageFactory* message_factory,
                      google::protobuf::Arena* arena)
      : expected_val_(std::move(expected_val)),
        pool_(pool),
        message_factory_(message_factory),
        arena_(arena) {}

  bool MatchAndExplain(const cel::Value& actual_val,
                       testing::MatchResultListener* listener) const override {
    cel::Value actual = actual_val;
    if (actual.IsOptional() && !expected_val_.IsOptional()) {
      auto opt_val = actual.AsOptional();
      if (opt_val->HasValue()) {
        actual = opt_val->Value();
      }
    }
    cel::Value eq_result;
    auto eq_status = actual.Equal(expected_val_, pool_, message_factory_,
                                  arena_, &eq_result);
    if (!eq_status.ok()) {
      *listener << "equality check failed with status: " << eq_status;
      return false;
    }
    if (!eq_result.IsTrue()) {
      *listener << "expected: " << expected_val_.DebugString()
                << "\nactual: " << actual.DebugString();
      return false;
    }
    return true;
  }

  void DescribeTo(std::ostream* os) const override {
    *os << "is equal to " << expected_val_.DebugString();
  }

  void DescribeNegationTo(std::ostream* os) const override {
    *os << "is not equal to " << expected_val_.DebugString();
  }

 private:
  cel::Value expected_val_;
  const google::protobuf::DescriptorPool* pool_;
  google::protobuf::MessageFactory* message_factory_;
  google::protobuf::Arena* arena_;
};

absl::StatusOr<testing::Matcher<cel::Value>> MakeExpectedValueMatcher(
    const cel::expr::conformance::test::TestOutput& output,
    const InputEvaluator& input_evaluator, const google::protobuf::DescriptorPool* pool,
    google::protobuf::MessageFactory* message_factory, google::protobuf::Arena* arena) {
  cel::Value expected_val;
  if (output.has_result_expr()) {
    CEL_ASSIGN_OR_RETURN(
        expected_val,
        input_evaluator.Evaluate(output.result_expr(), arena, message_factory));
  } else if (output.has_result_value()) {
    CEL_ASSIGN_OR_RETURN(expected_val,
                         cel::test::FromExprValue(output.result_value(), pool,
                                                  message_factory, arena));
  } else {
    return absl::InvalidArgumentError("Unsupported output kind");
  }
  return testing::Matcher<cel::Value>(
      new CelValueMatcherImpl(expected_val, pool, message_factory, arena));
}

bool ShouldRunTest(absl::string_view test_name,
                   const std::vector<std::string>& skip_tests) {
  for (const std::string& skip : skip_tests) {
    if (absl::StartsWith(test_name, skip)) {
      return false;
    }
  }
  return true;
}

absl::Status PopulateActivation(
    const cel::expr::conformance::test::TestCase& test,
    const InputEvaluator& input_evaluator,
    const google::protobuf::DescriptorPool* descriptor_pool,
    google::protobuf::MessageFactory* message_factory,
    absl::string_view context_msg_type_name, google::protobuf::Arena* arena,
    Activation& activation) {
  if (!test.has_input_context()) {
    for (const auto& [var_name, input_val] : test.input()) {
      CEL_ASSIGN_OR_RETURN(
          auto val,
          EvaluateInputValue(input_val, input_evaluator, descriptor_pool,
                             message_factory, arena));
      activation.InsertOrAssignValue(var_name, std::move(val));
    }
    return absl::OkStatus();
  }

  const auto& input_context = test.input_context();
  const google::protobuf::Message* context_message = nullptr;

  if (input_context.has_context_message()) {
    const google::protobuf::Any& any_msg = input_context.context_message();
    const google::protobuf::Descriptor* msg_descriptor =
        descriptor_pool->FindMessageTypeByName(context_msg_type_name);
    if (msg_descriptor == nullptr) {
      return absl::NotFoundError(absl::StrCat(
          "Failed to find message descriptor for: ", context_msg_type_name));
    }
    const google::protobuf::Message* prototype =
        message_factory->GetPrototype(msg_descriptor);
    if (prototype == nullptr) {
      return absl::NotFoundError(
          absl::StrCat("Failed to get prototype for: ", context_msg_type_name));
    }
    auto* buf = prototype->New(arena);
    if (!any_msg.UnpackTo(buf)) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Failed to unpack context message to ", context_msg_type_name));
    }
    context_message = buf;
  } else if (input_context.has_context_expr() &&
             !context_msg_type_name.empty()) {
    CEL_ASSIGN_OR_RETURN(cel::Value evaluated_val,
                         input_evaluator.Evaluate(input_context.context_expr(),
                                                  arena, message_factory));

    if (!evaluated_val.IsParsedMessage()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Context expression did not evaluate to a message: ",
                       input_context.context_expr()));
    }
    if (evaluated_val.GetParsedMessage().GetDescriptor()->full_name() !=
        context_msg_type_name) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Context expression evaluated to a message of type ",
          evaluated_val.GetParsedMessage().GetDescriptor()->full_name(),
          " which does not match the expected type ", context_msg_type_name));
    }
    context_message = static_cast<const google::protobuf::Message*>(
        evaluated_val.GetParsedMessage().operator->());
  }
  if (context_message == nullptr) {
    return absl::InvalidArgumentError(
        "Failed to resolve context message for test case");
  }

  return cel::BindProtoToActivation(
      *context_message, cel::BindProtoUnsetFieldBehavior::kBindDefaultValue,
      descriptor_pool, message_factory, arena, &activation);
}

class PolicyTestSuiteRunner {
 public:
  PolicyTestSuiteRunner(std::string suite_name,
                        std::unique_ptr<cel::Compiler> compiler,
                        std::unique_ptr<cel::Runtime> runtime,
                        std::shared_ptr<CelPolicySource> policy_source,
                        CelPolicyValidationResult compile_result,
                        std::shared_ptr<const google::protobuf::DescriptorPool> pool,
                        std::shared_ptr<google::protobuf::MessageFactory> message_factory,
                        std::shared_ptr<InputEvaluator> input_evaluator,
                        std::string context_msg_type_name,
                        bool expect_compile_fail = false)
      : suite_name_(std::move(suite_name)),
        compiler_(std::move(compiler)),
        runtime_(std::move(runtime)),
        policy_source_(std::move(policy_source)),
        compile_result_(std::move(compile_result)),
        pool_(std::move(pool)),
        message_factory_(std::move(message_factory)),
        input_evaluator_(std::move(input_evaluator)),
        context_msg_type_name_(std::move(context_msg_type_name)),
        expect_compile_fail_(expect_compile_fail) {}

  void RunTest(const cel::expr::conformance::test::TestCase& test,
               absl::string_view full_test_name) {
    const auto& output = test.output();

    if (expect_compile_fail_) {
      ASSERT_FALSE(compile_result_.IsValid())
          << "Expected compilation to fail in " << full_test_name;
      ASSERT_TRUE(output.has_eval_error())
          << "Expected eval_error to be present in compile error test "
          << full_test_name;
      std::string err_msg = compile_result_.FormatIssues();
      for (const auto& expected_err : output.eval_error().errors()) {
        EXPECT_THAT(err_msg, HasSubstr(expected_err.message()))
            << "Did not find expected compile time error";
      }
      return;
    }

    // Compilation should have succeeded for evaluation tests
    ASSERT_TRUE(compile_result_.IsValid())
        << "Compilation has validation errors in " << full_test_name << ": "
        << compile_result_.FormatIssues();

    ASSERT_OK_AND_ASSIGN(std::unique_ptr<cel::Program> program,
                         runtime_->CreateProgram(std::make_unique<cel::Ast>(
                             *compile_result_.GetAst())));

    // Parse Inputs and evaluate them
    google::protobuf::Arena arena;
    Activation activation;
    ASSERT_THAT(PopulateActivation(test, *input_evaluator_, pool_.get(),
                                   message_factory_.get(),
                                   context_msg_type_name_, &arena, activation),
                IsOk());

    // Evaluate Policy
    auto eval_result_or = program->Evaluate(&arena, activation);
    ASSERT_THAT(eval_result_or.status(), IsOk())
        << "Evaluation failed in " << full_test_name;
    cel::Value actual_val = *eval_result_or;

    ASSERT_OK_AND_ASSIGN(
        auto matcher,
        MakeExpectedValueMatcher(output, *input_evaluator_, pool_.get(),
                                 message_factory_.get(), &arena));

    // Apply matcher to the output of evaluation
    EXPECT_THAT(actual_val, matcher) << "Test failed: " << full_test_name;
  }

 private:
  std::string suite_name_;
  std::unique_ptr<cel::Compiler> compiler_;
  std::unique_ptr<cel::Runtime> runtime_;
  std::shared_ptr<CelPolicySource> policy_source_;
  CelPolicyValidationResult compile_result_;
  std::shared_ptr<const google::protobuf::DescriptorPool> pool_;
  std::shared_ptr<google::protobuf::MessageFactory> message_factory_;
  std::shared_ptr<InputEvaluator> input_evaluator_;
  std::string context_msg_type_name_;
  bool expect_compile_fail_;
};

class CelPolicyTest : public testing::Test {
 public:
  explicit CelPolicyTest(std::shared_ptr<PolicyTestSuiteRunner> runner,
                         cel::expr::conformance::test::TestCase test_case,
                         std::string full_test_name, bool skip)
      : runner_(std::move(runner)),
        test_case_(std::move(test_case)),
        full_test_name_(std::move(full_test_name)),
        skip_(skip) {}

  void TestBody() override {
    if (skip_) {
      GTEST_SKIP() << "Skipping test: " << full_test_name_;
    }
    EXPECT_NO_FATAL_FAILURE(runner_->RunTest(test_case_, full_test_name_));
  }

 private:
  std::shared_ptr<PolicyTestSuiteRunner> runner_;
  cel::expr::conformance::test::TestCase test_case_;
  std::string full_test_name_;
  bool skip_;
};

absl::Status RegisterTestSuite(
    absl::string_view suite_name, const BundleSections& sections,
    const std::shared_ptr<InputEvaluator>& input_evaluator,
    const std::shared_ptr<const google::protobuf::DescriptorPool>& pool,
    const std::shared_ptr<google::protobuf::MessageFactory>& message_factory,
    const std::vector<std::string>& skip_tests) {
  // Check if the entire suite should be skipped (prefix match)
  for (const auto& skip : skip_tests) {
    if (suite_name == skip ||
        absl::StartsWith(suite_name, absl::StrCat(skip, "/"))) {
      std::cout << "[ SKIPPED SUITE ] " << suite_name << std::endl;
      return absl::OkStatus();
    }
  }

  if (sections.policy_content.empty() || sections.tests_content.empty()) {
    return absl::OkStatus();
  }

  // Parse Environment Config
  cel::Config config;
  if (!sections.config_content.empty()) {
    CEL_ASSIGN_OR_RETURN(
        config, cel::EnvConfigFromYaml(std::string(sections.config_content)));
  }

  // Enable default extensions (optional, bindings) in the config
  CEL_RETURN_IF_ERROR(config.AddExtensionConfig(
      "optional", cel::Config::ExtensionConfig::kLatest));
  CEL_RETURN_IF_ERROR(config.AddExtensionConfig(
      "bindings", cel::Config::ExtensionConfig::kLatest));

  // Set up compiler & runtime environments
  cel::Env env;
  env.SetDescriptorPool(pool);
  cel::RegisterStandardExtensions(env);
  env.SetConfig(config);

  cel::EnvRuntime env_runtime;
  env_runtime.SetDescriptorPool(pool);
  cel::RegisterStandardExtensions(env_runtime);
  env_runtime.SetConfig(config);
  env_runtime.mutable_runtime_options().enable_qualified_type_identifiers =
      true;

  CEL_ASSIGN_OR_RETURN(auto compiler_builder, env.NewCompilerBuilder());
  compiler_builder->GetParserBuilder().GetOptions().enable_optional_syntax =
      true;

  CEL_ASSIGN_OR_RETURN(auto compiler, compiler_builder->Build());

  CEL_ASSIGN_OR_RETURN(auto runtime_builder,
                       env_runtime.CreateRuntimeBuilder());

  // Register conformance enums
  for (const auto& enum_name :
       {"cel.expr.conformance.proto2.GlobalEnum",
        "cel.expr.conformance.proto3.GlobalEnum",
        "cel.expr.conformance.proto2.TestAllTypes.NestedEnum",
        "cel.expr.conformance.proto3.TestAllTypes.NestedEnum"}) {
    auto* enum_desc = pool->FindEnumTypeByName(enum_name);
    if (enum_desc != nullptr) {
      CEL_RETURN_IF_ERROR(cel::extensions::RegisterProtobufEnum(
          runtime_builder.type_registry(), enum_desc));
    }
  }

  // Register locationCode in runtime
  CEL_RETURN_IF_ERROR(
      (cel::UnaryFunctionAdapter<cel::Value, const cel::StringValue&>::
           RegisterGlobalOverload("locationCode", LocationCode,
                                  runtime_builder.function_registry())));

  CEL_ASSIGN_OR_RETURN(auto runtime, std::move(runtime_builder).Build());

  // Parse Policy
  CEL_ASSIGN_OR_RETURN(auto source,
                       cel::NewSource(sections.policy_content, "policy.yaml"));
  auto policy_source = std::make_shared<CelPolicySource>(std::move(source));
  CEL_ASSIGN_OR_RETURN(CelPolicyParseResult parse_result,
                       cel::ParseYamlCelPolicy(policy_source));
  if (!parse_result.IsValid()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to parse policy.yaml in ", suite_name,
                     "\nIssues:\n", parse_result.FormattedIssues()));
  }
  const CelPolicy* policy = parse_result.GetPolicy();

  // Compile Policy (unexpected non-ok status represents a bug)
  CEL_ASSIGN_OR_RETURN(CelPolicyValidationResult compile_result,
                       CompilePolicy(*compiler, *policy));

  TestSuite test_suite;
  CEL_ASSIGN_OR_RETURN(
      test_suite, cel::test::ParsePolicyTestSuiteYaml(sections.tests_content));

  std::string suite_name_str(suite_name);
  auto runner = std::make_shared<PolicyTestSuiteRunner>(
      suite_name_str, std::move(compiler), std::move(runtime),
      std::move(policy_source), std::move(compile_result), pool,
      message_factory, input_evaluator, config.GetContextType(),
      /*expect_compile_fail=*/absl::StrContains(suite_name, "compile_error"));

  for (const auto& section : test_suite.sections()) {
    std::string section_name = section.name();
    for (const auto& test : section.tests()) {
      std::string test_name = test.name();
      std::string full_test_name =
          absl::StrCat(suite_name, "/", section_name, "/", test_name);

      bool skip = !ShouldRunTest(full_test_name, skip_tests);

      testing::RegisterTest(
          suite_name_str.c_str(),
          absl::StrCat(section_name, "/", test_name).c_str(), nullptr,
          test_name.c_str(), __FILE__, __LINE__,
          [runner, test, full_test_name, skip]() -> CelPolicyTest* {
            return new CelPolicyTest(runner, test, full_test_name, skip);
          });
    }
  }
  return absl::OkStatus();
}

void RegisterAllTests() {
  std::vector<std::string> bundle_paths = absl::GetFlag(FLAGS_test_bundles);
  std::vector<std::string> skip_tests = absl::GetFlag(FLAGS_skip_tests);

  ABSL_CHECK(!bundle_paths.empty())
      << "No test bundles specified in --test_bundles flag.";

  std::shared_ptr<const google::protobuf::DescriptorPool> pool =
      GetSharedTestingDescriptorPool();
  auto message_factory =
      std::make_shared<google::protobuf::DynamicMessageFactory>(pool.get());
  message_factory->SetDelegateToGeneratedFactory(true);
  auto evaluator_or = InputEvaluator::Create(pool);
  ABSL_CHECK_OK(evaluator_or.status()) << "Failed to create input evaluator";
  std::shared_ptr<InputEvaluator> evaluator = std::move(evaluator_or.value());

  for (const std::string& bundle_path : bundle_paths) {
    std::string abs_path = cel::internal::ResolveRunfilesPath(bundle_path);
    ABSL_CHECK(!abs_path.empty())
        << "Could not resolve runfile path for test bundle: " << bundle_path;

    std::string bundle_content;
    ABSL_CHECK_OK(cel::internal::GetFileContents(abs_path, &bundle_content))
        << "Failed to read bundle file: " << abs_path;

    auto sections_or = ParseYamlBundle(bundle_content);
    ABSL_CHECK_OK(sections_or.status())
        << "Failed to parse bundle file: " << abs_path;

    absl::string_view filename = bundle_path;
    size_t last_slash = filename.find_last_of("/\\");
    if (last_slash != absl::string_view::npos) {
      filename = filename.substr(last_slash + 1);
    }
    absl::string_view suite_view = filename;
    absl::ConsumeSuffix(&suite_view, "_bundle.yaml");
    std::string suite_name = std::string(suite_view);

    ABSL_CHECK_OK(RegisterTestSuite(suite_name, sections_or.value(), evaluator,
                                    pool, message_factory, skip_tests));
  }
}

}  // namespace
}  // namespace cel

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);

  cel::RegisterAllTests();
  return RUN_ALL_TESTS();
}
