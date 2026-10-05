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

#include <any>
#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "common/ast.h"
#include "common/constant.h"
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

struct TestStructProperties {
  std::string custom_name;
};
struct TestFieldProperties {
  std::string json_name;
};
struct TestEnumProperties {
  bool allow_alias = false;
};
struct TestEnumConstantProperties {
  std::string custom_name;
};

TEST(TypeDefTest, DefaultTypeDefIsStruct) {
  TypeDef def;
  EXPECT_TRUE(def.is_struct());
  EXPECT_FALSE(def.is_enum());
  EXPECT_EQ(def.kind_case(), TypeDefKindCase::kStruct);
  EXPECT_THAT(def.struct_type().name(), Eq(""));
  EXPECT_THAT(def.struct_type().doc(), Eq(""));
  EXPECT_THAT(def.struct_type().aspects(), IsEmpty());
  EXPECT_TRUE(def.struct_type().fields().empty());
  EXPECT_THAT(def.struct_type().FindField("id"), IsNull());
}

TEST(TypeDefTest, StructTypeDefConstructionAndLookup) {
  StructTypeDef person(
      "com.example.Person", "User account profile entity.",
      {{"json", TestStructProperties{.custom_name = "Person"}}});

  ASSERT_THAT(
      person.AddField(StructTypeDef::Field(
          "id", TypeSpec(PrimitiveType::kInt64), "Unique user identifier.")),
      IsOk());

  StructTypeDef::Field display_name_field(
      "display_name", TypeSpec(PrimitiveType::kString), "Display name.",
      Constant(StringConstant("Anonymous")),
      {
          {"json", TestFieldProperties{.json_name = "displayName"}},
          {"proto3", TestFieldProperties{.json_name = "display_name"}},
      });
  ASSERT_THAT(person.AddField(std::move(display_name_field)), IsOk());

  ASSERT_THAT(person.AddField(StructTypeDef::Field(
                  "tags", TypeSpec(ListTypeSpec(std::make_unique<TypeSpec>(
                              PrimitiveType::kString))))),
              IsOk());

  EXPECT_THAT(person.AddField(
                  StructTypeDef::Field("", TypeSpec(PrimitiveType::kString))),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Field name cannot be empty.")));

  EXPECT_THAT(person.AddField(
                  StructTypeDef::Field("id", TypeSpec(PrimitiveType::kString))),
              StatusIs(absl::StatusCode::kAlreadyExists,
                       HasSubstr("Field 'id' is already defined.")));

  const StructTypeDef::Field* id_field = person.FindField("id");
  ASSERT_THAT(id_field, NotNull());
  EXPECT_EQ(id_field->name(), "id");
  EXPECT_EQ(id_field->type(), TypeSpec(PrimitiveType::kInt64));
  EXPECT_EQ(id_field->doc(), "Unique user identifier.");
  EXPECT_FALSE(id_field->default_value().has_value());
  EXPECT_THAT(id_field->aspects(), IsEmpty());

  const StructTypeDef::Field* name_field = person.FindField("display_name");
  ASSERT_THAT(name_field, NotNull());
  EXPECT_EQ(name_field->name(), "display_name");
  EXPECT_TRUE(name_field->default_value().has_string_value());
  EXPECT_EQ(name_field->default_value().string_value(), "Anonymous");
  EXPECT_EQ(name_field->aspects().size(), 2);
  ASSERT_TRUE(name_field->aspects().contains("json"));
  EXPECT_EQ(std::any_cast<const TestFieldProperties&>(
                name_field->aspects().at("json"))
                .json_name,
            "displayName");
  ASSERT_TRUE(name_field->aspects().contains("proto3"));
  EXPECT_EQ(std::any_cast<const TestFieldProperties&>(
                name_field->aspects().at("proto3"))
                .json_name,
            "display_name");

  const StructTypeDef::Field* tags_field = person.FindField("tags");
  ASSERT_THAT(tags_field, NotNull());
  EXPECT_EQ(tags_field->name(), "tags");
  EXPECT_EQ(tags_field->type(),
            TypeSpec(ListTypeSpec(
                std::make_unique<TypeSpec>(PrimitiveType::kString))));

  EXPECT_EQ(person.aspects().size(), 1);
  ASSERT_TRUE(person.aspects().contains("json"));
  EXPECT_EQ(
      std::any_cast<const TestStructProperties&>(person.aspects().at("json"))
          .custom_name,
      "Person");

  EXPECT_THAT(person.FindField("nonexistent"), IsNull());

  TypeDef type_def(std::move(person));
  EXPECT_TRUE(type_def.is_struct());
  EXPECT_FALSE(type_def.is_enum());
  EXPECT_EQ(type_def.kind_case(), TypeDefKindCase::kStruct);
  EXPECT_EQ(type_def.struct_type().name(), "com.example.Person");
  EXPECT_EQ(type_def.struct_type().doc(), "User account profile entity.");
  EXPECT_EQ(type_def.struct_type().fields().size(), 3);
  EXPECT_EQ(type_def.struct_type().aspects().size(), 1);
  EXPECT_TRUE(type_def.struct_type().aspects().contains("json"));
}

TEST(TypeDefTest, EnumTypeDefConstructionAndLookup) {
  EnumTypeDef status_enum("com.example.OrderStatus",
                          "Lifecycle state of a customer order.",
                          {{"json", TestEnumProperties{.allow_alias = true}}});

  ASSERT_THAT(status_enum.AddConstant(EnumTypeDef::EnumConstant(
                  "ORDER_STATUS_UNSPECIFIED", 0, "Default unspecified state.")),
              IsOk());

  EnumTypeDef::EnumConstant shipped_val(
      "SHIPPED", 2, "Order has been dispatched.",
      {{"json", TestEnumConstantProperties{.custom_name = "shipped"}}});
  ASSERT_THAT(status_enum.AddConstant(std::move(shipped_val)), IsOk());

  EXPECT_THAT(status_enum.AddConstant(EnumTypeDef::EnumConstant("")),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Enum constant name cannot be empty.")));

  EXPECT_THAT(
      status_enum.AddConstant(EnumTypeDef::EnumConstant("SHIPPED")),
      StatusIs(absl::StatusCode::kAlreadyExists,
               HasSubstr("Enum constant 'SHIPPED' is already defined.")));

  const EnumTypeDef::EnumConstant* shipped =
      status_enum.FindConstant("SHIPPED");
  ASSERT_THAT(shipped, NotNull());
  EXPECT_EQ(shipped->name(), "SHIPPED");
  EXPECT_EQ(shipped->id(), 2);
  EXPECT_EQ(shipped->doc(), "Order has been dispatched.");
  ASSERT_TRUE(shipped->aspects().contains("json"));
  EXPECT_EQ(std::any_cast<const TestEnumConstantProperties&>(
                shipped->aspects().at("json"))
                .custom_name,
            "shipped");

  EnumTypeDef empty_enum;
  EXPECT_THAT(empty_enum.FindConstant("ORDER_STATUS_UNSPECIFIED"), IsNull());

  EXPECT_EQ(status_enum.aspects().size(), 1);
  ASSERT_TRUE(status_enum.aspects().contains("json"));
  EXPECT_TRUE(
      std::any_cast<const TestEnumProperties&>(status_enum.aspects().at("json"))
          .allow_alias);

  const EnumTypeDef::EnumConstant* unspecified =
      status_enum.FindConstant("ORDER_STATUS_UNSPECIFIED");
  ASSERT_THAT(unspecified, NotNull());
  EXPECT_EQ(unspecified->name(), "ORDER_STATUS_UNSPECIFIED");
  EXPECT_EQ(unspecified->id(), 0);
  EXPECT_EQ(unspecified->doc(), "Default unspecified state.");
  EXPECT_THAT(unspecified->aspects(), IsEmpty());

  EXPECT_THAT(status_enum.FindConstant("UNKNOWN"), IsNull());

  TypeDef type_def(std::move(status_enum));
  EXPECT_FALSE(type_def.is_struct());
  EXPECT_TRUE(type_def.is_enum());
  EXPECT_EQ(type_def.kind_case(), TypeDefKindCase::kEnum);
  EXPECT_EQ(type_def.enum_type().name(), "com.example.OrderStatus");
  EXPECT_EQ(type_def.enum_type().doc(), "Lifecycle state of a customer order.");
  EXPECT_EQ(type_def.enum_type().constants().size(), 2);
  EXPECT_EQ(type_def.enum_type().aspects().size(), 1);
  EXPECT_TRUE(type_def.enum_type().aspects().contains("json"));
}

TEST(TypeDefTest, DefaultEnumTypeDef) {
  TypeDef def((EnumTypeDef()));
  EXPECT_FALSE(def.is_struct());
  EXPECT_TRUE(def.is_enum());
  EXPECT_EQ(def.kind_case(), TypeDefKindCase::kEnum);
  EXPECT_THAT(def.enum_type().name(), Eq(""));
  EXPECT_THAT(def.enum_type().doc(), Eq(""));
  EXPECT_THAT(def.enum_type().aspects(), IsEmpty());
  EXPECT_TRUE(def.enum_type().constants().empty());
  EXPECT_THAT(def.enum_type().FindConstant("ANY"), IsNull());
}

}  // namespace
}  // namespace cel
