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

#include "common/typedef/proto_schema_yaml.h"

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "common/typedef/proto_schema.h"
#include "common/typedef/schema.h"
#include "common/typedef/schema_yaml.h"
#include "common/typedef/typedef.h"
#include "common/typedef/typedef_yaml.h"
#include "common/typedef/yaml_helpers.h"
#include "common/typedef/yaml_test_helpers.h"
#include "internal/status_macros.h"
#include "internal/testing.h"
#include "yaml-cpp/emitter.h"
#include "yaml-cpp/node/node.h"
#include "yaml-cpp/node/parse.h"

namespace cel {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::HasSubstr;
using ::testing::NotNull;
using ::testing::Optional;

absl::StatusOr<std::string> RoundTripObject(const SchemaYaml& schema,
                                            absl::string_view yaml) {
  CEL_ASSIGN_OR_RETURN(YAML::Node node, internal::LoadYaml(yaml));
  CEL_ASSIGN_OR_RETURN(auto props, schema.DecodeObjectProperties(yaml, node));
  std::stringstream ss;
  YAML::Emitter out(ss);
  out.SetIndent(2);
  CEL_RETURN_IF_ERROR(schema.EncodeObjectProperties(*props, out));
  return ss.str();
}

absl::StatusOr<std::string> RoundTripField(const SchemaYaml& schema,
                                           absl::string_view yaml) {
  CEL_ASSIGN_OR_RETURN(YAML::Node node, internal::LoadYaml(yaml));
  CEL_ASSIGN_OR_RETURN(auto props, schema.DecodeFieldProperties(yaml, node));
  std::stringstream ss;
  YAML::Emitter out(ss);
  out.SetIndent(2);
  CEL_RETURN_IF_ERROR(schema.EncodeFieldProperties(*props, out));
  return ss.str();
}

absl::StatusOr<std::string> RoundTripEnum(const SchemaYaml& schema,
                                          absl::string_view yaml) {
  CEL_ASSIGN_OR_RETURN(YAML::Node node, internal::LoadYaml(yaml));
  CEL_ASSIGN_OR_RETURN(auto props, schema.DecodeEnumProperties(yaml, node));
  std::stringstream ss;
  YAML::Emitter out(ss);
  out.SetIndent(2);
  CEL_RETURN_IF_ERROR(schema.EncodeEnumProperties(*props, out));
  return ss.str();
}

absl::StatusOr<std::string> RoundTripEnumConstant(const SchemaYaml& schema,
                                                  absl::string_view yaml) {
  CEL_ASSIGN_OR_RETURN(YAML::Node node, internal::LoadYaml(yaml));
  CEL_ASSIGN_OR_RETURN(auto props,
                       schema.DecodeEnumConstantProperties(yaml, node));
  std::stringstream ss;
  YAML::Emitter out(ss);
  out.SetIndent(2);
  CEL_RETURN_IF_ERROR(schema.EncodeEnumConstantProperties(*props, out));
  return ss.str();
}

TEST(ProtoSchemaYamlTest, DecodeAndEncodeProperties) {
  SchemaYamlRegistry registry(std::make_unique<Proto2SchemaYaml>(),
                              std::make_unique<Proto3SchemaYaml>());

  for (absl::string_view schema_name : {kProto2SchemaName, kProto3SchemaName}) {
    const SchemaYaml* schema = registry.Find(schema_name);
    ASSERT_THAT(schema, NotNull());

    EXPECT_THAT(RoundTripObject(*schema, "name: \"com.example.Item\""),
                IsOkAndHolds("name: \"com.example.Item\""));
    EXPECT_THAT(RoundTripObject(*schema, "unknown: val"),
                StatusIs(absl::StatusCode::kInvalidArgument));

    absl::string_view field_yaml =
        "name: \"com.example.store.Price\"\n"
        "id: 12\n"
        "json_name: \"priceVal\"";
    EXPECT_THAT(RoundTripField(*schema, field_yaml), IsOkAndHolds(field_yaml));
    EXPECT_THAT(RoundTripField(*schema, "id: not_an_int"),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("Node 'id' is not an integer")));
    EXPECT_THAT(RoundTripField(*schema, "unknown: val"),
                StatusIs(absl::StatusCode::kInvalidArgument));

    absl::string_view enum_yaml =
        "name: \"com.example.Status\"\n"
        "allow_alias: true";
    EXPECT_THAT(RoundTripEnum(*schema, enum_yaml), IsOkAndHolds(enum_yaml));
    EXPECT_THAT(RoundTripEnum(*schema, "allow_alias: bad_bool"),
                StatusIs(absl::StatusCode::kInvalidArgument));
    EXPECT_THAT(RoundTripEnum(*schema, "unknown: val"),
                StatusIs(absl::StatusCode::kInvalidArgument));

    absl::string_view val_yaml = "name: \"SHIPPED\"";
    EXPECT_THAT(RoundTripEnumConstant(*schema, val_yaml),
                IsOkAndHolds(val_yaml));
    EXPECT_THAT(RoundTripEnumConstant(*schema, "unknown: val"),
                StatusIs(absl::StatusCode::kInvalidArgument));
  }
}

