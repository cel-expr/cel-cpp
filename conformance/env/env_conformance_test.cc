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

#include <fstream>
#include <ios>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/types/span.h"
#include "cel/expr/conformance/test/env/suite.pb.h"
#include "conformance/env/normalize_yaml.h"
#include "env/config.h"
#include "env/env_yaml.h"
#include "internal/runfiles.h"
#include "internal/testing.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "google/protobuf/text_format.h"

ABSL_FLAG(std::vector<std::string>, test_suites, {},
          "Comma-separated list of test suite textproto runfiles paths.");
ABSL_FLAG(std::vector<std::string>, skip_tests, {},
          "Comma-separated list of tests to skip.");

namespace cel {
namespace {

using absl_testing::IsOk;
using cel::expr::conformance::test::env::TestCase;
using cel::expr::conformance::test::env::TestOutput;
using cel::expr::conformance::test::env::TestSuite;
using testing::HasSubstr;
using testing::Not;

bool ShouldSkipTest(absl::Span<const std::string> tests_to_skip,
                    absl::string_view name) {
  for (absl::string_view test_to_skip : tests_to_skip) {
    absl::string_view consumed_name = name;
    if (absl::ConsumePrefix(&consumed_name, test_to_skip) &&
        (consumed_name.empty() || absl::StartsWith(consumed_name, "/"))) {
      return true;
    }
  }
  return false;
}

class EnvConformanceTest : public testing::Test {
 public:
  EnvConformanceTest(TestCase test_case, std::string full_test_name, bool skip)
      : test_case_(std::move(test_case)),
        full_test_name_(std::move(full_test_name)),
        skip_(skip) {}

  void TestBody() override {
    if (skip_) {
      GTEST_SKIP() << "Skipping test: " << full_test_name_;
    }

    absl::StatusOr<Config> config_or;
    switch (test_case_.env_config_case()) {
      case TestCase::kEnvYaml:
        config_or = EnvConfigFromYaml(test_case_.env_yaml());
        break;
      case TestCase::kEnv:
        FAIL() << "Proto Environment input is not yet supported in "
               << full_test_name_;
      case TestCase::ENV_CONFIG_NOT_SET:
        FAIL() << "No env_config specified in " << full_test_name_;
    }

    const TestOutput& output = test_case_.output();
    const bool has_exported_yaml = !output.exported_env_yaml().empty();
    const bool has_expected_errors = !output.errors().empty();

    if (!has_exported_yaml && !has_expected_errors) {
      FAIL() << "No expected output specified in " << full_test_name_;
    }

    if (has_expected_errors) {
      ASSERT_THAT(config_or, Not(IsOk()))
          << "Expected error in " << full_test_name_ << ", but succeeded.";
      for (absl::string_view expected_err : output.errors()) {
        if (!expected_err.empty()) {
          EXPECT_THAT(config_or.status().message(), HasSubstr(expected_err));
        }
      }
      return;
    }

    ASSERT_THAT(config_or, IsOk())
        << "Failed to parse env_yaml in " << full_test_name_;

    if (has_exported_yaml) {
      std::ostringstream actual_stream;
      EnvConfigToYamlOptions options;
      options.use_type_signatures = true;
      EnvConfigToYaml(*config_or, actual_stream, options);
      std::string actual_yaml = actual_stream.str();

      EXPECT_EQ(NormalizeYaml(actual_yaml),
                NormalizeYaml(output.exported_env_yaml()))
          << "Env YAML mismatch in " << full_test_name_ << "\nExpected:\n"
          << output.exported_env_yaml() << "\nActual:\n"
          << actual_yaml;
    }
  }

 private:
  TestCase test_case_;
  std::string full_test_name_;
  bool skip_;
};

absl::Status RegisterTestsFromFile(absl::Span<const std::string> skip_tests,
                                   absl::string_view runfile_path) {
  std::string abs_path = cel::internal::ResolveRunfilesPath(runfile_path);
  if (abs_path.empty()) {
    return absl::NotFoundError(
        absl::StrCat("could not resolve runfiles path: ", runfile_path));
  }

  TestSuite suite;
  {
    std::ifstream in(abs_path, std::ios_base::in | std::ios_base::binary);
    if (!in.is_open()) {
      return absl::UnknownError(
          absl::StrCat("failed to open test suite file: ", abs_path));
    }
    google::protobuf::io::IstreamInputStream stream(&in);
    if (!google::protobuf::TextFormat::Parse(&stream, &suite)) {
      return absl::InvalidArgumentError(
          absl::StrCat("failed to parse test suite textproto: ", abs_path));
    }
  }

  const std::string& suite_name = suite.name();
  for (const auto& section : suite.sections()) {
    const std::string& section_name = section.name();
    for (const auto& test : section.tests()) {
      const std::string& test_name = test.name();
      std::string full_test_name =
          absl::StrCat(suite_name, "/", section_name, "/", test_name);
      bool skip = ShouldSkipTest(skip_tests, full_test_name);

      testing::RegisterTest(
          suite_name.c_str(),
          absl::StrCat(section_name, "/", test_name).c_str(), nullptr, nullptr,
          __FILE__, __LINE__, [test, full_test_name, skip]() {
            return new EnvConformanceTest(test, full_test_name, skip);
          });
    }
  }
  return absl::OkStatus();
}

void RegisterAllTests() {
  std::vector<std::string> suite_paths = absl::GetFlag(FLAGS_test_suites);
  std::vector<std::string> skip_tests = absl::GetFlag(FLAGS_skip_tests);

  ABSL_CHECK(!suite_paths.empty())
      << "No test suites specified in --test_suites flag.";

  for (const std::string& suite_path : suite_paths) {
    ABSL_CHECK_OK(RegisterTestsFromFile(skip_tests, suite_path));
  }
}

}  // namespace
}  // namespace cel

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);

  cel::RegisterAllTests();
  return RUN_ALL_TESTS();
}
