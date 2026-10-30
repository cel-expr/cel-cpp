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

#include "policy/cel_policy.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/strings/str_replace.h"
#include "common/source.h"
#include "internal/testing.h"

namespace cel {
namespace {

using testing::Field;
using testing::Optional;
using testing::SizeIs;

TEST(CelPolicyBuilderTest, Build) {
  CelPolicyElementId next_id = 1;
  ASSERT_OK_AND_ASSIGN(SourcePtr source, NewSource("CEL\n  policy\n  source"));
  std::shared_ptr<CelPolicySource> policy_source =
      std::make_shared<CelPolicySource>(std::move(source));
  CelPolicy policy(policy_source);
  policy.set_name(ValueString(next_id++, "test_policy"));
  policy.set_description(ValueString(next_id++, "test_description"));
  policy.set_display_name(ValueString(next_id++, "test_display_name"));
  ValueString import1_name = ValueString(next_id++, "test_import1");
  policy.mutable_imports().push_back(Import(next_id++, import1_name));
  ValueString import2_name = ValueString(next_id++, "test_import2");
  policy.mutable_imports().push_back(Import(next_id++, import2_name));

  Rule& rule = policy.mutable_rule();
  rule.set_id(next_id++);
  rule.set_rule_id(ValueString(next_id++, "test_rule_id"));
  rule.set_description(ValueString(next_id++, "test_rule_description"));

  Variable variable;
  variable.set_name(ValueString(next_id++, "test_variable"));
  variable.set_expression(ValueString(next_id++, "test_expression"));
  variable.set_description(ValueString(next_id++, "test_variable_description"));
  variable.set_display_name(
      ValueString(next_id++, "test_variable_display_name"));

  Match match1;
  match1.set_id(next_id++);
  match1.set_condition(ValueString(next_id++, "test_condition"));
  CelPolicyElementId output_id = next_id++;
  CelPolicyElementId explanation_id = next_id++;
  match1.set_result(
      OutputBlock(ValueString(output_id, "test_result"),
                  ValueString(explanation_id, "test_explanation")));

  Match match2;
  match2.set_id(next_id++);
  match2.set_condition(ValueString(next_id++, "test_condition2"));

  auto sub_rule = std::make_unique<Rule>();
  sub_rule->set_id(next_id++);
  sub_rule->set_rule_id(ValueString(next_id++, "sub_rule_id"));
  sub_rule->set_description(ValueString(next_id++, "sub_rule_description"));
  Match sub_rule_match;
  sub_rule_match.set_id(next_id++);
  sub_rule_match.set_condition(ValueString(next_id++, "sub_rule_condition"));
  sub_rule_match.set_result(
      OutputBlock(ValueString(next_id++, "sub_rule_result"), std::nullopt));
  sub_rule->mutable_matches().push_back(sub_rule_match);

  match2.set_result(std::move(sub_rule));

  rule.mutable_variables().push_back(variable);
  rule.mutable_matches().push_back(match1);
  rule.mutable_matches().push_back(match2);

  EXPECT_EQ(policy.name().value(), "test_policy");
  ASSERT_TRUE(policy.description().has_value());
  EXPECT_EQ(policy.description()->value(), "test_description");
  ASSERT_TRUE(policy.display_name().has_value());
  EXPECT_EQ(policy.display_name()->value(), "test_display_name");

  ASSERT_THAT(policy.imports(), SizeIs(2));

  EXPECT_EQ(policy.imports()[0].name().value(), "test_import1");
  EXPECT_EQ(policy.imports()[1].name().value(), "test_import2");
  ASSERT_TRUE(policy.rule().rule_id().has_value());
  EXPECT_EQ(policy.rule().rule_id()->value(), "test_rule_id");
  ASSERT_TRUE(policy.rule().description().has_value());
  EXPECT_EQ(policy.rule().description()->value(), "test_rule_description");

  ASSERT_THAT(policy.rule().variables(), SizeIs(1));

  EXPECT_EQ(policy.rule().variables()[0].name().value(), "test_variable");
  EXPECT_EQ(policy.rule().variables()[0].expression().value(),
            "test_expression");
  ASSERT_TRUE(policy.rule().variables()[0].description().has_value());
  EXPECT_EQ(policy.rule().variables()[0].description()->value(),
            "test_variable_description");
  ASSERT_TRUE(policy.rule().variables()[0].display_name().has_value());
  EXPECT_EQ(policy.rule().variables()[0].display_name()->value(),
            "test_variable_display_name");

  ASSERT_THAT(policy.rule().matches(), SizeIs(2));

  EXPECT_EQ(policy.rule().matches()[0].condition().value().value(),
            "test_condition");
  ASSERT_TRUE(policy.rule().matches()[0].has_output_block());
  EXPECT_EQ(policy.rule().matches()[0].output_block().output().value(),
            "test_result");
  ASSERT_TRUE(
      policy.rule().matches()[0].output_block().explanation().has_value());
  EXPECT_EQ(policy.rule().matches()[0].output_block().explanation()->value(),
            "test_explanation");

  EXPECT_EQ(policy.rule().matches()[1].condition().value().value(),
            "test_condition2");
  ASSERT_TRUE(policy.rule().matches()[1].has_rule());
  ASSERT_TRUE(policy.rule().matches()[1].rule().rule_id().has_value());
  EXPECT_EQ(policy.rule().matches()[1].rule().rule_id()->value(),
            "sub_rule_id");
  ASSERT_TRUE(policy.rule().matches()[1].rule().description().has_value());
  EXPECT_EQ(policy.rule().matches()[1].rule().description()->value(),
            "sub_rule_description");
  ASSERT_THAT(policy.rule().matches()[1].rule().matches(), SizeIs(1));
  EXPECT_EQ(policy.rule()
                .matches()[1]
                .rule()
                .matches()[0]
                .condition()
                .value()
                .value(),
            "sub_rule_condition");

  std::string actual = policy.DebugString();
  EXPECT_EQ(actual, absl::StrReplaceAll(R"(CelPolicy{
      ===========================================================
        CEL
          policy
          source
      ===========================================================
      name: #1> "test_policy"
      description: #2> "test_description"
      display_name: #3> "test_display_name"
      imports:
        #5> name: #4> "test_import1"
        #7> name: #6> "test_import2"
      #8> rule: {
        rule_id: #9> "test_rule_id"
        description: #10> "test_rule_description"
        variable: {
          name: #11> "test_variable"
          expression: #12> "test_expression"
          description: #13> "test_variable_description"
          display_name: #14> "test_variable_display_name"
        }
        #15> match: {
          condition: #16> "test_condition"
          result: {
            output: #17> "test_result"
            explanation: #18> "test_explanation"
          }
        }
        #19> match: {
          condition: #20> "test_condition2"
          result:
            #21> rule: {
              rule_id: #22> "sub_rule_id"
              description: #23> "sub_rule_description"
              #24> match: {
                condition: #25> "sub_rule_condition"
                result: {
                  output: #26> "sub_rule_result"
                }
              }
            }
        }
      }
    })",
                                        {{"\n    ", "\n"}}));
}

