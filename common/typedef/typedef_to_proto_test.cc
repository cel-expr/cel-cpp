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

#include "common/typedef/typedef_to_proto.h"

#include <sstream>
#include <string>
#include <utility>

#include "google/protobuf/descriptor.pb.h"
#include "absl/log/absl_log.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "common/ast.h"
#include "common/typedef/proto_to_typedef.h"
#include "common/typedef/proto_type_helpers.h"
#include "common/typedef/typedef.h"
#include "common/typedef/typedef_yaml.h"
#include "internal/runfiles.h"
#include "internal/testing.h"
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

TEST(TypeDefToProtoTest, TypeDefToProtoBaseline) {
  std::string baseline;
  std::string baseline_file = cel::internal::ResolveRunfilesPath(
      absl::StrCat(kTestTypeDefFilePath, "typedef_to_proto.baseline"));
  ASSERT_THAT(cel::internal::GetFileContents(baseline_file, &baseline), IsOk());
  baseline = absl::StripAsciiWhitespace(baseline);

  std::string contents;
  std::string test_file = cel::internal::ResolveRunfilesPath(
      absl::StrCat(kTestTypeDefFilePath, "typedef_to_proto.yaml"));
  ASSERT_THAT(cel::internal::GetFileContents(test_file, &contents), IsOk());

  ASSERT_OK_AND_ASSIGN(TypeDefSet set, TypeDefSetFromYaml(contents));

  ASSERT_OK_AND_ASSIGN(google::protobuf::FileDescriptorSet fds,
                       TypeDefSetToFileDescriptorSet(
                           set, {.include_well_known_type_files = false}));

  // Verify that the generated FileDescriptorSet builds cleanly in a descriptor
  // pool.
  EXPECT_THAT(typedef_internal::ValidateFileDescriptorSet(fds), IsOk());

  // Verify with include_well_known_type_files = true includes all referenced
  // well-known .proto files.
  ASSERT_OK_AND_ASSIGN(google::protobuf::FileDescriptorSet fds_with_wkt,
                       TypeDefSetToFileDescriptorSet(
                           set, {.include_well_known_type_files = true}));
  EXPECT_EQ(fds_with_wkt.file_size(), 9);
  EXPECT_THAT(typedef_internal::ValidateFileDescriptorSet(fds_with_wkt),
              IsOk());

  // Verify round-tripping through FileDescriptorSetToTypeDefSet (which uses
  // MessageTypeSpec for message and enum fields) and back to FileDescriptorSet.
  ASSERT_OK_AND_ASSIGN(TypeDefSet roundtrip_set,
                       FileDescriptorSetToTypeDefSet(fds_with_wkt));
  ASSERT_OK_AND_ASSIGN(google::protobuf::FileDescriptorSet roundtrip_fds,
                       TypeDefSetToFileDescriptorSet(roundtrip_set));
  EXPECT_THAT(typedef_internal::ValidateFileDescriptorSet(roundtrip_fds),
              IsOk());

  std::string textproto;
  ASSERT_TRUE(google::protobuf::TextFormat::PrintToString(fds, &textproto));

  std::ostringstream out;
  out << "SOURCE: typedef_to_proto.yaml\n";
  out << kBaselineSeparator;
  out << "FILE DESCRIPTOR SET PROTO:\n";
  out << textproto;
  if (!absl::EndsWith(textproto, "\n")) {
    out << "\n";
  }

  std::string actual(absl::StripAsciiWhitespace(out.str()));
  if (actual != baseline) {
    ABSL_LOG(INFO) << "Actual:\n" << actual;
    EXPECT_EQ(actual, baseline);
  }

  TypeDefSet empty_set;
  ASSERT_OK_AND_ASSIGN(google::protobuf::FileDescriptorSet empty_fds,
                       TypeDefSetToFileDescriptorSet(empty_set));
  EXPECT_EQ(empty_fds.file_size(), 0);
}

struct TypeDefToProtoErrorTestCase {
  std::string test_name;
  std::string typedef_yaml;
  std::string expected_error;
};

using TypeDefToProtoErrorTest =
    testing::TestWithParam<TypeDefToProtoErrorTestCase>;

