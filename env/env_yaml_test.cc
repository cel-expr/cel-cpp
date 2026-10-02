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

#include "env/env_yaml.h"

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "common/constant.h"
#include "common/typedef/type_ref.h"
#include "env/config.h"
#include "internal/status_macros.h"
#include "internal/testing.h"

namespace cel {
namespace {

using ::absl_testing::StatusIs;
using ::testing::AllOf;
using ::testing::ElementsAreArray;
using ::testing::Field;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::SizeIs;
using ::testing::UnorderedElementsAre;

TEST(EnvYamlTest, ParseContainerConfig) {
  ASSERT_OK_AND_ASSIGN(Config config, EnvConfigFromYaml(R"yaml(
                    container: "test.container"
                  )yaml"));

  EXPECT_THAT(config.GetContainerConfig(),
              Field(&Config::ContainerConfig::name, "test.container"));
}

TEST(EnvYamlTest, ParseContainerConfig_AlternativeSyntax) {
  ASSERT_OK_AND_ASSIGN(Config config, EnvConfigFromYaml(R"yaml(
                    container:
                      name: test.container
                      abbreviations:
                      - abbr1.Abbr1
                      - abbr2.Abbr2
                      aliases:
                      - alias: alias1
                        qualified_name: qual.name1
                      - alias: alias2
                        qualified_name: qual.name2
                  )yaml"));

  const auto& container_config = config.GetContainerConfig();
  EXPECT_EQ(container_config.name, "test.container");
  EXPECT_THAT(container_config.abbreviations,
              UnorderedElementsAre("abbr1.Abbr1", "abbr2.Abbr2"));
  ASSERT_THAT(container_config.aliases, SizeIs(2));
  EXPECT_EQ(container_config.aliases[0].alias, "alias1");
  EXPECT_EQ(container_config.aliases[0].qualified_name, "qual.name1");
  EXPECT_EQ(container_config.aliases[1].alias, "alias2");
  EXPECT_EQ(container_config.aliases[1].qualified_name, "qual.name2");
}

TEST(EnvYamlTest, ParseExtensionConfigs) {
  ASSERT_OK_AND_ASSIGN(Config config, EnvConfigFromYaml(R"yaml(
                    extensions:
                    - name: "math"
                      version: latest
                    - name: "optional"
                      version: 2
                    - name: "strings"
                  )yaml"));

  EXPECT_THAT(config.GetExtensionConfigs(),
              UnorderedElementsAre(
                  AllOf(Field(&Config::ExtensionConfig::name, "math"),
                        Field(&Config::ExtensionConfig::version,
                              Config::ExtensionConfig::kLatest)),
                  AllOf(Field(&Config::ExtensionConfig::name, "optional"),
                        Field(&Config::ExtensionConfig::version, 2)),
                  AllOf(Field(&Config::ExtensionConfig::name, "strings"),
                        Field(&Config::ExtensionConfig::version,
                              Config::ExtensionConfig::kLatest))));
}

TEST(EnvYamlTest, DefaultExtensionConfigs) {
  ASSERT_OK_AND_ASSIGN(Config config, EnvConfigFromYaml(R"yaml(
                  )yaml"));

  EXPECT_THAT(config.GetExtensionConfigs(), IsEmpty());
}

TEST(EnvYamlTest, ParseStdlibConfig_ExclusionStyle) {
  ASSERT_OK_AND_ASSIGN(Config config, EnvConfigFromYaml(R"yaml(
                    stdlib:
                      disable: true
                      disable_macros: true
                      exclude_macros:
                      - map
                      - filter
                      exclude_functions:
                      - name: "_+_"
                        overloads:
                        - id: add_bytes
                        - id: add_list
                      - name: "matches"
                      - name: "timestamp"
                        overloads:
                        - id: "string_to_timestamp"
                  )yaml"));

  const auto& stdlib_config = config.GetStandardLibraryConfig();
  EXPECT_TRUE(stdlib_config.disable);
  EXPECT_TRUE(stdlib_config.disable_macros);
  EXPECT_THAT(stdlib_config.excluded_macros,
              UnorderedElementsAre("map", "filter"));
  EXPECT_THAT(stdlib_config.included_macros, IsEmpty());
  EXPECT_THAT(
      stdlib_config.excluded_functions,
      UnorderedElementsAre(std::make_pair("_+_", "add_bytes"),
                           std::make_pair("_+_", "add_list"),
                           std::make_pair("matches", ""),
                           std::make_pair("timestamp", "string_to_timestamp")))
      << " Actual stdlib config: " << stdlib_config;
}

TEST(EnvYamlTest, ParseStdlibConfig_InclusionStyle) {
  ASSERT_OK_AND_ASSIGN(Config config, EnvConfigFromYaml(R"yaml(
                    stdlib:
                      include_macros:
                      - map
                      - filter
                      include_functions:
                      - name: "_+_"
                        overloads:
                        - id: add_bytes
                        - id: "_+_(list<~A>,list<~A>)"
                      - name: "matches"
                      - name: "timestamp"
                        overloads:
                        - id: "string_to_timestamp"
                  )yaml"));

  const auto& stdlib_config = config.GetStandardLibraryConfig();
  EXPECT_THAT(stdlib_config.included_macros,
              UnorderedElementsAre("map", "filter"));
  EXPECT_THAT(
      stdlib_config.included_functions,
      UnorderedElementsAre(std::make_pair("_+_", "add_bytes"),
                           std::make_pair("_+_", "_+_(list<~A>,list<~A>)"),
                           std::make_pair("matches", ""),
                           std::make_pair("timestamp", "string_to_timestamp")))
      << " Actual stdlib config: " << stdlib_config;
}

TEST(EnvYamlTest, ParseVariableConfigs) {
  ASSERT_OK_AND_ASSIGN(Config config, EnvConfigFromYaml(R"yaml(
                    variables:
                    - name: "msg"
                      type_name: "google.expr.proto3.test.TestAllTypes"
                      description: >-
                        msg represents all possible type permutation which
                        CEL understands from a proto perspective
                  )yaml"));

  const Config::VariableConfig& variable_config =
      config.GetVariableConfigs()[0];
  EXPECT_EQ(variable_config.name, "msg");
  const TypeRef& type_ref = variable_config.type_info;
  EXPECT_EQ(type_ref.name, "google.expr.proto3.test.TestAllTypes");
  EXPECT_FALSE(type_ref.is_type_param);
  EXPECT_THAT(type_ref.params, IsEmpty());
  EXPECT_EQ(variable_config.description,
            "msg represents all possible type permutation which CEL "
            "understands from a proto perspective");
}

TEST(EnvYamlTest, ParseVariableConfigWithTypeParams) {
  ASSERT_OK_AND_ASSIGN(Config config, EnvConfigFromYaml(R"yaml(
                    variables:
                    - name: "dict"
                      type: "map<string, ~A>"
                  )yaml"));

  const Config::VariableConfig& variable_config =
      config.GetVariableConfigs()[0];
  EXPECT_EQ(variable_config.name, "dict");
  const TypeRef& type_ref = variable_config.type_info;
  EXPECT_EQ(type_ref.name, "map");
  EXPECT_FALSE(type_ref.is_type_param);
  EXPECT_THAT(type_ref.params, SizeIs(2));
  EXPECT_EQ(type_ref.params[0].name, "string");
  EXPECT_FALSE(type_ref.params[0].is_type_param);
  EXPECT_THAT(type_ref.params[0].params, IsEmpty());
  EXPECT_EQ(type_ref.params[1].name, "A");
  EXPECT_TRUE(type_ref.params[1].is_type_param);
  EXPECT_THAT(type_ref.params[1].params, IsEmpty());
}

TEST(EnvYamlTest, ParseContextVariableConfig) {
  ASSERT_OK_AND_ASSIGN(Config config, EnvConfigFromYaml(R"yaml(
                    context_variable:
                      type_name: "cel.expr.conformance.proto3.TestAllTypes"
                  )yaml"));

  EXPECT_EQ(config.GetContextType(),
            "cel.expr.conformance.proto3.TestAllTypes");
}

TEST(EnvYamlTest, ParseContextVariableConfigAlternativeSyntax) {
  ASSERT_OK_AND_ASSIGN(Config config, EnvConfigFromYaml(R"yaml(
                    context_variable:
                      type: "cel.expr.conformance.proto3.TestAllTypes"
                  )yaml"));

  EXPECT_EQ(config.GetContextType(),
            "cel.expr.conformance.proto3.TestAllTypes");
}

TEST(EnvYamlTest, ParseContextVariableMalformedContextVariable) {
  EXPECT_THAT(EnvConfigFromYaml(R"yaml(
                    context_variable: 123

                  )yaml"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Node 'context_variable' is not a map")));
}

TEST(EnvYamlTest, ParseContextVariableMalformedContextVariable2) {
  EXPECT_THAT(
      EnvConfigFromYaml(R"yaml(
                    context_variable:
                      type:
                        foo: bar
                  )yaml"),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr("Node 'context_variable' does not have a valid type")));
}

TEST(EnvYamlTest, ParseVariableConfigWithTypeParamsLegacySyntax) {
  ASSERT_OK_AND_ASSIGN(Config config, EnvConfigFromYaml(R"yaml(
                    variables:
                    - name: "dict"
                      type_name: "map"
                      params:
                      - type_name: "string"
                      - type_name: "A"
                        is_type_param: true
                  )yaml"));

  const Config::VariableConfig& variable_config =
      config.GetVariableConfigs()[0];
  EXPECT_EQ(variable_config.name, "dict");
  const TypeRef& type_ref = variable_config.type_info;
  EXPECT_EQ(type_ref.name, "map");
  EXPECT_FALSE(type_ref.is_type_param);
  EXPECT_THAT(type_ref.params, SizeIs(2));
  EXPECT_EQ(type_ref.params[0].name, "string");
  EXPECT_FALSE(type_ref.params[0].is_type_param);
  EXPECT_THAT(type_ref.params[0].params, IsEmpty());
  EXPECT_EQ(type_ref.params[1].name, "A");
  EXPECT_TRUE(type_ref.params[1].is_type_param);
  EXPECT_THAT(type_ref.params[1].params, IsEmpty());
}

TEST(EnvYamlTest, ParseVariableConfigWithNestedRuleOldFormat) {
  ASSERT_OK_AND_ASSIGN(Config config, EnvConfigFromYaml(R"yaml(
                    variables:
                    - name: "x"
                      type:
                        type_name: "int"
                  )yaml"));

  ASSERT_THAT(config.GetVariableConfigs(), SizeIs(1));
  const Config::VariableConfig& variable_config =
      config.GetVariableConfigs()[0];
  EXPECT_EQ(variable_config.name, "x");
  const TypeRef& type_ref = variable_config.type_info;
  EXPECT_EQ(type_ref.name, "int");
  EXPECT_FALSE(type_ref.is_type_param);
  EXPECT_THAT(type_ref.params, IsEmpty());
}

struct ParseConstantTestCase {
  std::string type;
  std::string value;
  std::string expected_error;  // Empty if no error.
  Constant expected_constant;
};

class EnvYamlParseConstantTest
    : public testing::TestWithParam<ParseConstantTestCase> {};

TEST_P(EnvYamlParseConstantTest, EnvYamlParseConstant) {
  const ParseConstantTestCase& param = GetParam();
  const std::string yaml = absl::StrFormat(
      R"yaml(
        variables:
        - name: "const"
          type: "%s"
          value: %s
      )yaml",
      param.type, param.value);
  absl::StatusOr<Config> status_or_config = EnvConfigFromYaml(yaml);
  if (!param.expected_error.empty()) {
    EXPECT_THAT(status_or_config, StatusIs(absl::StatusCode::kInvalidArgument,
                                           HasSubstr(param.expected_error)));
    return;
  }
  ASSERT_OK_AND_ASSIGN(Config config, status_or_config);

  const Config::VariableConfig& variable_config =
      config.GetVariableConfigs()[0];
  EXPECT_EQ(variable_config.name, "const");
  EXPECT_EQ(variable_config.type_info.name, param.type) << " yaml: " << yaml;
  EXPECT_EQ(variable_config.value, param.expected_constant)
      << " yaml: " << yaml;
}

std::vector<ParseConstantTestCase> GetParseConstantTestCases() {
  return {
      ParseConstantTestCase{
          .type = "null",
          .value = "\"\"",
          .expected_constant = Constant(nullptr),
      },
      ParseConstantTestCase{
          .type = "null",
          .value = "anything",
          .expected_error = "Failed to parse null constant",
      },
      ParseConstantTestCase{
          .type = "bool",
          .value = "TRUE",
          .expected_constant = Constant(true),
      },
      ParseConstantTestCase{
          .type = "bool",
          .value = "false",
          .expected_constant = Constant(false),
      },
      ParseConstantTestCase{
          .type = "bool",
          .value = "yes",
          .expected_error = "Failed to parse bool constant",
      },
      ParseConstantTestCase{
          .type = "int",
          .value = "42",
          .expected_constant = Constant(int64_t{42}),
      },
      ParseConstantTestCase{
          .type = "int",
          .value = "41.999",
          .expected_error = "Failed to parse int constant",
      },
      ParseConstantTestCase{
          .type = "uint",
          .value = "42",
          .expected_constant = Constant(uint64_t{42}),
      },
      ParseConstantTestCase{
          .type = "uint",
          .value = "42u",
          .expected_constant = Constant(uint64_t{42}),
      },
      ParseConstantTestCase{
          .type = "uint",
          .value = "-1",
          .expected_error = "Failed to parse uint constant",
      },
      ParseConstantTestCase{
          .type = "double",
          .value = "42.42",
          .expected_constant = Constant(42.42),
      },
      ParseConstantTestCase{
          .type = "double",
          .value = "abc",
          .expected_error = "Failed to parse double constant",
      },
      ParseConstantTestCase{
          .type = "bytes",
          .value = "abc",
          .expected_constant = Constant(BytesConstant("abc")),
      },
      ParseConstantTestCase{
          .type = "bytes",
          .value = "b\"\\xFF\\x00\\x01\"",
          .expected_constant =
              Constant(BytesConstant(absl::string_view("\xff\x00\x01", 3))),
      },
      ParseConstantTestCase{
          .type = "bytes",
          .value = "!!binary /wAB",
          .expected_constant =
              Constant(BytesConstant(absl::string_view("\xff\x00\x01", 3))),
      },
      ParseConstantTestCase{
          .type = "bytes",
          .value = "!!binary YWJj=",
          .expected_error = "Node 'YWJj=' is not a valid Base64 encoded binary",
      },
      ParseConstantTestCase{
          .type = "bytes",
          .value = "abc",
          .expected_constant = Constant(BytesConstant("abc")),
      },
      ParseConstantTestCase{
          .type = "string",
          .value = "abc",
          .expected_constant = Constant(StringConstant("abc")),
      },
      ParseConstantTestCase{
          .type = "string",
          .value = "\"\\\"abc\\\"\"",
          .expected_constant = Constant(StringConstant("\"abc\"")),
      },
      ParseConstantTestCase{
          .type = "duration",
          .value = "1s",
          .expected_constant = Constant(absl::Seconds(1)),
      },
      ParseConstantTestCase{
          .type = "duration",
          .value = "abc",
          .expected_error = "Failed to parse duration constant",
      },
      ParseConstantTestCase{
          .type = "timestamp",
          .value = "2023-01-01T00:00:00Z",
          .expected_constant = Constant(absl::FromUnixSeconds(1672531200)),
      },
      ParseConstantTestCase{
          .type = "timestamp",
          .value = "abc",
          .expected_error = "Failed to parse timestamp constant",
      },
  };
}

INSTANTIATE_TEST_SUITE_P(EnvYamlParseConstantTest, EnvYamlParseConstantTest,
                         ::testing::ValuesIn(GetParseConstantTestCases()));

struct ParseFunctionTestCase {
  std::string yaml;
  Config::FunctionConfig expected_function_config;
};

class EnvYamlParseFunctionTest
    : public testing::TestWithParam<ParseFunctionTestCase> {};

void ExpectTypeRefEqual(const TypeRef& actual, const TypeRef& expected) {
  EXPECT_EQ(actual.name, expected.name);
  EXPECT_EQ(actual.is_type_param, expected.is_type_param);
  ASSERT_THAT(actual.params, SizeIs(expected.params.size()));
  for (size_t i = 0; i < expected.params.size(); ++i) {
    ExpectTypeRefEqual(actual.params[i], expected.params[i]);
  }
}

TEST_P(EnvYamlParseFunctionTest, EnvYamlParseFunction) {
  const ParseFunctionTestCase& param = GetParam();
  ASSERT_OK_AND_ASSIGN(Config config, EnvConfigFromYaml(param.yaml));

  ASSERT_THAT(config.GetFunctionConfigs(), SizeIs(1));
  const Config::FunctionConfig& function_config =
      config.GetFunctionConfigs()[0];
  const Config::FunctionConfig& expected = param.expected_function_config;

  EXPECT_EQ(function_config.name, expected.name);
  EXPECT_EQ(function_config.description, expected.description);

  ASSERT_THAT(function_config.overload_configs,
              SizeIs(expected.overload_configs.size()));

  for (size_t i = 0; i < expected.overload_configs.size(); ++i) {
    const auto& actual_overload = function_config.overload_configs[i];
    const auto& expected_overload = expected.overload_configs[i];

    EXPECT_EQ(actual_overload.overload_id, expected_overload.overload_id);
    EXPECT_THAT(actual_overload.examples,
                ElementsAreArray(expected_overload.examples));
    EXPECT_EQ(actual_overload.is_member_function,
              expected_overload.is_member_function);

    ASSERT_THAT(actual_overload.parameters,
                SizeIs(expected_overload.parameters.size()));
    for (size_t j = 0; j < expected_overload.parameters.size(); ++j) {
      ExpectTypeRefEqual(actual_overload.parameters[j],
                         expected_overload.parameters[j]);
    }

    ExpectTypeRefEqual(actual_overload.return_type,
                       expected_overload.return_type);
  }
}

std::vector<ParseFunctionTestCase> GetParseFunctionTestCases() {
  return {
      ParseFunctionTestCase{
          .yaml = R"yaml(
            functions:
            - name: "isEmpty"
              description: |-
                determines whether a list is empty,
                or a string has no characters
              overloads:
                - signature: "google.protobuf.StringValue.isEmpty()"
                  examples:
                    - "''.isEmpty() // true"
                  return: "bool"
                - signature: "list<~T>.isEmpty()"
                  examples:
                    - "[].isEmpty() // true"
                    - "[1].isEmpty() // false"
                  return: "bool"
          )yaml",
          .expected_function_config =
              {
                  .name = "isEmpty",
                  .description = "determines whether a list is empty,\nor a "
                                 "string has no characters",
                  .overload_configs =
                      {
                          Config::FunctionOverloadConfig{
                              .overload_id =
                                  "google.protobuf.StringValue.isEmpty()",
                              .examples = {"''.isEmpty() // true"},
                              .is_member_function = true,
                              .parameters = {{.name = "string_wrapper"}},
                              .return_type = {.name = "bool"},
                          },
                          Config::FunctionOverloadConfig{
                              .overload_id = "list<~T>.isEmpty()",
                              .examples = {"[].isEmpty() // true",
                                           "[1].isEmpty() // false"},
                              .is_member_function = true,
                              .parameters = {{.name = "list",
                                              .params = {{.name = "T",
                                                          .is_type_param =
                                                              true}}}},
                              .return_type = {.name = "bool"},
                          },
                      },
              },
      },
      ParseFunctionTestCase{
          .yaml = R"yaml(
            functions:
            - name: "isEmpty"
              description: |-
                determines whether a list is empty,
                or a string has no characters
              overloads:
                - id: "wrapper_string_isEmpty"
                  examples:
                    - "''.isEmpty() // true"
                  target:
                    type_name: "google.protobuf.StringValue"
                  return:
                    type_name: "bool"
                - id: "list_isEmpty"
                  examples:
                    - "[].isEmpty() // true"
                    - "[1].isEmpty() // false"
                  target:
                    type_name: "list"
                    params:
                      - type_name: "T"
                        is_type_param: true
                  return:
                    type_name: "bool"
          )yaml",
          .expected_function_config =
              {
                  .name = "isEmpty",
                  .description = "determines whether a list is empty,\nor a "
                                 "string has no characters",
                  .overload_configs =
                      {
                          Config::FunctionOverloadConfig{
                              .overload_id = "wrapper_string_isEmpty",
                              .examples = {"''.isEmpty() // true"},
                              .is_member_function = true,
                              .parameters =
                                  {{.name = "google.protobuf.StringValue"}},
                              .return_type = {.name = "bool"},
                          },
                          Config::FunctionOverloadConfig{
                              .overload_id = "list_isEmpty",
                              .examples = {"[].isEmpty() // true",
                                           "[1].isEmpty() // false"},
                              .is_member_function = true,
                              .parameters = {{.name = "list",
                                              .params = {{.name = "T",
                                                          .is_type_param =
                                                              true}}}},
                              .return_type = {.name = "bool"},
                          },
                      },
              },
      },
      ParseFunctionTestCase{
          .yaml = R"yaml(
            functions:
            - name: "contains"
              overloads:
                - signature: "contains(list<~T>, ~T)"
                  examples:
                    - "contains([1, 2, 3], 2) // true"
                  return: "bool"
          )yaml",
          .expected_function_config =
              {
                  .name = "contains",
                  .overload_configs =
                      {
                          Config::FunctionOverloadConfig{
                              .overload_id = "contains(list<~T>, ~T)",
                              .examples = {"contains([1, 2, 3], 2) // true"},
                              .is_member_function = false,
                              .parameters =
                                  {{.name = "list",
                                    .params = {{.name = "T",
                                                .is_type_param = true}}},
                                   {.name = "T", .is_type_param = true}},
                              .return_type = {.name = "bool"},
                          },
                      },
              },
      },
      ParseFunctionTestCase{
          .yaml = R"yaml(
            functions:
            - name: "contains"
              overloads:
                - id: "global_contains"
                  examples:
                    - "contains([1, 2, 3], 2) // true"
                  args:
                    - type_name: "list"
                      params:
                        - type_name: "T"
                          is_type_param: true
                    - type_name: "T"
                      is_type_param: true
                  return:
                    type_name: "bool"
          )yaml",
          .expected_function_config =
              {
                  .name = "contains",
                  .overload_configs =
                      {
                          Config::FunctionOverloadConfig{
                              .overload_id = "global_contains",
                              .examples = {"contains([1, 2, 3], 2) // true"},
                              .is_member_function = false,
                              .parameters =
                                  {{.name = "list",
                                    .params = {{.name = "T",
                                                .is_type_param = true}}},
                                   {.name = "T", .is_type_param = true}},
                              .return_type = {.name = "bool"},
                          },
                      },
              },
      },
  };
}

INSTANTIATE_TEST_SUITE_P(EnvYamlParseFunctionTest, EnvYamlParseFunctionTest,
                         ::testing::ValuesIn(GetParseFunctionTestCases()));

struct ParseTestCase {
  std::string yaml;
  std::string expected_error;
};

class EnvYamlParseTest : public testing::TestWithParam<ParseTestCase> {};

TEST_P(EnvYamlParseTest, EnvYamlSyntaxError) {
  const ParseTestCase& param = GetParam();
  absl::StatusOr<Config> config = EnvConfigFromYaml(param.yaml);
  EXPECT_THAT(config, StatusIs(absl::StatusCode::kInvalidArgument,
                               HasSubstr(param.expected_error)));
}

INSTANTIATE_TEST_SUITE_P(
    EnvYamlParseTest, EnvYamlParseTest,
    ::testing::Values(
        ParseTestCase{
            .yaml = R"yaml( invalid yaml )yaml",
            .expected_error = "1:2: Invalid CEL environment config YAML\n"
                              "| invalid yaml \n"
                              "| ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
                name:
                  - error: "error"
            )yaml",
            .expected_error = "3:19: Node 'name' is not a string\n"
                              "|                  - error: \"error\"\n"
                              "|                  ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
                container:
                  - error: "error"
            )yaml",
            .expected_error =
                "3:19: Node 'container' is neither a string nor a map\n"
                "|                  - error: \"error\"\n"
                "|                  ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
                container:
                  name: []
            )yaml",
            .expected_error = "3:25: Node 'name' in container is not a string\n"
                              "|                  name: []\n"
                              "|                        ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
                container:
                  abbreviations: "abbr"
            )yaml",
            .expected_error = "3:34: Node 'abbreviations' is not a sequence\n"
                              "|                  abbreviations: \"abbr\"\n"
                              "|                                 ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
                container:
                  abbreviations:
                  - []
            )yaml",
            .expected_error = "4:21: Abbreviation is not a string\n"
                              "|                  - []\n"
                              "|                    ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
                container:
                  aliases: "not a sequence"
            )yaml",
            .expected_error = "3:28: Node 'aliases' is not a sequence\n"
                              "|                  aliases: \"not a sequence\"\n"
                              "|                           ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
                container:
                  aliases:
                  - "not a map"
            )yaml",
            .expected_error = "4:21: Alias entry is not a map\n"
                              "|                  - \"not a map\"\n"
                              "|                    ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
                container:
                  aliases:
                  - qualified_name: "qual"
            )yaml",
            .expected_error = "4:21: Alias entry missing 'alias' string\n"
                              "|                  - qualified_name: \"qual\"\n"
                              "|                    ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
                container:
                  aliases:
                  - alias: "my_alias"
            )yaml",
            .expected_error = "4:21: Alias entry missing"
                              " 'qualified_name' string\n"
                              "|                  - alias: \"my_alias\"\n"
                              "|                    ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
                extensions:
                - name: "math"
                  -name: "optional"
                    - name: "other"
            )yaml",
            .expected_error = "5:21: end of map not found\n"
                              "|                    - name: \"other\"\n"
                              "|                    ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
              extensions: "bar"
            )yaml",
            .expected_error = "2:27: Node 'extensions' is not a sequence\n"
                              "|              extensions: \"bar\"\n"
                              "|                          ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
              extensions:
                - name:
                  - something: "bar"
            )yaml",
            .expected_error = "4:19: Extension name is not a string\n"
                              "|                  - something: \"bar\"\n"
                              "|                  ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
              extensions:
                - name: "math"
                  version: last
            )yaml",
            .expected_error = "4:28: Extension 'math' version is not a valid "
                              "number or 'latest'\n"
                              "|                  version: last\n"
                              "|                           ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
              extensions:
                - name: "math"
                  version: -15
            )yaml",
            .expected_error = "4:28: Extension 'math' version is not a valid "
                              "number or 'latest'\n"
                              "|                  version: -15\n"
                              "|                           ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
              extensions:
                - name: "math"
                  version: 1
                - name: "math"
                  version: 2
            )yaml",
            .expected_error = "5:19: Extension 'math' version 1 is already "
                              "included. Cannot also include version 2\n"
                              "|                - name: \"math\"\n"
                              "|                  ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
              stdlib: "error"
            )yaml",
            .expected_error = "2:23: Standard library config ('stdlib') "
                              "is not a map\n"
                              "|              stdlib: \"error\"\n"
                              "|                      ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
              stdlib:
                disable: "error"
            )yaml",
            .expected_error = "3:26: Node 'disable' is not a boolean\n"
                              "|                disable: \"error\"\n"
                              "|                         ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
              stdlib:
                disable_macros: "error"
            )yaml",
            .expected_error = "3:33: Node 'disable_macros' is not a boolean\n"
                              "|                disable_macros: \"error\"\n"
                              "|                                ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
              stdlib:
                exclude_macros: "error"
            )yaml",
            .expected_error = "3:33: Node 'exclude_macros' is not a sequence\n"
                              "|                exclude_macros: \"error\"\n"
                              "|                                ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
              stdlib:
                exclude_macros:
                - foo: "error"
            )yaml",
            .expected_error = "4:19: Entry in 'exclude_macros' "
                              "is not a string\n"
                              "|                - foo: \"error\"\n"
                              "|                  ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
              stdlib:
                include_functions: "error"
            )yaml",
            .expected_error = "3:36: Node 'include_functions' "
                              "is not a sequence\n"
                              "|                include_functions: \"error\"\n"
                              "|                                   ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
              stdlib:
                include_functions:
                - "error"
            )yaml",
            .expected_error = "4:19: Entry in 'include_functions' "
                              "is not a map\n"
                              "|                - \"error\"\n"
                              "|                  ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
              stdlib:
                include_functions:
                - foo: "error"
            )yaml",
            .expected_error = "4:19: Function name in not specified in "
                              "'include_functions'\n"
                              "|                - foo: \"error\"\n"
                              "|                  ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
              stdlib:
                include_functions:
                - name: "foo"
                  overloads: "error"
            )yaml",
            .expected_error = "5:30: Overloads in 'include_functions' entry "
                              "is not a sequence\n"
                              "|                  overloads: \"error\"\n"
                              "|                             ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
              stdlib:
                include_functions:
                - name: "foo"
                  overloads:
                  - foo_string
            )yaml",
            .expected_error = "6:21: Overload in 'include_functions' entry "
                              "is not a map\n"
                              "|                  - foo_string\n"
                              "|                    ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
              stdlib:
                include_functions:
                - name: "foo"
                  overloads:
                  - id:
                    - foo_int64
            )yaml",
            .expected_error = "7:21: Overload id in 'include_functions' entry "
                              "is not a string\n"
                              "|                    - foo_int64\n"
                              "|                    ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
              variables:
                - name:
                  - type_name: "opaque"
            )yaml",
            .expected_error = "4:19: Variable name is not a string\n"
                              "|                  - type_name: \"opaque\"\n"
                              "|                  ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
              variables:
                - name: "foo"
                  type_name:
                    - params:
            )yaml",
            .expected_error = "5:21: Node 'type_name' is not a string\n"
                              "|                    - params:\n"
                              "|                    ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
              variables:
                - name: "foo"
                  type_name: "opaque"
                  params:
                    - type_name: "int"
                    - type_name: "A"
                      is_type_param: maybe
            )yaml",
            .expected_error = "8:38: Node 'is_type_param' is not a boolean\n"
                              "|                      is_type_param: maybe\n"
                              "|                                     ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
              variables:
                - name: "foo"
                  type_name: "opaque"
                  type: "opaque"
            )yaml",
            .expected_error = "4:19: Node 'type' and 'type_name'"
                              " are mutually exclusive\n"
                              "|                  type_name: \"opaque\"\n"
                              "|                  ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
              variables:
                - name: "foo"
                  type_name: "uint"
                  value: -1
            )yaml",
            .expected_error = "5:26: Failed to parse uint constant\n"
                              "|                  value: -1\n"
                              "|                         ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
              functions: many
            )yaml",
            .expected_error = "2:26: Node 'functions' is not a sequence\n"
                              "|              functions: many\n"
                              "|                         ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
              functions:
                - name:
                  - overloads:
            )yaml",
            .expected_error = "4:19: Function name is not a string\n"
                              "|                  - overloads:\n"
                              "|                  ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
              functions:
                - name: "foo"
                  overloads: "error"
            )yaml",
            .expected_error = "4:30: Function 'overloads' item "
                              "is not a sequence\n"
                              "|                  overloads: \"error\"\n"
                              "|                             ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
              functions:
                - name: "foo"
                  overloads:
                    - id:
                        - "error"
            )yaml",
            .expected_error = "6:25: Function overload id is not a string\n"
                              "|                        - \"error\"\n"
                              "|                        ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
              functions:
                - name: "foo"
                  overloads:
                    - id: "foo_int64"
                      target:
                        - "error"
            )yaml",
            .expected_error = "7:25: Function overload target is not a map\n"
                              "|                        - \"error\"\n"
                              "|                        ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
              functions:
                - name: "foo"
                  overloads:
                    - id: "foo_int64"
                      target:
                        type_name: "Foo"
                        params:
                          - type_name:
                              - is_type_param: true
            )yaml",
            .expected_error = "10:31: Node 'type_name' is not a string\n"
                              "|                              "
                              "- is_type_param: true\n"
                              "|                              ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
              functions:
                - name: "foo"
                  overloads:
                    - id: "foo_int64"
                      args: "a bunch"
            )yaml",
            .expected_error = "6:29: Function overload args is not a sequence\n"
                              "|                      args: \"a bunch\"\n"
                              "|                            ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
              functions:
                - name: "foo"
                  overloads:
                    - id: "foo_int64"
                      return: [1]
            )yaml",
            .expected_error = "6:31: Function overload return type"
                              " is neither a string nor a map\n"
                              "|                      return: [1]\n"
                              "|                              ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
              functions:
                - name: "foo"
                  overloads:
                    - id: "foo_int64"
                      signature: "bar()"
            )yaml",
            .expected_error = "6:34: Function overload name \"bar\" "
                              "does not match function name \"foo\"\n"
                              "|                      signature: \"bar()\"\n"
                              "|                                 ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
              functions:
                - name: "foo"
                  overloads:
                    - signature: [ "foo()" ]
            )yaml",
            .expected_error =
                "5:34: Function overload signature is not a string\n"
                "|                    - signature: [ \"foo()\" ]\n"
                "|                                 ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
              functions:
                - name: "foo"
                  overloads:
                    - signature: "foo()"
                      target:
                        type_name: "int"
            )yaml",
            .expected_error = "6:23: Function overload signature and target "
                              "are mutually exclusive\n"
                              "|                      target:\n"
                              "|                      ^",
        },
        ParseTestCase{
            .yaml = R"yaml(
              functions:
                - name: "foo"
                  overloads:
                    - signature: "foo()"
                      args:
                        - type_name: "int"
            )yaml",
            .expected_error = "6:23: Function overload signature and args are "
                              "mutually exclusive\n"
                              "|                      args:\n"
                              "|                      ^",
        }));

