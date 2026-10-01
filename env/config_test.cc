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

#include "env/config.h"

#include <string>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "common/constant.h"
#include "internal/testing.h"

namespace cel {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::HasSubstr;
using ::testing::UnorderedElementsAre;

TEST(EnvConfigTest, ExtensionConfigs) {
  Config config;
  ASSERT_THAT(
      config.AddExtensionConfig("math", Config::ExtensionConfig::kLatest),
      IsOk());
  ASSERT_THAT(config.AddExtensionConfig("optional", 2), IsOk());
  ASSERT_THAT(config.AddExtensionConfig("strings"), IsOk());

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

TEST(EnvConfigTest, ExtensionConfigConflict) {
  Config config;
  ASSERT_THAT(config.AddExtensionConfig("math", 2), IsOk());
  ASSERT_THAT(config.AddExtensionConfig("math", 2), IsOk());
  ASSERT_THAT(config.AddExtensionConfig("math", 3),
              StatusIs(absl::StatusCode::kAlreadyExists));
}

struct StandardLibraryConfigTestCase {
  Config::StandardLibraryConfig standard_library_config;
  std::string expected_error;  // Empty if no error is expected.
};

class StandardLibraryConfigTest
    : public testing::TestWithParam<StandardLibraryConfigTestCase> {};

TEST_P(StandardLibraryConfigTest, StandardLibraryConfig) {
  const StandardLibraryConfigTestCase& param = GetParam();

  Config config;
  absl::Status status =
      config.SetStandardLibraryConfig(param.standard_library_config);
  if (param.expected_error.empty()) {
    EXPECT_THAT(status, IsOk());
  } else {
    EXPECT_THAT(status, StatusIs(absl::StatusCode::kInvalidArgument,
                                 HasSubstr(param.expected_error)));
  }
}

INSTANTIATE_TEST_SUITE_P(
    StandardLibraryConfigTest, StandardLibraryConfigTest,
    ::testing::Values(
        StandardLibraryConfigTestCase{
            .standard_library_config = {},
        },
        StandardLibraryConfigTestCase{
            .standard_library_config =
                {
                    .included_functions = {{"_+_", "add_int64"},
                                           {"_+_", "add_list"}},
                },
        },
        StandardLibraryConfigTestCase{
            .standard_library_config =
                {
                    .included_functions = {{"_+_", "add(int,int)"},
                                           {"_+_", "add(list,list)"}},
                },
        },
        StandardLibraryConfigTestCase{
            .standard_library_config =
                {
                    .excluded_functions = {{"_+_", "add_int64"},
                                           {"_+_", "add_list"}},
                },
        },
        StandardLibraryConfigTestCase{
            .standard_library_config =
                {
                    .excluded_functions = {{"_+_", "add(int,int)"},
                                           {"_+_", "add(list,list)"}},
                },
        },
        StandardLibraryConfigTestCase{
            .standard_library_config =
                {
                    .included_macros = {"all", "exists"},
                    .excluded_macros = {"map", "filter"},
                },
            .expected_error = "Cannot set both included and excluded macros.",
        },
        StandardLibraryConfigTestCase{
            .standard_library_config =
                {
                    .included_functions = {{"_+_", "add_int64"},
                                           {"_+_", "add_list"}},
                    .excluded_functions = {{"_-_", ""}},
                },
            .expected_error =
                "Cannot set both included and excluded functions.",
        },
        StandardLibraryConfigTestCase{
            .standard_library_config =
                {
                    .included_functions = {{"_+_", "add(int,int)"}},
                    .excluded_functions = {{"_-_", ""}},
                },
            .expected_error =
                "Cannot set both included and excluded functions.",
        },
        StandardLibraryConfigTestCase{
            .standard_library_config =
                {
                    .included_functions = {{"_+_", ""}, {"_+_", "add_list"}},
                },
            .expected_error = "Cannot include function '_+_' and also its "
                              "specific overload 'add_list'",
        },
        StandardLibraryConfigTestCase{
            .standard_library_config =
                {
                    .included_functions = {{"_+_", ""},
                                           {"_+_", "add(int,int)"}},
                },
            .expected_error = "Cannot include function '_+_' and also its "
                              "specific overload 'add(int,int)'",
        },
        StandardLibraryConfigTestCase{
            .standard_library_config =
                {
                    .excluded_functions = {{"_+_", ""}, {"_+_", "add_list"}},
                },
            .expected_error = "Cannot exclude function '_+_' and also its "
                              "specific overload 'add_list'",
        },
        StandardLibraryConfigTestCase{
            .standard_library_config =
                {
                    .excluded_functions = {{"_+_", ""},
                                           {"_+_", "add(int,int)"}},
                },
            .expected_error = "Cannot exclude function '_+_' and also its "
                              "specific overload 'add(int,int)'",
        }));

TEST(VariableConfigTest, VariableConfig) {
  Config config;
  Config::VariableConfig variable_config{
      .name = "test",
      .type_info =
          {
              .name = "mytype",
              .params = {{.name = "int"}, {.name = "A", .is_type_param = true}},
          },
  };
  ASSERT_THAT(config.AddVariableConfig(variable_config), IsOk());

  ASSERT_EQ(config.GetVariableConfigs().size(), 1);
  const auto& added_config = config.GetVariableConfigs()[0];
  EXPECT_EQ(added_config.type_info.name, "mytype");
  ASSERT_THAT(added_config.type_info.params.size(), 2);
  EXPECT_EQ(added_config.type_info.params[0].name, "int");
  EXPECT_FALSE(added_config.type_info.params[0].is_type_param);
  EXPECT_EQ(added_config.type_info.params[1].name, "A");
  EXPECT_TRUE(added_config.type_info.params[1].is_type_param);
}

TEST(VariableConfigTest, VariableConfigConflict) {
  Config config;
  Config::VariableConfig variable_config{
      .name = "test",
      .type_info = {.name = "int"},
  };
  EXPECT_THAT(config.AddVariableConfig(variable_config), IsOk());
  EXPECT_THAT(config.AddVariableConfig(variable_config),
              StatusIs(absl::StatusCode::kAlreadyExists));
}

TEST(VariableConfigTest, VariableConfigValueTypeMismatch) {
  Config config;
  Config::VariableConfig variable_config{
      .name = "test",
      .type_info = {.name = "int"},
      .value = Constant(StringConstant("hello")),
  };
  EXPECT_THAT(config.AddVariableConfig(variable_config),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Variable 'test' has type int but is assigned "
                                 "a constant value of type string.")));
}