TEST(ProtoSchemaYamlTest, EndToEndTypeDefsYamlRoundTrip) {
  SchemaYamlRegistry registry(std::make_unique<Proto2SchemaYaml>(),
                              std::make_unique<Proto3SchemaYaml>());

  std::string expected_yaml = Unindent(R"yaml(
      - object:
          name: "com.example.Item"
          schemas:
            proto2:
              name: "com.example.proto.Item"
          fields:
            - name: "item_id"
              type: "string"
              schemas:
                proto2:
                  name: "com.example.store.Price"
                  id: 12
                  json_name: "itemId"
      - enum:
          name: "com.example.Status"
          schemas:
            proto3:
              name: "com.example.proto.Status"
              allow_alias: false
          constants:
            - name: "UNSPECIFIED"
              id: 0
            - name: "ACTIVE"
              id: 1
              schemas:
                proto3:
                  name: "STATUS_ACTIVE"
  )yaml");

  ASSERT_OK_AND_ASSIGN(std::vector<TypeDef> type_defs,
                       TypeDefsFromYaml(expected_yaml, registry));
  ASSERT_EQ(type_defs.size(), 2);

  const auto* obj_props = dynamic_cast<const ProtoObjectProperties*>(
      type_defs[0].object_type().schema_specific_properties.at("proto2").get());
  ASSERT_THAT(obj_props, NotNull());
  EXPECT_THAT(obj_props->name, Optional(std::string("com.example.proto.Item")));

  const auto* field_props = dynamic_cast<const ProtoFieldProperties*>(
      type_defs[0]
          .object_type()
          .fields[0]
          .schema_specific_properties.at("proto2")
          .get());
  ASSERT_THAT(field_props, NotNull());
  EXPECT_THAT(field_props->name,
              Optional(std::string("com.example.store.Price")));
  EXPECT_THAT(field_props->id, Optional(12));
  EXPECT_THAT(field_props->json_name, Optional(std::string("itemId")));

  std::stringstream ss;
  ASSERT_THAT(TypeDefsToYaml(type_defs, registry, ss), IsOk());
  EXPECT_EQ(ss.str(), expected_yaml);
}

struct DummyObjectProperties : public SchemaObjectProperties {};
struct DummyFieldProperties : public SchemaFieldProperties {};
struct DummyEnumProperties : public SchemaEnumProperties {};
struct DummyEnumConstantProperties : public SchemaEnumConstantProperties {};

TEST(ProtoSchemaYamlTest, SchemaNames) {
  Proto2SchemaYaml p2;
  Proto3SchemaYaml p3;
  EXPECT_EQ(p2.name(), kProto2SchemaName);
  EXPECT_EQ(p3.name(), kProto3SchemaName);
}

