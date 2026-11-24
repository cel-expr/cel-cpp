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

#include "conformance/env/yaml_matcher.h"

#include <sstream>
#include <string>

#include "internal/testing.h"

namespace cel {
namespace {

using testing::Not;

TEST(YamlMatcherTest, MatchesIdenticalYaml) {
  EXPECT_THAT(
      "variables:\n"
      "  - name: x\n"
      "    type: bool\n",
      EqualsYaml("variables:\n"
                 "  - name: x\n"
                 "    type: bool\n"));
}

TEST(YamlMatcherTest, IgnoresWhitespaceLineEndingsCommentsAndIndentation) {
  EXPECT_THAT(
      "variables:  \r\n"
      "\r\n"
      "  # a comment\n"
      "  - name: x\t \r\n"
      "    type: bool  \n",
      EqualsYaml("variables:\n"
                 "    - name: x\n"
                 "      type: bool"));
}

TEST(YamlMatcherTest, IgnoresQuotingStyleDifferences) {
  EXPECT_THAT(
      "variables:\n"
      "  - name: \"x\"\n"
      "    type: \"map<int, string>\"\n",
      EqualsYaml("variables:\n"
                 "  - name: 'x'\n"
                 "    type: map<int, string>\n"));
}

TEST(YamlMatcherTest, MatchesEquivalentDoubleAndSingleQuotedEscapes) {
  EXPECT_THAT(R"(type: "~T\\.U")", EqualsYaml(R"(type: '~T\.U')"));
  EXPECT_THAT("a: \"it's\"\n", EqualsYaml("a: 'it''s'\n"));
}

TEST(YamlMatcherTest, IgnoresMapKeyOrdering) {
  EXPECT_THAT(
      "a: 1\n"
      "b:\n"
      "  x: true\n"
      "  y: false\n",
      EqualsYaml("b:\n"
                 "  y: false\n"
                 "  x: true\n"
                 "a: 1\n"));
}

TEST(YamlMatcherTest, MatchesFlowAndBlockStyles) {
  EXPECT_THAT(
      "params: [int, bool, string]\n"
      "meta: {k1: v1, k2: v2}\n",
      EqualsYaml("params:\n"
                 "  - int\n"
                 "  - bool\n"
                 "  - string\n"
                 "meta:\n"
                 "  k2: v2\n"
                 "  k1: v1\n"));
}

TEST(YamlMatcherTest, MatchesEmptyAndNullDocuments) {
  EXPECT_THAT("", EqualsYaml(""));
  EXPECT_THAT("# only a comment\n", EqualsYaml(""));
  EXPECT_THAT("a:\n", EqualsYaml("a: null\n"));
  EXPECT_THAT("a: ~", EqualsYaml("a: null"));
}

TEST(YamlMatcherTest, DistinguishesEmptyStringFromNull) {
  EXPECT_THAT("a: \"\"", Not(EqualsYaml("a: null")));
  EXPECT_THAT("a: ''", Not(EqualsYaml("a:")));
}

TEST(YamlMatcherTest, DetectsScalarMismatch) {
  EXPECT_THAT(
      "variables:\n"
      "  - name: x\n"
      "    type: int\n",
      Not(EqualsYaml("variables:\n"
                     "  - name: x\n"
                     "    type: bool\n")));
}

TEST(YamlMatcherTest, MatchesEquivalentFloatScalars) {
  EXPECT_THAT("value: 3.1400000000000001\n", EqualsYaml("value: 3.14\n"));
  EXPECT_THAT("value: 1.50\n", EqualsYaml("value: 1.5\n"));
  EXPECT_THAT("value: 0\n", EqualsYaml("value: 0.0\n"));
  EXPECT_THAT("value: 1e2\n", EqualsYaml("value: 100.0\n"));
  EXPECT_THAT("value: .nan\n", EqualsYaml("value: .NaN\n"));
  EXPECT_THAT("value: .inf\n", EqualsYaml("value: +.Inf\n"));
  EXPECT_THAT("value: -.inf\n", EqualsYaml("value: -.INF\n"));
}

TEST(YamlMatcherTest, RejectsNonEquivalentFloatAndIntegerScalars) {
  EXPECT_THAT("value: 3.14\n", Not(EqualsYaml("value: 3.15\n")));
  EXPECT_THAT("value: .inf\n", Not(EqualsYaml("value: -.inf\n")));
  EXPECT_THAT("value: inf\n", Not(EqualsYaml("value: .inf\n")));
  EXPECT_THAT("value: \"3.1400000000000001\"\n",
              Not(EqualsYaml("value: \"3.14\"\n")));
  EXPECT_THAT("value: \"3.1400000000000001\"\n",
              Not(EqualsYaml("value: 3.14\n")));
  EXPECT_THAT("value: !!str 3.140\n", Not(EqualsYaml("value: !!str 3.14\n")));
  EXPECT_THAT("value: 9007199254740992\n",
              Not(EqualsYaml("value: 9007199254740993\n")));
}

TEST(YamlMatcherTest, DetectsSequenceOrderAndSizeMismatch) {
  EXPECT_THAT(
      "params:\n"
      "  - int\n"
      "  - bool\n",
      Not(EqualsYaml("params:\n"
                     "  - bool\n"
                     "  - int\n")));
  EXPECT_THAT(
      "params:\n"
      "  - int\n",
      Not(EqualsYaml("params:\n"
                     "  - int\n"
                     "  - bool\n")));
}

TEST(YamlMatcherTest, DetectsMissingAndExtraMapKeys) {
  EXPECT_THAT("a: 1\n", Not(EqualsYaml("a: 1\nb: 2\n")));
  EXPECT_THAT("a: 1\nb: 2\n", Not(EqualsYaml("a: 1\n")));
}

TEST(YamlMatcherTest, DetectsNodeTypeMismatch) {
  testing::StringMatchResultListener seq_vs_map_listener;
  EXPECT_FALSE(testing::ExplainMatchResult(
      EqualsYaml("a:\n  b: 1\n"), "a:\n  - 1\n", &seq_vs_map_listener));
  EXPECT_THAT(
      seq_vs_map_listener.str(),
      testing::HasSubstr("type mismatch (actual: sequence, expected: map)"));

  testing::StringMatchResultListener scalar_vs_seq_listener;
  EXPECT_FALSE(testing::ExplainMatchResult(EqualsYaml("a:\n  - 1\n"), "a: 1\n",
                                           &scalar_vs_seq_listener));
  EXPECT_THAT(
      scalar_vs_seq_listener.str(),
      testing::HasSubstr("type mismatch (actual: scalar, expected: sequence)"));

  testing::StringMatchResultListener null_vs_scalar_listener;
  EXPECT_FALSE(testing::ExplainMatchResult(EqualsYaml("a: 1\n"), "a: null\n",
                                           &null_vs_scalar_listener));
  EXPECT_THAT(
      null_vs_scalar_listener.str(),
      testing::HasSubstr("type mismatch (actual: null, expected: scalar)"));
}

TEST(YamlMatcherTest, DetectsExplicitTagMismatch) {
  EXPECT_THAT("data: !!binary SGVsbG8=", EqualsYaml("data: !!binary SGVsbG8="));
  EXPECT_THAT("data: !!binary SGVsbG8=", Not(EqualsYaml("data: SGVsbG8=")));
}

TEST(YamlMatcherTest, FailsGracefullyOnInvalidYaml) {
  EXPECT_THAT("a: [unclosed", Not(EqualsYaml("a: 1")));
  EXPECT_THAT("a: 1", Not(EqualsYaml("a: [unclosed")));
}

TEST(YamlMatcherTest, RejectsNonScalarMapKeys) {
  testing::StringMatchResultListener expected_listener;
  EXPECT_FALSE(testing::ExplainMatchResult(
      EqualsYaml("? [a, b]\n: 1\n"), "? [a, b]\n: 1\n", &expected_listener));
  EXPECT_THAT(expected_listener.str(),
              testing::HasSubstr("unsupported non-scalar map key"));

  testing::StringMatchResultListener actual_listener;
  EXPECT_FALSE(testing::ExplainMatchResult(
      EqualsYaml("a: 1\n"), "a: 1\n? [b, c]\n: 2\n", &actual_listener));
  EXPECT_THAT(actual_listener.str(),
              testing::HasSubstr("unsupported non-scalar map key"));
}

TEST(YamlMatcherTest, RejectsDuplicateMapKeys) {
  testing::StringMatchResultListener actual_listener;
  EXPECT_FALSE(testing::ExplainMatchResult(EqualsYaml("variables:\n"
                                                      "  - name: x\n"
                                                      "    type: bool\n"),
                                           "variables:\n"
                                           "  - name: x\n"
                                           "    type: bool\n"
                                           "    type: int\n",
                                           &actual_listener));
  EXPECT_THAT(actual_listener.str(),
              testing::HasSubstr("duplicate key \"type\" in actual YAML"));

  testing::StringMatchResultListener expected_listener;
  EXPECT_FALSE(testing::ExplainMatchResult(EqualsYaml("variables:\n"
                                                      "  - name: x\n"
                                                      "    type: bool\n"
                                                      "    type: int\n"),
                                           "variables:\n"
                                           "  - name: x\n"
                                           "    type: bool\n",
                                           &expected_listener));
  EXPECT_THAT(expected_listener.str(),
              testing::HasSubstr("duplicate key \"type\" in expected YAML"));

  testing::StringMatchResultListener both_listener;
  EXPECT_FALSE(testing::ExplainMatchResult(EqualsYaml("a: 1\na: 1\n"),
                                           "a: 1\na: 2\n", &both_listener));
  EXPECT_THAT(both_listener.str(), testing::HasSubstr("duplicate key \"a\""));
}

TEST(YamlMatcherTest, RejectsExcessiveNestingDepth) {
  std::string deep_yaml = std::string(102, '[') + std::string(102, ']');
  testing::StringMatchResultListener listener;
  EXPECT_FALSE(
      testing::ExplainMatchResult(EqualsYaml(deep_yaml), deep_yaml, &listener));
  EXPECT_THAT(listener.str(),
              testing::HasSubstr("exceeded maximum nesting depth"));
}

TEST(YamlMatcherTest, DescribesMatcherAndNegation) {
  auto matcher = EqualsYaml("a: 1\n");

  std::ostringstream os;
  matcher.DescribeTo(&os);
  EXPECT_THAT(os.str(), testing::HasSubstr("equals YAML:\na: 1\n"));

  std::ostringstream neg_os;
  matcher.DescribeNegationTo(&neg_os);
  EXPECT_THAT(neg_os.str(), testing::HasSubstr("does not equal YAML:\na: 1\n"));
}

}  // namespace
}  // namespace cel
