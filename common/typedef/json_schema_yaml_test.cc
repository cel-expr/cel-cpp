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

#include "common/typedef/json_schema_yaml.h"

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "common/typedef/json_schema.h"
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

TEST(JsonSchemaYamlTest, DecodeAndEncodeProperties) {
  JsonSchemaYaml schema;

  EXPECT_THAT(RoundTripObject(schema, "name: \"PersonObj\""),
              IsOkAndHolds("name: \"PersonObj\""));
  EXPECT_THAT(RoundTripObject(schema, "invalid: val"),
              StatusIs(absl::StatusCode::kInvalidArgument));

  EXPECT_THAT(RoundTripField(schema, "name: \"itemId\""),
              IsOkAndHolds("name: \"itemId\""));
  EXPECT_THAT(RoundTripField(schema, "id: 12"),
              StatusIs(absl::StatusCode::kInvalidArgument));

  absl::string_view enum_yaml =
      "style: \"string\"\n"
      "omit_unspecified: true";
  EXPECT_THAT(RoundTripEnum(schema, enum_yaml), IsOkAndHolds(enum_yaml));
  EXPECT_THAT(RoundTripEnum(schema, "omit_unspecified: not_a_bool"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Node 'omit_unspecified' is not a boolean")));
  EXPECT_THAT(RoundTripEnum(schema, "unknown: val"),
              StatusIs(absl::StatusCode::kInvalidArgument));

  EXPECT_THAT(RoundTripEnumConstant(schema, "name: \"shipped\""),
              IsOkAndHolds("name: \"shipped\""));
  EXPECT_THAT(RoundTripEnumConstant(schema, "unknown: val"),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(JsonSchemaYamlTest, EndToEndTypeDefsYamlRoundTrip) {
  SchemaYamlRegistry registry(std::make_unique<JsonSchemaYaml>());

  std::string expected_yaml = Unindent(R"yaml(
      - object:
          name: "com.example.Item"
          schemas:
            json:
              name: "Item"
          fields:
            - name: "item_id"
              type: "string"
              schemas:
                json:
                  name: "itemId"
      - enum:
          name: "com.example.Status"
          schemas:
            json:
              style: "string"
              omit_unspecified: true
          constants:
            - name: "UNSPECIFIED"
              id: 0
            - name: "ACTIVE"
              id: 1
              schemas:
                json:
                  name: "active"
  )yaml");

  ASSERT_OK_AND_ASSIGN(std::vector<TypeDef> type_defs,
                       TypeDefsFromYaml(expected_yaml, registry));
  ASSERT_EQ(type_defs.size(), 2);

  const auto* obj_props = dynamic_cast<const JsonSchemaObjectProperties*>(
      type_defs[0].object_type().schema_specific_properties.at("json").get());
  ASSERT_THAT(obj_props, NotNull());
  EXPECT_THAT(obj_props->name, Optional(std::string("Item")));

  const auto* enum_props = dynamic_cast<const JsonSchemaEnumProperties*>(
      type_defs[1].enum_type().schema_specific_properties.at("json").get());
  ASSERT_THAT(enum_props, NotNull());
  EXPECT_THAT(enum_props->style, Optional(std::string("string")));
  EXPECT_THAT(enum_props->omit_unspecified, Optional(true));

  std::stringstream ss;
  ASSERT_THAT(TypeDefsToYaml(type_defs, registry, ss), IsOk());
  EXPECT_EQ(ss.str(), expected_yaml);
}

struct DummyObjectProperties : public SchemaObjectProperties {};
struct DummyFieldProperties : public SchemaFieldProperties {};
struct DummyEnumProperties : public SchemaEnumProperties {};
struct DummyEnumConstantProperties : public SchemaEnumConstantProperties {};

TEST(JsonSchemaYamlTest, SchemaName) {
  JsonSchemaYaml schema;
  EXPECT_EQ(schema.name(), "json");
}

TEST(JsonSchemaYamlTest, EmptyPropertiesRoundTrip) {
  JsonSchemaYaml schema;
  EXPECT_THAT(RoundTripObject(schema, "{}"), IsOkAndHolds("{}"));
  EXPECT_THAT(RoundTripField(schema, "{}"), IsOkAndHolds("{}"));
  EXPECT_THAT(RoundTripEnum(schema, "{}"), IsOkAndHolds("{}"));
  EXPECT_THAT(RoundTripEnumConstant(schema, "{}"), IsOkAndHolds("{}"));
}

TEST(JsonSchemaYamlTest, ValidationErrors) {
  JsonSchemaYaml schema;
  YAML::Node seq = YAML::Load("[]");
  EXPECT_THAT(schema.DecodeObjectProperties("[]", seq),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Node 'json' object schema is not a map")));
  EXPECT_THAT(schema.DecodeFieldProperties("[]", seq),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Node 'json' field schema is not a map")));
  EXPECT_THAT(schema.DecodeEnumProperties("[]", seq),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Node 'json' enum schema is not a map")));
  EXPECT_THAT(
      schema.DecodeEnumConstantProperties("[]", seq),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Node 'json' enum constant schema is not a map")));

  EXPECT_THAT(
      RoundTripObject(schema, "? [1]: val"),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr("Property key in 'json' object schema is not a string")));
  EXPECT_THAT(
      RoundTripField(schema, "? [1]: val"),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr("Property key in 'json' field schema is not a string")));
  EXPECT_THAT(
      RoundTripEnum(schema, "? [1]: val"),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr("Property key in 'json' enum schema is not a string")));
  EXPECT_THAT(
      RoundTripEnumConstant(schema, "? [1]: val"),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr(
              "Property key in 'json' enum constant schema is not a string")));

  EXPECT_THAT(RoundTripObject(schema, "name: [1]"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Node 'name' is not a string")));
  EXPECT_THAT(RoundTripField(schema, "name: [1]"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Node 'name' is not a string")));
  EXPECT_THAT(RoundTripEnum(schema, "style: [1]"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Node 'style' is not a string")));
  EXPECT_THAT(RoundTripEnumConstant(schema, "name: [1]"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Node 'name' is not a string")));
}

TEST(JsonSchemaYamlTest, EncodeTypeMismatchErrors) {
  JsonSchemaYaml schema;
  std::stringstream ss;
  YAML::Emitter out(ss);

  DummyObjectProperties dummy_obj;
  EXPECT_THAT(
      schema.EncodeObjectProperties(dummy_obj, out),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Invalid object properties type for 'json' schema")));

  DummyFieldProperties dummy_field;
  EXPECT_THAT(
      schema.EncodeFieldProperties(dummy_field, out),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Invalid field properties type for 'json' schema")));

  DummyEnumProperties dummy_enum;
  EXPECT_THAT(
      schema.EncodeEnumProperties(dummy_enum, out),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Invalid enum properties type for 'json' schema")));

  DummyEnumConstantProperties dummy_const;
  EXPECT_THAT(schema.EncodeEnumConstantProperties(dummy_const, out),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Invalid enum constant properties type for "
                                 "'json' schema")));
}

}  // namespace
}  // namespace cel
