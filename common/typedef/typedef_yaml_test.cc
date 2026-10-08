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

#include "common/typedef/typedef_yaml.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "common/ast.h"
#include "common/constant.h"
#include "common/typedef/schema.h"
#include "common/typedef/schema_yaml.h"
#include "common/typedef/typedef.h"
#include "common/typedef/yaml_helpers.h"
#include "common/typedef/yaml_test_helpers.h"
#include "internal/status_macros.h"
#include "internal/testing.h"
#include "yaml-cpp/emitter.h"
#include "yaml-cpp/emittermanip.h"
#include "yaml-cpp/mark.h"
#include "yaml-cpp/node/node.h"
#include "yaml-cpp/node/parse.h"
#include "yaml-cpp/yaml.h"  // IWYU pragma: keep

namespace cel {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::SizeIs;

struct TestPropsData {
  std::optional<std::string> name;
  std::optional<int32_t> id;
};

absl::StatusOr<TestPropsData> DecodeTestProps(absl::string_view schema_name,
                                              absl::string_view context,
                                              absl::string_view yaml,
                                              const YAML::Node& node) {
  if (!node || !node.IsMap()) {
    return internal::YamlError(yaml, node, "Properties node is not a map");
  }
  TestPropsData data;
  for (const auto& kv : node) {
    std::string key = internal::GetString(yaml, kv.first);
    if (key == "name") {
      data.name = internal::GetString(yaml, kv.second);
    } else if (key == "id") {
      CEL_ASSIGN_OR_RETURN(data.id, internal::GetInt32(yaml, "id", kv.second));
    } else {
      return internal::YamlError(
          yaml, kv.first,
          absl::StrCat("Unsupported '", schema_name, "' ", context,
                       " property: '", key, "'"));
    }
  }
  return data;
}

void EncodeTestProps(const TestPropsData& data, YAML::Emitter& out) {
  out << YAML::BeginMap;
  if (data.name.has_value()) {
    out << YAML::Key << "name" << YAML::Value << YAML::DoubleQuoted
        << *data.name;
  }
  if (data.id.has_value()) {
    out << YAML::Key << "id" << YAML::Value << *data.id;
  }
  out << YAML::EndMap;
}

class TestObjectProps final : public SchemaObjectProperties {
 public:
  explicit TestObjectProps(TestPropsData d) : data_(std::move(d)) {}
  const TestPropsData& data() const { return data_; }

 private:
  TestPropsData data_;
};

class TestFieldProps final : public SchemaFieldProperties {
 public:
  explicit TestFieldProps(TestPropsData d) : data_(std::move(d)) {}
  const TestPropsData& data() const { return data_; }

 private:
  TestPropsData data_;
};

class TestEnumProps final : public SchemaEnumProperties {
 public:
  explicit TestEnumProps(TestPropsData d) : data_(std::move(d)) {}
  const TestPropsData& data() const { return data_; }

 private:
  TestPropsData data_;
};

class TestEnumConstantProps final : public SchemaEnumConstantProperties {
 public:
  explicit TestEnumConstantProps(TestPropsData d) : data_(std::move(d)) {}
  const TestPropsData& data() const { return data_; }

 private:
  TestPropsData data_;
};

class TestSchema final : public SchemaYaml {
 public:
  explicit TestSchema(absl::string_view name) : name_(name) {}

  absl::string_view name() const override { return name_; }

  absl::StatusOr<std::unique_ptr<SchemaObjectProperties>>
  DecodeObjectProperties(absl::string_view yaml,
                         const YAML::Node& node) const override {
    CEL_ASSIGN_OR_RETURN(TestPropsData d,
                         DecodeTestProps(name_, "object", yaml, node));
    return std::make_unique<TestObjectProps>(std::move(d));
  }

  absl::StatusOr<std::unique_ptr<SchemaFieldProperties>> DecodeFieldProperties(
      absl::string_view yaml, const YAML::Node& node) const override {
    CEL_ASSIGN_OR_RETURN(TestPropsData d,
                         DecodeTestProps(name_, "field", yaml, node));
    return std::make_unique<TestFieldProps>(std::move(d));
  }

  absl::StatusOr<std::unique_ptr<SchemaEnumProperties>> DecodeEnumProperties(
      absl::string_view yaml, const YAML::Node& node) const override {
    CEL_ASSIGN_OR_RETURN(TestPropsData d,
                         DecodeTestProps(name_, "enum", yaml, node));
    return std::make_unique<TestEnumProps>(std::move(d));
  }

  absl::StatusOr<std::unique_ptr<SchemaEnumConstantProperties>>
  DecodeEnumConstantProperties(absl::string_view yaml,
                               const YAML::Node& node) const override {
    CEL_ASSIGN_OR_RETURN(TestPropsData d,
                         DecodeTestProps(name_, "enum constant", yaml, node));
    return std::make_unique<TestEnumConstantProps>(std::move(d));
  }

  absl::Status EncodeObjectProperties(const SchemaObjectProperties& properties,
                                      YAML::Emitter& out) const override {
    EncodeTestProps(static_cast<const TestObjectProps&>(properties).data(),
                    out);
    return absl::OkStatus();
  }

  absl::Status EncodeFieldProperties(const SchemaFieldProperties& properties,
                                     YAML::Emitter& out) const override {
    EncodeTestProps(static_cast<const TestFieldProps&>(properties).data(), out);
    return absl::OkStatus();
  }

  absl::Status EncodeEnumProperties(const SchemaEnumProperties& properties,
                                    YAML::Emitter& out) const override {
    EncodeTestProps(static_cast<const TestEnumProps&>(properties).data(), out);
    return absl::OkStatus();
  }

  absl::Status EncodeEnumConstantProperties(
      const SchemaEnumConstantProperties& properties,
      YAML::Emitter& out) const override {
    EncodeTestProps(
        static_cast<const TestEnumConstantProps&>(properties).data(), out);
    return absl::OkStatus();
  }

 private:
  std::string name_;
};

class StubSchema final : public SchemaYaml {
 public:
  explicit StubSchema(absl::string_view name) : name_(name) {}

  absl::string_view name() const override { return name_; }