std::string Unindent(std::string_view yaml) {
  absl::string_view yaml_view = yaml;
  std::vector<std::string> lines = absl::StrSplit(yaml_view, '\n');
  int indent = -1;
  std::vector<std::string> unindented_lines;
  for (auto& line : lines) {
    std::size_t pos = line.find_first_not_of(" \t");
    if (pos == std::string::npos) {
      // Skip blank lines.
      continue;
    }
    if (indent == -1) {
      indent = pos;
    }
    if (pos >= indent) {
      unindented_lines.push_back(line.substr(indent));
    } else {
      unindented_lines.push_back(line);
    }
  }
  return absl::StrJoin(unindented_lines, "\n");
}

struct ExportTestCase {
  absl::StatusOr<Config> config;
  std::string expected_yaml;
  std::string expected_alt_yaml;
};

class EnvYamlExportTest : public testing::TestWithParam<ExportTestCase> {};

TEST_P(EnvYamlExportTest, EnvYamlExport) {
  const ExportTestCase& param = GetParam();
  ASSERT_OK_AND_ASSIGN(Config config, param.config);
  std::stringstream ss;
  EnvConfigToYaml(config, ss, {.use_type_signatures = true});
  std::string yaml_output = Unindent(ss.str());
  std::string expected_yaml = Unindent(param.expected_yaml);
  EXPECT_EQ(yaml_output, expected_yaml);

  if (!param.expected_alt_yaml.empty()) {
    std::stringstream alt_ss;
    EnvConfigToYaml(config, alt_ss, {.use_type_signatures = false});
    std::string alt_yaml_output = Unindent(alt_ss.str());
    std::string expected_alt_yaml = Unindent(param.expected_alt_yaml);
    EXPECT_EQ(alt_yaml_output, expected_alt_yaml);
  }
}