TEST(CelPolicySourceTest, Build) {
  std::string source =
      "name: test_policy\n    imports:\n     - name: test_import\n";

  ASSERT_OK_AND_ASSIGN(SourcePtr source_ptr, NewSource(source));
  CelPolicySource policy_source(std::move(source_ptr));
  policy_source.NoteSourcePosition(1, source.find("test_policy"));
  policy_source.NoteSourcePosition(2, source.find("test_import"));

  EXPECT_THAT(policy_source.GetSourcePosition(1), Optional(6));
  EXPECT_THAT(policy_source.GetSourceLocation(1),
              Optional(AllOf(Field(&SourceLocation::line, 1),
                             Field(&SourceLocation::column, 6))));
  EXPECT_THAT(policy_source.GetSourcePosition(2), Optional(44));
  EXPECT_THAT(policy_source.GetSourceLocation(2),
              Optional(AllOf(Field(&SourceLocation::line, 3),
                             Field(&SourceLocation::column, 13))));
  EXPECT_EQ(policy_source.GetSourcePosition(3), std::nullopt);
  EXPECT_EQ(policy_source.GetSourceLocation(3), std::nullopt);
  EXPECT_EQ(
      policy_source.DebugString(),
      "name: #1> test_policy\n    imports:\n     - name: #2> test_import\n");
}

TEST(CelPolicyBuilderTest, AggregateRule) {
  Rule rule;
  EXPECT_FALSE(rule.is_aggregate());
  rule.set_id(1);
  rule.set_rule_id(ValueString(2, "agg_rule"));
  rule.set_is_aggregate(true);
  EXPECT_TRUE(rule.is_aggregate());

  Match match;
  match.set_id(3);
  match.set_result(OutputBlock(ValueString(4, "test_result"), std::nullopt));
  rule.mutable_matches().push_back(match);

  EXPECT_EQ(rule.DebugString(), absl::StrReplaceAll(R"(#1> rule: {
      rule_id: #2> "agg_rule"
      is_aggregate: true
      #3> match: {
        result: {
          output: #4> "test_result"
        }
      }
    })",
                                                    {{"\n    ", "\n"}}));
}

}  // namespace
}  // namespace cel
