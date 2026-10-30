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

#include "policy/yaml_policy_parser.h"

#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/absl_log.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "common/source.h"
#include "internal/runfiles.h"
#include "internal/testing.h"
#include "policy/cel_policy.h"
#include "policy/cel_policy_parse_result.h"
#include "policy/cel_policy_parser.h"
#include "yaml-cpp/node/node.h"

namespace cel {

namespace internal {
const CelPolicyParser<YAML::Node>& GetTestCustomYamlPolicyParser();
}  // namespace internal

namespace {

using ::absl_testing::IsOk;
using ::testing::HasSubstr;
using ::testing::IsNull;

constexpr absl::string_view kTestPolicyFilePath =
"_main/policy/testdata/";

constexpr absl::string_view kBaselineSeparator =
    "--------------------------------------------------------------------\n";

struct YamlPolicyParserTestCase {
  std::string policy_source_file;
  std::string baseline_file;
  const cel::CelPolicyParser<YAML::Node>& (*parser_factory)();
};

using YamlPolicyParserTest = testing::TestWithParam<YamlPolicyParserTestCase>;

TEST_P(YamlPolicyParserTest, Parse) {
  std::string contents;
  std::string test_file = cel::internal::ResolveRunfilesPath(
      absl::StrCat(kTestPolicyFilePath, GetParam().policy_source_file));
  ASSERT_THAT(cel::internal::GetFileContents(test_file, &contents), IsOk());

  std::string baseline;
  std::string baseline_file = cel::internal::ResolveRunfilesPath(
      absl::StrCat(kTestPolicyFilePath, GetParam().baseline_file));
  ASSERT_THAT(cel::internal::GetFileContents(baseline_file, &baseline), IsOk());
  baseline = absl::StripAsciiWhitespace(baseline);

  std::ostringstream out;
  out << "POLICY SOURCE: " << GetParam().policy_source_file << "\n";

  ASSERT_OK_AND_ASSIGN(cel::SourcePtr source,
                       cel::NewSource(contents, GetParam().policy_source_file));
  std::shared_ptr<CelPolicySource> policy_source =
      std::make_shared<CelPolicySource>(std::move(source));

  ASSERT_OK_AND_ASSIGN(
      CelPolicyParseResult parse_result,
      cel::ParseYamlCelPolicy(policy_source, GetParam().parser_factory()));

  out << kBaselineSeparator;
  if (parse_result.IsValid()) {
    out << "PARSED POLICY:\n";
    out << parse_result.GetPolicy()->DebugString();
  } else {
    ASSERT_THAT(parse_result.GetPolicy(), IsNull());
    out << kBaselineSeparator;
    out << "PARSER ISSUES:\n";
    for (const auto& issue : parse_result.GetIssues()) {
      out << issue.ToDisplayString(*policy_source) << "\n";
    }
  }

  std::string actual(absl::StripAsciiWhitespace(out.str()));
  if (actual != baseline) {
    // Log the actual result to make it easier to copy/paste into the baseline
    // file when updating the tests.
    ABSL_LOG(INFO) << "Actual:\n" << actual;
    EXPECT_EQ(actual, baseline);
  }
}

INSTANTIATE_TEST_SUITE_P(
    Formats, YamlPolicyParserTest,
    testing::ValuesIn({
        YamlPolicyParserTestCase{
            .policy_source_file = "cel_policy.yaml",
            .baseline_file = "cel_policy_parser.baseline",
            .parser_factory = GetDefaultYamlPolicyParser,
        },
        YamlPolicyParserTestCase{
            .policy_source_file = "nested_rule.yaml",
            .baseline_file = "nested_rule_parser.baseline",
            .parser_factory = GetDefaultYamlPolicyParser,
        },
        YamlPolicyParserTestCase{
            .policy_source_file = "custom_policy_format.yaml",
            .baseline_file = "custom_policy_format_parser.baseline",
            .parser_factory = internal::GetTestCustomYamlPolicyParser,
        },
        YamlPolicyParserTestCase{
            .policy_source_file = "custom_policy_format_with_errors.yaml",
            .baseline_file = "custom_policy_format_with_errors_parser.baseline",
            .parser_factory = internal::GetTestCustomYamlPolicyParser,
        },
    }));

struct ParseTestCase {
  std::string yaml;
  std::string expected_error;
};

using YamlPolicyParseErrorTest = testing::TestWithParam<ParseTestCase>;

TEST_P(YamlPolicyParseErrorTest, YamlSyntaxError) {
  const ParseTestCase& param = GetParam();
  ASSERT_OK_AND_ASSIGN(cel::SourcePtr source,
                       cel::NewSource(param.yaml, "test"));
  std::shared_ptr<CelPolicySource> policy_source =
      std::make_shared<CelPolicySource>(std::move(source));
  ASSERT_OK_AND_ASSIGN(CelPolicyParseResult parse_result,
                       cel::ParseYamlCelPolicy(policy_source));
  EXPECT_THAT(parse_result.FormattedIssues(), HasSubstr(param.expected_error));
}

std::vector<ParseTestCase> GetParseTestCases() {
  return {
      ParseTestCase{
          .yaml = "name: \"unclosed",
          .expected_error = "1:16: Invalid CEL policy YAML syntax\n"
                            " | name: \"unclosed\n"
                            " | ...............^",
      },
      ParseTestCase{
          .yaml = R"yaml( ? [ John, Doe ]: age: 30 )yaml",
          .expected_error = "1:22: Invalid CEL policy YAML syntax\n"
                            " |  ? [ John, Doe ]: age: 30 \n"
                            " | .....................^",
      },
      ParseTestCase{
          .yaml = R"yaml( invalid yaml )yaml",
          .expected_error = "1:2: Policy is not a map\n"
                            " |  invalid yaml \n"
                            " | .^",
      },
      ParseTestCase{
          .yaml = R"yaml(
                    ? [1, 2, 3]
                    : "Prime numbers sequence"
                  )yaml",
          .expected_error = "2:23: Policy tag is not a string\n"
                            " |                     ? [1, 2, 3]\n"
                            " | ......................^",
      },
      ParseTestCase{
          .yaml = R"yaml(
                  imports: N/A
                )yaml",
          .expected_error = "2:28: Policy 'imports' is not a sequence\n"
                            " |                   imports: N/A\n"
                            " | ...........................^",
      },
      ParseTestCase{
          .yaml = R"yaml(
                  imports:
                  - cel.expr.conformance
                )yaml",
          .expected_error = "3:21: Import is not a map\n"
                            " |                   - cel.expr.conformance\n"
                            " | ....................^",
      },
      ParseTestCase{
          .yaml = R"yaml(
                  imports:
                  - name:
                    - cel.expr.conformance
                )yaml",
          .expected_error = "4:21: Import name is not a string\n"
                            " |                     - cel.expr.conformance\n"
                            " | ....................^",
      },
      ParseTestCase{
          .yaml = R"yaml(
                  # Comment with multi-byte char: €
                  imports:
                  - name:
                    - cel.expr.conformance
                )yaml",
          .expected_error = "5:21: Import name is not a string\n"
                            " |                     - cel.expr.conformance\n"
                            " | ....................^",
      },
      ParseTestCase{
          .yaml = R"yaml(
                  rule: do something
                )yaml",
          .expected_error = "2:25: Policy 'rule' is not a map\n"
                            " |                   rule: do something\n"
                            " | ........................^",
      },
      ParseTestCase{
          .yaml = R"yaml(
                  rule:
                    id:
                    - 22
                )yaml",
          .expected_error = "4:21: Policy rule 'id' is not a string\n"
                            " |                     - 22\n"
                            " | ....................^",
      },
      ParseTestCase{
          .yaml = R"yaml(
                  rule:
                    variables:
                      no vars
                )yaml",
          .expected_error = "4:23: Policy rule 'variables' is not a sequence\n"
                            " |                       no vars\n"
                            " | ......................^",
      },
      ParseTestCase{
          .yaml = R"yaml(
                  rule:
                    variables:
                    - name:
                        foo: bar
                )yaml",
          .expected_error = "5:25: Policy variable 'name' is not a string\n"
                            " |                         foo: bar\n"
                            " | ........................^",
      },
      ParseTestCase{
          .yaml = R"yaml(
                  rule:
                    variables:
                    - name: test_var
                      expression:
                      - 22
                )yaml",
          .expected_error =
              "6:23: Policy variable 'expression' is not a string\n"
              " |                       - 22\n"
              " | ......................^",
      },
      ParseTestCase{
          .yaml = R"yaml(
                  rule:
                    variables:
                    - name: '\u0041\u00a9\u20ac\U0001f680'
                    - '\u0041\u00a9\u20ac\U0001f680': name
                )yaml",
          .expected_error =
              "5:23: Unrecognized policy variable tag: "
              "\\u0041\\u00a9\\u20ac\\U0001f680\n"
              " |                     - '\\u0041\\u00a9\\u20ac\\U0001f680': "
              "name\n"
              " | ......................^",
      },
      ParseTestCase{
          .yaml = R"yaml(
                  name: test
                  rule:
                    match:
                      - output: 'true'
                    aggregate:
                      - output: 'true'
                )yaml",
          .expected_error =
              "6:21: Only one of 'match' or 'aggregate' may be set in a rule\n"
              " |                     aggregate:\n"
              " | ....................^",
      },
      ParseTestCase{
          .yaml = R"yaml(
                  name: test
                  rule:
                    aggregate:
                      - output: 'true'
                    match:
                      - output: 'true'
                )yaml",
          .expected_error =
              "6:21: Only one of 'match' or 'aggregate' may be set in a rule\n"
              " |                     match:\n"
              " | ....................^",
      },
      ParseTestCase{
          .yaml = R"yaml(
                  rule:
                    aggregate: illegal
                )yaml",
          .expected_error = "3:32: Policy rule 'aggregate' is not a sequence\n"
                            " |                     aggregate: illegal\n"
                            " | ...............................^",
      },
  };
}