std::vector<ExportTestCase> GetExportTestCases() {
  return {
      ExportTestCase{
          .config =
              []() {
                Config config;
                config.SetName("test.env");
                config.SetContainerConfig({.name = "test.container"});
                return config;
              }(),
          .expected_yaml = R"yaml(
                name: "test.env"
                container: "test.container"
            )yaml",
      },
      ExportTestCase{
          .config = []() -> absl::StatusOr<Config> {
            Config config;
            config.SetName("test.env");
            config.SetContainerConfig({.name = "test.container"});
            return config;
          }(),
          .expected_yaml = R"yaml(
                name: "test.env"
                container: "test.container"
            )yaml",
      },
      ExportTestCase{
          .config = []() -> absl::StatusOr<Config> {
            Config config;
            config.SetName("test.env");
            config.SetContainerConfig(
                {.name = "test.container",
                 .abbreviations = {"foo", "bar"},
                 .aliases = {
                     {.alias = "foo", .qualified_name = "test.foo"},
                     {.alias = "bar", .qualified_name = "test.bar"},
                 }});
            return config;
          }(),
          .expected_yaml = R"yaml(
                name: "test.env"
                container:
                  name: "test.container"
                  abbreviations:
                    - "bar"
                    - "foo"
                  aliases:
                    - alias: "bar"
                      qualified_name: "test.bar"
                    - alias: "foo"
                      qualified_name: "test.foo"
            )yaml",
      },
      ExportTestCase{
          .config = []() -> absl::StatusOr<Config> {
            Config config;
            CEL_RETURN_IF_ERROR(config.AddExtensionConfig("math"));
            CEL_RETURN_IF_ERROR(config.AddExtensionConfig("optional", 2));
            CEL_RETURN_IF_ERROR(config.AddExtensionConfig("bindings"));
            return config;
          }(),
          .expected_yaml = R"yaml(
                extensions:
                  - name: "bindings"
                  - name: "math"
                  - name: "optional"
                    version: 2
            )yaml",
      },
      ExportTestCase{
          .config = []() -> absl::StatusOr<Config> {
            Config config;
            CEL_RETURN_IF_ERROR(
                config.SetStandardLibraryConfig(Config::StandardLibraryConfig{
                    .disable = true,
                }));
            return config;
          }(),
          .expected_yaml = R"yaml(
                stdlib:
                  disable: true
            )yaml",
      },
      ExportTestCase{
          .config = []() -> absl::StatusOr<Config> {
            Config config;
            CEL_RETURN_IF_ERROR(
                config.SetStandardLibraryConfig(Config::StandardLibraryConfig{
                    .disable_macros = true,
                }));
            return config;
          }(),
          .expected_yaml = R"yaml(
                stdlib:
                  disable_macros: true
            )yaml",
      },
      ExportTestCase{
          .config = []() -> absl::StatusOr<Config> {
            Config config;
            CEL_RETURN_IF_ERROR(
                config.SetStandardLibraryConfig(Config::StandardLibraryConfig{
                    .excluded_macros = {"map", "filter"},
                }));
            return config;
          }(),
          .expected_yaml = R"yaml(
                stdlib:
                  exclude_macros:
                    - "filter"
                    - "map"
            )yaml",
      },
      ExportTestCase{
          .config = []() -> absl::StatusOr<Config> {
            Config config;
            CEL_RETURN_IF_ERROR(
                config.SetStandardLibraryConfig(Config::StandardLibraryConfig{
                    .included_macros = {"map", "filter"},
                }));
            return config;
          }(),
          .expected_yaml = R"yaml(
                stdlib:
                  include_macros:
                    - "filter"
                    - "map"
            )yaml",
      },
      ExportTestCase{
          .config = []() -> absl::StatusOr<Config> {
            Config config;
            CEL_RETURN_IF_ERROR(
                config.SetStandardLibraryConfig(Config::StandardLibraryConfig{
                    .excluded_functions =
                        {
                            std::make_pair("timestamp", "string_to_timestamp"),
                            std::make_pair("_+_", "add_list"),
                            std::make_pair("matches", ""),
                            std::make_pair("_+_", "add_bytes"),
                        },
                }));
            return config;
          }(),
          .expected_yaml = R"yaml(
                stdlib:
                  exclude_functions:
                    - name: "_+_"
                      overloads:
                        - id: "add_bytes"
                        - id: "add_list"
                    - name: "matches"
                    - name: "timestamp"
                      overloads:
                        - id: "string_to_timestamp"
            )yaml",
      },
      ExportTestCase{
          .config = []() -> absl::StatusOr<Config> {
            Config config;
            CEL_RETURN_IF_ERROR(
                config.SetStandardLibraryConfig(Config::StandardLibraryConfig{
                    .included_functions =
                        {
                            std::make_pair("timestamp", "string_to_timestamp"),
                            std::make_pair("_+_", "_+_(list<~A>,list<~A>)"),
                            std::make_pair("matches", ""),
                            std::make_pair("_+_", "_+_(bytes,bytes)"),
                        },
                }));
            return config;
          }(),
          .expected_yaml = R"yaml(
                stdlib:
                  include_functions:
                    - name: "_+_"
                      overloads:
                        - id: "_+_(bytes,bytes)"
                        - id: "_+_(list<~A>,list<~A>)"
                    - name: "matches"
                    - name: "timestamp"
                      overloads:
                        - id: "string_to_timestamp"
            )yaml",
      },
      ExportTestCase{
          .config = []() -> absl::StatusOr<Config> {
            Config config;
            CEL_RETURN_IF_ERROR(
                config.AddVariableConfig({.name = "foo",
                                          .type_info = {.name = "null"},
                                          .value = Constant(nullptr)}));
            return config;
          }(),
          .expected_yaml = R"yaml(
                variables:
                  - name: "foo"
                    type: "null"
            )yaml",
      },
      ExportTestCase{
          .config = []() -> absl::StatusOr<Config> {
            Config config;
            CEL_RETURN_IF_ERROR(
                config.AddVariableConfig({.name = "foo",
                                          .type_info = {.name = "bool"},
                                          .value = Constant(true)}));
            return config;
          }(),
          .expected_yaml = R"yaml(
                variables:
                  - name: "foo"
                    type: "bool"
                    value: true
            )yaml",
          .expected_alt_yaml = R"yaml(
                variables:
                  - name: "foo"
                    type_name: "bool"
                    value: true
            )yaml",
      },
      ExportTestCase{
          .config = []() -> absl::StatusOr<Config> {
            Config config;
            CEL_RETURN_IF_ERROR(
                config.AddVariableConfig({.name = "foo",
                                          .type_info = {.name = "int"},
                                          .value = Constant(int64_t{42})}));
            return config;
          }(),
          .expected_yaml = R"yaml(
                variables:
                  - name: "foo"
                    type: "int"
                    value: 42
            )yaml",
          .expected_alt_yaml = R"yaml(
                variables:
                  - name: "foo"
                    type_name: "int"
                    value: 42
            )yaml",
      },
      ExportTestCase{
          .config = []() -> absl::StatusOr<Config> {
            Config config;
            CEL_RETURN_IF_ERROR(
                config.AddVariableConfig({.name = "foo",
                                          .type_info = {.name = "uint"},
                                          .value = Constant(uint64_t{777})}));
            return config;
          }(),
          .expected_yaml = R"yaml(
                variables:
                  - name: "foo"
                    type: "uint"
                    value: 777
            )yaml",
      },
      ExportTestCase{
          .config = []() -> absl::StatusOr<Config> {
            Config config;
            CEL_RETURN_IF_ERROR(
                config.AddVariableConfig({.name = "foo",
                                          .type_info = {.name = "double"},
                                          .value = Constant(0.75)}));
            return config;
          }(),
          .expected_yaml = R"yaml(
                variables:
                  - name: "foo"
                    type: "double"
                    value: 0.75
            )yaml",
      },
      ExportTestCase{
          .config = []() -> absl::StatusOr<Config> {
            Config config;
            CEL_RETURN_IF_ERROR(config.AddVariableConfig(
                {.name = "foo",
                 .type_info = {.name = "bytes"},
                 .value = Constant(
                     BytesConstant(absl::string_view("\xff\x00\x01", 3)))}));
            return config;
          }(),
          .expected_yaml = R"yaml(
                variables:
                  - name: "foo"
                    type: "bytes"
                    value: b"\xff\x00\x01"
            )yaml",
      },
      ExportTestCase{
          .config = []() -> absl::StatusOr<Config> {
            Config config;
            Constant c;
            c.set_string_value("'single' \"double\"");
            CEL_RETURN_IF_ERROR(config.AddVariableConfig(
                {.name = "foo",
                 .type_info = {.name = "string"},
                 .value = Constant(StringConstant("'single' \"double\""))}));
            return config;
          }(),
          .expected_yaml = R"yaml(
                variables:
                  - name: "foo"
                    type: "string"
                    value: "'single' \"double\""
            )yaml",
      },
      ExportTestCase{
          .config = []() -> absl::StatusOr<Config> {
            Config config;
            CEL_RETURN_IF_ERROR(config.AddVariableConfig(
                {.name = "foo",
                 .type_info = {.name = "duration"},
                 .value = Constant(absl::Hours(1) + absl::Minutes(2) +
                                   absl::Seconds(3))}));
            return config;
          }(),
          .expected_yaml = R"yaml(
                variables:
                  - name: "foo"
                    type: "duration"
                    value: 1h2m3s
            )yaml",
          .expected_alt_yaml = R"yaml(
                variables:
                  - name: "foo"
                    type_name: "duration"
                    value: 1h2m3s
            )yaml",
      },
      ExportTestCase{
          .config = []() -> absl::StatusOr<Config> {
            Config config;
            CEL_RETURN_IF_ERROR(config.AddVariableConfig(
                {.name = "foo",
                 .type_info = {.name = "timestamp"},
                 .value = Constant(absl::FromUnixSeconds(1767323045))}));
            return config;
          }(),
          .expected_yaml = R"yaml(
                variables:
                  - name: "foo"
                    type: "timestamp"
                    value: 2026-01-02T03:04:05Z
            )yaml",
          .expected_alt_yaml = R"yaml(
                variables:
                  - name: "foo"
                    type_name: "timestamp"
                    value: 2026-01-02T03:04:05Z
            )yaml",
      },
      ExportTestCase{
          .config = []() -> absl::StatusOr<Config> {
            Config config;
            CEL_RETURN_IF_ERROR(config.AddVariableConfig(
                {.name = "foo",
                 .type_info = {.name =
                                   "google.expr.proto3.test.TestAllTypes"}}));
            return config;
          }(),
          .expected_yaml = R"yaml(
                variables:
                  - name: "foo"
                    type: "google.expr.proto3.test.TestAllTypes"
            )yaml",
      },
      ExportTestCase{
          .config = []() -> absl::StatusOr<Config> {
            Config config;
            CEL_RETURN_IF_ERROR(config.AddVariableConfig(
                {.name = "foo",
                 .type_info = {
                     .name = "A",
                     .params = {{.name = "int"},
                                {.name = "B", .is_type_param = true}}}}));
            return config;
          }(),
          .expected_yaml = R"yaml(
                variables:
                  - name: "foo"
                    type: "A<int,~B>"
            )yaml",
          .expected_alt_yaml = R"yaml(
                variables:
                  - name: "foo"
                    type_name: "A"
                    params:
                      - type_name: "int"
                      - type_name: "B"
                        is_type_param: true
            )yaml",
      },
      ExportTestCase{
          .config = []() -> absl::StatusOr<Config> {
            Config config;
            CEL_RETURN_IF_ERROR(config.AddFunctionConfig({.name = "foo"}));
            return config;
          }(),
          .expected_yaml = R"yaml(
                functions:
                  - name: "foo"
            )yaml",
      },
      ExportTestCase{
          .config = []() -> absl::StatusOr<Config> {
            Config config;
            CEL_RETURN_IF_ERROR(config.AddFunctionConfig(
                {.name = "foo",
                 .overload_configs = {
                     {.overload_id = "foo_overload_id",
                      .is_member_function = true,
                      .parameters = {{.name = "timestamp"},
                                     {.name = "A",
                                      .params = {{.name = "B",
                                                  .is_type_param = true}}}},
                      .return_type = {.name = "int"}},
                 }}));
            return config;
          }(),
          .expected_yaml = R"yaml(
                functions:
                  - name: "foo"
                    overloads:
                      - id: "foo_overload_id"
                        signature: "timestamp.foo(A<~B>)"
                        return: "int"
            )yaml",
          .expected_alt_yaml = R"yaml(
                functions:
                  - name: "foo"
                    overloads:
                      - id: "foo_overload_id"
                        target:
                          type_name: "timestamp"
                        args:
                          - type_name: "A"
                            params:
                              - type_name: "B"
                                is_type_param: true
                        return:
                          type_name: "int"
            )yaml",
      },
      ExportTestCase{
          .config = []() -> absl::StatusOr<Config> {
            Config config;
            CEL_RETURN_IF_ERROR(config.AddFunctionConfig(
                {.name = "foo",
                 .description = "my desc",
                 .overload_configs = {
                     {.overload_id = "foo_overload_a",
                      .parameters = {{.name = "timestamp"}},
                      .return_type = {.name = "list",
                                      .params = {{.name = "int"}}}},
                     {.overload_id = "foo_overload_b",
                      .parameters = {{.name = "double"},
                                     {.name = "A", .params = {{.name = "B"}}}},
                      .return_type = {.name = "string"}},
                 }}));
            return config;
          }(),
          .expected_yaml = R"yaml(
                functions:
                  - name: "foo"
                    description: "my desc"
                    overloads:
                      - id: "foo_overload_b"
                        signature: "foo(double,A<B>)"
                        return: "string"
                      - id: "foo_overload_a"
                        signature: "foo(timestamp)"
                        return: "list<int>"
            )yaml",
          .expected_alt_yaml = R"yaml(
                functions:
                  - name: "foo"
                    description: "my desc"
                    overloads:
                      - id: "foo_overload_b"
                        args:
                          - type_name: "double"
                          - type_name: "A"
                            params:
                              - type_name: "B"
                        return:
                          type_name: "string"
                      - id: "foo_overload_a"
                        args:
                          - type_name: "timestamp"
                        return:
                          type_name: "list"
                          params:
                            - type_name: "int"
            )yaml",
      },
      ExportTestCase{
          .config = []() -> absl::StatusOr<Config> {
            Config config;
            CEL_RETURN_IF_ERROR(config.AddFunctionConfig(
                {.name = "foo",
                 .overload_configs = {
                     {.overload_id = "timestamp.foo(A<~B>)",
                      .is_member_function = true,
                      .parameters = {{.name = "timestamp"},
                                     {.name = "A",
                                      .params = {{.name = "B",
                                                  .is_type_param = true}}}},
                      .return_type = {.name = "int"}},
                 }}));
            return config;
          }(),
          .expected_yaml = R"yaml(
                functions:
                  - name: "foo"
                    overloads:
                      - signature: "timestamp.foo(A<~B>)"
                        return: "int"
            )yaml",
          .expected_alt_yaml = R"yaml(
                functions:
                  - name: "foo"
                    overloads:
                      - id: "timestamp.foo(A<~B>)"
                        target:
                          type_name: "timestamp"
                        args:
                          - type_name: "A"
                            params:
                              - type_name: "B"
                                is_type_param: true
                        return:
                          type_name: "int"
            )yaml",
      },
  };
};