 private:
  std::string name_;
};

SchemaYamlRegistry MakeTestRegistry() {
  return SchemaYamlRegistry(std::make_unique<TestSchema>("schema_a"),
                            std::make_unique<TestSchema>("schema_b"),
                            std::make_unique<StubSchema>("stub_schema"));
}

TEST(TypeDefYamlTest, ParseObjectTypeDefWithSchemasAndDefaults) {
  SchemaYamlRegistry registry = MakeTestRegistry();

  ASSERT_OK_AND_ASSIGN(TypeDef type_def, TypeDefFromYaml(R"yaml(
        object:
          name: "com.example.Item"
          doc: "Store item entity."
          schemas:
            schema_a:
              name: "ItemObj"
            schema_b:
              name: "com.example.store.ItemProto"
          fields:
            - name: "item_id"
              type: "string"
              doc: "Unique item identifier."
              default: "default_id"
              schemas:
                schema_a:
                  name: "itemId"
                schema_b:
                  name: "com.example.store.Price"
                  id: 12
            - name: "tags"
              type: "list<string>"
            - name: "attributes"
              type:
                type_name: "map"
                params:
                  - type_name: "string"
                  - type_name: "int"
            - name: "nested_type"
              type:
                type_name: "list"
                params:
                  - type_name: "T"
                    is_type_param: true
      )yaml",
                                                         registry));

  ASSERT_TRUE(type_def.is_object());
  const ObjectTypeDef& obj = type_def.object_type();
  EXPECT_EQ(obj.name, "com.example.Item");
  EXPECT_EQ(obj.doc, "Store item entity.");

  ASSERT_TRUE(obj.schema_specific_properties.contains("schema_a"));
  EXPECT_NE(obj.schema_specific_properties.at("schema_a"), nullptr);
  ASSERT_TRUE(obj.schema_specific_properties.contains("schema_b"));
  EXPECT_NE(obj.schema_specific_properties.at("schema_b"), nullptr);

  ASSERT_THAT(obj.fields, SizeIs(4));
  const ObjectTypeDef::Field& f0 = obj.fields[0];
  EXPECT_EQ(f0.name, "item_id");
  EXPECT_EQ(f0.type, TypeSpec(PrimitiveType::kString));
  EXPECT_EQ(f0.doc, "Unique item identifier.");
  EXPECT_EQ(f0.default_value, Constant(StringConstant("default_id")));

  ASSERT_TRUE(f0.schema_specific_properties.contains("schema_a"));
  EXPECT_NE(f0.schema_specific_properties.at("schema_a"), nullptr);
  ASSERT_TRUE(f0.schema_specific_properties.contains("schema_b"));
  EXPECT_NE(f0.schema_specific_properties.at("schema_b"), nullptr);

  EXPECT_EQ(obj.fields[1].name, "tags");
  EXPECT_EQ(obj.fields[1].type,
            TypeSpec(ListTypeSpec(
                std::make_unique<TypeSpec>(PrimitiveType::kString))));

  EXPECT_EQ(obj.fields[2].name, "attributes");
  EXPECT_EQ(
      obj.fields[2].type,
      TypeSpec(MapTypeSpec(std::make_unique<TypeSpec>(PrimitiveType::kString),
                           std::make_unique<TypeSpec>(PrimitiveType::kInt64))));

  EXPECT_EQ(obj.fields[3].name, "nested_type");
  EXPECT_EQ(
      obj.fields[3].type,
      TypeSpec(ListTypeSpec(std::make_unique<TypeSpec>(ParamTypeSpec("T")))));
}

