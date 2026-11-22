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

#include "common/typedef/typedef.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "common/ast.h"
#include "common/constant.h"
#include "common/typedef/schema.h"
#include "internal/testing.h"

namespace cel {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsNull;
using ::testing::NotNull;

class TestObjectProperties final : public SchemaObjectProperties {};
class TestFieldProperties final : public SchemaFieldProperties {};
class TestEnumProperties final : public SchemaEnumProperties {};
class TestEnumConstantProperties final : public SchemaEnumConstantProperties {};

TEST(TypeDefTest, DefaultTypeDefIsObject) {
  TypeDef def;
  EXPECT_TRUE(def.is_object());
  EXPECT_FALSE(def.is_enum());
  EXPECT_EQ(def.kind_case(), TypeDefKindCase::kObject);
  EXPECT_THAT(def.name(), Eq(""));
  EXPECT_THAT(def.doc(), Eq(""));
  EXPECT_THAT(def.object_type().schema_specific_properties, IsEmpty());
  EXPECT_TRUE(def.object_type().fields.empty());
  EXPECT_THAT(def.object_type().FindField("id"), IsNull());
}

TEST(TypeDefTest, ObjectTypeDefConstructionAndLookup) {
  ObjectTypeDef person;
  person.name = "com.example.Person";
  person.doc = "User account profile entity.";
  person.schema_specific_properties["json"] =
      std::make_unique<TestObjectProperties>();

  ASSERT_THAT(person.AddField(ObjectTypeDef::Field{
                  .name = "id",
                  .type = TypeSpec(PrimitiveType::kInt64),
                  .doc = "Unique user identifier.",
              }),
              IsOk());

  ObjectTypeDef::Field display_name_field{
      .name = "display_name",
      .type = TypeSpec(PrimitiveType::kString),
      .doc = "Display name.",
      .default_value = Constant(StringConstant("Anonymous")),
  };
  display_name_field.schema_specific_properties["json"] =
      std::make_unique<TestFieldProperties>();
  display_name_field.schema_specific_properties["proto3"] =
      std::make_unique<TestFieldProperties>();
  ASSERT_THAT(person.AddField(std::move(display_name_field)), IsOk());

  ASSERT_THAT(person.AddField(ObjectTypeDef::Field{
                  .name = "tags",
                  .type = TypeSpec(ListTypeSpec(
                      std::make_unique<TypeSpec>(PrimitiveType::kString))),
              }),
              IsOk());

  EXPECT_THAT(person.AddField(ObjectTypeDef::Field{
                  .name = "id",
                  .type = TypeSpec(PrimitiveType::kString),
              }),
              StatusIs(absl::StatusCode::kAlreadyExists,
                       HasSubstr("Field 'id' is already defined.")));

  const ObjectTypeDef::Field* id_field = person.FindField("id");
  ASSERT_THAT(id_field, NotNull());
  EXPECT_EQ(id_field->name, "id");
  EXPECT_EQ(id_field->type, TypeSpec(PrimitiveType::kInt64));
  EXPECT_EQ(id_field->doc, "Unique user identifier.");
  EXPECT_FALSE(id_field->default_value.has_value());
  EXPECT_THAT(id_field->schema_specific_properties, IsEmpty());

  const ObjectTypeDef::Field* name_field = person.FindField("display_name");
  ASSERT_THAT(name_field, NotNull());
  EXPECT_EQ(name_field->name, "display_name");
  EXPECT_TRUE(name_field->default_value.has_string_value());
  EXPECT_EQ(name_field->default_value.string_value(), "Anonymous");
  EXPECT_EQ(name_field->schema_specific_properties.size(), 2);
  EXPECT_TRUE(name_field->schema_specific_properties.contains("json"));
  EXPECT_TRUE(name_field->schema_specific_properties.contains("proto3"));

  const ObjectTypeDef::Field* tags_field = person.FindField("tags");
  ASSERT_THAT(tags_field, NotNull());
  EXPECT_EQ(tags_field->name, "tags");
  EXPECT_EQ(tags_field->type, TypeSpec(ListTypeSpec(std::make_unique<TypeSpec>(
                                  PrimitiveType::kString))));

  EXPECT_EQ(person.schema_specific_properties.size(), 1);
  EXPECT_TRUE(person.schema_specific_properties.contains("json"));

  EXPECT_THAT(person.FindField("nonexistent"), IsNull());

  TypeDef type_def(std::move(person));
  EXPECT_TRUE(type_def.is_object());
  EXPECT_FALSE(type_def.is_enum());
  EXPECT_EQ(type_def.kind_case(), TypeDefKindCase::kObject);
  EXPECT_EQ(type_def.name(), "com.example.Person");
  EXPECT_EQ(type_def.doc(), "User account profile entity.");
  EXPECT_EQ(type_def.object_type().fields.size(), 3);
  EXPECT_EQ(type_def.object_type().schema_specific_properties.size(), 1);
  EXPECT_TRUE(
      type_def.object_type().schema_specific_properties.contains("json"));
}

TEST(TypeDefTest, EnumTypeDefConstructionAndLookup) {
  EnumTypeDef status_enum;
  status_enum.name = "com.example.OrderStatus";
  status_enum.doc = "Lifecycle state of a customer order.";
  status_enum.schema_specific_properties["json"] =
      std::make_unique<TestEnumProperties>();

  ASSERT_THAT(status_enum.AddConstant(EnumTypeDef::EnumConstant{
                  .name = "ORDER_STATUS_UNSPECIFIED",
                  .doc = "Default unspecified state.",
              }),
              IsOk());

  EnumTypeDef::EnumConstant shipped_val{
      .name = "SHIPPED",
      .id = 2,
      .doc = "Order has been dispatched.",
  };
  shipped_val.schema_specific_properties["json"] =
      std::make_unique<TestEnumConstantProperties>();
  ASSERT_THAT(status_enum.AddConstant(std::move(shipped_val)), IsOk());

  EXPECT_THAT(
      status_enum.AddConstant(EnumTypeDef::EnumConstant{.name = "SHIPPED"}),
      StatusIs(absl::StatusCode::kAlreadyExists,
               HasSubstr("Enum constant 'SHIPPED' is already defined.")));

  const EnumTypeDef::EnumConstant* shipped =
      status_enum.FindConstant("SHIPPED");
  ASSERT_THAT(shipped, NotNull());
  EXPECT_EQ(shipped->name, "SHIPPED");
  EXPECT_EQ(shipped->id, 2);
  EXPECT_EQ(shipped->doc, "Order has been dispatched.");
  EXPECT_TRUE(shipped->schema_specific_properties.contains("json"));

  EnumTypeDef empty_enum;
  EXPECT_THAT(empty_enum.FindConstant("ORDER_STATUS_UNSPECIFIED"), IsNull());

  EXPECT_EQ(status_enum.schema_specific_properties.size(), 1);
  EXPECT_TRUE(status_enum.schema_specific_properties.contains("json"));

  const EnumTypeDef::EnumConstant* unspecified =
      status_enum.FindConstant("ORDER_STATUS_UNSPECIFIED");
  ASSERT_THAT(unspecified, NotNull());
  EXPECT_EQ(unspecified->name, "ORDER_STATUS_UNSPECIFIED");
  EXPECT_EQ(unspecified->id, 0);
  EXPECT_EQ(unspecified->doc, "Default unspecified state.");
  EXPECT_THAT(unspecified->schema_specific_properties, IsEmpty());

  EXPECT_THAT(status_enum.FindConstant("UNKNOWN"), IsNull());

  TypeDef type_def(std::move(status_enum));
  EXPECT_FALSE(type_def.is_object());
  EXPECT_TRUE(type_def.is_enum());
  EXPECT_EQ(type_def.kind_case(), TypeDefKindCase::kEnum);
  EXPECT_EQ(type_def.name(), "com.example.OrderStatus");
  EXPECT_EQ(type_def.doc(), "Lifecycle state of a customer order.");
  EXPECT_EQ(type_def.enum_type().constants.size(), 2);
  EXPECT_EQ(type_def.enum_type().schema_specific_properties.size(), 1);
  EXPECT_TRUE(type_def.enum_type().schema_specific_properties.contains("json"));
}

TEST(TypeDefTest, DefaultEnumTypeDef) {
  TypeDef def((EnumTypeDef()));
  EXPECT_FALSE(def.is_object());
  EXPECT_TRUE(def.is_enum());
  EXPECT_EQ(def.kind_case(), TypeDefKindCase::kEnum);
  EXPECT_THAT(def.name(), Eq(""));
  EXPECT_THAT(def.doc(), Eq(""));
  EXPECT_THAT(def.enum_type().schema_specific_properties, IsEmpty());
  EXPECT_TRUE(def.enum_type().constants.empty());
  EXPECT_THAT(def.enum_type().FindConstant("ANY"), IsNull());
}

}  // namespace
}  // namespace cel
