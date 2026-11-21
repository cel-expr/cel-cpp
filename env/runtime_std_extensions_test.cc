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

#include "env/runtime_std_extensions.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "checker/optional.h"
#include "checker/validation_result.h"
#include "common/ast.h"
#include "common/value.h"
#include "compiler/compiler.h"
#include "env/config.h"
#include "env/env.h"
#include "env/env_runtime.h"
#include "env/env_std_extensions.h"
#include "extensions/lists_functions.h"
#include "extensions/math_ext_decls.h"
#include "extensions/network_ext_decls.h"
#include "extensions/strings.h"
#include "internal/testing.h"
#include "internal/testing_descriptor_pool.h"
#include "runtime/activation.h"
#include "runtime/runtime.h"
#include "google/protobuf/arena.h"

namespace cel {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::IsEmpty;
using ::testing::ValuesIn;

struct TestCase {
  std::string extension_name;
  std::vector<int> extension_versions = {0};
  int latest_extension_version = 0;
  std::string expr;
  bool requires_optional_extension = false;
};

using RuntimeStdExtensionTest = testing::TestWithParam<TestCase>;

TEST_P(RuntimeStdExtensionTest, RegisterStandardExtensions) {
  const TestCase& param = GetParam();
  Env env;
  env.SetDescriptorPool(cel::internal::GetSharedTestingDescriptorPool());
  RegisterStandardExtensions(env);

  Config compiler_config;
  // For the compilation step, assume latest version of the extension to ensure
  // a successful compilation. Later, we will test the runtime with different
  // extension versions.
  ASSERT_THAT(compiler_config.AddExtensionConfig(
                  param.extension_name, Config::ExtensionConfig::kLatest),
              IsOk());
  env.SetConfig(compiler_config);
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> compiler, env.NewCompiler());
  ASSERT_OK_AND_ASSIGN(ValidationResult result, compiler->Compile(param.expr));
  EXPECT_THAT(result.GetIssues(), IsEmpty()) << result.FormatError();
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Ast> ast, result.ReleaseAst());

  for (int version = 0; version <= param.latest_extension_version; ++version) {
    Config runtime_config;
    // Request a specific version of the extension to be configured in the
    // runtime.
    ASSERT_THAT(
        runtime_config.AddExtensionConfig(param.extension_name, version),
        IsOk());
    if (param.requires_optional_extension) {
      ASSERT_THAT(runtime_config.AddExtensionConfig("optional"), IsOk());
    }

    EnvRuntime env_runtime;
    env_runtime.SetDescriptorPool(
        cel::internal::GetSharedTestingDescriptorPool());
    RegisterStandardExtensions(env_runtime);
    env_runtime.SetConfig(runtime_config);
    ASSERT_OK_AND_ASSIGN(std::unique_ptr<Runtime> runtime,
                         env_runtime.NewRuntime());
    absl::StatusOr<std::unique_ptr<Program>> program_or =
        runtime->CreateProgram(std::make_unique<Ast>(*ast));

    // If the function is not supported in this extension version, check that
    // the program creation returned an error.
    if (!absl::c_contains(param.extension_versions, version)) {
      EXPECT_THAT(program_or, StatusIs(absl::StatusCode::kInvalidArgument))
          << " expr: " << param.expr << " version: " << version;
      continue;
    }

    ASSERT_THAT(program_or, IsOk())
        << " expr: " << param.expr << " version: " << version;
    std::unique_ptr<Program> program = *std::move(program_or);
    google::protobuf::Arena arena;
    Activation activation;
    ASSERT_OK_AND_ASSIGN(Value value, program->Evaluate(&arena, activation));
    EXPECT_TRUE(value.GetBool())
        << " expr: " << param.expr << " version: " << version;
  }
}

