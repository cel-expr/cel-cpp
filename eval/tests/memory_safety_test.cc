// Copyright 2022 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//       https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Tests for memory safety using the CEL Evaluator.
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "cel/expr/syntax.pb.h"
#include "google/rpc/context/attribute_context.pb.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "common/value.h"
#include "common/value_testing.h"
#include "extensions/protobuf/runtime_adapter.h"
#include "extensions/protobuf/value_testing.h"
#include "internal/status_macros.h"
#include "internal/testing.h"
#include "parser/parser.h"
#include "runtime/activation.h"
#include "runtime/comprehension_vulnerability_check.h"
#include "runtime/constant_folding.h"
#include "runtime/function_adapter.h"
#include "runtime/reference_resolver.h"
#include "runtime/regex_precompilation.h"
#include "runtime/runtime.h"
#include "runtime/runtime_builder.h"
#include "runtime/runtime_options.h"
#include "runtime/standard_runtime_builder_factory.h"
#include "testutil/util.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"

namespace google::api::expr::runtime {
namespace {

using ::absl_testing::IsOkAndHolds;
using ::cel::Activation;
using ::cel::BoolValue;
using ::cel::CreateStandardRuntimeBuilder;
using ::cel::EnableComprehensionVulnerabiltyCheck;
using ::cel::EnableReferenceResolver;
using ::cel::ReferenceResolverEnabled;
using ::cel::Runtime;
using ::cel::RuntimeOptions;
using ::cel::StringValue;
using ::cel::UnaryFunctionAdapter;
using ::cel::Value;
using ::cel::extensions::EnableConstantFolding;
using ::cel::extensions::EnableRegexPrecompilation;
using ::cel::extensions::ProtobufRuntimeAdapter;
using ::cel::extensions::test::StructValueAsProto;
using ::cel::test::BoolValueIs;
using ::cel::test::StringValueIs;
using ::cel::test::ValueMatcher;
using ::cel::expr::ParsedExpr;
using ::google::rpc::context::AttributeContext;
using testutil::EqualsProto;

struct TestCase {
  std::string name;
  std::string expression;
  absl::flat_hash_map<absl::string_view, Value> activation;
  ValueMatcher expected_matcher;
  bool reference_resolver_enabled = false;
};

enum Options {
  kDefault,
  kExhaustive,
  kFoldConstants,
  kFoldConstantsManagedArena
};

using ParamType = std::tuple<TestCase, Options>;

std::string TestCaseName(const testing::TestParamInfo<ParamType>& param_info) {
  const ParamType& param = param_info.param;
  absl::string_view opt;
  switch (std::get<1>(param)) {
    case Options::kDefault:
      opt = "default";
      break;
    case Options::kExhaustive:
      opt = "exhaustive";
      break;
    case Options::kFoldConstants:
      opt = "opt";
      break;
    case Options::kFoldConstantsManagedArena:
      opt = "opt_managed_arena";
      break;
  }

  return absl::StrCat(std::get<0>(param).name, "_", opt);
}

bool IsPrivateIpv4Impl(const StringValue& addr) {
  // Implementation for demonstration, this is simple but incomplete and
  // brittle.
  std::string buf;
  absl::string_view view = addr.ToStringView(&buf);
  return absl::StartsWith(view, "192.168.") || absl::StartsWith(view, "10.");
}

class EvaluatorMemorySafetyTest : public testing::TestWithParam<ParamType> {
 public:
  EvaluatorMemorySafetyTest() {
    google::protobuf::LinkMessageReflection<AttributeContext>();
    google::protobuf::LinkMessageReflection<AttributeContext::Request>();
    google::protobuf::LinkMessageReflection<AttributeContext::Peer>();
  }

 protected:
  const TestCase& GetTestCase() { return std::get<0>(GetParam()); }

  absl::StatusOr<std::unique_ptr<Runtime>> CreateRuntime() {
    RuntimeOptions options;
    options.container = "google.rpc.context";

    switch (std::get<1>(GetParam())) {
      case Options::kDefault:
        options.enable_comprehension_list_append = false;
        options.short_circuiting = true;
        break;
      case Options::kExhaustive:
        options.enable_comprehension_list_append = false;
        options.short_circuiting = false;
        break;
      case Options::kFoldConstants:
      case Options::kFoldConstantsManagedArena:
        options.enable_comprehension_list_append = true;
        options.short_circuiting = true;
        break;
    }

    CEL_ASSIGN_OR_RETURN(
        auto builder, CreateStandardRuntimeBuilder(
                          google::protobuf::DescriptorPool::generated_pool(), options));

    if (GetTestCase().reference_resolver_enabled) {
      CEL_RETURN_IF_ERROR(
          EnableReferenceResolver(builder, ReferenceResolverEnabled::kAlways));
    }

    switch (std::get<1>(GetParam())) {
      case Options::kDefault:
      case Options::kExhaustive:
        CEL_RETURN_IF_ERROR(EnableComprehensionVulnerabiltyCheck(builder));
        break;
      case Options::kFoldConstants:
        CEL_RETURN_IF_ERROR(EnableRegexPrecompilation(builder));
        CEL_RETURN_IF_ERROR(EnableConstantFolding(
            builder, &arena_, google::protobuf::MessageFactory::generated_factory()));
        break;
      case Options::kFoldConstantsManagedArena:
        CEL_RETURN_IF_ERROR(EnableRegexPrecompilation(builder));
        CEL_RETURN_IF_ERROR(EnableConstantFolding(
            builder, google::protobuf::MessageFactory::generated_factory()));
        break;
    }

    absl::string_view function_name = "IsPrivate";
    if (GetTestCase().reference_resolver_enabled) {
      function_name = "net.IsPrivate";
    }
    CEL_RETURN_IF_ERROR(
        (UnaryFunctionAdapter<bool, const StringValue&>::RegisterGlobalOverload(
            function_name, &IsPrivateIpv4Impl, builder.function_registry())));

    return std::move(builder).Build();
  }