TEST(TypeDefYamlTest, ConciseAndStructuredTypeSyntax) {
  absl::string_view concise_yaml = R"yaml(
    object:
      name: com.example.store.Order
      fields:
        - name: items
          type: list<com.example.store.Item>
        - name: metadata
          type: map<string, dyn>
        - name: notes
          type: string_wrapper
  )yaml";

  absl::string_view structured_yaml = R"yaml(
    object:
      name: com.example.store.Order
      fields:
        - name: items
          type:
            type_name: list
            params:
              - type_name: com.example.store.Item
        - name: metadata
          type:
            type_name: map
            params:
              - type_name: string
              - type_name: dyn
        - name: notes
          type:
            type_name: string_wrapper
  )yaml";

  ASSERT_OK_AND_ASSIGN(TypeDef from_concise, TypeDefFromYaml(concise_yaml));
  ASSERT_OK_AND_ASSIGN(TypeDef from_structured,
                       TypeDefFromYaml(structured_yaml));

  for (const TypeDef* def : {&from_concise, &from_structured}) {
    ASSERT_TRUE(def->is_object());
    const ObjectTypeDef& obj = def->object_type();
    EXPECT_EQ(obj.name, "com.example.store.Order");
    ASSERT_THAT(obj.fields, SizeIs(3));
    EXPECT_EQ(obj.fields[0].name, "items");
    EXPECT_EQ(obj.fields[0].type,
              TypeSpec(ListTypeSpec(std::make_unique<TypeSpec>(
                  AbstractType("com.example.store.Item", {})))));
    EXPECT_EQ(obj.fields[1].name, "metadata");
    EXPECT_EQ(
        obj.fields[1].type,
        TypeSpec(MapTypeSpec(std::make_unique<TypeSpec>(PrimitiveType::kString),
                             std::make_unique<TypeSpec>(DynTypeSpec()))));
    EXPECT_EQ(obj.fields[2].name, "notes");
    EXPECT_EQ(obj.fields[2].type,
              TypeSpec(PrimitiveTypeWrapper(PrimitiveType::kString)));
  }

  std::stringstream emitted_concise;
  ASSERT_THAT(TypeDefToYaml(from_structured, emitted_concise,
                            {.use_type_signatures = true}),
              IsOk());
  EXPECT_EQ(emitted_concise.str(), Unindent(R"yaml(
    object:
      name: "com.example.store.Order"
      fields:
        - name: "items"
          type: "list<com.example.store.Item>"
        - name: "metadata"
          type: "map<string,dyn>"
        - name: "notes"
          type: "string_wrapper"
  )yaml"));

  std::stringstream emitted_structured;
  ASSERT_THAT(TypeDefToYaml(from_concise, emitted_structured,
                            {.use_type_signatures = false}),
              IsOk());
  EXPECT_EQ(emitted_structured.str(), Unindent(R"yaml(
    object:
      name: "com.example.store.Order"
      fields:
        - name: "items"
          type:
            type_name: "list"
            params:
              - type_name: "com.example.store.Item"
        - name: "metadata"
          type:
            type_name: "map"
            params:
              - type_name: "string"
              - type_name: "dyn"
        - name: "notes"
          type:
            type_name: "string_wrapper"
  )yaml"));
}

TEST(TypeDefYamlTest, ParseAllConstantDefaultTypes) {
  ASSERT_OK_AND_ASSIGN(TypeDef type_def, TypeDefFromYaml(R"yaml(
    object:
      name: "ConstantsHolder"
      fields:
        - name: "f_null"
          type: "null"
          default: ~
        - name: "f_bool"
          type: "bool"
          default: true
        - name: "f_int"
          type: "int"
          default: -42
        - name: "f_uint"
          type: "uint"
          default: 100u
        - name: "f_double"
          type: "double"
          default: 3.25
        - name: "f_bytes"
          type: "bytes"
          default: b"\xff\x00\x01"
        - name: "f_bytes_b64"
          type: "bytes"
          default: !!binary "AQID"
        - name: "f_string"
          type: "string"
          default: "hello"
        - name: "f_duration"
          type: "duration"
          default: "1h2m3s"
        - name: "f_timestamp"
          type: "timestamp"
          default: "2026-01-02T03:04:05Z"
  )yaml"));

  ASSERT_TRUE(type_def.is_object());
  const ObjectTypeDef& obj = type_def.object_type();
  ASSERT_THAT(obj.fields, SizeIs(10));
  EXPECT_EQ(obj.fields[0].default_value, Constant(nullptr));
  EXPECT_EQ(obj.fields[1].default_value, Constant(true));
  EXPECT_EQ(obj.fields[2].default_value, Constant(int64_t{-42}));
  EXPECT_EQ(obj.fields[3].default_value, Constant(uint64_t{100}));
  EXPECT_EQ(obj.fields[4].default_value, Constant(3.25));
  EXPECT_EQ(obj.fields[5].default_value,
            Constant(BytesConstant(absl::string_view("\xff\x00\x01", 3))));
  EXPECT_EQ(obj.fields[6].default_value,
            Constant(BytesConstant(absl::string_view("\x01\x02\x03", 3))));
  EXPECT_EQ(obj.fields[7].default_value, Constant(StringConstant("hello")));
  EXPECT_EQ(obj.fields[8].default_value,
            Constant(absl::Hours(1) + absl::Minutes(2) + absl::Seconds(3)));
  EXPECT_EQ(obj.fields[9].default_value,
            Constant(absl::FromUnixSeconds(1767323045)));
}

TEST(TypeDefYamlTest, ParseEnumTypeDef) {
  SchemaYamlRegistry registry = MakeTestRegistry();

  ASSERT_OK_AND_ASSIGN(TypeDef type_def, TypeDefFromYaml(R"yaml(
        enum:
          name: "com.example.OrderStatus"
          doc: "Order lifecycle state."
          schemas:
            schema_a:
              name: "OrderStatusA"
            schema_b:
              name: "com.example.proto.OrderStatus"
          constants:
            - name: "UNSPECIFIED"
              doc: "Default value."
            - name: "PENDING"
            - name: "SHIPPED"
              id: 10
              doc: "Shipped to customer."
              schemas:
                schema_a:
                  name: "shipped"
                schema_b:
                  name: "ORDER_STATUS_SHIPPED"
            - name: "DELIVERED"
      )yaml",
                                                         registry));

  ASSERT_TRUE(type_def.is_enum());
  const EnumTypeDef& enum_def = type_def.enum_type();
  EXPECT_EQ(enum_def.name, "com.example.OrderStatus");
  EXPECT_EQ(enum_def.doc, "Order lifecycle state.");

  ASSERT_TRUE(enum_def.schema_specific_properties.contains("schema_a"));
  EXPECT_NE(enum_def.schema_specific_properties.at("schema_a"), nullptr);
  ASSERT_TRUE(enum_def.schema_specific_properties.contains("schema_b"));
  EXPECT_NE(enum_def.schema_specific_properties.at("schema_b"), nullptr);

  ASSERT_THAT(enum_def.constants, SizeIs(4));
  EXPECT_EQ(enum_def.constants[0].name, "UNSPECIFIED");
  EXPECT_EQ(enum_def.constants[0].id, 0);
  EXPECT_EQ(enum_def.constants[0].doc, "Default value.");

  EXPECT_EQ(enum_def.constants[1].name, "PENDING");
  EXPECT_EQ(enum_def.constants[1].id, 1);

  EXPECT_EQ(enum_def.constants[2].name, "SHIPPED");
  EXPECT_EQ(enum_def.constants[2].id, 10);
  EXPECT_EQ(enum_def.constants[2].doc, "Shipped to customer.");
  ASSERT_TRUE(
      enum_def.constants[2].schema_specific_properties.contains("schema_a"));
  EXPECT_NE(enum_def.constants[2].schema_specific_properties.at("schema_a"),
            nullptr);
  ASSERT_TRUE(
      enum_def.constants[2].schema_specific_properties.contains("schema_b"));
  EXPECT_NE(enum_def.constants[2].schema_specific_properties.at("schema_b"),
            nullptr);

  EXPECT_EQ(enum_def.constants[3].name, "DELIVERED");
  EXPECT_EQ(enum_def.constants[3].id, 3);
}

TEST(TypeDefYamlTest, TypeDefsFromYamlSequenceAndTypesMap) {
  SchemaYamlRegistry registry = MakeTestRegistry();

  absl::string_view types_map_yaml = R"yaml(
    types:
      - object:
          name: "Item"
          fields:
            - name: "item_id"
              type: "string"
              schemas:
                schema_a:
                  name: "itemId"
                schema_b:
                  name: "com.example.store.Price"
                  id: 12
      - enum:
          name: "Status"
          constants:
            - name: "UNKNOWN"
  )yaml";

  absl::string_view seq_yaml = R"yaml(
    - object:
        name: "Item"
        fields:
          - name: "item_id"
            type: "string"
            schemas:
              schema_a:
                name: "itemId"
              schema_b:
                name: "com.example.store.Price"
                id: 12
    - enum:
        name: "Status"
        constants:
          - name: "UNKNOWN"
  )yaml";

  ASSERT_OK_AND_ASSIGN(std::vector<TypeDef> from_map,
                       TypeDefsFromYaml(types_map_yaml, registry));
  ASSERT_OK_AND_ASSIGN(std::vector<TypeDef> from_seq,
                       TypeDefsFromYaml(seq_yaml, registry));
  ASSERT_THAT(from_map, SizeIs(2));
  ASSERT_THAT(from_seq, SizeIs(2));
  std::stringstream map_ss;
  std::stringstream seq_ss;
  ASSERT_THAT(TypeDefsToYaml(from_map, registry, map_ss), IsOk());
  ASSERT_THAT(TypeDefsToYaml(from_seq, registry, seq_ss), IsOk());
  EXPECT_EQ(map_ss.str(), seq_ss.str());
}