TEST_P(TypeDefToProtoErrorTest, ReturnsInvalidArgument) {
  ASSERT_OK_AND_ASSIGN(TypeDefSet set,
                       TypeDefSetFromYaml(GetParam().typedef_yaml));
  EXPECT_THAT(TypeDefSetToFileDescriptorSet(set),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr(GetParam().expected_error)));
}

INSTANTIATE_TEST_SUITE_P(
    ValidationErrors, TypeDefToProtoErrorTest,
    testing::ValuesIn<TypeDefToProtoErrorTestCase>({
        {
            .test_name = "UnknownReferencedType",
            .typedef_yaml = R"yaml(
              - struct:
                  name: "com.example.Bad"
                  fields:
                    - name: "ref"
                      type: "com.example.Missing"
            )yaml",
            .expected_error =
                "Unknown referenced protobuf type 'com.example.Missing'",
        },
        {
            .test_name = "ConflictingPackagesInSameFile",
            .typedef_yaml = R"yaml(
              - struct:
                  name: "com.example.Foo"
                  aspects:
                    proto:
                      file_name: "shared.proto"
              - struct:
                  name: "com.other.Bar"
                  aspects:
                    proto:
                      file_name: "shared.proto"
            )yaml",
            .expected_error =
                "Conflicting packages 'com.example' and 'com.other'",
        },
        {
            .test_name = "DuplicateOneofNamesAcrossOneofGroups",
            .typedef_yaml = R"yaml(
              - struct:
                  name: "com.example.ConflictOneof"
                  fields:
                    - one_of_fields:
                        - name: "a"
                          type: "int"
                      aspects:
                        proto:
                          name: "dup_choice"
                    - one_of_fields:
                        - name: "b"
                          type: "string"
                      aspects:
                        proto:
                          name: "dup_choice"
            )yaml",
            .expected_error = "Duplicate proto oneof name 'dup_choice'",
        },
        {
            .test_name = "RepeatedFieldInOneofGroup",
            .typedef_yaml = R"yaml(
              - struct:
                  name: "com.example.RepeatedInOneof"
                  fields:
                    - one_of_fields:
                        - name: "items"
                          type: "list<int>"
            )yaml",
            .expected_error = "cannot be a repeated field",
        },
        {
            .test_name = "NonStringFileName",
            .typedef_yaml = R"yaml(
              - struct:
                  name: "com.example.BadFile"
                  aspects:
                    proto:
                      file_name: 123
            )yaml",
            .expected_error =
                "Aspect property 'proto.file_name' must be a string",
        },
        {
            .test_name = "EmptyProtoName",
            .typedef_yaml = R"yaml(
              - struct:
                  name: "com.example.EmptyName"
                  aspects:
                    proto:
                      name: ""
            )yaml",
            .expected_error = "Aspect property 'proto.name' cannot be empty",
        },
        {
            .test_name = "NonIntFieldId",
            .typedef_yaml = R"yaml(
              - struct:
                  name: "com.example.BadId"
                  fields:
                    - name: "x"
                      type: "int"
                      aspects:
                        proto:
                          id: "1"
            )yaml",
            .expected_error = "Aspect property 'proto.id' must be an int",
        },
        {
            .test_name = "NonPositiveFieldId",
            .typedef_yaml = R"yaml(
              - struct:
                  name: "com.example.ZeroId"
                  fields:
                    - name: "x"
                      type: "int"
                      aspects:
                        proto:
                          id: 0
            )yaml",
            .expected_error =
                "Aspect property 'proto.id' must be a positive 32-bit integer",
        },
        {
            .test_name = "OverflowFieldId",
            .typedef_yaml = R"yaml(
              - struct:
                  name: "com.example.HugeId"
                  fields:
                    - name: "x"
                      type: "int"
                      aspects:
                        proto:
                          id: 3000000000
            )yaml",
            .expected_error =
                "Aspect property 'proto.id' must be a positive 32-bit integer",
        },
        {
            .test_name = "NonBoolAllowAlias",
            .typedef_yaml = R"yaml(
              - enum:
                  name: "com.example.BadEnum"
                  aspects:
                    proto:
                      allow_alias: "true"
                  constants:
                    - name: "UNSPECIFIED"
                      id: 0
            )yaml",
            .expected_error =
                "Aspect property 'proto.allow_alias' must be a bool",
        },
        {
            .test_name = "EmptyOneofName",
            .typedef_yaml = R"yaml(
              - struct:
                  name: "com.example.EmptyOneof"
                  fields:
                    - one_of_fields:
                        - name: "x"
                          type: "int"
                      aspects:
                        proto:
                          name: ""
            )yaml",
            .expected_error = "Aspect property 'proto.name' cannot be empty",
        },
        {
            .test_name = "MapFieldInOneofGroup",
            .typedef_yaml = R"yaml(
              - struct:
                  name: "com.example.MapInOneof"
                  fields:
                    - one_of_fields:
                        - name: "entries"
                          type: "map<string, int>"
            )yaml",
            .expected_error = "cannot be a map field",
        },
        {
            .test_name = "InvalidMapProtoTypeOverride",
            .typedef_yaml = R"yaml(
              - struct:
                  name: "com.example.BadMapOverride"
                  fields:
                    - name: "entries"
                      type: "map<string, int>"
                      aspects:
                        proto:
                          type: "map<string>"
            )yaml",
            .expected_error =
                "Invalid protobuf map type override: 'map<string>'",
        },
        {
            .test_name = "CyclicFileDependency",
            .typedef_yaml = R"yaml(
              - struct:
                  name: "com.example.First"
                  aspects:
                    proto:
                      file_name: "first.proto"
                  fields:
                    - name: "second"
                      type: "com.example.Second"
              - struct:
                  name: "com.example.Second"
                  aspects:
                    proto:
                      file_name: "second.proto"
                  fields:
                    - name: "first"
                      type: "com.example.First"
            )yaml",
            .expected_error = "Cyclic file dependency involving",
        },
        {
            .test_name = "DuplicateProtoTypeName",
            .typedef_yaml = R"yaml(
              - struct:
                  name: "com.example.First"
                  aspects:
                    proto:
                      name: "SharedProtoName"
                      file_name: "first.proto"
              - struct:
                  name: "com.example.Second"
                  aspects:
                    proto:
                      name: "SharedProtoName"
                      file_name: "second.proto"
            )yaml",
            .expected_error =
                "Duplicate protobuf type name 'com.example.SharedProtoName'",
        },
        {
            .test_name = "Int32DefaultOverflow",
            .typedef_yaml = R"yaml(
              - struct:
                  name: "com.example.BadInt32Default"
                  fields:
                    - name: "x"
                      type: "int"
                      default: 3000000000
                      aspects:
                        proto:
                          type: "int32"
            )yaml",
            .expected_error = "out of range for 32-bit signed integer field",
        },
        {
            .test_name = "Uint32DefaultOverflow",
            .typedef_yaml = R"yaml(
              - struct:
                  name: "com.example.BadUint32Default"
                  fields:
                    - name: "x"
                      type: "uint"
                      default: 5000000000
                      aspects:
                        proto:
                          type: "uint32"
            )yaml",
            .expected_error = "out of range for 32-bit unsigned integer field",
        },
        {
            .test_name = "FloatDefaultOverflow",
            .typedef_yaml = R"yaml(
              - struct:
                  name: "com.example.BadFloatDefault"
                  fields:
                    - name: "x"
                      type: "double"
                      default: 1e100
                      aspects:
                        proto:
                          type: "float"
            )yaml",
            .expected_error = "out of range for float field",
        },
        {
            .test_name = "BoolDefaultTypeMismatch",
            .typedef_yaml = R"yaml(
              - struct:
                  name: "com.example.BadBoolDefault"
                  fields:
                    - name: "x"
                      type: "int"
                      default: 1
                      aspects:
                        proto:
                          type: "bool"
            )yaml",
            .expected_error = "Expected bool default value for bool field",
        },
        {
            .test_name = "IntDefaultTypeMismatch",
            .typedef_yaml = R"yaml(
              - struct:
                  name: "com.example.BadIntDefault"
                  fields:
                    - name: "x"
                      type: "string"
                      default: "abc"
                      aspects:
                        proto:
                          type: "int32"
            )yaml",
            .expected_error = "Expected int default value for integer field",
        },
        {
            .test_name = "UintDefaultTypeMismatch",
            .typedef_yaml = R"yaml(
              - struct:
                  name: "com.example.BadUintDefault"
                  fields:
                    - name: "x"
                      type: "string"
                      default: "abc"
                      aspects:
                        proto:
                          type: "uint32"
            )yaml",
            .expected_error =
                "Expected uint default value for unsigned integer field",
        },
        {
            .test_name = "FloatDefaultTypeMismatch",
            .typedef_yaml = R"yaml(
              - struct:
                  name: "com.example.BadFloatDefaultType"
                  fields:
                    - name: "x"
                      type: "string"
                      default: "abc"
                      aspects:
                        proto:
                          type: "double"
            )yaml",
            .expected_error =
                "Expected double default value for floating-point field",
        },
        {
            .test_name = "StringDefaultTypeMismatch",
            .typedef_yaml = R"yaml(
              - struct:
                  name: "com.example.BadStringDefault"
                  fields:
                    - name: "x"
                      type: "int"
                      default: 1
                      aspects:
                        proto:
                          type: "string"
            )yaml",
            .expected_error =
                "Expected string default value for string or enum field",
        },
        {
            .test_name = "BytesDefaultTypeMismatch",
            .typedef_yaml = R"yaml(
              - struct:
                  name: "com.example.BadBytesDefault"
                  fields:
                    - name: "x"
                      type: "int"
                      default: 1
                      aspects:
                        proto:
                          type: "bytes"
            )yaml",
            .expected_error = "Expected bytes default value for bytes field",
        },
        {
            .test_name = "MessageDefaultNotSupported",
            .typedef_yaml = R"yaml(
              - struct:
                  name: "com.example.BadMessageDefault"
                  fields:
                    - name: "x"
                      type: "int"
                      default: 1
                      aspects:
                        proto:
                          type: "google.protobuf.Timestamp"
            )yaml",
            .expected_error =
                "Scalar Constant default value is only supported on primitive "
                "and enum fields",
        },
        {
            .test_name = "NestedListField",
            .typedef_yaml = R"yaml(
              - struct:
                  name: "com.example.NestedList"
                  fields:
                    - name: "matrix"
                      type: "list<list<int>>"
            )yaml",
            .expected_error =
                "Nested list type other than list<dyn> is not representable",
        },
        {
            .test_name = "NestedMapField",
            .typedef_yaml = R"yaml(
              - struct:
                  name: "com.example.NestedMap"
                  fields:
                    - name: "nested"
                      type: "map<string, map<string, int>>"
            )yaml",
            .expected_error =
                "Nested map type other than map<string, dyn> is not "
                "representable",
        },
        {
            .test_name = "UnsupportedTypeSpecField",
            .typedef_yaml = R"yaml(
              - struct:
                  name: "com.example.GenericField"
                  fields:
                    - name: "custom"
                      type: "CustomGeneric<int>"
            )yaml",
            .expected_error =
                "Unsupported TypeSpec for protobuf field conversion",
        },
        {
            .test_name = "MapEntryNameCollision",
            .typedef_yaml = R"yaml(
              - struct:
                  name: "com.example.CollideMap"
                  fields:
                    - name: "items"
                      type: "map<string, int>"
              - struct:
                  name: "com.example.CollideMap.ItemsEntry"
                  fields:
                    - name: "x"
                      type: "int"
            )yaml",
            .expected_error =
                "Map entry type 'ItemsEntry' for field 'items' collides with a "
                "nested type",
        },
    }),
    [](const testing::TestParamInfo<TypeDefToProtoErrorTestCase>& info) {
      return info.param.test_name;
    });

