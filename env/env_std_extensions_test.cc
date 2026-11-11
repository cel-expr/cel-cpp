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

#include "env/env_std_extensions.h"

#include <memory>
#include <string>

#include "absl/strings/string_view.h"
#include "compiler/compiler.h"
#include "env/config.h"
#include "env/env.h"
#include "internal/testing.h"
#include "internal/testing_descriptor_pool.h"

namespace cel {
namespace {

using ::absl_testing::IsOk;
using ::testing::TestWithParam;

struct TestCase {
  std::string extension;
  std::string expr;
};

class EnvStdExtensions : public testing::TestWithParam<TestCase> {};

TEST_P(EnvStdExtensions, RegistrationTest) {
  const TestCase& param = GetParam();

  Env env;
  RegisterStandardExtensions(env);
  env.SetDescriptorPool(internal::GetSharedTestingDescriptorPool());

  Config config;
  // These test cases exercise features from the newest version of each
  // extension.
  ASSERT_THAT(config.AddExtensionConfig(param.extension,
                                        Config::ExtensionConfig::kLatest),
              IsOk());
  env.SetConfig(config);

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> compiler, env.NewCompiler());

  ASSERT_OK_AND_ASSIGN(auto result, compiler->Compile(param.expr));
  ASSERT_TRUE(result.IsValid()) << "Expected no issues for expr: " << param.expr
                                << " but got: " << result.FormatError();
}

INSTANTIATE_TEST_SUITE_P(
    RegistrationTest, EnvStdExtensions,
    ::testing::Values<TestCase>(
        TestCase{
            .extension = "cel.lib.ext.bindings",  // official name
            .expr = "cel.bind(t, true, t)",
        },
        TestCase{
            .extension = "bindings",  // alias
            .expr = "cel.bind(t, true, t)",
        },
        TestCase{
            .extension = "encoders",
            .expr = "base64.encode(b'hello')",
        },
        TestCase{
            .extension = "lists",
            .expr = "[1, 2, 3].sort()",
        },
        TestCase{
            .extension = "lists",
            .expr = "['a'].sortBy(e, e)",
        },
        TestCase{
            .extension = "math",
            .expr = "math.sqrt(-1)",
        },
        TestCase{
            .extension = "optional",
            .expr = "[1, 2].first()",
        },
        TestCase{
            .extension = "optional",
            .expr = "[0][?1]",  // optional syntax auto-enabled
        },
        TestCase{
            .extension = "protos",
            .expr = "!proto.hasExt(cel.expr.conformance.proto2.TestAllTypes{}, "
                    "cel.expr.conformance.proto2.nested_ext)",
        },
        TestCase{
            .extension = "sets",
            .expr = "sets.contains([1], [1])",
        },
        TestCase{
            .extension = "strings",
            .expr = "'foo'.reverse()",
        },
        TestCase{
            .extension = "two-var-comprehensions",
            .expr = "[1, 2, 3, 4].all(i, v, i < v)",
        },
        TestCase{
            .extension = "regex",
            .expr = "regex.replace('abc', '$', '_end')",
        }));

}  // namespace
}  // namespace cel