TEST(TypeDefYamlTest, ExportAndRoundTripObjectAndEnum) {
  SchemaYamlRegistry registry = MakeTestRegistry();

  std::string expected_signature_yaml = Unindent(R"yaml(
    - object:
        name: "com.example.Item"
        doc: "Store item."
        schemas:
          schema_a:
            name: "Item"
          schema_b:
            name: "com.example.proto.Item"
        fields:
          - name: "item_id"
            type: "string"
            doc: "ID of the item."
            default: "abc"
            schemas:
              schema_a:
                name: "itemId"
              schema_b:
                name: "com.example.store.Price"
                id: 12
          - name: "tags"
            type: "list<string>"
    - enum:
        name: "com.example.Status"
        doc: "Status enum."
        schemas:
          schema_a:
            name: "StatusA"
          schema_b:
            name: "com.example.proto.Status"
        constants:
          - name: "UNSPECIFIED"
            id: 0
          - name: "ACTIVE"
            id: 1
            doc: "Active state."
            schemas:
              schema_a:
                name: "active"
              schema_b:
                name: "STATUS_ACTIVE"
  )yaml");

  std::string expected_structured_yaml = Unindent(R"yaml(
    - object:
        name: "com.example.Item"
        doc: "Store item."
        schemas:
          schema_a:
            name: "Item"
          schema_b:
            name: "com.example.proto.Item"
        fields:
          - name: "item_id"
            type:
              type_name: "string"
            doc: "ID of the item."
            default: "abc"
            schemas:
              schema_a:
                name: "itemId"
              schema_b:
                name: "com.example.store.Price"
                id: 12
          - name: "tags"
            type:
              type_name: "list"
              params:
                - type_name: "string"
    - enum:
        name: "com.example.Status"
        doc: "Status enum."
        schemas:
          schema_a:
            name: "StatusA"
          schema_b:
            name: "com.example.proto.Status"
        constants:
          - name: "UNSPECIFIED"
            id: 0
          - name: "ACTIVE"
            id: 1
            doc: "Active state."
            schemas:
              schema_a:
                name: "active"
              schema_b:
                name: "STATUS_ACTIVE"
  )yaml");

  ASSERT_OK_AND_ASSIGN(std::vector<TypeDef> type_defs,
                       TypeDefsFromYaml(expected_signature_yaml, registry));

  std::stringstream sig_ss;
  ASSERT_THAT(TypeDefsToYaml(type_defs, registry, sig_ss,
                             {.use_type_signatures = true}),
              IsOk());
  EXPECT_EQ(sig_ss.str(), expected_signature_yaml);

  std::stringstream struct_ss;
  ASSERT_THAT(TypeDefsToYaml(type_defs, registry, struct_ss,
                             {.use_type_signatures = false}),
              IsOk());
  EXPECT_EQ(struct_ss.str(), expected_structured_yaml);

  ASSERT_OK_AND_ASSIGN(std::vector<TypeDef> roundtrip_defs,
                       TypeDefsFromYaml(struct_ss.str(), registry));
  std::stringstream roundtrip_ss;
  ASSERT_THAT(TypeDefsToYaml(roundtrip_defs, registry, roundtrip_ss,
                             {.use_type_signatures = true}),
              IsOk());
  EXPECT_EQ(roundtrip_ss.str(), expected_signature_yaml);
}

struct MalformedYamlTestCase {
  std::string yaml;
  std::string expected_error;
};

class TypeDefYamlErrorTest
    : public testing::TestWithParam<MalformedYamlTestCase> {};

TEST_P(TypeDefYamlErrorTest, ReportsFormattedErrorWithMark) {
  const MalformedYamlTestCase& test_case = GetParam();
  SchemaYamlRegistry registry = MakeTestRegistry();
  EXPECT_THAT(TypeDefsFromYaml(test_case.yaml, registry),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       Eq(test_case.expected_error)));
}