TEST(TypeDefToProtoTest, AutoFieldNumberSkipsReservedRange) {
  constexpr int kFieldCount = google::protobuf::FieldDescriptor::kFirstReservedNumber;
  StructTypeDef struct_def("com.example.LargeStruct");
  for (int i = 0; i < kFieldCount; ++i) {
    ASSERT_THAT(struct_def.AddField(StructTypeDef::Field(
                    absl::StrCat("f_", i), TypeSpec(PrimitiveType::kInt64))),
                IsOk());
  }
  TypeDefSet set;
  ASSERT_THAT(set.AddTypeDef(TypeDef(std::move(struct_def))), IsOk());
  ASSERT_OK_AND_ASSIGN(google::protobuf::FileDescriptorSet fds,
                       TypeDefSetToFileDescriptorSet(set));
  ASSERT_EQ(fds.file_size(), 1);
  ASSERT_EQ(fds.file(0).message_type_size(), 1);
  const google::protobuf::DescriptorProto& msg = fds.file(0).message_type(0);
  ASSERT_EQ(msg.field_size(), kFieldCount);
  EXPECT_EQ(msg.field(kFieldCount - 2).number(),
            google::protobuf::FieldDescriptor::kFirstReservedNumber - 1);
  EXPECT_EQ(msg.field(kFieldCount - 1).number(),
            google::protobuf::FieldDescriptor::kLastReservedNumber + 1);
}