std::vector<TestCase> GetRuntimeStdExtensionTestCases() {
  return {
      TestCase{
          // The "bindings" extension does not register any runtime functions -
          // only macros.
          .extension_name = "bindings",
          .expr = "cel.bind(t, 42, t + 1) == 43",
      },
      TestCase{
          .extension_name = "encoders",
          .expr = "base64.encode(b'hello') == 'aGVsbG8='",
      },
      TestCase{
          .extension_name = "lists",
          .extension_versions = {0, 1, 2},
          .latest_extension_version = extensions::kListsExtensionLatestVersion,
          .expr = "[3, 2, 1].slice(0, 1) == [3]",
      },
      TestCase{
          .extension_name = "lists",
          .extension_versions = {1, 2},
          .latest_extension_version = extensions::kListsExtensionLatestVersion,
          .expr = "[[1, 2], 3].flatten() == [1, 2, 3]",
      },
      TestCase{
          .extension_name = "lists",
          .extension_versions = {2},
          .latest_extension_version = extensions::kListsExtensionLatestVersion,
          .expr = "[3, 2, 1].sort() == [1, 2, 3]",
      },
      TestCase{
          .extension_name = "math",
          .extension_versions = {0, 1, 2},
          .latest_extension_version = extensions::kMathExtensionLatestVersion,
          .expr = "math.least([1, -2, 3]) == -2",
      },
      TestCase{
          .extension_name = "math",
          .extension_versions = {1, 2},
          .latest_extension_version = extensions::kMathExtensionLatestVersion,
          .expr = "math.floor(42.9) == 42.0",
      },
      TestCase{
          .extension_name = "math",
          .extension_versions = {2},
          .latest_extension_version = extensions::kMathExtensionLatestVersion,
          .expr = "math.sqrt(4) == 2.0",
      },
      TestCase{
          .extension_name = "optional",
          .extension_versions = {0, 1, 2},
          .latest_extension_version = kOptionalExtensionLatestVersion,
          .expr = "optional.of(1).hasValue()",
      },
      TestCase{
          // No runtime functions.
          .extension_name = "protos",
          .expr = "!proto.hasExt(cel.expr.conformance.proto2.TestAllTypes{}, "
                  "cel.expr.conformance.proto2.nested_ext)",
      },
      TestCase{
          .extension_name = "sets",
          .expr = "sets.contains([1], [1])",
      },
      TestCase{
          .extension_name = "strings",
          .extension_versions = {0, 1, 2, 3, 4},
          .latest_extension_version =
              extensions::kStringsExtensionLatestVersion,
          .expr = "'Hello, who!'.replace('who', 'World') == 'Hello, World!'",
      },
      TestCase{
          .extension_name = "strings",
          .extension_versions = {1, 2, 3, 4},
          .latest_extension_version =
              extensions::kStringsExtensionLatestVersion,
          .expr = "strings.quote('hello') == '\"hello\"'",
      },
      TestCase{
          .extension_name = "strings",
          .extension_versions = {2, 3, 4},
          .latest_extension_version =
              extensions::kStringsExtensionLatestVersion,
          .expr = "['hello', 'world'].join(', ') == 'hello, world'",
      },
      TestCase{
          .extension_name = "strings",
          .extension_versions = {3, 4},
          .latest_extension_version =
              extensions::kStringsExtensionLatestVersion,
          .expr = "'stressed'.reverse() == 'desserts'",
      },
      TestCase{
          // No runtime functions.
          .extension_name = "cel.lib.ext.comprev2",
          .expr = "[1, 2, 3].map(i, i * 2) == [2, 4, 6]",
      },
      TestCase{
          .extension_name = "cel.lib.ext.regex",
          .expr = "regex.replace('abc', '$', '_end') == 'abc_end'",
          .requires_optional_extension = true,
      },
      TestCase{
          .extension_name = "cel.lib.ext.network",
          .extension_versions = {0, 1},
          .latest_extension_version =
              extensions::kNetworkExtensionLatestVersion,
          .expr = "type(ip('192.168.0.1')) == net.IP && "
                  "ip.isCanonical('192.168.0.1') && "
                  "cidr('10.0.0.0/8').containsIP('10.0.0.1')",
      },
      TestCase{
          .extension_name = "network",
          .extension_versions = {0, 1},
          .latest_extension_version =
              extensions::kNetworkExtensionLatestVersion,
          .expr = "cidr('192.168.0.1/24').masked() == cidr('192.168.0.0/24')",
      },
  };
}

INSTANTIATE_TEST_SUITE_P(RuntimeStdExtensionTest, RuntimeStdExtensionTest,
                         ValuesIn(GetRuntimeStdExtensionTestCases()));

}  // namespace
}  // namespace cel
