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

#include "common/typedef/proto_to_typedef.h"

#include <sstream>
#include <string>
#include <vector>

#include "google/protobuf/descriptor.pb.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/absl_log.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "common/typedef/typedef.h"
#include "common/typedef/typedef.pb.h"
#include "common/typedef/typedef_proto.h"
#include "common/typedef/typedef_yaml.h"
#include "internal/runfiles.h"
#include "internal/testing.h"
#include "cel/expr/conformance/proto2/test_all_types.pb.h"
#include "cel/expr/conformance/proto3/test_all_types.pb.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/text_format.h"

namespace cel {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::HasSubstr;

constexpr absl::string_view kTestTypeDefFilePath =
"_main/common/typedef/testdata/";

constexpr absl::string_view kBaselineSeparator =
    "--------------------------------------------------------------------\n";

google::protobuf::FileDescriptorSet BuildTransitiveFileDescriptorSet(
    const google::protobuf::Descriptor* root_descriptor) {
  google::protobuf::FileDescriptorSet file_set;
  absl::flat_hash_set<absl::string_view> visited;
  std::vector<const google::protobuf::FileDescriptor*> stack = {root_descriptor->file()};
  while (!stack.empty()) {
    const google::protobuf::FileDescriptor* file = stack.back();
    stack.pop_back();
    if (!visited.insert(file->name()).second) {
      continue;
    }
    google::protobuf::FileDescriptorProto* file_proto = file_set.add_file();
    file->CopyTo(file_proto);
    file->CopySourceCodeInfoTo(file_proto);
    file->CopyJsonNameTo(file_proto);
    for (int i = file->dependency_count() - 1; i >= 0; --i) {
      stack.push_back(file->dependency(i));
    }
  }
  return file_set;
}

struct ProtoToTypeDefBaselineTestCase {
  const google::protobuf::Descriptor* root_descriptor;
  std::string baseline_file;
};

using ProtoToTypeDefBaselineTest =
    testing::TestWithParam<ProtoToTypeDefBaselineTestCase>;

TEST_P(ProtoToTypeDefBaselineTest, ConvertToYamlBaseline) {
  std::string baseline;
  std::string baseline_file = cel::internal::ResolveRunfilesPath(
      absl::StrCat(kTestTypeDefFilePath, GetParam().baseline_file));
  ASSERT_THAT(cel::internal::GetFileContents(baseline_file, &baseline), IsOk());
  baseline = absl::StripAsciiWhitespace(baseline);

  google::protobuf::FileDescriptorSet file_set =
      BuildTransitiveFileDescriptorSet(GetParam().root_descriptor);
  ASSERT_OK_AND_ASSIGN(TypeDefSet type_def_set,
                       FileDescriptorSetToTypeDefSet(file_set));

  std::ostringstream yaml_out;
  ASSERT_THAT(TypeDefSetToYaml(type_def_set, yaml_out), IsOk());

  std::ostringstream out;
  out << "PROTO DESCRIPTOR: " << GetParam().root_descriptor->full_name()
      << "\n";
  out << kBaselineSeparator;
  out << "TYPEDEF YAML:\n";
  out << yaml_out.str();

  std::string actual(absl::StripAsciiWhitespace(out.str()));
  if (actual != baseline) {
    // Log the actual result to make it easier to copy/paste into the baseline
    // file when updating the tests.
    ABSL_LOG(INFO) << "Actual:\n" << actual;
    EXPECT_EQ(actual, baseline);
  }
}

INSTANTIATE_TEST_SUITE_P(
    TestAllTypes, ProtoToTypeDefBaselineTest,
    testing::ValuesIn({
        ProtoToTypeDefBaselineTestCase{
            .root_descriptor =
                cel::expr::conformance::proto2::TestAllTypes::descriptor(),
            .baseline_file = "proto2_test_all_types_to_typedef.baseline",
        },
        ProtoToTypeDefBaselineTestCase{
            .root_descriptor =
                cel::expr::conformance::proto3::TestAllTypes::descriptor(),
            .baseline_file = "proto3_test_all_types_to_typedef.baseline",
        },
    }));

TEST(ProtoToTypeDefTest, TestProtoDescriptorsBaseline) {
  std::string baseline;
  std::string baseline_file = cel::internal::ResolveRunfilesPath(absl::StrCat(
      kTestTypeDefFilePath, "test_proto_descriptors_to_typedef.baseline"));
  ASSERT_THAT(cel::internal::GetFileContents(baseline_file, &baseline), IsOk());
  baseline = absl::StripAsciiWhitespace(baseline);

  std::string contents;
  std::string test_file = cel::internal::ResolveRunfilesPath(
      absl::StrCat(kTestTypeDefFilePath, "test_proto_descriptors.txtproto"));
  ASSERT_THAT(cel::internal::GetFileContents(test_file, &contents), IsOk());

  google::protobuf::FileDescriptorSet file_set;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(contents, &file_set));

  ASSERT_OK_AND_ASSIGN(TypeDefSet default_set,
                       FileDescriptorSetToTypeDefSet(file_set));

