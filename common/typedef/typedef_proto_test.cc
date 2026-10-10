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
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/absl_log.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "common/ast.h"
#include "common/typedef/json_schema.h"
#include "common/typedef/json_schema.pb.h"
#include "common/typedef/json_schema_proto.h"
#include "common/typedef/json_schema_yaml.h"
#include "common/typedef/proto_schema_proto.h"
#include "common/typedef/proto_schema_yaml.h"
#include "common/typedef/schema_proto.h"
#include "common/typedef/schema_yaml.h"
#include "common/typedef/typedef.h"
#include "common/typedef/typedef.pb.h"
#include "common/typedef/typedef_yaml.h"
#include "internal/runfiles.h"
#include "internal/testing.h"
#include "google/protobuf/text_format.h"
#include "google/protobuf/unknown_field_set.h"

namespace cel {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::SizeIs;

constexpr absl::string_view kTestTypeDefFilePath =
"_main/common/typedef/testdata/";

constexpr absl::string_view kBaselineSeparator =
    "--------------------------------------------------------------------\n";

SchemaYamlRegistry MakeYamlRegistry() {
  return SchemaYamlRegistry(std::make_unique<JsonSchemaYaml>(),
                            std::make_unique<Proto2SchemaYaml>(),
                            std::make_unique<Proto3SchemaYaml>());
}

SchemaProtoRegistry MakeProtoRegistry() {
  return SchemaProtoRegistry(std::make_unique<JsonSchemaProto>(),
                             std::make_unique<Proto2SchemaProto>(),
                             std::make_unique<Proto3SchemaProto>());
}

struct YamlToProtoTestCase {
  std::string yaml_source_file;
  std::string baseline_file;
};

using YamlToProtoTest = testing::TestWithParam<YamlToProtoTestCase>;

TEST_P(YamlToProtoTest, Convert) {
  std::string contents;
  std::string test_file = cel::internal::ResolveRunfilesPath(
      absl::StrCat(kTestTypeDefFilePath, GetParam().yaml_source_file));
  ASSERT_THAT(cel::internal::GetFileContents(test_file, &contents), IsOk());

  std::string baseline;
  std::string baseline_file = cel::internal::ResolveRunfilesPath(
      absl::StrCat(kTestTypeDefFilePath, GetParam().baseline_file));
  ASSERT_THAT(cel::internal::GetFileContents(baseline_file, &baseline), IsOk());
  baseline = absl::StripAsciiWhitespace(baseline);

  SchemaYamlRegistry yaml_registry = MakeYamlRegistry();
  SchemaProtoRegistry proto_registry = MakeProtoRegistry();

  ASSERT_OK_AND_ASSIGN(std::vector<TypeDef> type_defs,
                       TypeDefsFromYaml(contents, yaml_registry));
  ASSERT_OK_AND_ASSIGN(types::TypeDefSet proto_set,
                       TypeDefsToProto(type_defs, proto_registry));

  std::string textproto;
  ASSERT_TRUE(google::protobuf::TextFormat::PrintToString(proto_set, &textproto));

  std::ostringstream out;
  out << "YAML SOURCE: " << GetParam().yaml_source_file << "\n";
  out << kBaselineSeparator;
  out << "TYPEDEF PROTO:\n";
  out << textproto;

  std::string actual(absl::StripAsciiWhitespace(out.str()));
  if (actual != baseline) {
    // Log the actual result to make it easier to copy/paste into the baseline
    // file when updating the tests.
    ABSL_LOG(INFO) << "Actual:\n" << actual;
    EXPECT_EQ(actual, baseline);
  }
}

INSTANTIATE_TEST_SUITE_P(
    Formats, YamlToProtoTest,
    testing::ValuesIn({
        YamlToProtoTestCase{
            .yaml_source_file = "typedefs.yaml",
            .baseline_file = "typedefs_yaml_to_proto.baseline",
        },
    }));

struct ProtoToYamlTestCase {
  std::string proto_source_file;
  std::string baseline_file;
};

using ProtoToYamlTest = testing::TestWithParam<ProtoToYamlTestCase>;

TEST_P(ProtoToYamlTest, Convert) {
  std::string contents;
  std::string test_file = cel::internal::ResolveRunfilesPath(
      absl::StrCat(kTestTypeDefFilePath, GetParam().proto_source_file));
  ASSERT_THAT(cel::internal::GetFileContents(test_file, &contents), IsOk());

  std::string baseline;
  std::string baseline_file = cel::internal::ResolveRunfilesPath(
      absl::StrCat(kTestTypeDefFilePath, GetParam().baseline_file));
  ASSERT_THAT(cel::internal::GetFileContents(baseline_file, &baseline), IsOk());
  baseline = absl::StripAsciiWhitespace(baseline);

  SchemaYamlRegistry yaml_registry = MakeYamlRegistry();
  SchemaProtoRegistry proto_registry = MakeProtoRegistry();

  types::TypeDefSet proto_set;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(contents, &proto_set));
  ASSERT_OK_AND_ASSIGN(std::vector<TypeDef> type_defs,
                       TypeDefsFromProto(proto_set, proto_registry));