INSTANTIATE_TEST_SUITE_P(
    TypeDefYamlErrorTest, TypeDefYamlErrorTest,
    ::testing::Values(
        MalformedYamlTestCase{
            .yaml = R"yaml(
              types: "not_a_seq"
            )yaml",
            .expected_error = "2:22: Node 'types' is not a sequence\n"
                              "|              types: \"not_a_seq\"\n"
                              "|                     ^",
        },
        MalformedYamlTestCase{
            .yaml = R"yaml(
              types:
                - "not_a_map"
            )yaml",
            .expected_error = "3:19: Type definition is not a map\n"
                              "|                - \"not_a_map\"\n"
                              "|                  ^",
        },
        MalformedYamlTestCase{
            .yaml = R"yaml(
              types:
                - object:
                    name:
                      - "bad_name"
            )yaml",
            .expected_error = "5:23: Type definition 'name' is not a string\n"
                              "|                      - \"bad_name\"\n"
                              "|                      ^",
        },
        MalformedYamlTestCase{
            .yaml = R"yaml(
              types:
                - union:
                    name: "Item"
            )yaml",
            .expected_error =
                "3:19: Unsupported type definition kind: 'union'\n"
                "|                - union:\n"
                "|                  ^",
        },
        MalformedYamlTestCase{
            .yaml = R"yaml(
              types:
                - object:
                    name: "Item"
                    constants: []
            )yaml",
            .expected_error =
                "5:21: Object type definition cannot have 'constants'\n"
                "|                    constants: []\n"
                "|                    ^",
        },
        MalformedYamlTestCase{
            .yaml = R"yaml(
              types:
                - enum:
                    name: "Status"
                    fields: []
            )yaml",
            .expected_error =
                "5:21: Enum type definition cannot have 'fields'\n"
                "|                    fields: []\n"
                "|                    ^",
        },
        MalformedYamlTestCase{
            .yaml = R"yaml(
              types:
                - object:
                    name: "Item"
                    fields:
                      - name: "f"
                        type: ""
            )yaml",
            .expected_error = "7:31: Empty type signature\n"
                              "|                        type: \"\"\n"
                              "|                              ^",
        },
        MalformedYamlTestCase{
            .yaml = R"yaml(
              types:
                - object:
                    name: "Item"
                    fields:
                      - name: "f"
                        type: {}
            )yaml",
            .expected_error = "7:31: Node 'type_name' is not specified\n"
                              "|                        type: {}\n"
                              "|                              ^",
        },
        MalformedYamlTestCase{
            .yaml = R"yaml(
              types:
                - object:
                    name: "Item"
                    fields:
                      - name: "f"
                        type: "uint"
                        default: -1
            )yaml",
            .expected_error = "8:34: Failed to parse uint constant\n"
                              "|                        default: -1\n"
                              "|                                 ^",
        },
        MalformedYamlTestCase{
            .yaml = R"yaml(
              types:
                - object:
                    name: "Item"
                    schemas:
                      unknown_schema:
                        name: "Foo"
            )yaml",
            .expected_error = "6:23: Unsupported schema: 'unknown_schema'\n"
                              "|                      unknown_schema:\n"
                              "|                      ^",
        },
        MalformedYamlTestCase{
            .yaml = R"yaml(
              types:
                - object:
                    name: "Item"
                    fields:
                      - name: "f"
                        type: "string"
                        schemas:
                          schema_a:
                            unknown_prop: 12
            )yaml",
            .expected_error =
                "10:29: Unsupported 'schema_a' field property: 'unknown_prop'\n"
                "|                            unknown_prop: 12\n"
                "|                            ^",
        },
        MalformedYamlTestCase{
            .yaml = R"yaml(
              types:
                - object:
                    name: "Item"
                    fields:
                      - name: "f"
                        type: "string"
                        schemas:
                          schema_b:
                            id: "not_an_int"
            )yaml",
            .expected_error =
                "10:33: Node 'id' is not an integer\n"
                "|                            id: \"not_an_int\"\n"
                "|                                ^",
        },
        MalformedYamlTestCase{
            .yaml = R"yaml(
              types:
                - object:
                    name: "Item"
                    fields:
                      - name: "f"
                        type: "string"
                      - name: "f"
                        type: "int"
            )yaml",
            .expected_error = "8:31: Field 'f' is already defined.\n"
                              "|                      - name: \"f\"\n"
                              "|                              ^",
        },
        MalformedYamlTestCase{
            .yaml = R"yaml(
              types:
                - enum:
                    name: "Status"
                    constants:
                      - name: "ACTIVE"
                        id: "not_an_int"
            )yaml",
            .expected_error = "7:29: Node 'id' is not an integer\n"
                              "|                        id: \"not_an_int\"\n"
                              "|                            ^",
        },
        MalformedYamlTestCase{
            .yaml = R"yaml(
              types:
                - enum:
                    name: "Status"
                    constants:
                      - name: "ACTIVE"
                      - name: "ACTIVE"
            )yaml",
            .expected_error =
                "7:31: Enum constant 'ACTIVE' is already defined.\n"
                "|                      - name: \"ACTIVE\"\n"
                "|                              ^",
        },
        MalformedYamlTestCase{
            .yaml = R"yaml(
              types:
                - object:
                    name: "Item"
                    schemas:
                      stub_schema:
                        name: "Foo"
            )yaml",
            .expected_error =
                "7:25: Custom object properties are not supported for schema "
                "'stub_schema'\n"
                "|                        name: \"Foo\"\n"
                "|                        ^",
        },
        MalformedYamlTestCase{
            .yaml = R"yaml(
              types:
                - object:
                    name: "Item"
                    fields:
                      - name: "f"
                        type: "string"
                        schemas:
                          stub_schema:
                            name: "Foo"
            )yaml",
            .expected_error =
                "10:29: Custom field properties are not supported for schema "
                "'stub_schema'\n"
                "|                            name: \"Foo\"\n"
                "|                            ^",
        },
        MalformedYamlTestCase{
            .yaml = R"yaml(
              types:
                - enum:
                    name: "Status"
                    schemas:
                      stub_schema:
                        name: "Foo"
                    constants:
                      - name: "ACTIVE"
            )yaml",
            .expected_error =
                "7:25: Custom enum properties are not supported for schema "
                "'stub_schema'\n"
                "|                        name: \"Foo\"\n"
                "|                        ^",
        },
        MalformedYamlTestCase{
            .yaml = R"yaml(
              types:
                - enum:
                    name: "Status"
                    constants:
                      - name: "ACTIVE"
                        schemas:
                          stub_schema:
                            name: "Foo"
            )yaml",
            .expected_error =
                "9:29: Custom enum constant properties are not supported for "
                "schema 'stub_schema'\n"
                "|                            name: \"Foo\"\n"
                "|                            ^",
        }));

TEST(TypeDefYamlTest, EmptySchemaPropertiesOnUnimplementedSchemaSucceeds) {
  SchemaYamlRegistry registry = MakeTestRegistry();
  ASSERT_OK_AND_ASSIGN(std::vector<TypeDef> defs, TypeDefsFromYaml(R"yaml(
                         types:
                           - object:
                               name: "Item"
                               schemas:
                                 stub_schema: {}
                               fields:
                                 - name: "f"
                                   type: "int"
                                   schemas:
                                     stub_schema: ~
                           - enum:
                               name: "Status"
                               schemas:
                                 stub_schema: {}
                               constants:
                                 - name: "A"
                                   schemas:
                                     stub_schema: ~
                       )yaml",
                                                                   registry));
  ASSERT_THAT(defs, SizeIs(2));
  EXPECT_TRUE(defs[0].object_type().schema_specific_properties.empty());
  EXPECT_TRUE(
      defs[0].object_type().fields[0].schema_specific_properties.empty());
  EXPECT_TRUE(defs[1].enum_type().schema_specific_properties.empty());
  EXPECT_TRUE(
      defs[1].enum_type().constants[0].schema_specific_properties.empty());
}

TEST(TypeDefYamlTest, EncodeErrorOnUnregisteredSchema) {
  SchemaYamlRegistry registry = MakeTestRegistry();
  ASSERT_OK_AND_ASSIGN(TypeDef type_def, TypeDefFromYaml(R"yaml(
                         object:
                           name: "Item"
                           schemas:
                             schema_a:
                               name: "Item"
                           fields:
                             - name: "f"
                               type: "int"
                               schemas:
                                 schema_a:
                                   name: "f"
                       )yaml",
                                                         registry));

  SchemaYamlRegistry empty_registry;
  std::stringstream ss;
  EXPECT_THAT(TypeDefToYaml(type_def, empty_registry, ss),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Unsupported schema: 'schema_a'")));

  ASSERT_OK_AND_ASSIGN(TypeDef enum_def, TypeDefFromYaml(R"yaml(
                         enum:
                           name: "Status"
                           constants:
                             - name: "A"
                               schemas:
                                 schema_a:
                                   name: "A"
                       )yaml",
                                                         registry));
  std::stringstream ss_enum;
  EXPECT_THAT(TypeDefToYaml(enum_def, empty_registry, ss_enum),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Unsupported schema: 'schema_a'")));
}