TEST(FunctionConfigTest, FunctionConfig) {
  Config config;
  Config::FunctionConfig function_config;
  function_config.name = "test";
  function_config.description = "Ultimate test";
  function_config.overload_configs.push_back(Config::FunctionOverloadConfig{
      .overload_id = "test_with_pill",
      .examples = {"oracle.isTheOne('Neo', RED)"},
      .is_member_function = true,
      .parameters = {{.name = "string"}, {.name = "Choice"}},
      .return_type = {.name = "bool"},
  });
  ASSERT_THAT(config.AddFunctionConfig(function_config), IsOk());
  ASSERT_EQ(config.GetFunctionConfigs().size(), 1);
  const auto& added_config = config.GetFunctionConfigs()[0];
  EXPECT_EQ(added_config.name, "test");
  EXPECT_EQ(added_config.description, "Ultimate test");
  EXPECT_EQ(added_config.overload_configs.size(), 1);

  const auto& overload_config = added_config.overload_configs[0];
  EXPECT_EQ(overload_config.overload_id, "test_with_pill");
  EXPECT_THAT(overload_config.examples,
              ElementsAre("oracle.isTheOne('Neo', RED)"));
  EXPECT_TRUE(overload_config.is_member_function);
  EXPECT_THAT(overload_config.parameters,
              ElementsAre(AllOf(Field(&TypeRef::name, "string"),
                                Field(&TypeRef::is_type_param, false)),
                          AllOf(Field(&TypeRef::name, "Choice"),
                                Field(&TypeRef::is_type_param, false))));
  EXPECT_THAT(overload_config.return_type, Field(&TypeRef::name, "bool"));
}

TEST(FunctionConfigTest, FunctionConfigInvalidMember) {
  Config config;
  Config::FunctionConfig function_config;
  function_config.name = "test";
  function_config.overload_configs.push_back(Config::FunctionOverloadConfig{
      .overload_id = "test_member_no_params",
      .is_member_function = true,
      .parameters = {},
  });
  EXPECT_THAT(config.AddFunctionConfig(function_config),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("is marked as a member function but has no "
                                 "parameters")));
}

}  // namespace
}  // namespace cel