  google::protobuf::Arena arena_;
};

TEST_P(EvaluatorMemorySafetyTest, Basic) {
  const auto& test_case = GetTestCase();
  ASSERT_OK_AND_ASSIGN(auto runtime, CreateRuntime());

  ASSERT_OK_AND_ASSIGN(ParsedExpr expr, parser::Parse(test_case.expression));

  ASSERT_OK_AND_ASSIGN(auto program,
                       ProtobufRuntimeAdapter::CreateProgram(*runtime, expr));

  Activation activation;
  for (const auto& [key, value] : test_case.activation) {
    activation.InsertOrAssignValue(key, value);
  }

  absl::StatusOr<Value> got = program->Evaluate(&arena_, activation);

  EXPECT_THAT(got, IsOkAndHolds(test_case.expected_matcher));
}

// Check no use after free errors if evaluated after AST is freed.
TEST_P(EvaluatorMemorySafetyTest, NoAstDependency) {
  const auto& test_case = GetTestCase();
  ASSERT_OK_AND_ASSIGN(auto runtime, CreateRuntime());

  auto parsed_expr = parser::Parse(test_case.expression);
  ASSERT_OK(parsed_expr.status());
  auto expr = std::make_unique<ParsedExpr>(std::move(parsed_expr).value());
  ASSERT_OK_AND_ASSIGN(auto program,
                       ProtobufRuntimeAdapter::CreateProgram(*runtime, *expr));

  expr.reset();  // ParsedExpr expr freed

  Activation activation;
  for (const auto& [key, value] : test_case.activation) {
    activation.InsertOrAssignValue(key, value);
  }

  absl::StatusOr<Value> got = program->Evaluate(&arena_, activation);

  EXPECT_THAT(got, IsOkAndHolds(test_case.expected_matcher));
}

// TODO(uncreated-issue/25): make expression plan memory safe after builder is freed.
// TEST_P(EvaluatorMemorySafetyTest, NoBuilderDependency)

INSTANTIATE_TEST_SUITE_P(
    Expression, EvaluatorMemorySafetyTest,
    testing::Combine(
        testing::ValuesIn(std::vector<TestCase>{
            {
                "bool",
                "(true && false) || x || y == 'test_str'",
                {{"x", BoolValue(false)},
                 {"y", StringValue::WrapUnsafe("test_str")}},
                BoolValueIs(true),
            },
            {
                "const_str",
                "condition ? 'left_hand_string' : 'right_hand_string'",
                {{"condition", BoolValue(false)}},
                StringValueIs("right_hand_string"),
            },
            {
                "long_const_string",
                "condition ? 'left_hand_string' : "
                "'long_right_hand_string_0123456789'",
                {{"condition", BoolValue(false)}},
                StringValueIs("long_right_hand_string_0123456789"),
            },
            {
                "computed_string",
                "(condition ? 'a.b' : 'b.c') + '.d.e.f'",
                {{"condition", BoolValue(false)}},
                StringValueIs("b.c.d.e.f"),
            },
            {
                "regex",
                R"('192.168.128.64'.matches(r'^192\.168\.[0-2]?[0-9]?[0-9]\.[0-2]?[0-9]?[0-9]') )",
                {},
                BoolValueIs(true),
            },
            {
                "list_create",
                "[1, 2, 3, 4, 5, 6][3] == 4",
                {},
                BoolValueIs(true),
            },
            {
                "list_create_strings",
                "['1', '2', '3', '4', '5', '6'][2] == '3'",
                {},
                BoolValueIs(true),
            },
            {
                "map_create",
                "{'1': 'one', '2': 'two'}['2']",
                {},
                StringValueIs("two"),
            },
            {
                "struct_create",
                R"(
                  AttributeContext{
                    request: AttributeContext.Request{
                      method: 'GET',
                      path: '/index'
                    },
                    origin: AttributeContext.Peer{
                      ip: '10.0.0.1'
                    }
                  }
                )",
                {},
                StructValueAsProto<AttributeContext>(EqualsProto(R"pb(
                  request { method: "GET" path: "/index" }
                  origin { ip: "10.0.0.1" }
                )pb")),
            },
            {"extension_function",
             "IsPrivate('8.8.8.8')",
             {},
             BoolValueIs(false),
             /*enable_reference_resolver=*/false},
            {"namespaced_function",
             "net.IsPrivate('192.168.0.1')",
             {},
             BoolValueIs(true),
             /*enable_reference_resolver=*/true},
            {
                "comprehension",
                "['abc', 'def', 'ghi', 'jkl'].exists(el, el == 'mno')",
                {},
                BoolValueIs(false),
            },
            {
                "comprehension_complex",
                "['a' + 'b' + 'c', 'd' + 'ef', 'g' + 'hi', 'j' + 'kl']"
                ".exists(el, el.startsWith('g'))",
                {},
                BoolValueIs(true),
            }}),
        testing::Values(Options::kDefault, Options::kExhaustive,
                        Options::kFoldConstants,
                        Options::kFoldConstantsManagedArena)),
    &TestCaseName);

}  // namespace
}  // namespace google::api::expr::runtime
