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

#include "common/typedef/schema_proto.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/string_view.h"
#include "common/typedef/schema.h"
#include "common/typedef/typedef.pb.h"
#include "internal/testing.h"

namespace cel {
namespace {

using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::Eq;
using ::testing::IsNull;
using ::testing::NotNull;

class StubSchemaProto final : public SchemaProto {
 public:
  explicit StubSchemaProto(absl::string_view name) : name_(name) {}

  absl::string_view name() const override { return name_; }

 private:
  std::string name_;
};

TEST(SchemaProtoTest, DefaultDecodeReturnsNullptr) {
  StubSchemaProto schema("stub");

  types::Object object_proto;
  types::Object::Field field_proto;
  types::Enum enum_proto;
  types::Enum::EnumConstant enum_const_proto;

  EXPECT_THAT(schema.DecodeObjectProperties(object_proto),
              IsOkAndHolds(IsNull()));
  EXPECT_THAT(schema.DecodeFieldProperties(field_proto),
              IsOkAndHolds(IsNull()));
  EXPECT_THAT(schema.DecodeEnumProperties(enum_proto), IsOkAndHolds(IsNull()));
  EXPECT_THAT(schema.DecodeEnumConstantProperties(enum_const_proto),
              IsOkAndHolds(IsNull()));
}

TEST(SchemaProtoTest, DefaultEncodeReturnsUnimplementedError) {
  StubSchemaProto schema("stub");
  types::Object object_proto;
  types::Object::Field field_proto;
  types::Enum enum_proto;
  types::Enum::EnumConstant enum_const_proto;

  SchemaObjectProperties obj_props;
  SchemaFieldProperties field_props;
  SchemaEnumProperties enum_props;
  SchemaEnumConstantProperties enum_const_props;

  EXPECT_THAT(schema.EncodeObjectProperties(obj_props, &object_proto),
              StatusIs(absl::StatusCode::kUnimplemented,
                       Eq("Custom object properties are not supported for "
                          "schema 'stub'")));
  EXPECT_THAT(schema.EncodeFieldProperties(field_props, &field_proto),
              StatusIs(absl::StatusCode::kUnimplemented,
                       Eq("Custom field properties are not supported for "
                          "schema 'stub'")));
  EXPECT_THAT(schema.EncodeEnumProperties(enum_props, &enum_proto),
              StatusIs(absl::StatusCode::kUnimplemented,
                       Eq("Custom enum properties are not supported for "
                          "schema 'stub'")));
  EXPECT_THAT(
      schema.EncodeEnumConstantProperties(enum_const_props, &enum_const_proto),
      StatusIs(absl::StatusCode::kUnimplemented,
               Eq("Custom enum constant properties are not supported for "
                  "schema 'stub'")));
}

TEST(SchemaProtoRegistryTest, ConstructAndFindSchemas) {
  SchemaProtoRegistry empty_registry;
  EXPECT_THAT(empty_registry.Find("schema_a"), IsNull());
  EXPECT_TRUE(empty_registry.schemas().empty());

  SchemaProtoRegistry registry(std::make_unique<StubSchemaProto>("schema_a"),
                               std::make_unique<StubSchemaProto>("schema_b"));

  const SchemaProto* schema_a = registry.Find("schema_a");
  ASSERT_THAT(schema_a, NotNull());
  EXPECT_EQ(schema_a->name(), "schema_a");

  const SchemaProto* schema_b = registry.Find("schema_b");
  ASSERT_THAT(schema_b, NotNull());
  EXPECT_EQ(schema_b->name(), "schema_b");

  EXPECT_THAT(registry.Find("unknown_schema"), IsNull());
  EXPECT_EQ(registry.schemas().size(), 2);
}

TEST(SchemaProtoRegistryTest, ConstructFromVector) {
  std::vector<std::unique_ptr<const SchemaProto>> schemas;
  schemas.push_back(std::make_unique<StubSchemaProto>("schema_a"));
  schemas.push_back(nullptr);
  SchemaProtoRegistry registry(std::move(schemas));

  EXPECT_THAT(registry.Find("schema_a"), NotNull());
  EXPECT_THAT(registry.Find("schema_b"), IsNull());
  EXPECT_EQ(registry.schemas().size(), 1);
}

}  // namespace
}  // namespace cel
