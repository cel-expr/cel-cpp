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

#include "common/typedef/typedef_proto.h"

#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "common/typedef/aspect_proto.h"
#include "common/typedef/proto_schema_proto.h"
#include "common/typedef/typedef.h"
#include "common/typedef/typedef.pb.h"
#include "internal/proto_matchers.h"
#include "internal/runfiles.h"
#include "internal/testing.h"
#include "google/protobuf/text_format.h"

namespace cel {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::cel::internal::test::EqualsProto;
using ::testing::HasSubstr;

constexpr absl::string_view kTestTypeDefFilePath =
"_main/common/typedef/testdata/";

AspectProtoRegistry MakeProtoRegistry() {
  return AspectProtoRegistry(std::make_unique<Proto2SchemaProto>(),
                             std::make_unique<Proto3SchemaProto>());
}

TEST(TypeDefProtoTest, TextProtoRoundTrip) {
  std::string contents;
  std::string test_file = cel::internal::ResolveRunfilesPath(
      absl::StrCat(kTestTypeDefFilePath, "typedefs.txtproto"));
  ASSERT_THAT(cel::internal::GetFileContents(test_file, &contents), IsOk());

  AspectProtoRegistry proto_registry = MakeProtoRegistry();

  types::TypeDefSet proto_set;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(contents, &proto_set));
  ASSERT_OK_AND_ASSIGN(std::vector<TypeDef> type_defs,
                       TypeDefsFromProto(proto_set, proto_registry));
  ASSERT_OK_AND_ASSIGN(types::TypeDefSet roundtrip_proto_set,
                       TypeDefsToProto(type_defs, proto_registry));

  std::string expected_text, roundtrip_text;
  ASSERT_TRUE(google::protobuf::TextFormat::PrintToString(proto_set, &expected_text));
  ASSERT_TRUE(
      google::protobuf::TextFormat::PrintToString(roundtrip_proto_set, &roundtrip_text));
  EXPECT_EQ(roundtrip_text, expected_text);
}

struct TypeDefProtoTestCase {
  std::string textproto;
  absl::StatusCode expected_code = absl::StatusCode::kOk;
  std::string expected_error;
  std::string expected_typedef;
};

class TypeDefProtoTest : public testing::TestWithParam<TypeDefProtoTestCase> {};

TEST_P(TypeDefProtoTest, TypeDefProto) {
  const TypeDefProtoTestCase& param = GetParam();
  types::TypeDef proto;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(param.textproto, &proto));
  absl::StatusOr<TypeDef> type_def =
      TypeDefFromProto(proto, MakeProtoRegistry());
  ASSERT_THAT(type_def,
              StatusIs(param.expected_code, HasSubstr(param.expected_error)));
  if (type_def.ok()) {
    types::TypeDef expected_proto;
    ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(param.expected_typedef,
                                                    &expected_proto));
    EXPECT_THAT(TypeDefToProto(*type_def),
                IsOkAndHolds(EqualsProto(expected_proto)));
    if (type_def->is_struct()) {
      EXPECT_THAT(StructTypeDefToProto(type_def->struct_type()),
                  IsOkAndHolds(EqualsProto(expected_proto.struct_type())));
    } else {
      EXPECT_THAT(EnumTypeDefToProto(type_def->enum_type()),
                  IsOkAndHolds(EqualsProto(expected_proto.enum_type())));
    }
  }
}

INSTANTIATE_TEST_SUITE_P(
    TypeDefProtoTest, TypeDefProtoTest,
    ::testing::Values(
        TypeDefProtoTestCase{
            .textproto = "",
            .expected_code = absl::StatusCode::kInvalidArgument,
            .expected_error =
                "cel.types.TypeDef must specify 'struct_type' or 'enum_type'",
        },
        TypeDefProtoTestCase{
            .textproto = R"pb(
              struct_type {
                name: "com.example.SpecialTypes"
                fields {
                  name: "err_field"
                  type { error {} }
                }
                fields {
                  name: "unspec_prim_field"
                  type { primitive: PRIMITIVE_TYPE_UNSPECIFIED }
                }
                fields {
                  name: "unspec_wk_field"
                  type { well_known: WELL_KNOWN_TYPE_UNSPECIFIED }
                }
                fields {
                  name: "unset_type_field"
                  type {}
                }
              }
            )pb",
            .expected_typedef = R"pb(
              struct_type {
                name: "com.example.SpecialTypes"
                fields {
                  name: "err_field"
                  type { error {} }
                }
                fields {
                  name: "unspec_prim_field"
                  type { primitive: PRIMITIVE_TYPE_UNSPECIFIED }
                }
                fields {
                  name: "unspec_wk_field"
                  type { well_known: WELL_KNOWN_TYPE_UNSPECIFIED }
                }
                fields { name: "unset_type_field" }
              }
            )pb",
        },
        TypeDefProtoTestCase{
            .textproto = R"pb(
              struct_type {
                name: "com.example.DupFields"
                fields { name: "dup" }
                fields { name: "dup" }
              }
            )pb",
            .expected_code = absl::StatusCode::kAlreadyExists,
            .expected_error = "Field 'dup' is already defined.",
        },
        TypeDefProtoTestCase{
            .textproto = R"pb(
              enum_type {
                name: "com.example.DupConstants"
                constants { name: "DUP_VAL" }
                constants { name: "DUP_VAL" }
              }
            )pb",
            .expected_code = absl::StatusCode::kAlreadyExists,
            .expected_error = "Enum constant 'DUP_VAL' is already defined.",
        },
        // Aspects for an unregistered aspect on
        // cel.types.Struct are ignored.
        TypeDefProtoTestCase{
            .textproto = R"pb(
              struct_type {
                name: "com.example.Struct"
                [cel.types.proto2_struct_aspect] { name: "x" }
              }
            )pb",
            .expected_typedef = R"pb(
              struct_type { name: "com.example.Struct" }
            )pb",
        },
        // Aspects for an unregistered aspect on
        // cel.types.Struct.Field are ignored.
        TypeDefProtoTestCase{
            .textproto = R"pb(
              struct_type {
                name: "com.example.Struct"
                fields {
                  name: "f"
                  [cel.types.proto2_field_aspect] { name: "x" }
                }
              }
            )pb",
            .expected_typedef = R"pb(
              struct_type {
                name: "com.example.Struct"
                fields { name: "f" }
              }
            )pb",
        },
        // Aspects for an unregistered aspect on
        // cel.types.Enum are ignored.
        TypeDefProtoTestCase{
            .textproto = R"pb(
              enum_type {
                name: "com.example.Enum"
                [cel.types.proto2_enum_aspect] { name: "x" }
              }
            )pb",
            .expected_typedef = R"pb(
              enum_type { name: "com.example.Enum" }
            )pb",
        },
        // Aspects for an unregistered aspect on
        // cel.types.Enum.EnumConstant are ignored.
        TypeDefProtoTestCase{
            .textproto = R"pb(
              enum_type {
                name: "com.example.Enum"
                constants {
                  name: "C"
                  [cel.types.proto2_enum_constant_aspect] { name: "x" }
                }
              }
            )pb",
            .expected_typedef = R"pb(
              enum_type {
                name: "com.example.Enum"
                constants { name: "C" id: 0 }
              }
            )pb",
        }));

}  // namespace
}  // namespace cel
