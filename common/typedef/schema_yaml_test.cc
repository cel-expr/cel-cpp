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

#include "common/typedef/schema_yaml.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/string_view.h"
#include "common/typedef/schema.h"
#include "internal/testing.h"
#include "yaml-cpp/emitter.h"
#include "yaml-cpp/node/node.h"
#include "yaml-cpp/node/parse.h"
#include "yaml-cpp/node/type.h"
#include "yaml-cpp/yaml.h"  // IWYU pragma: keep

namespace cel {
namespace {

using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::Eq;
using ::testing::IsNull;
using ::testing::NotNull;

class StubSchemaYaml final : public SchemaYaml {
 public:
  explicit StubSchemaYaml(absl::string_view name) : name_(name) {}

  absl::string_view name() const override { return name_; }

 private:
  std::string name_;
};

TEST(SchemaYamlTest, DefaultDecodeSucceedsOnEmptyNodes) {
  StubSchemaYaml schema("stub");

  std::vector<YAML::Node> empty_nodes = {
      YAML::Node(YAML::NodeType::Undefined),
      YAML::Load("~"),
      YAML::Load("\"\""),
      YAML::Load("{}"),
      YAML::Load("[]"),
  };

  for (const YAML::Node& node : empty_nodes) {
    EXPECT_THAT(schema.DecodeObjectProperties("", node),
                IsOkAndHolds(IsNull()));
    EXPECT_THAT(schema.DecodeFieldProperties("", node), IsOkAndHolds(IsNull()));
    EXPECT_THAT(schema.DecodeEnumProperties("", node), IsOkAndHolds(IsNull()));
    EXPECT_THAT(schema.DecodeEnumConstantProperties("", node),
                IsOkAndHolds(IsNull()));
  }
}

TEST(SchemaYamlTest, DefaultDecodeFailsWithYamlErrorOnNonEmptyNode) {
  StubSchemaYaml schema("stub");
  absl::string_view yaml = "prop: 123";
  YAML::Node node = YAML::Load(std::string(yaml));

  EXPECT_THAT(schema.DecodeObjectProperties(yaml, node),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       Eq("1:1: Custom object properties are not supported for "
                          "schema 'stub'\n|prop: 123\n|^")));
  EXPECT_THAT(schema.DecodeFieldProperties(yaml, node),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       Eq("1:1: Custom field properties are not supported for "
                          "schema 'stub'\n|prop: 123\n|^")));
  EXPECT_THAT(schema.DecodeEnumProperties(yaml, node),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       Eq("1:1: Custom enum properties are not supported for "
                          "schema 'stub'\n|prop: 123\n|^")));
  EXPECT_THAT(
      schema.DecodeEnumConstantProperties(yaml, node),
      StatusIs(absl::StatusCode::kInvalidArgument,
               Eq("1:1: Custom enum constant properties are not supported for "
                  "schema 'stub'\n|prop: 123\n|^")));
}

TEST(SchemaYamlTest, DefaultEncodeReturnsUnimplementedError) {
  StubSchemaYaml schema("stub");
  YAML::Emitter emitter;
  SchemaObjectProperties obj_props;
  SchemaFieldProperties field_props;
  SchemaEnumProperties enum_props;
  SchemaEnumConstantProperties enum_const_props;

  EXPECT_THAT(schema.EncodeObjectProperties(obj_props, emitter),
              StatusIs(absl::StatusCode::kUnimplemented,
                       Eq("Custom object properties are not supported for "
                          "schema 'stub'")));
  EXPECT_THAT(schema.EncodeFieldProperties(field_props, emitter),
              StatusIs(absl::StatusCode::kUnimplemented,
                       Eq("Custom field properties are not supported for "
                          "schema 'stub'")));
  EXPECT_THAT(schema.EncodeEnumProperties(enum_props, emitter),
              StatusIs(absl::StatusCode::kUnimplemented,
                       Eq("Custom enum properties are not supported for "
                          "schema 'stub'")));
  EXPECT_THAT(
      schema.EncodeEnumConstantProperties(enum_const_props, emitter),
      StatusIs(absl::StatusCode::kUnimplemented,
               Eq("Custom enum constant properties are not supported for "
                  "schema 'stub'")));
}

TEST(SchemaYamlRegistryTest, ConstructAndFindSchemas) {
  SchemaYamlRegistry empty_registry;
  EXPECT_THAT(empty_registry.Find("schema_a"), IsNull());

  SchemaYamlRegistry registry(std::make_unique<StubSchemaYaml>("schema_a"),
                              std::make_unique<StubSchemaYaml>("schema_b"));

  const SchemaYaml* schema_a = registry.Find("schema_a");
  ASSERT_THAT(schema_a, NotNull());
  EXPECT_EQ(schema_a->name(), "schema_a");

  const SchemaYaml* schema_b = registry.Find("schema_b");
  ASSERT_THAT(schema_b, NotNull());
  EXPECT_EQ(schema_b->name(), "schema_b");

  EXPECT_THAT(registry.Find("unknown_schema"), IsNull());
}

TEST(SchemaYamlRegistryTest, ConstructFromVector) {
  std::vector<std::unique_ptr<const SchemaYaml>> schemas;
  schemas.push_back(std::make_unique<StubSchemaYaml>("schema_a"));
  schemas.push_back(nullptr);
  SchemaYamlRegistry registry(std::move(schemas));

  EXPECT_THAT(registry.Find("schema_a"), NotNull());
  EXPECT_THAT(registry.Find("schema_b"), IsNull());
}

}  // namespace
}  // namespace cel