  std::ostringstream default_yaml_out;
  ASSERT_THAT(TypeDefSetToYaml(default_set, default_yaml_out), IsOk());

  types::TypeDefSet default_proto_set;
  ASSERT_THAT(TypeDefSetToProto(default_set, &default_proto_set), IsOk());
  google::protobuf::TextFormat::Printer printer;
  std::string default_textproto;
  ASSERT_TRUE(printer.PrintToString(default_proto_set, &default_textproto));

  ASSERT_OK_AND_ASSIGN(
      TypeDefSet stripped_set,
      FileDescriptorSetToTypeDefSet(
          file_set, {.include_comments = false, .include_defaults = false}));
  std::ostringstream stripped_yaml_out;
  ASSERT_THAT(TypeDefSetToYaml(stripped_set, stripped_yaml_out), IsOk());

  std::ostringstream out;
  out << "SOURCE: test_proto_descriptors.txtproto\n";
  out << kBaselineSeparator;
  out << "TYPEDEF YAML (DEFAULT OPTIONS):\n";
  out << default_yaml_out.str();
  if (!absl::EndsWith(default_yaml_out.str(), "\n")) {
    out << "\n";
  }
  out << kBaselineSeparator;
  out << "TYPEDEF PROTO (DEFAULT OPTIONS):\n";
  out << default_textproto;
  if (!absl::EndsWith(default_textproto, "\n")) {
    out << "\n";
  }
  out << kBaselineSeparator;
  out << "TYPEDEF YAML (NO COMMENTS, NO DEFAULTS):\n";
  out << stripped_yaml_out.str();
  if (!absl::EndsWith(stripped_yaml_out.str(), "\n")) {
    out << "\n";
  }

  std::string actual(absl::StripAsciiWhitespace(out.str()));
  if (actual != baseline) {
    ABSL_LOG(INFO) << "Actual:\n" << actual;
    EXPECT_EQ(actual, baseline);
  }
}

struct ProtoToTypeDefErrorTestCase {
  std::string test_name;
  std::string file_set_textproto;
  std::string expected_error;
};

using ProtoToTypeDefErrorTest =
    testing::TestWithParam<ProtoToTypeDefErrorTestCase>;

TEST_P(ProtoToTypeDefErrorTest, ReturnsInvalidArgument) {
  google::protobuf::FileDescriptorSet file_set;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(GetParam().file_set_textproto,
                                                  &file_set));
  EXPECT_THAT(FileDescriptorSetToTypeDefSet(file_set),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr(GetParam().expected_error)));
}

INSTANTIATE_TEST_SUITE_P(
    ValidationErrors, ProtoToTypeDefErrorTest,
    testing::ValuesIn<ProtoToTypeDefErrorTestCase>({
        {
            .test_name = "EmptyFileName",
            .file_set_textproto = R"pb(
              file {}
            )pb",
            .expected_error = "file descriptor name cannot be empty",
        },
        {
            .test_name = "DuplicateFileName",
            .file_set_textproto = R"pb(
              file { name: "dup.proto" }
              file { name: "dup.proto" }
            )pb",
            .expected_error =
                "duplicate or conflicting file descriptor: 'dup.proto'",
        },
        {
            .test_name = "UnresolvedDependency",
            .file_set_textproto = R"pb(
              file {
                name: "broken.proto"
                dependency: "nonexistent1.proto"
                dependency: "nonexistent2.proto"
              }
            )pb",
            .expected_error =
                "failed to build file descriptor 'broken.proto': "
                "broken.proto: nonexistent1.proto: Import "
                "\"nonexistent1.proto\" was not found or had errors.; "
                "broken.proto: nonexistent2.proto: Import "
                "\"nonexistent2.proto\" was not found or had errors.",
        },
        {
            .test_name = "DuplicateTypeAcrossFiles",
            .file_set_textproto = R"pb(
              file {
                name: "first.proto"
                package: "com.example"
                message_type { name: "Dup" }
              }
              file {
                name: "second.proto"
                package: "com.example"
                message_type { name: "Dup" }
              }
            )pb",
            .expected_error =
                "duplicate or conflicting file descriptor: 'second.proto'",
        },
        {
            .test_name = "DuplicateFieldInMessage",
            .file_set_textproto = R"pb(
              file {
                name: "first.proto"
                package: "com.example"
                message_type { name: "ValidMsg" }
              }
              file {
                name: "second.proto"
                package: "com.example"
                message_type {
                  name: "BrokenMsg"
                  field {
                    name: "dup_field"
                    number: 1
                    label: LABEL_OPTIONAL
                    type: TYPE_INT64
                  }
                  field {
                    name: "dup_field"
                    number: 2
                    label: LABEL_OPTIONAL
                    type: TYPE_STRING
                  }
                }
              }
            )pb",
            .expected_error = "failed to build file descriptor 'second.proto': "
                              "second.proto: com.example.BrokenMsg.dup_field: "
                              "\"dup_field\" is already defined in "
                              "\"com.example.BrokenMsg\".",
        },
    }),
    [](const testing::TestParamInfo<ProtoToTypeDefErrorTestCase>& info) {
      return info.param.test_name;
    });

}  // namespace
}  // namespace cel