INSTANTIATE_TEST_SUITE_P(YamlPolicyParseErrorTest, YamlPolicyParseErrorTest,
                         ::testing::ValuesIn(GetParseTestCases()));

TEST(YamlPolicyParserTest, ParseAggregateRule) {
  absl::string_view yaml = R"yaml(
name: aggregate_policy
rule:
  id: top_agg
  aggregate:
    - condition: 'x > 0'
      output: '"positive"'
    - condition: 'true'
      rule:
        match:
          - condition: 'y > 0'
            rule:
              aggregate:
                - output: '"nested_agg"'
)yaml";
  ASSERT_OK_AND_ASSIGN(cel::SourcePtr source,
                       cel::NewSource(yaml, "aggregate.yaml"));
  std::shared_ptr<CelPolicySource> policy_source =
      std::make_shared<CelPolicySource>(std::move(source));
  ASSERT_OK_AND_ASSIGN(CelPolicyParseResult parse_result,
                       cel::ParseYamlCelPolicy(policy_source));
  ASSERT_TRUE(parse_result.IsValid()) << parse_result.FormattedIssues();
  const CelPolicy* policy = parse_result.GetPolicy();
  ASSERT_NE(policy, nullptr);
  EXPECT_TRUE(policy->rule().is_aggregate());
  ASSERT_EQ(policy->rule().matches().size(), 2);
  EXPECT_TRUE(policy->rule().matches()[0].has_output_block());
  ASSERT_TRUE(policy->rule().matches()[1].has_rule());
  const Rule& sub_rule = policy->rule().matches()[1].rule();
  EXPECT_FALSE(sub_rule.is_aggregate());
  ASSERT_EQ(sub_rule.matches().size(), 1);
  ASSERT_TRUE(sub_rule.matches()[0].has_rule());
  const Rule& nested_agg_rule = sub_rule.matches()[0].rule();
  EXPECT_TRUE(nested_agg_rule.is_aggregate());
  ASSERT_EQ(nested_agg_rule.matches().size(), 1);
  EXPECT_EQ(nested_agg_rule.matches()[0].output_block().output().value(),
            "\"nested_agg\"");
}

