// Copyright 2024 Google LLC
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

#include "tools/proto_to_predicate.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "common/ast.h"
#include "common/ast_proto.h"
#include "common/value.h"
#include "env/config.h"
#include "env/env_runtime.h"
#include "env/env_yaml.h"
#include "env/runtime_std_extensions.h"
#include "eval/testutil/test_message.pb.h"
#include "extensions/protobuf/value.h"
#include "internal/status_macros.h"
#include "internal/testing.h"
#include "internal/testing_descriptor_pool.h"
#include "parser/parser.h"
#include "runtime/activation.h"
#include "runtime/runtime.h"
#include "tools/cel_unparser.h"
#include "tools/testdata/test_policy.pb.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/json/json.h"
#include "google/protobuf/message.h"
#include "google/protobuf/text_format.h"

namespace cel::tools {
namespace {

using ::absl_testing::IsOk;
using ::google::api::expr::runtime::TestMessage;

constexpr absl::string_view kEnvYaml = R"(
name: "test"
extensions:
  - name: "bindings"
  - name: "optional"
variables:
  - name: "input"
    type: "google.api.expr.runtime.TestMessage"
)";

TestMessage ParseTestMessage(absl::string_view textproto) {
  TestMessage msg;
  google::protobuf::TextFormat::ParseFromString(textproto, &msg);
  return msg;
}

absl::StatusOr<bool> EvaluatePredicate(const cel::Ast& ast,
                                       const TestMessage& input) {
  auto descriptor_pool = cel::internal::GetSharedTestingDescriptorPool();

  CEL_ASSIGN_OR_RETURN(cel::Config config,
                       cel::EnvConfigFromYaml(std::string(kEnvYaml)));

  cel::EnvRuntime env_runtime;
  env_runtime.SetDescriptorPool(descriptor_pool);
  cel::RegisterStandardExtensions(env_runtime);
  env_runtime.SetConfig(config);

  CEL_ASSIGN_OR_RETURN(std::unique_ptr<cel::Runtime> runtime,
                       env_runtime.NewRuntime());
  CEL_ASSIGN_OR_RETURN(std::unique_ptr<cel::Program> program,
                       runtime->CreateProgram(std::make_unique<cel::Ast>(ast)));

  google::protobuf::Arena arena;
  cel::Activation activation;
  CEL_ASSIGN_OR_RETURN(
      cel::Value val, cel::extensions::ProtoMessageToValue(
                          input, descriptor_pool.get(),
                          google::protobuf::MessageFactory::generated_factory(), &arena));
  activation.InsertOrAssignValue("input", val);

  CEL_ASSIGN_OR_RETURN(cel::Value result,
                       program->Evaluate(&arena, activation));
  if (!result.IsBool()) {
    return absl::InvalidArgumentError(
        "Predicate evaluate result must be a boolean value.");
  }
  return result.GetBool();
}

struct TestCase {
  std::string name;
  std::vector<std::string> input_textprotos;
  std::string expected_unparsed;
  std::string eval_textproto;
  bool expected_eval_result = true;
  // If true, skip the eval step of the test. This is useful for tests where
  // the expected expression does not share the same type structure as the
  // input proto, such as empty messages.
  bool skip_eval = false;
};

class ProtoToPredicateTest : public ::testing::TestWithParam<TestCase> {};

TEST_P(ProtoToPredicateTest, ConformanceTests) {
  const TestCase& param = GetParam();

  std::vector<TestMessage> input_messages;
  input_messages.reserve(param.input_textprotos.size());
  for (const auto& proto_str : param.input_textprotos) {
    input_messages.push_back(ParseTestMessage(proto_str));
  }

  std::vector<const ::google::protobuf::Message*> ptr_messages;
  ptr_messages.reserve(input_messages.size());
  for (const auto& msg : input_messages) {
    ptr_messages.push_back(&msg);
  }

  absl::StatusOr<cel::Ast> ast_or;
  if (input_messages.size() == 1) {
    ast_or = ProtoToPredicateAst("input", input_messages[0]);
  } else {
    ast_or = ProtoToPredicateAst("input", absl::MakeSpan(ptr_messages));
  }

  ASSERT_THAT(ast_or, IsOk());
  cel::Ast ast = std::move(*ast_or);

  cel::expr::ParsedExpr parsed_expr;
  ASSERT_THAT(cel::AstToParsedExpr(ast, &parsed_expr), IsOk());
  ASSERT_OK_AND_ASSIGN(auto unparsed, google::api::expr::Unparse(parsed_expr));

  EXPECT_EQ(unparsed, param.expected_unparsed);

  if (!param.skip_eval) {
    TestMessage eval_msg = ParseTestMessage(param.eval_textproto);
    ASSERT_OK_AND_ASSIGN(bool eval_result, EvaluatePredicate(ast, eval_msg));
    EXPECT_EQ(eval_result, param.expected_eval_result);
  }
}

INSTANTIATE_TEST_SUITE_P(
    ProtoToPredicateSubCases, ProtoToPredicateTest,
    testing::Values(
        TestCase{
            .name = "EmptyMessageTest",
            .input_textprotos = {""},
            .expected_unparsed = "true",
            .eval_textproto = "",
        },
        TestCase{
            .name = "EmptyMessagesListTest",
            .input_textprotos = {},
            .expected_unparsed = "true",
            .eval_textproto = "",
        },
        TestCase{
            .name = "PrimitivesTest",
            .input_textprotos = {R"pb(
                                   int32_value: 42 string_value: "hello"
                                 )pb"},
            .expected_unparsed =
                "input.int32_value == 42 && input.string_value == \"hello\"",
            .eval_textproto = R"pb(
              int32_value: 42 string_value: "hello"
            )pb",
        },
        TestCase{
            .name = "AllPrimitivesTest",
            .input_textprotos = {R"pb(
                                   int32_value: 42
                                   int64_value: 43
                                   uint32_value: 44
                                   uint64_value: 45
                                   float_value: 46.5
                                   double_value: 47.5
                                   bool_value: true
                                   enum_value: TEST_ENUM_1
                                   string_value: "hello"
                                   bytes_value: "world"
                                 )pb"},
            .expected_unparsed =
                "input.int32_value == 42 && input.int64_value == 43 && "
                "input.uint32_value == 44u && input.uint64_value == 45u && "
                "input.float_value == 46.5 && input.double_value == 47.5 && "
                "input.string_value == \"hello\" && "
                "input.bytes_value == b\"world\" && "
                "input.bool_value == true && "
                "input.enum_value == 1",
            .eval_textproto = R"pb(
              int32_value: 42
              int64_value: 43
              uint32_value: 44
              uint64_value: 45
              float_value: 46.5
              double_value: 47.5
              bool_value: true
              enum_value: TEST_ENUM_1
              string_value: "hello"
              bytes_value: "world"
            )pb",
        },
        TestCase{
            .name = "NestedMessageTest",
            .input_textprotos = {R"pb(
                                   message_value: { int32_value: 42 }
                                 )pb"},
            .expected_unparsed = "input.message_value.int32_value == 42",
            .eval_textproto = R"pb(
              message_value: { int32_value: 42 }
            )pb",
        },
        TestCase{
            .name = "RepeatedFieldTest",
            .input_textprotos = {R"pb(
                                   int32_list: [ 1, 2 ]
                                 )pb"},
            .expected_unparsed =
                "1 in input.int32_list && 2 in input.int32_list",
            .eval_textproto = R"pb(
              int32_list: [ 1, 2 ]
            )pb",
        },
        TestCase{
            .name = "RepeatedFieldSingleElementTest",
            .input_textprotos = {R"pb(
                                   int32_list: [ 42 ]
                                 )pb"},
            .expected_unparsed = "42 in input.int32_list",
            .eval_textproto = R"pb(
              int32_list: [ 42 ]
            )pb",
        },
        TestCase{
            .name = "RepeatedFieldEmptyTest",
            .input_textprotos = {R"pb(
                                   int32_list: []
                                 )pb"},
            .expected_unparsed = "true",
            .eval_textproto = R"pb(
              int32_list: []
            )pb",
        },
        TestCase{
            .name = "ListFieldEvalNegative",
            .input_textprotos = {R"pb(
                                   int32_list: [ 1, 2 ]
                                 )pb"},
            .expected_unparsed =
                "1 in input.int32_list && 2 in input.int32_list",
            .eval_textproto = R"pb(
              int32_list: [ 1, 3 ]
            )pb",
            .expected_eval_result = false,
        },
        TestCase{
            .name = "SingleRepeatedFieldAllPrimitivesTest",
            .input_textprotos = {R"pb(
                                   int32_list: [ 42 ]
                                   int64_list: [ 43 ]
                                   uint32_list: [ 44 ]
                                   uint64_list: [ 45 ]
                                   float_list: [ 46.5 ]
                                   double_list: [ 47.5 ]
                                   bool_list: [ true ]
                                   enum_list: [ TEST_ENUM_1 ]
                                   string_list: [ "hello" ]
                                   bytes_list: [ "world" ]
                                 )pb"},
            .expected_unparsed = "42 in input.int32_list && "
                                 "43 in input.int64_list && "
                                 "44u in input.uint32_list && "
                                 "45u in input.uint64_list && "
                                 "46.5 in input.float_list && "
                                 "47.5 in input.double_list && "
                                 "\"hello\" in input.string_list && "
                                 "b\"world\" in input.bytes_list && "
                                 "true in input.bool_list && "
                                 "1 in input.enum_list",
            .eval_textproto = R"pb(
              int32_list: [ 42 ]
              int64_list: [ 43 ]
              uint32_list: [ 44 ]
              uint64_list: [ 45 ]
              float_list: [ 46.5 ]
              double_list: [ 47.5 ]
              bool_list: [ true ]
              enum_list: [ TEST_ENUM_1 ]
              string_list: [ "hello" ]
              bytes_list: [ "world" ]
            )pb",
        },
        TestCase{
            .name = "MultipleRepeatedFieldAllPrimitivesTest",
            .input_textprotos = {R"pb(
                                   int32_list: [ 42, 142 ]
                                   int64_list: [ 43, 143 ]
                                   uint32_list: [ 44, 144 ]
                                   uint64_list: [ 45, 145 ]
                                   float_list: [ 46.5, 146.5 ]
                                   double_list: [ 47.5, 147.5 ]
                                   bool_list: [ true, false ]
                                   enum_list: [ TEST_ENUM_1, TEST_ENUM_2 ]
                                   string_list: [ "hello", "universe" ]
                                   bytes_list: [ "world", "space" ]
                                 )pb"},
            .expected_unparsed =
                "42 in input.int32_list && 142 in input.int32_list && "
                "43 in input.int64_list && 143 in input.int64_list && "
                "44u in input.uint32_list && 144u in input.uint32_list && "
                "45u in input.uint64_list && 145u in input.uint64_list && "
                "46.5 in input.float_list && 146.5 in input.float_list && "
                "47.5 in input.double_list && 147.5 in input.double_list && "
                "\"hello\" in input.string_list && \"universe\" in "
                "input.string_list && "
                "b\"world\" in input.bytes_list && b\"space\" in "
                "input.bytes_list && "
                "true in input.bool_list && false in input.bool_list && "
                "1 in input.enum_list && 2 in input.enum_list",
            .eval_textproto = R"pb(
              int32_list: [ 42, 142 ]
              int64_list: [ 43, 143 ]
              uint32_list: [ 44, 144 ]
              uint64_list: [ 45, 145 ]
              float_list: [ 46.5, 146.5 ]
              double_list: [ 47.5, 147.5 ]
              bool_list: [ true, false ]
              enum_list: [ TEST_ENUM_1, TEST_ENUM_2 ]
              string_list: [ "hello", "universe" ]
              bytes_list: [ "world", "space" ]
            )pb",
        },
        TestCase{
            .name = "MapFieldTest",
            .input_textprotos = {R"pb(
                                   string_int32_map: { key: "foo" value: 1 }
                                   string_int32_map: { key: "bar" value: 2 }
                                 )pb"},
            .expected_unparsed = "\"bar\" in input.string_int32_map && "
                                 "input.string_int32_map[\"bar\"] == 2 && "
                                 "\"foo\" in input.string_int32_map && "
                                 "input.string_int32_map[\"foo\"] == 1",
            .eval_textproto = R"pb(
              string_int32_map: { key: "foo" value: 1 }
              string_int32_map: { key: "bar" value: 2 }
            )pb",
        },
        TestCase{
            .name = "MapFieldEvalNegativeVal",
            .input_textprotos = {R"pb(
                                   string_int32_map: { key: "foo" value: 1 }
                                   string_int32_map: { key: "bar" value: 2 }
                                 )pb"},
            .expected_unparsed = "\"bar\" in input.string_int32_map && "
                                 "input.string_int32_map[\"bar\"] == 2 && "
                                 "\"foo\" in input.string_int32_map && "
                                 "input.string_int32_map[\"foo\"] == 1",
            .eval_textproto = R"pb(
              string_int32_map: { key: "foo" value: 1 }
              string_int32_map: { key: "bar" value: 3 }
            )pb",
            .expected_eval_result = false,
        },
        TestCase{
            .name = "MapFieldEvalNegativeNoKey",
            .input_textprotos = {R"pb(
                                   string_int32_map: { key: "foo" value: 1 }
                                   string_int32_map: { key: "bar" value: 2 }
                                 )pb"},
            .expected_unparsed = "\"bar\" in input.string_int32_map && "
                                 "input.string_int32_map[\"bar\"] == 2 && "
                                 "\"foo\" in input.string_int32_map && "
                                 "input.string_int32_map[\"foo\"] == 1",
            .eval_textproto = R"pb(
              string_int32_map: { key: "foo" value: 1 }
            )pb",
            .expected_eval_result = false,
        },
        TestCase{
            .name = "MapFieldIntKeySortingTest",
            .input_textprotos = {R"pb(
                                   int32_int32_map: { key: 10 value: 100 }
                                   int32_int32_map: { key: 5 value: 50 }
                                   int32_int32_map: { key: 8 value: 80 }
                                 )pb"},
            .expected_unparsed = "5 in input.int32_int32_map && "
                                 "input.int32_int32_map[5] == 50 && "
                                 "8 in input.int32_int32_map && "
                                 "input.int32_int32_map[8] == 80 && "
                                 "10 in input.int32_int32_map && "
                                 "input.int32_int32_map[10] == 100",
            .eval_textproto = R"pb(
              int32_int32_map: { key: 5 value: 50 }
              int32_int32_map: { key: 8 value: 80 }
              int32_int32_map: { key: 10 value: 100 }
            )pb",
        },
        TestCase{
            .name = "MultipleMessagesTest",
            .input_textprotos = {R"pb(
                                   int32_value: 42
                                 )pb",
                                 R"pb(
                                   int32_value: 41 string_value: "hello"
                                 )pb"},
            .expected_unparsed =
                "input.int32_value == 42 || input.int32_value == 41 && "
                "input.string_value == \"hello\"",
            .eval_textproto = R"pb(
              int32_value: 41 string_value: "hello"
            )pb",
        },
        TestCase{
            .name = "RepeatedMessageFieldTest",
            .input_textprotos = {R"pb(
                                   message_list:
                                   [ { int32_value: 42 }
                                     , { int32_value: 43 }]
                                 )pb"},
            .expected_unparsed = "input.message_list.int32_value == 42 || "
                                 "input.message_list.int32_value == 43",
            .skip_eval = true,
        },
        TestCase{
            .name = "RepeatedMessageSingleElementTest",
            .input_textprotos = {R"pb(
                                   message_list:
                                   [ { int32_value: 42 }]
                                 )pb"},
            .expected_unparsed = "input.message_list.int32_value == 42",
            .skip_eval = true,
        }));