TEST(TypeDefYamlTest, ConstantDefaultValuesRoundTrip) {
  SchemaYamlRegistry registry = MakeTestRegistry();
  std::string yaml = Unindent(R"yaml(
    object:
      name: "com.example.Defaults"
      fields:
        - name: "bool_field"
          type: "bool"
          default: true
        - name: "int_field"
          type: "int"
          default: 123
        - name: "uint_field"
          type: "uint"
          default: 456u
        - name: "double_field"
          type: "double"
          default: 3.14
        - name: "bytes_field"
          type: "bytes"
          default: b"\x01\x02\x03"
        - name: "duration_field"
          type: "duration"
          default: "10s"
        - name: "timestamp_field"
          type: "timestamp"
          default: "2026-10-05T00:00:00Z"
  )yaml");

  ASSERT_OK_AND_ASSIGN(TypeDef def, TypeDefFromYaml(yaml, registry));
  ASSERT_TRUE(def.is_object());
  const auto& fields = def.object_type().fields;
  ASSERT_THAT(fields, SizeIs(7));
  EXPECT_TRUE(fields[0].default_value.has_bool_value());
  EXPECT_EQ(fields[0].default_value.bool_value(), true);
  EXPECT_TRUE(fields[1].default_value.has_int_value());
  EXPECT_EQ(fields[1].default_value.int_value(), 123);
  EXPECT_TRUE(fields[2].default_value.has_uint_value());
  EXPECT_EQ(fields[2].default_value.uint_value(), 456u);
  EXPECT_TRUE(fields[3].default_value.has_double_value());
  EXPECT_DOUBLE_EQ(fields[3].default_value.double_value(), 3.14);
  EXPECT_TRUE(fields[4].default_value.has_bytes_value());
  EXPECT_EQ(fields[4].default_value.bytes_value(), "\x01\x02\x03");
  EXPECT_TRUE(fields[5].default_value.has_duration_value());
  EXPECT_EQ(fields[5].default_value.duration_value(), absl::Seconds(10));
  EXPECT_TRUE(fields[6].default_value.has_timestamp_value());
  EXPECT_EQ(fields[6].default_value.timestamp_value(),
            absl::FromCivil(absl::CivilSecond(2026, 10, 5, 0, 0, 0),
                            absl::UTCTimeZone()));

  std::stringstream ss;
  ASSERT_THAT(TypeDefToYaml(def, registry, ss, {.use_type_signatures = true}),
              IsOk());
  ASSERT_OK_AND_ASSIGN(TypeDef def2, TypeDefFromYaml(ss.str(), registry));
  ASSERT_TRUE(def2.is_object());
  const auto& fields2 = def2.object_type().fields;
  ASSERT_THAT(fields2, SizeIs(7));
  for (size_t i = 0; i < 7; ++i) {
    EXPECT_EQ(fields[i].name, fields2[i].name);
    EXPECT_EQ(fields[i].type, fields2[i].type);
    EXPECT_EQ(fields[i].default_value, fields2[i].default_value);
  }
}