TEST(TypeDefToProtoTest, ProtoSyntaxProto2VsProto3) {
  absl::string_view yaml_with_defaults = R"yaml(
    - enum:
        name: "com.example.Status"
        constants:
          - name: "UNSPECIFIED"
            id: 0
          - name: "ACTIVE"
            id: 1
    - struct:
        name: "com.example.Config"
        fields:
          - name: "retries"
            type: "int"
            default: 3
          - name: "status"
            type: "com.example.Status"
            default: "ACTIVE"
  )yaml";
  ASSERT_OK_AND_ASSIGN(TypeDefSet set, TypeDefSetFromYaml(yaml_with_defaults));

  // Default options (kProto2) emits syntax = "proto2" and field defaults.
  ASSERT_OK_AND_ASSIGN(
      google::protobuf::FileDescriptorSet proto2_fds,
      TypeDefSetToFileDescriptorSet(set, {.syntax = ProtoSyntax::kProto2}));
  ASSERT_EQ(proto2_fds.file_size(), 1);
  EXPECT_EQ(proto2_fds.file(0).syntax(), "proto2");
  ASSERT_EQ(proto2_fds.file(0).message_type_size(), 1);
  EXPECT_EQ(proto2_fds.file(0).message_type(0).field(0).default_value(), "3");
  EXPECT_EQ(proto2_fds.file(0).message_type(0).field(1).default_value(),
            "ACTIVE");

  // kProto3 emits syntax = "proto3" and ignores field defaults so the
  // resulting FileDescriptorSet is valid in DescriptorPool.
  ASSERT_OK_AND_ASSIGN(
      google::protobuf::FileDescriptorSet proto3_fds,
      TypeDefSetToFileDescriptorSet(set, {.syntax = ProtoSyntax::kProto3}));
  ASSERT_EQ(proto3_fds.file_size(), 1);
  EXPECT_EQ(proto3_fds.file(0).syntax(), "proto3");
  ASSERT_EQ(proto3_fds.file(0).message_type_size(), 1);
  EXPECT_FALSE(proto3_fds.file(0).message_type(0).field(0).has_default_value());
  EXPECT_FALSE(proto3_fds.file(0).message_type(0).field(1).has_default_value());

  // Non-zero first enum constant is valid in kProto2 and rejected in kProto3.
  absl::string_view nonzero_enum_yaml = R"yaml(
    - enum:
        name: "com.example.NonZeroEnum"
        constants:
          - name: "FIRST"
            id: 1
  )yaml";
  ASSERT_OK_AND_ASSIGN(TypeDefSet nz_set,
                       TypeDefSetFromYaml(nonzero_enum_yaml));
  EXPECT_THAT(
      TypeDefSetToFileDescriptorSet(nz_set, {.syntax = ProtoSyntax::kProto2}),
      IsOk());
  EXPECT_THAT(
      TypeDefSetToFileDescriptorSet(nz_set, {.syntax = ProtoSyntax::kProto3}),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

}  // namespace
}  // namespace cel
