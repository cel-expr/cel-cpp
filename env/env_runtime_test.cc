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

#include "env/env_runtime.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "checker/validation_result.h"
#include "common/ast.h"
#include "common/source.h"
#include "common/value.h"
#include "compiler/compiler.h"
#include "env/config.h"
#include "env/env.h"
#include "env/env_std_extensions.h"
#include "env/env_yaml.h"
#include "env/runtime_std_extensions.h"
#include "extensions/math_ext.h"
#include "internal/testing.h"
#include "internal/testing_descriptor_pool.h"
#include "runtime/activation.h"
#include "runtime/runtime.h"
#include "runtime/runtime_builder.h"
#include "runtime/runtime_options.h"
#include "google/protobuf/arena.h"

namespace cel {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::IsEmpty;
using ::testing::ValuesIn;

struct TestCase {
  std::string config_yaml;
  std::string expr;
  bool expected_to_fail = false;
};

class EnvRuntimeTest : public testing::TestWithParam<TestCase> {};

TEST_P(EnvRuntimeTest, EndToEnd) {
  const TestCase& param = GetParam();
  auto descriptor_pool = cel::internal::GetSharedTestingDescriptorPool();
  ASSERT_OK_AND_ASSIGN(Config config, EnvConfigFromYaml(param.config_yaml));

  Env env;
  env.SetDescriptorPool(descriptor_pool);
  RegisterStandardExtensions(env);
  env.SetConfig(config);

  EnvRuntime env_runtime;
  env_runtime.SetDescriptorPool(descriptor_pool);
  RegisterStandardExtensions(env_runtime);
  env_runtime.SetConfig(config);

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> compiler, env.NewCompiler());
  std::unique_ptr<Ast> ast;
  if (!param.expected_to_fail) {
    ASSERT_OK_AND_ASSIGN(ValidationResult result,
                         compiler->Compile(param.expr));
    EXPECT_THAT(result.GetIssues(), IsEmpty()) << result.FormatError();
    ASSERT_OK_AND_ASSIGN(ast, result.ReleaseAst());
  } else {
    // Bypass type checking to allow compilation to succeed since we expect the
    // runtime to fail.
    ASSERT_OK_AND_ASSIGN(std::unique_ptr<Source> source,
                         NewSource(param.expr, ""));
    ASSERT_OK_AND_ASSIGN(ast, compiler->GetParser().Parse(*source));
  }
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Runtime> runtime,
                       env_runtime.NewRuntime());

  absl::StatusOr<std::unique_ptr<Program>> program_or =
      runtime->CreateProgram(std::move(ast));
  if (param.expected_to_fail) {
    EXPECT_THAT(program_or, StatusIs(absl::StatusCode::kInvalidArgument))
        << " expr: " << param.expr;
    return;
  }

  ASSERT_THAT(program_or, IsOk()) << " expr: " << param.expr;

  std::unique_ptr<Program> program = *std::move(program_or);
  ASSERT_NE(program, nullptr);

  google::protobuf::Arena arena;
  Activation activation;
  ASSERT_OK_AND_ASSIGN(Value value, program->Evaluate(&arena, activation));
  EXPECT_TRUE(value.GetBool()) << " expr: " << param.expr;
}

std::vector<TestCase> GetEnvRuntimeTestCases() {
  return {
      TestCase{
          .config_yaml = R"yaml(
                extensions:
                - name: "encoders"
              )yaml",
          .expr = "base64.encode(b'hello') == 'aGVsbG8='",
      },
      TestCase{
          .config_yaml = R"yaml(
                extensions:
                - name: "encoders"
                - name: "optional"
              )yaml",
          .expr = "base64.encode(b'hello') == 'aGVsbG8=' && "
                  "optional.of(1).hasValue()",
      },
      TestCase{
          .config_yaml = R"yaml(
                extensions:
                - name: "encoders"
              )yaml",
          .expr = "base64.encode(b'hello') == 'aGVsbG8=' && "
                  "optional.of(1).hasValue()",
          .expected_to_fail = true,
      },
      TestCase{
          .config_yaml = R"yaml(
                stdlib:
                  disable: true
              )yaml",
          .expr = "1 + 2 == 3",
          .expected_to_fail = true,
      },
      TestCase{
          .config_yaml = R"yaml(
                stdlib:
                  disable: true
                extensions:
                - name: "encoders"
              )yaml",
          .expr = "base64.encode(b'hello') == 'aGVsbG8=' && "
                  "1 + 2 == 3",
          .expected_to_fail = true,
      },
      TestCase{
          .config_yaml = R"yaml(
                extensions:
                - name: "network"
              )yaml",
          .expr = "type(ip('192.168.0.1')) == net.IP && "
                  "cidr('10.0.0.0/8').containsIP('10.0.0.1')",
      },
      TestCase{
          .config_yaml = R"yaml(
                extensions:
                - name: "cel.lib.ext.network"
                  version: 1
              )yaml",
          .expr = "cidr('192.168.0.1/24').masked() == cidr('192.168.0.0/24')",
      },
  };
}

INSTANTIATE_TEST_SUITE_P(EnvRuntimeTest, EnvRuntimeTest,
                         ValuesIn(GetEnvRuntimeTestCases()));

TEST(EnvRuntimeTest, RegisterExtensionFunctions) {
  auto descriptor_pool = cel::internal::GetSharedTestingDescriptorPool();
  Config config;
  ASSERT_THAT(config.AddExtensionConfig("math", 2), IsOk());

  Env env;
  env.SetDescriptorPool(descriptor_pool);
  RegisterStandardExtensions(env);
  env.SetConfig(config);
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> compiler, env.NewCompiler());
  ASSERT_OK_AND_ASSIGN(ValidationResult result,
                       compiler->Compile("math.sqrt(4) == 2.0"));
  EXPECT_THAT(result.GetIssues(), IsEmpty()) << result.FormatError();
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Ast> ast, result.ReleaseAst());

  EnvRuntime env_runtime;
  env_runtime.SetDescriptorPool(descriptor_pool);
  env_runtime.RegisterExtensionFunctions(
      "cel.lib.math", "math", 2,
      [](cel::RuntimeBuilder& runtime_builder,
         const cel::RuntimeOptions& opts) -> absl::Status {
        return cel::extensions::RegisterMathExtensionFunctions(
            runtime_builder.function_registry(), opts, 2);
      });
  env_runtime.SetConfig(config);
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Runtime> runtime,
                       env_runtime.NewRuntime());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Program> program,
                       runtime->CreateProgram(std::move(ast)));
  ASSERT_NE(program, nullptr);

  google::protobuf::Arena arena;
  Activation activation;
  ASSERT_OK_AND_ASSIGN(Value value, program->Evaluate(&arena, activation));
  EXPECT_TRUE(value.GetBool());
}
}  // namespace
}  // namespace cel
