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

#include "validator/bind_nesting_validator.h"

#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "checker/validation_result.h"
#include "compiler/compiler.h"
#include "compiler/compiler_factory.h"
#include "compiler/standard_library.h"
#include "extensions/bindings_ext.h"
#include "internal/status_macros.h"
#include "internal/testing.h"
#include "internal/testing_descriptor_pool.h"
#include "validator/validator.h"

namespace cel {
namespace {

using ::testing::HasSubstr;

absl::StatusOr<std::unique_ptr<Compiler>> StdLibCompiler() {
  CEL_ASSIGN_OR_RETURN(
      std::unique_ptr<CompilerBuilder> builder,
      NewCompilerBuilder(internal::GetSharedTestingDescriptorPool()));
  CEL_RETURN_IF_ERROR(builder->AddLibrary(StandardCompilerLibrary()));
  CEL_RETURN_IF_ERROR(
      builder->AddLibrary(cel::extensions::BindingsCompilerLibrary()));
  return builder->Build();
}

struct TestCase {
  std::string expression;
  int limit;
  bool valid;
  std::string error_substr = "";
};

using BindNestingValidatorTest = testing::TestWithParam<TestCase>;

TEST_P(BindNestingValidatorTest, Validate) {
  const TestCase& test_case = GetParam();
  Validator validator;
  validator.AddValidation(BindNestingLimitValidator(test_case.limit));

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> compiler, StdLibCompiler());
  ASSERT_OK_AND_ASSIGN(ValidationResult result,
                       compiler->Compile(test_case.expression));

  validator.UpdateValidationResult(result);

  EXPECT_EQ(result.IsValid(), test_case.valid)
      << "Expression: " << test_case.expression
      << " Limit: " << test_case.limit;
  if (!test_case.valid) {
    EXPECT_THAT(result.FormatError(), HasSubstr(test_case.error_substr));
  }
}

INSTANTIATE_TEST_SUITE_P(
    BindNestingValidatorTest, BindNestingValidatorTest,
    testing::Values(
        // No cel.bind calls.
        TestCase{"1 + 2", 0, true},
        // Single cel.bind.
        TestCase{"cel.bind(x, 1, x + x)", 0, false,
                 "cel.bind nesting level of 1 exceeds limit of 0"},
        TestCase{"cel.bind(x, 1, x + x)", 1, true},
        // Sibling (non-nested) cel.bind expressions.
        TestCase{"cel.bind(x, 1, x) + cel.bind(y, 2, y)", 1, true},
        // Nested in result expression.
        TestCase{"cel.bind(x, 'a', cel.bind(y, x + x, y + y))", 1, false,
                 "cel.bind nesting level of 2 exceeds limit of 1"},
        TestCase{"cel.bind(x, 'a', cel.bind(y, x + x, y + y))", 2, true},
        // Nested in init expression.
        TestCase{"cel.bind(x, cel.bind(y, 'a', y + y), x + x)", 1, false,
                 "cel.bind nesting level of 2 exceeds limit of 1"},
        TestCase{"cel.bind(x, cel.bind(y, 'a', y + y), x + x)", 2, true},
        // Three levels of nesting.
        TestCase{"cel.bind(a, 'x', cel.bind(b, a + a, cel.bind(c, b + b, c)))",
                 2, false, "cel.bind nesting level of 3 exceeds limit of 2"},
        TestCase{"cel.bind(a, 'x', cel.bind(b, a + a, cel.bind(c, b + b, c)))",
                 3, true},
        // Regular and empty-range comprehensions do not count as cel.bind.
        TestCase{"[1, 2].all(x, [1, 2].all(y, x > 0 && y > 0))", 0, true},
        TestCase{"[].all(x, [].all(y, true))", 0, true},
        TestCase{"[1, 2].all(x, cel.bind(y, x + 1, y > 0))", 1, true},
        TestCase{"cel.bind(x, [1, 2], x.all(y, cel.bind(z, y + 1, z > 0)))", 1,
                 false, "cel.bind nesting level of 2 exceeds limit of 1"},
        TestCase{"cel.bind(x, [1, 2], x.all(y, cel.bind(z, y + 1, z > 0)))", 2,
                 true}));

}  // namespace
}  // namespace cel