TEST(TypeDefYamlTest, YamlHelpersCoverage) {
  EXPECT_EQ(internal::FormatYamlErrorMessage("yaml", "error",
                                             YAML::Mark::null_mark()),
            "error");

  YAML::Mark mark;
  mark.pos = 7;
  mark.line = 0;
  mark.column = 7;
  EXPECT_THAT(internal::FormatYamlErrorMessage("single line", "err", mark),
              HasSubstr("single line\n|       ^"));

  EXPECT_THAT(internal::LoadYaml("{ invalid: [ }"),
              StatusIs(absl::StatusCode::kInvalidArgument));

  YAML::Node seq = YAML::Load("[1, 2]");
  EXPECT_EQ(internal::GetString("yaml", seq), "");

  YAML::Node str_node = YAML::Load("hello");
  EXPECT_THAT(internal::GetBinary("hello", str_node), IsOkAndHolds(""));
  YAML::Node bad_b64 = YAML::Load("!!binary '%%%'");
  EXPECT_THAT(internal::GetBinary("!!binary '%%%'", bad_b64),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("not a valid Base64 encoded binary")));

  EXPECT_THAT(internal::GetBool("[1]", "k", seq),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Node 'k' is not a boolean")));
  YAML::Node not_bool = YAML::Load("not_a_bool");
  EXPECT_THAT(internal::GetBool("not_a_bool", "k", not_bool),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Node 'k' is not a boolean")));

  EXPECT_THAT(internal::GetInt32("[1]", "k", seq),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Node 'k' is not an integer")));
  YAML::Node not_int = YAML::Load("not_an_int");
  EXPECT_THAT(internal::GetInt32("not_an_int", "k", not_int),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Node 'k' is not an integer")));

  YAML::Node map_node = YAML::Load("a: 1\nb: 2");
  YAML::Node missing_node = YAML::Load("3");
  YAML::Node ctx = internal::GetContextNodeForKeyValue(map_node, missing_node);
  EXPECT_TRUE(ctx.is(missing_node));
}

TEST(TypeDefYamlTest, MoreValidationErrors) {
  SchemaYamlRegistry registry = MakeTestRegistry();

  EXPECT_THAT(TypeDefFromYaml("object: [1, 2]", registry),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Object type definition is not a map")));
  EXPECT_THAT(TypeDefFromYaml("object:\n  name: [1]", registry),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Type definition 'name' is not a string")));
  EXPECT_THAT(TypeDefFromYaml("object:\n  name: 'A'\n  doc: [1]", registry),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Type definition 'doc' is not a string")));
  EXPECT_THAT(
      TypeDefFromYaml("object:\n  name: 'A'\n  constants: []", registry),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Object type definition cannot have 'constants'")));
  EXPECT_THAT(TypeDefFromYaml("object:\n  name: 'A'\n  fields: 123", registry),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Node 'fields' is not a sequence")));
  EXPECT_THAT(
      TypeDefFromYaml("object:\n  name: 'A'\n  fields: [123]", registry),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Field is not a map")));
  EXPECT_THAT(TypeDefFromYaml(
                  "object:\n  name: 'A'\n  fields:\n    - name: [1]", registry),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Field name is not a string")));
  EXPECT_THAT(TypeDefFromYaml(
                  "object:\n  name: 'A'\n  fields:\n    - name: 'f'", registry),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Field 'type' is not specified")));
  EXPECT_THAT(
      TypeDefFromYaml(
          "object:\n  name: 'A'\n  fields:\n    - name: 'f'\n      type: [1]",
          registry),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Field 'type' is neither a string nor a map")));
  EXPECT_THAT(
      TypeDefFromYaml("object:\n  name: 'A'\n  fields:\n    - name: 'f'\n      "
                      "type: ''",
                      registry),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Empty type signature")));
  EXPECT_THAT(
      TypeDefFromYaml("object:\n  name: 'A'\n  fields:\n    - name: 'f'\n      "
                      "type: 'int'\n      doc: [1]",
                      registry),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Field 'doc' is not a string")));
  EXPECT_THAT(
      TypeDefFromYaml("object:\n  name: 'A'\n  fields:\n    - name: 'f'\n      "
                      "type: 'bad_type'\n      default: 123",
                      registry),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Constant type 'bad_type' is not supported")));
  EXPECT_THAT(
      TypeDefFromYaml("object:\n  name: 'A'\n  fields:\n    - name: 'f'\n      "
                      "type: 'int'\n      default: [1]",
                      registry),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Field 'default' is not a scalar")));

  EXPECT_THAT(
      TypeDefFromYaml("object:\n  name: 'A'\n  fields:\n    - name: 'f'\n      "
                      "type:\n        type_name: [1]",
                      registry),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Node 'type_name' is not a string")));
  EXPECT_THAT(TypeDefFromYaml(
                  "object:\n  name: 'A'\n  fields:\n    - name: 'f'\n      "
                  "type:\n        type_name: 'int'\n        is_type_param: [1]",
                  registry),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Node 'is_type_param' is not a boolean")));
  EXPECT_THAT(
      TypeDefFromYaml("object:\n  name: 'A'\n  fields:\n    - name: 'f'\n      "
                      "type:\n        type_name: 'map'\n        params: 123",
                      registry),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Node 'params' is not a sequence")));
  EXPECT_THAT(
      TypeDefFromYaml("object:\n  name: 'A'\n  fields:\n    - name: 'f'\n      "
                      "type:\n        type_name: 'map'\n        params: [123]",
                      registry),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Type parameter is not a map")));

  EXPECT_THAT(TypeDefFromYaml("enum: [1, 2]", registry),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Enum type definition is not a map")));
  EXPECT_THAT(TypeDefFromYaml("enum:\n  name: 'E'\n  constants: 123", registry),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Node 'constants' is not a sequence")));
  EXPECT_THAT(
      TypeDefFromYaml("enum:\n  name: 'E'\n  constants: [123]", registry),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Enum constant is not a map")));
  EXPECT_THAT(
      TypeDefFromYaml("enum:\n  name: 'E'\n  constants:\n    - name: [1]",
                      registry),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Enum constant name is not a string")));
  EXPECT_THAT(TypeDefFromYaml(
                  "enum:\n  name: 'E'\n  constants:\n    - name: 'C'\n      "
                  "doc: [1]",
                  registry),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Enum constant 'doc' is not a string")));
  EXPECT_THAT(TypeDefFromYaml("enum:\n  name: 'E'\n  fields: []", registry),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Enum type definition cannot have 'fields'")));

  EXPECT_THAT(TypeDefFromYaml("[1]", registry),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Invalid CEL type definition YAML")));
  EXPECT_THAT(TypeDefsFromYaml("[1]", registry),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Type definition is not a map")));
  EXPECT_THAT(TypeDefsFromYaml("123", registry),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Invalid CEL type definitions YAML")));
  EXPECT_THAT(TypeDefsFromYaml("not_types: 1", registry),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Invalid CEL type definitions YAML")));
  EXPECT_THAT(TypeDefsFromYaml("types: 123", registry),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Node 'types' is not a sequence")));
  EXPECT_THAT(
      TypeDefFromYaml("object: {}\nenum: {}", registry),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Node 'object' and 'enum' are mutually exclusive")));
  EXPECT_THAT(TypeDefFromYaml("foo: 1", registry),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Unsupported type definition kind: 'foo'")));
  EXPECT_THAT(
      TypeDefFromYaml("{}", registry),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Type definition must specify 'object' or 'enum'")));
  EXPECT_THAT(TypeDefFromYaml("? [1]: 2", registry),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Type definition kind is not a string")));

  EXPECT_THAT(TypeDefFromYaml("object:\n  name: 'O'\n  schemas: 123", registry),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Node 'schemas' is not a map")));
  EXPECT_THAT(TypeDefFromYaml(
                  "object:\n  name: 'O'\n  schemas:\n    ? [1]: val", registry),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Target schema name is not a string")));
  EXPECT_THAT(
      TypeDefFromYaml("object:\n  name: 'O'\n  schemas:\n    unsupported: {}",
                      registry),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Unsupported schema: 'unsupported'")));

  EXPECT_THAT(
      TypeDefFromYaml("object:\n  name: 'O'\n  fields:\n    - name: 'f'\n      "
                      "type: 'int'\n      schemas: 123",
                      registry),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Node 'schemas' is not a map")));
  EXPECT_THAT(
      TypeDefFromYaml("object:\n  name: 'O'\n  fields:\n    - name: 'f'\n      "
                      "type: 'int'\n      schemas:\n        ? [1]: val",
                      registry),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Target schema name is not a string")));
  EXPECT_THAT(
      TypeDefFromYaml("object:\n  name: 'O'\n  fields:\n    - name: 'f'\n      "
                      "type: 'int'\n      schemas:\n        unsupported: {}",
                      registry),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Unsupported schema: 'unsupported'")));

  EXPECT_THAT(
      TypeDefFromYaml("enum:\n  name: 'E'\n  schemas: 123\n  constants: []",
                      registry),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Node 'schemas' is not a map")));
  EXPECT_THAT(
      TypeDefFromYaml("enum:\n  name: 'E'\n  schemas:\n    ? [1]: val\n  "
                      "constants: []",
                      registry),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Target schema name is not a string")));
  EXPECT_THAT(
      TypeDefFromYaml("enum:\n  name: 'E'\n  schemas:\n    unsupported: {}\n  "
                      "constants: []",
                      registry),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Unsupported schema: 'unsupported'")));

  EXPECT_THAT(TypeDefFromYaml(
                  "enum:\n  name: 'E'\n  constants:\n    - name: 'C'\n      "
                  "schemas: 123",
                  registry),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Node 'schemas' is not a map")));
  EXPECT_THAT(TypeDefFromYaml(
                  "enum:\n  name: 'E'\n  constants:\n    - name: 'C'\n      "
                  "schemas:\n        ? [1]: val",
                  registry),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Target schema name is not a string")));
  EXPECT_THAT(TypeDefFromYaml(
                  "enum:\n  name: 'E'\n  constants:\n    - name: 'C'\n      "
                  "schemas:\n        unsupported: {}",
                  registry),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Unsupported schema: 'unsupported'")));
}

TEST(TypeDefYamlTest, MoreConstantParsingAndErrorBranches) {
  SchemaYamlRegistry registry = MakeTestRegistry();

  ASSERT_OK_AND_ASSIGN(TypeDef empty_def, TypeDefFromYaml(""));
  EXPECT_TRUE(empty_def.name().empty());
  ASSERT_OK_AND_ASSIGN(std::vector<TypeDef> empty_defs, TypeDefsFromYaml(""));
  EXPECT_TRUE(empty_defs.empty());

  EXPECT_THAT(TypeDefFromYaml("object:\n  name: 'NoFields'", registry), IsOk());
  EXPECT_THAT(TypeDefFromYaml("enum:\n  name: 'NoConstants'", registry),
              IsOk());

  EXPECT_THAT(TypeDefFromYaml("enum: {}", registry),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Type definition 'name' is not a string")));
  EXPECT_THAT(TypeDefFromYaml("enum:\n  name: 'E'\n  doc: [1]", registry),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Type definition 'doc' is not a string")));

  EXPECT_THAT(
      TypeDefFromYaml("object:\n  name: 'A'\n  fields:\n    - name: 'f'\n      "
                      "type:\n        type_name: ''",
                      registry),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Field 'type' is not specified")));

  // Null constant
  EXPECT_THAT(
      TypeDefFromYaml("object:\n  name: 'A'\n  fields:\n    - name: 'f'\n      "
                      "type: 'null'\n      default: null",
                      registry),
      IsOk());
  EXPECT_THAT(
      TypeDefFromYaml("object:\n  name: 'A'\n  fields:\n    - name: 'f'\n      "
                      "type: 'null'\n      default: 'abc'",
                      registry),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Failed to parse null constant")));

  // False bool
  EXPECT_THAT(
      TypeDefFromYaml("object:\n  name: 'A'\n  fields:\n    - name: 'f'\n      "
                      "type: 'bool'\n      default: false",
                      registry),
      IsOk());
  EXPECT_THAT(
      TypeDefFromYaml("object:\n  name: 'A'\n  fields:\n    - name: 'f'\n      "
                      "type: 'bool'\n      default: 'bad'",
                      registry),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Failed to parse bool constant")));

  // Invalid int/uint/double/duration/timestamp
  EXPECT_THAT(
      TypeDefFromYaml("object:\n  name: 'A'\n  fields:\n    - name: 'f'\n      "
                      "type: 'int'\n      default: 'bad'",
                      registry),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Failed to parse int constant")));
  EXPECT_THAT(
      TypeDefFromYaml("object:\n  name: 'A'\n  fields:\n    - name: 'f'\n      "
                      "type: 'uint'\n      default: 'bad'",
                      registry),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Failed to parse uint constant")));
  EXPECT_THAT(
      TypeDefFromYaml("object:\n  name: 'A'\n  fields:\n    - name: 'f'\n      "
                      "type: 'double'\n      default: 'bad'",
                      registry),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Failed to parse double constant")));
  EXPECT_THAT(
      TypeDefFromYaml("object:\n  name: 'A'\n  fields:\n    - name: 'f'\n      "
                      "type: 'duration'\n      default: 'bad'",
                      registry),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Failed to parse duration constant")));
  EXPECT_THAT(
      TypeDefFromYaml("object:\n  name: 'A'\n  fields:\n    - name: 'f'\n      "
                      "type: 'timestamp'\n      default: 'bad'",
                      registry),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Failed to parse timestamp constant")));

  // TypeParam serialization
  ObjectTypeDef type_param_def;
  type_param_def.name = "ParamHolder";
  type_param_def.fields.push_back(ObjectTypeDef::Field{
      .name = "p",
      .type = TypeSpec(ParamTypeSpec("T")),
  });
  std::stringstream ss;
  EXPECT_THAT(TypeDefToYaml(TypeDef(type_param_def), ss,
                            {.use_type_signatures = false}),
              IsOk());
  EXPECT_THAT(ss.str(), HasSubstr("is_type_param: true"));
}

TEST(TypeDefYamlTest, StructuredWellKnownStructAndFunctionTypesRoundTrip) {
  absl::string_view yaml = R"yaml(
    object:
      name: "StructuredSpecialTypes"
      fields:
        - name: "struct_field"
          type:
            type_name: "google.protobuf.Struct"
        - name: "list_value_field"
          type:
            type_name: "google.protobuf.ListValue"
        - name: "any_field"
          type:
            type_name: "any"
        - name: "timestamp_field"
          type:
            type_name: "timestamp"
        - name: "duration_field"
          type:
            type_name: "duration"
        - name: "func_field"
          type:
            type_name: "function"
            params:
              - type_name: "bool"
              - type_name: "int"
              - type_name: "string"
  )yaml";

  ASSERT_OK_AND_ASSIGN(TypeDef def, TypeDefFromYaml(yaml));
  ASSERT_TRUE(def.is_object());
  const auto& fields = def.object_type().fields;
  ASSERT_THAT(fields, SizeIs(6));

  EXPECT_EQ(
      fields[0].type,
      TypeSpec(MapTypeSpec(std::make_unique<TypeSpec>(PrimitiveType::kString),
                           std::make_unique<TypeSpec>(DynTypeSpec()))));
  EXPECT_EQ(fields[1].type,
            TypeSpec(ListTypeSpec(std::make_unique<TypeSpec>(DynTypeSpec()))));
  EXPECT_EQ(fields[2].type, TypeSpec(WellKnownTypeSpec::kAny));
  EXPECT_EQ(fields[3].type, TypeSpec(WellKnownTypeSpec::kTimestamp));
  EXPECT_EQ(fields[4].type, TypeSpec(WellKnownTypeSpec::kDuration));
  EXPECT_EQ(fields[5].type,
            TypeSpec(FunctionTypeSpec(
                std::make_unique<TypeSpec>(PrimitiveType::kBool),
                {TypeSpec(PrimitiveType::kInt64),
                 TypeSpec(PrimitiveType::kString)})));

  std::stringstream emitted;
  ASSERT_THAT(TypeDefToYaml(def, emitted, {.use_type_signatures = false}),
              IsOk());
  EXPECT_EQ(emitted.str(), Unindent(R"yaml(
    object:
      name: "StructuredSpecialTypes"
      fields:
        - name: "struct_field"
          type:
            type_name: "map"
            params:
              - type_name: "string"
              - type_name: "dyn"
        - name: "list_value_field"
          type:
            type_name: "list"
            params:
              - type_name: "dyn"
        - name: "any_field"
          type:
            type_name: "any"
        - name: "timestamp_field"
          type:
            type_name: "timestamp"
        - name: "duration_field"
          type:
            type_name: "duration"
        - name: "func_field"
          type:
            type_name: "function"
            params:
              - type_name: "bool"
              - type_name: "int"
              - type_name: "string"
  )yaml"));
}

}  // namespace
}  // namespace cel