TEST(YamlPolicyParserTest, OffsetIssueFormatting) {
  // TODO(b/506179116): will need to copy the go implementation in extracting
  // the source string from the YAML document instead of the interpreted string
  // value to fix up error locations in folded and block literals.
  std::string contents;
  std::string test_file = cel::internal::ResolveRunfilesPath(
      absl::StrCat(kTestPolicyFilePath, "cel_policy.yaml"));
  ASSERT_THAT(cel::internal::GetFileContents(test_file, &contents), IsOk());

  ASSERT_OK_AND_ASSIGN(cel::SourcePtr source,
                       cel::NewSource(contents, "cel_policy.yaml"));
  std::shared_ptr<CelPolicySource> policy_source =
      std::make_shared<CelPolicySource>(std::move(source));
  ASSERT_OK_AND_ASSIGN(CelPolicyParseResult parse_result,
                       cel::ParseYamlCelPolicy(policy_source));

  ASSERT_TRUE(parse_result.IsValid());
  const CelPolicy* policy = parse_result.GetPolicy();

  CelPolicyElementId name_id = policy->name().id();

  CelPolicyIssue issue(name_id, 4, CelPolicyIssue::Severity::kError,
                       "Test error");

  std::string formatted = issue.ToDisplayString(*policy_source);

  EXPECT_THAT(formatted, HasSubstr("ERROR: cel_policy.yaml:16:11: Test error"));
  EXPECT_THAT(formatted, HasSubstr(" | name: cel_policy"));
  EXPECT_THAT(formatted, HasSubstr(" | ..........^"));
}

}  // namespace
}  // namespace cel