INSTANTIATE_TEST_SUITE_P(EnvYamlExportTest, EnvYamlExportTest,
                         ::testing::ValuesIn(GetExportTestCases()));

class EnvYamlStructuredRoundTripTest
    : public testing::TestWithParam<std::string> {};

TEST_P(EnvYamlStructuredRoundTripTest, EnvYamlRoundTrip) {
  const std::string& yaml = Unindent(GetParam());
  ASSERT_OK_AND_ASSIGN(Config config, EnvConfigFromYaml(yaml));

  std::stringstream ss;
  EnvConfigToYaml(config, ss);
  EXPECT_EQ(ss.str(), yaml);
}

std::vector<std::string> GetStructuredRoundTripTestCases() {
  return {
      R"yaml(
        stdlib:
          disable: true
          disable_macros: true
      )yaml",
      R"yaml(
        name: "test.env"
        container: "common.proto.prefix"
        extensions:
          - name: "math"
            version: 0
          - name: "optional"
            version: 2
        stdlib:
          include_macros:
            - "filter"
            - "map"
          include_functions:
            - name: "_+_"
              overloads:
                - id: "add_bytes"
                - id: "add_list"
            - name: "matches"
            - name: "timestamp"
              overloads:
                - id: "string_to_timestamp"
      )yaml",
      R"yaml(
        container:
          name: "test.container"
          abbreviations:
            - "abbr1.Abbr1"
            - "abbr2.Abbr2"
          aliases:
            - alias: "alias1"
              qualified_name: "qual.name1"
            - alias: "alias2"
              qualified_name: "qual.name2"
      )yaml",
      R"yaml(
        extensions:
          - name: "bindings"
          - name: "math"
        stdlib:
          exclude_macros:
            - "filter"
            - "map"
          exclude_functions:
            - name: "_+_"
              overloads:
                - id: "add_bytes"
                - id: "add_list"
            - name: "matches"
            - name: "timestamp"
              overloads:
                - id: "string_to_timestamp"
      )yaml",
      R"yaml(
          functions:
            - name: "bar"
            - name: "foo"
      )yaml",
  };
}

