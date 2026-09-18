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

#ifndef THIRD_PARTY_CEL_CPP_POLICY_TEST_UTIL_H_
#define THIRD_PARTY_CEL_CPP_POLICY_TEST_UTIL_H_

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "cel/expr/conformance/test/suite.pb.h"

namespace cel::test {

// Parses a YAML content representing a policy test suite (tests.yaml)
// and adapts it to the cel.expr.conformance.test.TestSuite protobuf message.
//
// TODO(uncreated-issue/92): Move to the testrunner library.
absl::StatusOr<cel::expr::conformance::test::TestSuite>
ParsePolicyTestSuiteYaml(absl::string_view yaml_content);

// Parses a YAML string representing an individual test case and adapts it to
// the cel.expr.conformance.test.TestCase protobuf message.
absl::StatusOr<cel::expr::conformance::test::TestCase> ParseTestCaseYaml(
    absl::string_view yaml_content);

}  // namespace cel::test

#endif  // THIRD_PARTY_CEL_CPP_POLICY_TEST_UTIL_H_