struct PolicyTestCase {
  std::string name;
  std::string json_input;
  std::string expected_unparsed;
};

class PolicyJsonTest : public ::testing::TestWithParam<PolicyTestCase> {};

TEST_P(PolicyJsonTest, Conformance) {
  const PolicyTestCase& param = GetParam();

  cel::cpp::tools::Policy policy;
  google::protobuf::json::ParseOptions options;
  options.ignore_unknown_fields = true;
  auto status =
      google::protobuf::json::JsonStringToMessage(param.json_input, &policy, options);
  ASSERT_THAT(status, IsOk()) << "Failed to parse JSON: " << param.json_input;

  absl::StatusOr<cel::Ast> ast_or;
  std::vector<const ::google::protobuf::Message*> ptr_messages;
  ptr_messages.reserve(policy.destinations_size());
  for (const auto& dest : policy.destinations()) {
    ptr_messages.push_back(&dest);
  }

  if (ptr_messages.empty()) {
    auto parsed_expr_or = google::api::expr::parser::Parse("false");
    ASSERT_THAT(parsed_expr_or, IsOk());
    auto ast_ptr_or = cel::CreateAstFromParsedExpr(*parsed_expr_or);
    ASSERT_THAT(ast_ptr_or, IsOk());
    ast_or = std::move(**ast_ptr_or);
  } else if (ptr_messages.size() == 1) {
    ast_or = ProtoToPredicateAst("dest", *ptr_messages[0]);
  } else {
    ast_or = ProtoToPredicateAst("dest", absl::MakeSpan(ptr_messages));
  }

  ASSERT_THAT(ast_or, IsOk());
  cel::Ast ast = std::move(*ast_or);

  cel::expr::ParsedExpr parsed_expr;
  ASSERT_THAT(cel::AstToParsedExpr(ast, &parsed_expr), IsOk());
  ASSERT_OK_AND_ASSIGN(auto unparsed, google::api::expr::Unparse(parsed_expr));

  EXPECT_EQ(unparsed, param.expected_unparsed);
}