INSTANTIATE_TEST_SUITE_P(
    EnvYamlStructuredRoundTripTest, EnvYamlStructuredRoundTripTest,
    ::testing::ValuesIn(GetStructuredRoundTripTestCases()));

class EnvYamlSignatureRoundTripTest
    : public testing::TestWithParam<std::string> {};

TEST_P(EnvYamlSignatureRoundTripTest, EnvYamlRoundTrip) {
  const std::string& yaml = Unindent(GetParam());
  ASSERT_OK_AND_ASSIGN(Config config, EnvConfigFromYaml(yaml));

  std::stringstream ss;
  EnvConfigToYaml(config, ss, {.use_type_signatures = true});
  EXPECT_EQ(ss.str(), yaml);
}

std::vector<std::string> GetSignatureRoundTripTestCases() {
  return {
      R"yaml(
        variables:
          - name: "a"
            type: "null"
          - name: "b"
            type: "bool"
            value: true
          - name: "c"
            type: "int"
            value: 42
          - name: "d"
            type: "uint"
            value: 777
          - name: "e"
            type: "double"
            value: 0.75
          - name: "f"
            type: "bytes"
            value: b"\xff\x00\x01"
          - name: "g"
            type: "string"
            value: "plain 'single' \"double\""
          - name: "h"
            type: "duration"
            value: 1h2m3s
          - name: "i"
            type: "timestamp"
            value: 2026-01-02T03:04:05Z
      )yaml",
      R"yaml(
          functions:
            - name: "foo"
              overloads:
                - id: "foo_overload_id"
                  signature: "timestamp.foo(A<~B>)"
                  return: "int"
      )yaml",
      R"yaml(
          functions:
            - name: "foo"
              overloads:
                - id: "foo_overload_id"
                  signature: "foo(timestamp,A<~B>)"
                  return: "list<int>"
      )yaml",
      R"yaml(
          functions:
            - name: "foo"
              overloads:
                - signature: "timestamp.foo(A<~B>)"
                  return: "int"
      )yaml",
  };
}

INSTANTIATE_TEST_SUITE_P(EnvYamlSignatureRoundTripTest,
                         EnvYamlSignatureRoundTripTest,
                         ::testing::ValuesIn(GetSignatureRoundTripTestCases()));

}  // namespace
}  // namespace cel