TEST(ProtoSchemaYamlTest, EmptyAndPartialPropertiesRoundTrip) {
  Proto2SchemaYaml p2;
  Proto3SchemaYaml p3;
  for (const SchemaYaml* schema : {static_cast<const SchemaYaml*>(&p2),
                                   static_cast<const SchemaYaml*>(&p3)}) {
    EXPECT_THAT(RoundTripObject(*schema, "{}"), IsOkAndHolds("{}"));
    EXPECT_THAT(RoundTripField(*schema, "{}"), IsOkAndHolds("{}"));
    EXPECT_THAT(RoundTripEnum(*schema, "{}"), IsOkAndHolds("{}"));
    EXPECT_THAT(RoundTripEnumConstant(*schema, "{}"), IsOkAndHolds("{}"));

    EXPECT_THAT(RoundTripField(*schema, "id: 5"), IsOkAndHolds("id: 5"));
    EXPECT_THAT(RoundTripField(*schema, "name: \"msg.Field\""),
                IsOkAndHolds("name: \"msg.Field\""));
    EXPECT_THAT(RoundTripField(*schema, "json_name: \"jsonFld\""),
                IsOkAndHolds("json_name: \"jsonFld\""));

    EXPECT_THAT(RoundTripEnum(*schema, "name: \"MyEnum\""),
                IsOkAndHolds("name: \"MyEnum\""));
    EXPECT_THAT(RoundTripEnum(*schema, "allow_alias: false"),
                IsOkAndHolds("allow_alias: false"));
  }
}

TEST(ProtoSchemaYamlTest, ValidationErrors) {
  Proto2SchemaYaml p2;
  Proto3SchemaYaml p3;
  for (const SchemaYaml* schema : {static_cast<const SchemaYaml*>(&p2),
                                   static_cast<const SchemaYaml*>(&p3)}) {
    YAML::Node seq = YAML::Load("[]");
    EXPECT_THAT(schema->DecodeObjectProperties("[]", seq),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("object schema is not a map")));
    EXPECT_THAT(schema->DecodeFieldProperties("[]", seq),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("field schema is not a map")));
    EXPECT_THAT(schema->DecodeEnumProperties("[]", seq),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("enum schema is not a map")));
    EXPECT_THAT(schema->DecodeEnumConstantProperties("[]", seq),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("enum constant schema is not a map")));

    EXPECT_THAT(RoundTripObject(*schema, "? [1]: val"),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("object schema is not a string")));
    EXPECT_THAT(RoundTripField(*schema, "? [1]: val"),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("field schema is not a string")));
    EXPECT_THAT(RoundTripEnum(*schema, "? [1]: val"),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("enum schema is not a string")));
    EXPECT_THAT(RoundTripEnumConstant(*schema, "? [1]: val"),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("enum constant schema is not a string")));

    EXPECT_THAT(RoundTripObject(*schema, "name: [1]"),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("Node 'name' is not a string")));
    EXPECT_THAT(RoundTripField(*schema, "name: [1]"),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("Node 'name' is not a string")));
    EXPECT_THAT(RoundTripField(*schema, "json_name: [1]"),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("Node 'json_name' is not a string")));
    EXPECT_THAT(RoundTripEnum(*schema, "name: [1]"),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("Node 'name' is not a string")));
    EXPECT_THAT(RoundTripEnumConstant(*schema, "name: [1]"),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("Node 'name' is not a string")));
  }
}

TEST(ProtoSchemaYamlTest, EncodeTypeMismatchErrors) {
  Proto2SchemaYaml p2;
  Proto3SchemaYaml p3;
  for (const SchemaYaml* schema : {static_cast<const SchemaYaml*>(&p2),
                                   static_cast<const SchemaYaml*>(&p3)}) {
    std::stringstream ss;
    YAML::Emitter out(ss);

    DummyObjectProperties dummy_obj;
    EXPECT_THAT(schema->EncodeObjectProperties(dummy_obj, out),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("Invalid object properties type for '")));

    DummyFieldProperties dummy_field;
    EXPECT_THAT(schema->EncodeFieldProperties(dummy_field, out),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("Invalid field properties type for '")));

    DummyEnumProperties dummy_enum;
    EXPECT_THAT(schema->EncodeEnumProperties(dummy_enum, out),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("Invalid enum properties type for '")));

    DummyEnumConstantProperties dummy_const;
    EXPECT_THAT(
        schema->EncodeEnumConstantProperties(dummy_const, out),
        StatusIs(absl::StatusCode::kInvalidArgument,
                 HasSubstr("Invalid enum constant properties type for '")));
  }
}

}  // namespace
}  // namespace cel