INSTANTIATE_TEST_SUITE_P(
    PolicyJsonSubCases, PolicyJsonTest,
    testing::Values(
        PolicyTestCase{
            .name = "SimpleMatch",
            .json_input =
                R"({ "destinations": [ { "agent": { "id": "agent-007" } } ] })",
            .expected_unparsed = "dest.agent.name == \"agent-007\"",
        },
        PolicyTestCase{
            .name = "MultipleFields",
            .json_input =
                R"({ "destinations": [ {
                     "tool": {
                       "name": "admin_tool",
                       "annotations": {
                         "read_only_hint": false
                        }
                      }
                    }
                  ] })",
            .expected_unparsed =
                "dest.tool.name == \"admin_tool\" && "
                "dest.tool.annotations.read_only_hint == false",
        },
        PolicyTestCase{
            .name = "RepeatedMessages",
            .json_input =
                R"({ "destinations": [
                  { "agent": { "id": "worker-1" } },
                  { "agent": { "id": "worker-2" } },
                ] })",
            .expected_unparsed = "dest.agent.name == \"worker-1\" || "
                                 "dest.agent.name == \"worker-2\"",
        },
        PolicyTestCase{
            .name = "RepeatedPrimitiveArraySingleElement",
            .json_input =
                R"({ "destinations": [ {
                     "tool": {
                       "role_members": {
                         "admin": {
                           "principals": ["alice"]
                         }
                       }
                     }
                   } ] })",
            .expected_unparsed =
                "\"admin\" in dest.tool.role_members && "
                "\"alice\" in dest.tool.role_members[\"admin\"].principals",
        },
        PolicyTestCase{
            .name = "RepeatedArrayEmpty",
            .json_input = R"({ "destinations": [ { "tool": { } } ] })",
            .expected_unparsed = "true",
        },
        PolicyTestCase{
            .name = "AnnotatedSingularFieldInMessage",
            .json_input =
                R"({ "destinations": [ {
                     "agent": {
                       "id": "agent-007",
                       "location": "us-central1"
                     }
                   } ] })",
            .expected_unparsed = "dest.agent.name == \"agent-007\" && "
                                 "custom.agent.location == \"us-central1\"",
        },
        PolicyTestCase{
            .name = "AnnotatedOneofPrimitiveField",
            .json_input =
                R"({ "destinations": [ {
                     "ip": "192.168.1.1"
                   } ] })",
            .expected_unparsed = "custom.ip == \"192.168.1.1\"",
        },
        PolicyTestCase{
            .name = "AnnotatedMapFieldPrimitive",
            .json_input =
                R"({ "destinations": [ {
                     "tool": {
                       "annotated_labels": {
                         "cluster": "us-central1"
                       }
                     }
                   } ] })",
            .expected_unparsed =
                "\"cluster\" in custom.labels && "
                "custom.labels[\"cluster\"] == \"us-central1\"",
        },
        PolicyTestCase{
            .name = "AnnotatedMapFieldMessage",
            .json_input =
                R"({ "destinations": [ {
                     "tool": {
                       "annotated_role_members": {
                         "admin": {
                           "all_users": true,
                           "principals": ["alice_user"],
                           "leader": "alice",
                           "leaders": ["bob"]
                         }
                       }
                     }
                   } ] })",
            .expected_unparsed =
                "\"admin\" in custom.role_members && "
                "\"alice_user\" in custom.role_members[\"admin\"].principals "
                "&& "
                "custom.role_members[\"admin\"].all_users == true && "
                "custom.member.leader == \"alice\" && "
                "custom.member.leaders in [\"bob\"]",
        },
        PolicyTestCase{
            .name = "AnnotatedMessageField",
            .json_input =
                R"({ "destinations": [ {
                     "tool": {
                       "annotated_annotations": {
                         "read_only_hint": true
                       }
                     }
                   } ] })",
            .expected_unparsed =
                "custom.tool_annotations.read_only_hint == true",
        },
        PolicyTestCase{
            .name = "AnnotatedRepeatedMessageFieldSingle",
            .json_input =
                R"({ "destinations": [ {
                     "tool": {
                       "backup_agents": [
                         { "id": "agent-007" }
                       ]
                     }
                   } ] })",
            .expected_unparsed = "custom.backup_agents.name == \"agent-007\"",
        },
        PolicyTestCase{
            .name = "AnnotatedRepeatedMessageFieldMultiple",
            .json_input =
                R"({ "destinations": [ {
                     "tool": {
                       "backup_agents": [
                         { "id": "agent-007" },
                         { "id": "agent-008" }
                       ]
                     }
                   } ] })",
            .expected_unparsed =
                "custom.backup_agents.name == \"agent-007\" || "
                "custom.backup_agents.name == \"agent-008\"",
        },
        PolicyTestCase{
            .name = "MapEquality",
            .json_input =
                R"({ "destinations": [
                  { "tool": {
                    "name": "shell",
                    "labels": {
                      "cluster": "us-central1",
                      "project": "dev"
                    }
                  }
                } ] })",
            .expected_unparsed =
                "dest.tool.name == \"shell\" && \"cluster\" in "
                "dest.tool.labels && dest.tool.labels[\"cluster\"] == "
                "\"us-central1\" && \"project\" in dest.tool.labels && "
                "dest.tool.labels[\"project\"] == \"dev\"",
        },
        PolicyTestCase{
            .name = "NestedMapEquality",
            .json_input =
                R"({ "destinations": [
                  { "tool": {
                    "role_members": {
                      "admin": {
                        "all_users": true
                      }
                    }
                  } }
                ] })",
            .expected_unparsed =
                "\"admin\" in dest.tool.role_members && "
                "dest.tool.role_members[\"admin\"].all_users == true",
        },
        PolicyTestCase{
            .name = "EmptyPolicy",
            .json_input = "{}",
            .expected_unparsed = "false",
        }));

}  // namespace
}  // namespace cel::tools