  std::ostringstream out;
  out << "PROTO SOURCE: " << GetParam().proto_source_file << "\n";
  out << kBaselineSeparator;
  out << "TYPEDEF YAML:\n";
  ASSERT_THAT(TypeDefsToYaml(type_defs, yaml_registry, out), IsOk());

  std::string actual(absl::StripAsciiWhitespace(out.str()));
  if (actual != baseline) {
    // Log the actual result to make it easier to copy/paste into the baseline
    // file when updating the tests.
    ABSL_LOG(INFO) << "Actual:\n" << actual;
    EXPECT_EQ(actual, baseline);
  }
}

INSTANTIATE_TEST_SUITE_P(
    Formats, ProtoToYamlTest,
    testing::ValuesIn({
        ProtoToYamlTestCase{
            .proto_source_file = "typedefs.txtproto",
            .baseline_file = "typedefs_proto_to_yaml.baseline",
        },
    }));

TEST(TypeDefProtoTest, DirectObjectAndEnumOverloads) {
  ObjectTypeDef obj;
  obj.name = "com.example.DirectObj";
  types::Object obj_proto;
  ASSERT_THAT(ObjectTypeDefToProto(obj, &obj_proto), IsOk());
  ASSERT_OK_AND_ASSIGN(ObjectTypeDef decoded_obj,
                       ObjectTypeDefFromProto(obj_proto));
  EXPECT_EQ(decoded_obj.name, "com.example.DirectObj");

  EnumTypeDef enm;
  enm.name = "com.example.DirectEnum";
  ASSERT_THAT(enm.AddConstant(EnumTypeDef::EnumConstant{
                  .name = "NEG_ONE",
                  .id = -1,
              }),
              IsOk());
  ASSERT_OK_AND_ASSIGN(types::Enum enum_proto, EnumTypeDefToProto(enm));
  ASSERT_OK_AND_ASSIGN(EnumTypeDef decoded_enum,
                       EnumTypeDefFromProto(enum_proto));
  EXPECT_EQ(decoded_enum.name, "com.example.DirectEnum");
  ASSERT_THAT(decoded_enum.constants, SizeIs(1));
  EXPECT_EQ(decoded_enum.constants[0].name, "NEG_ONE");
  EXPECT_EQ(decoded_enum.constants[0].id, -1);
}

TEST(TypeDefProtoTest, ValidationAndErrorCases) {
  // Empty cel.types.TypeDef (neither object_type nor enum_type set).
  types::TypeDef empty_td_proto;
  EXPECT_THAT(TypeDefFromProto(empty_td_proto),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("cel.types.TypeDef must specify "
                                 "'object_type' or 'enum_type'")));

  // Duplicate field names on cel.types.Object.
  types::Object dup_fields_proto;
  dup_fields_proto.set_name("com.example.DupFields");
  dup_fields_proto.add_fields()->set_name("dup");
  dup_fields_proto.add_fields()->set_name("dup");
  EXPECT_THAT(ObjectTypeDefFromProto(dup_fields_proto),
              StatusIs(absl::StatusCode::kAlreadyExists,
                       HasSubstr("Field 'dup' is already defined.")));

  // Duplicate enum constant names on cel.types.Enum.
  types::Enum dup_constants_proto;
  dup_constants_proto.set_name("com.example.DupConstants");
  dup_constants_proto.add_constants()->set_name("DUP_VAL");
  dup_constants_proto.add_constants()->set_name("DUP_VAL");
  EXPECT_THAT(
      EnumTypeDefFromProto(dup_constants_proto),
      StatusIs(absl::StatusCode::kAlreadyExists,
               HasSubstr("Enum constant 'DUP_VAL' is already defined.")));

  // Unsupported schema during encoding (empty registry).
  ObjectTypeDef obj_with_unregistered;
  obj_with_unregistered.name = "com.example.Obj";
  obj_with_unregistered.schema_specific_properties["json"] =
      std::make_unique<JsonSchemaObjectProperties>();
  EXPECT_THAT(ObjectTypeDefToProto(obj_with_unregistered),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       Eq("Unsupported schema: 'json'")));

  ObjectTypeDef obj_field_unregistered;
  obj_field_unregistered.name = "com.example.Obj";
  ObjectTypeDef::Field f{.name = "f", .type = TypeSpec(PrimitiveType::kString)};
  f.schema_specific_properties["json"] =
      std::make_unique<JsonSchemaFieldProperties>();
  ASSERT_THAT(obj_field_unregistered.AddField(std::move(f)), IsOk());
  EXPECT_THAT(ObjectTypeDefToProto(obj_field_unregistered),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       Eq("Unsupported schema: 'json'")));

  EnumTypeDef enum_with_unregistered;
  enum_with_unregistered.name = "com.example.Enum";
  enum_with_unregistered.schema_specific_properties["json"] =
      std::make_unique<JsonSchemaEnumProperties>();
  EXPECT_THAT(EnumTypeDefToProto(enum_with_unregistered),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       Eq("Unsupported schema: 'json'")));

  EnumTypeDef enum_const_unregistered;
  enum_const_unregistered.name = "com.example.Enum";
  EnumTypeDef::EnumConstant ec{.name = "C", .id = 0};
  ec.schema_specific_properties["json"] =
      std::make_unique<JsonSchemaEnumConstantProperties>();
  ASSERT_THAT(enum_const_unregistered.AddConstant(std::move(ec)), IsOk());
  EXPECT_THAT(EnumTypeDefToProto(enum_const_unregistered),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       Eq("Unsupported schema: 'json'")));

  // Unregistered extension during decoding (empty registry).
  types::Object obj_ext_proto;
  obj_ext_proto.set_name("com.example.Obj");
  obj_ext_proto.MutableExtension(types::json_object_schema)->set_name("x");
  EXPECT_THAT(
      ObjectTypeDefFromProto(obj_ext_proto),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Unsupported schema extension on cel.types.Object")));

  types::Object field_ext_proto;
  field_ext_proto.set_name("com.example.Obj");
  auto* f_proto = field_ext_proto.add_fields();
  f_proto->set_name("f");
  f_proto->MutableExtension(types::json_field_schema)->set_name("x");
  EXPECT_THAT(ObjectTypeDefFromProto(field_ext_proto),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Unsupported schema extension on "
                                 "cel.types.Object.Field")));

  types::Enum enum_ext_proto;
  enum_ext_proto.set_name("com.example.Enum");
  enum_ext_proto.MutableExtension(types::json_enum_schema)->set_style("string");
  EXPECT_THAT(
      EnumTypeDefFromProto(enum_ext_proto),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Unsupported schema extension on cel.types.Enum")));

  types::Enum const_ext_proto;
  const_ext_proto.set_name("com.example.Enum");
  auto* c_proto = const_ext_proto.add_constants();
  c_proto->set_name("C");
  c_proto->MutableExtension(types::json_enum_constant_schema)->set_name("x");
  EXPECT_THAT(EnumTypeDefFromProto(const_ext_proto),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Unsupported schema extension on "
                                 "cel.types.Enum.EnumConstant")));

  // Unknown fields on messages are ignored.
  types::TypeDefSet unknown_set;
  unknown_set.GetReflection()
      ->MutableUnknownFields(&unknown_set)
      ->AddVarint(9999, 1);
  EXPECT_THAT(TypeDefsFromProto(unknown_set), IsOk());

  types::TypeDef unknown_td;
  unknown_td.mutable_object_type()->set_name("com.example.Obj");
  unknown_td.GetReflection()
      ->MutableUnknownFields(&unknown_td)
      ->AddVarint(9999, 1);
  EXPECT_THAT(TypeDefFromProto(unknown_td), IsOk());

  types::Object unknown_obj;
  unknown_obj.set_name("com.example.Obj");
  unknown_obj.GetReflection()
      ->MutableUnknownFields(&unknown_obj)
      ->AddVarint(9999, 1);
  EXPECT_THAT(ObjectTypeDefFromProto(unknown_obj), IsOk());
}

}  // namespace
}  // namespace cel
