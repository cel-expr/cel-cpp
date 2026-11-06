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

#include <cstdint>
#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/types/span.h"
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
using ::testing::Pair;
using ::testing::Pointee;
using ::testing::SizeIs;
using ::testing::UnorderedElementsAre;

TEST(TypeDefTest, DefaultTypeDefIsStruct) {
  TypeDef def;
  EXPECT_TRUE(def.is_struct());
  EXPECT_FALSE(def.is_enum());
  EXPECT_EQ(def.kind_case(), TypeDefKindCase::kStruct);
  EXPECT_THAT(def.name(), Eq(""));
  EXPECT_THAT(def.doc(), Eq(""));
  EXPECT_THAT(def.aspects(), IsEmpty());
  EXPECT_THAT(def.struct_type().name(), Eq(""));
  EXPECT_THAT(def.struct_type().doc(), Eq(""));
  EXPECT_THAT(def.struct_type().aspects(), IsEmpty());
  EXPECT_TRUE(def.struct_type().members().empty());
  EXPECT_THAT(def.struct_type().FindField("id"), IsNull());
}

TEST(TypeDefTest, DefaultStructMemberIsField) {
  StructTypeDef::Member member;
  EXPECT_TRUE(member.is_field());
  EXPECT_FALSE(member.is_oneof());
  EXPECT_EQ(member.kind_case(), StructTypeDef::MemberKindCase::kField);
  EXPECT_THAT(member.field().name(), Eq(""));
}

TEST(TypeDefTest, StructTypeDefConstructionAndLookup) {
  StructTypeDef person(
      "com.example.Person", "User account profile entity.",
      {{"json",
        Aspect({{"custom_name", Constant(StringConstant("Person"))}})}});

  ASSERT_THAT(
      person.AddField(StructTypeDef::Field(
          "id", TypeSpec(PrimitiveType::kInt64), "Unique user identifier.")),
      IsOk());

  StructTypeDef::Field display_name_field(
      "display_name", TypeSpec(PrimitiveType::kString), "Display name.",
      Constant(StringConstant("Anonymous")),
      {
          {"json",
           Aspect({{"json_name", Constant(StringConstant("displayName"))}})},
          {"proto3",
           Aspect({{"json_name", Constant(StringConstant("display_name"))},
                   {"field_number", Constant(int64_t{2})}})},
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
  EXPECT_THAT(name_field->aspects(), SizeIs(2));
  ASSERT_TRUE(name_field->aspects().contains("json"));
  EXPECT_THAT(name_field->aspects().at("json").GetProperty("json_name"),
              Pointee(Eq(Constant(StringConstant("displayName")))));
  ASSERT_TRUE(name_field->aspects().contains("proto3"));
  EXPECT_THAT(name_field->aspects().at("proto3").GetProperty("json_name"),
              Pointee(Eq(Constant(StringConstant("display_name")))));
  EXPECT_THAT(name_field->aspects().at("proto3").GetProperty("field_number"),
              Pointee(Eq(Constant(int64_t{2}))));

  const StructTypeDef::Field* tags_field = person.FindField("tags");
  ASSERT_THAT(tags_field, NotNull());
  EXPECT_EQ(tags_field->name(), "tags");
  EXPECT_EQ(tags_field->type(),
            TypeSpec(ListTypeSpec(
                std::make_unique<TypeSpec>(PrimitiveType::kString))));

  EXPECT_THAT(person.aspects(), SizeIs(1));
  ASSERT_TRUE(person.aspects().contains("json"));
  EXPECT_THAT(person.aspects().at("json").GetProperty("custom_name"),
              Pointee(Eq(Constant(StringConstant("Person")))));

  EXPECT_THAT(person.FindField("nonexistent"), IsNull());

  TypeDef type_def(std::move(person));
  EXPECT_TRUE(type_def.is_struct());
  EXPECT_FALSE(type_def.is_enum());
  EXPECT_EQ(type_def.kind_case(), TypeDefKindCase::kStruct);
  EXPECT_EQ(type_def.name(), "com.example.Person");
  EXPECT_EQ(type_def.doc(), "User account profile entity.");
  EXPECT_THAT(type_def.aspects(), SizeIs(1));
  EXPECT_EQ(type_def.struct_type().name(), "com.example.Person");
  EXPECT_EQ(type_def.struct_type().doc(), "User account profile entity.");
  EXPECT_THAT(type_def.struct_type().members(), SizeIs(3));
  EXPECT_THAT(type_def.struct_type().aspects(), SizeIs(1));
  EXPECT_TRUE(type_def.struct_type().aspects().contains("json"));
}

TEST(TypeDefTest, StructTypeDefOneofContract) {
  StructTypeDef::Oneof oneof_def;
  EXPECT_TRUE(oneof_def.fields().empty());
  EXPECT_THAT(oneof_def.aspects(), IsEmpty());
  EXPECT_THAT(oneof_def.FindField("b"), IsNull());

  EXPECT_THAT(oneof_def.AddField(
                  StructTypeDef::Field("", TypeSpec(PrimitiveType::kInt64))),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Field name cannot be empty.")));

  ASSERT_THAT(oneof_def.AddField(
                  StructTypeDef::Field("b", TypeSpec(PrimitiveType::kInt64))),
              IsOk());
  ASSERT_THAT(oneof_def.AddField(
                  StructTypeDef::Field("c", TypeSpec(PrimitiveType::kDouble))),
              IsOk());
  EXPECT_THAT(oneof_def.AddField(
                  StructTypeDef::Field("b", TypeSpec(PrimitiveType::kString))),
              StatusIs(absl::StatusCode::kAlreadyExists,
                       HasSubstr("Field 'b' is already defined.")));

  ASSERT_EQ(oneof_def.fields().size(), 2);
  ASSERT_THAT(oneof_def.FindField("b"), NotNull());
  EXPECT_EQ(oneof_def.FindField("b")->name(), "b");
  EXPECT_THAT(oneof_def.FindField("missing"), IsNull());
}

TEST(TypeDefTest, StructTypeDefOneofConstructionAndLookup) {
  StructTypeDef def("com.example.Payment");
  ASSERT_THAT(
      def.AddField(StructTypeDef::Field("a", TypeSpec(PrimitiveType::kString))),
      IsOk());

  StructTypeDef::Oneof oneof_def(
      {{"proto", Aspect({{"name", Constant(StringConstant("choice"))}})}});
  ASSERT_THAT(oneof_def.AddField(
                  StructTypeDef::Field("b", TypeSpec(PrimitiveType::kInt64))),
              IsOk());
  ASSERT_THAT(oneof_def.AddField(
                  StructTypeDef::Field("c", TypeSpec(PrimitiveType::kDouble))),
              IsOk());
  ASSERT_THAT(def.AddOneof(std::move(oneof_def)), IsOk());
  ASSERT_THAT(def.AddOneof(StructTypeDef::Oneof()), IsOk());
  ASSERT_THAT(def.AddField(StructTypeDef::Field(
                  "d", TypeSpec(WellKnownTypeSpec::kDuration))),
              IsOk());

  ASSERT_EQ(def.members().size(), 4);
  EXPECT_TRUE(def.members()[0].is_field());
  EXPECT_EQ(def.members()[0].kind_case(),
            StructTypeDef::MemberKindCase::kField);
  EXPECT_EQ(def.members()[0].field().name(), "a");

  EXPECT_TRUE(def.members()[1].is_oneof());
  EXPECT_EQ(def.members()[1].kind_case(),
            StructTypeDef::MemberKindCase::kOneof);
  ASSERT_EQ(def.members()[1].oneof().fields().size(), 2);
  EXPECT_EQ(def.members()[1].oneof().fields()[0].name(), "b");
  EXPECT_EQ(def.members()[1].oneof().fields()[1].name(), "c");
  EXPECT_THAT(def.members()[1].oneof().aspects(), SizeIs(1));
  ASSERT_TRUE(def.members()[1].oneof().aspects().contains("proto"));
  EXPECT_THAT(
      def.members()[1].oneof().aspects().at("proto").GetProperty("name"),
      Pointee(Eq(Constant(StringConstant("choice")))));

  EXPECT_TRUE(def.members()[2].is_oneof());
  EXPECT_TRUE(def.members()[2].oneof().fields().empty());
  EXPECT_THAT(def.members()[2].oneof().aspects(), IsEmpty());

  EXPECT_TRUE(def.members()[3].is_field());
  EXPECT_EQ(def.members()[3].field().name(), "d");

  ASSERT_THAT(def.FindField("a"), NotNull());
  ASSERT_THAT(def.FindField("b"), NotNull());
  EXPECT_EQ(def.FindField("b")->type(), TypeSpec(PrimitiveType::kInt64));
  ASSERT_THAT(def.FindField("c"), NotNull());
  EXPECT_EQ(def.FindField("c")->type(), TypeSpec(PrimitiveType::kDouble));
  ASSERT_THAT(def.FindField("d"), NotNull());
  EXPECT_EQ(def.FindField("d")->type(), TypeSpec(WellKnownTypeSpec::kDuration));
  EXPECT_THAT(def.FindField("missing"), IsNull());
}

TEST(TypeDefTest, StructTypeDefOneofFieldCollisions) {
  StructTypeDef def("com.example.Payment");
  ASSERT_THAT(
      def.AddField(StructTypeDef::Field("a", TypeSpec(PrimitiveType::kString))),
      IsOk());
  StructTypeDef::Oneof oneof_def;
  ASSERT_THAT(oneof_def.AddField(
                  StructTypeDef::Field("b", TypeSpec(PrimitiveType::kInt64))),
              IsOk());
  ASSERT_THAT(oneof_def.AddField(
                  StructTypeDef::Field("c", TypeSpec(PrimitiveType::kDouble))),
              IsOk());
  ASSERT_THAT(def.AddOneof(std::move(oneof_def)), IsOk());

  // Standalone field colliding with field inside existing oneof.
  EXPECT_THAT(
      def.AddField(StructTypeDef::Field("b", TypeSpec(PrimitiveType::kString))),
      StatusIs(absl::StatusCode::kAlreadyExists,
               HasSubstr("Field 'b' is already defined.")));

  // Multi-field oneof colliding with existing standalone field leaves struct
  // unmodified without partial registration.
  StructTypeDef::Oneof dup_with_field;
  ASSERT_THAT(dup_with_field.AddField(StructTypeDef::Field(
                  "fresh", TypeSpec(PrimitiveType::kBool))),
              IsOk());
  ASSERT_THAT(dup_with_field.AddField(
                  StructTypeDef::Field("a", TypeSpec(PrimitiveType::kInt64))),
              IsOk());
  EXPECT_THAT(def.AddOneof(std::move(dup_with_field)),
              StatusIs(absl::StatusCode::kAlreadyExists,
                       HasSubstr("Field 'a' is already defined.")));
  EXPECT_THAT(def.FindField("fresh"), IsNull());
  EXPECT_EQ(def.members().size(), 2);

  // Oneof colliding with existing oneof field.
  StructTypeDef::Oneof dup_with_oneof;
  ASSERT_THAT(dup_with_oneof.AddField(
                  StructTypeDef::Field("c", TypeSpec(PrimitiveType::kInt64))),
              IsOk());
  EXPECT_THAT(def.AddOneof(std::move(dup_with_oneof)),
              StatusIs(absl::StatusCode::kAlreadyExists,
                       HasSubstr("Field 'c' is already defined.")));
  EXPECT_EQ(def.members().size(), 2);
}

TEST(TypeDefTest, EnumTypeDefConstructionAndLookup) {
  EnumTypeDef status_enum(
      "com.example.OrderStatus", "Lifecycle state of a customer order.",
      {{"proto2", Aspect({{"allow_alias", Constant(true)}})}});

  ASSERT_THAT(status_enum.AddConstant(EnumTypeDef::EnumConstant(
                  "ORDER_STATUS_UNSPECIFIED", 0, "Default unspecified state.")),
              IsOk());

  EnumTypeDef::EnumConstant shipped_val(
      "SHIPPED", 2, "Order has been dispatched.",
      {{"json",
        Aspect({{"custom_name", Constant(StringConstant("shipped"))}})}});
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
  EXPECT_THAT(shipped->aspects().at("json").GetProperty("custom_name"),
              Pointee(Eq(Constant(StringConstant("shipped")))));

  EnumTypeDef empty_enum;
  EXPECT_THAT(empty_enum.FindConstant("ORDER_STATUS_UNSPECIFIED"), IsNull());

  EXPECT_THAT(status_enum.aspects(), SizeIs(1));
  ASSERT_TRUE(status_enum.aspects().contains("proto2"));
  EXPECT_THAT(status_enum.aspects().at("proto2").GetProperty("allow_alias"),
              Pointee(Eq(Constant(true))));

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
  EXPECT_EQ(type_def.name(), "com.example.OrderStatus");
  EXPECT_EQ(type_def.doc(), "Lifecycle state of a customer order.");
  EXPECT_THAT(type_def.aspects(), SizeIs(1));
  EXPECT_EQ(type_def.enum_type().name(), "com.example.OrderStatus");
  EXPECT_EQ(type_def.enum_type().doc(), "Lifecycle state of a customer order.");
  EXPECT_THAT(type_def.enum_type().constants(), SizeIs(2));
  EXPECT_THAT(type_def.enum_type().aspects(), SizeIs(1));
  EXPECT_TRUE(type_def.enum_type().aspects().contains("proto2"));
}

TEST(TypeDefTest, DefaultEnumTypeDef) {
  TypeDef def((EnumTypeDef()));
  EXPECT_FALSE(def.is_struct());
  EXPECT_TRUE(def.is_enum());
  EXPECT_EQ(def.kind_case(), TypeDefKindCase::kEnum);
  EXPECT_THAT(def.name(), Eq(""));
  EXPECT_THAT(def.doc(), Eq(""));
  EXPECT_THAT(def.aspects(), IsEmpty());
  EXPECT_THAT(def.enum_type().name(), Eq(""));
  EXPECT_THAT(def.enum_type().doc(), Eq(""));
  EXPECT_THAT(def.enum_type().aspects(), IsEmpty());
  EXPECT_TRUE(def.enum_type().constants().empty());
  EXPECT_THAT(def.enum_type().FindConstant("ANY"), IsNull());
}

TEST(TypeDefTest,
     StructTypeDefOneofPreservesInsertionOrderAndCopyMoveIntegrity) {
  StructTypeDef::Oneof oneof;
  ASSERT_THAT(oneof.AddField(StructTypeDef::Field(
                  "zeta", TypeSpec(PrimitiveType::kInt64))),
              IsOk());
  ASSERT_THAT(oneof.AddField(StructTypeDef::Field(
                  "alpha", TypeSpec(PrimitiveType::kString))),
              IsOk());
  ASSERT_THAT(oneof.AddField(
                  StructTypeDef::Field("mu", TypeSpec(PrimitiveType::kBool))),
              IsOk());
  ASSERT_THAT(oneof.AddField(StructTypeDef::Field(
                  "beta", TypeSpec(PrimitiveType::kDouble))),
              IsOk());
  ASSERT_THAT(oneof.AddField(StructTypeDef::Field(
                  "omega", TypeSpec(PrimitiveType::kBytes))),
              IsOk());

  absl::Span<const StructTypeDef::Field> fields = oneof.fields();
  ASSERT_THAT(fields, SizeIs(5));
  EXPECT_EQ(fields[0].name(), "zeta");
  EXPECT_EQ(fields[1].name(), "alpha");
  EXPECT_EQ(fields[2].name(), "mu");
  EXPECT_EQ(fields[3].name(), "beta");
  EXPECT_EQ(fields[4].name(), "omega");

  EXPECT_EQ(oneof.FindField("zeta"), &fields[0]);
  EXPECT_EQ(oneof.FindField("alpha"), &fields[1]);
  EXPECT_EQ(oneof.FindField("mu"), &fields[2]);
  EXPECT_EQ(oneof.FindField("beta"), &fields[3]);
  EXPECT_EQ(oneof.FindField("omega"), &fields[4]);
  EXPECT_THAT(oneof.FindField("missing"), IsNull());

  StructTypeDef::Oneof copy_oneof = oneof;
  ASSERT_THAT(copy_oneof.fields(), SizeIs(5));
  EXPECT_EQ(copy_oneof.FindField("zeta"), &copy_oneof.fields()[0]);
  EXPECT_EQ(copy_oneof.FindField("omega"), &copy_oneof.fields()[4]);

  StructTypeDef::Oneof move_oneof = std::move(copy_oneof);
  ASSERT_THAT(move_oneof.fields(), SizeIs(5));
  EXPECT_EQ(move_oneof.FindField("alpha"), &move_oneof.fields()[1]);
  EXPECT_EQ(move_oneof.FindField("beta"), &move_oneof.fields()[3]);
}

TEST(TypeDefTest, StructTypeDefPreservesInsertionOrderAndCopyMoveIntegrity) {
  StructTypeDef def("com.example.TestStruct");

  ASSERT_THAT(def.AddField(StructTypeDef::Field(
                  "zeta", TypeSpec(PrimitiveType::kInt64))),
              IsOk());
  ASSERT_THAT(def.AddField(StructTypeDef::Field(
                  "alpha", TypeSpec(PrimitiveType::kString))),
              IsOk());

  StructTypeDef::Oneof choice;
  ASSERT_THAT(choice.AddField(StructTypeDef::Field(
                  "choice_z", TypeSpec(PrimitiveType::kInt64))),
              IsOk());
  ASSERT_THAT(choice.AddField(StructTypeDef::Field(
                  "choice_a", TypeSpec(PrimitiveType::kString))),
              IsOk());
  ASSERT_THAT(choice.AddField(StructTypeDef::Field(
                  "choice_m", TypeSpec(PrimitiveType::kBool))),
              IsOk());
  ASSERT_THAT(def.AddOneof(std::move(choice)), IsOk());

  ASSERT_THAT(
      def.AddField(StructTypeDef::Field("mu", TypeSpec(PrimitiveType::kBool))),
      IsOk());
  ASSERT_THAT(def.AddField(StructTypeDef::Field(
                  "beta", TypeSpec(PrimitiveType::kDouble))),
              IsOk());
  ASSERT_THAT(def.AddField(StructTypeDef::Field(
                  "omega", TypeSpec(PrimitiveType::kBytes))),
              IsOk());

  absl::Span<const StructTypeDef::Member> members = def.members();
  ASSERT_THAT(members, SizeIs(6));
  ASSERT_TRUE(members[0].is_field());
  EXPECT_EQ(members[0].field().name(), "zeta");
  ASSERT_TRUE(members[1].is_field());
  EXPECT_EQ(members[1].field().name(), "alpha");
  ASSERT_TRUE(members[2].is_oneof());
  ASSERT_THAT(members[2].oneof().fields(), SizeIs(3));
  EXPECT_EQ(members[2].oneof().fields()[0].name(), "choice_z");
  EXPECT_EQ(members[2].oneof().fields()[1].name(), "choice_a");
  EXPECT_EQ(members[2].oneof().fields()[2].name(), "choice_m");
  ASSERT_TRUE(members[3].is_field());
  EXPECT_EQ(members[3].field().name(), "mu");
  ASSERT_TRUE(members[4].is_field());
  EXPECT_EQ(members[4].field().name(), "beta");
  ASSERT_TRUE(members[5].is_field());
  EXPECT_EQ(members[5].field().name(), "omega");

  EXPECT_EQ(def.FindField("zeta"), &members[0].field());
  EXPECT_EQ(def.FindField("alpha"), &members[1].field());
  EXPECT_EQ(def.FindField("choice_z"), &members[2].oneof().fields()[0]);
  EXPECT_EQ(def.FindField("choice_a"), &members[2].oneof().fields()[1]);
  EXPECT_EQ(def.FindField("choice_m"), &members[2].oneof().fields()[2]);
  EXPECT_EQ(def.FindField("mu"), &members[3].field());
  EXPECT_EQ(def.FindField("beta"), &members[4].field());
  EXPECT_EQ(def.FindField("omega"), &members[5].field());
  EXPECT_THAT(def.FindField("missing"), IsNull());

  StructTypeDef copy_def = def;
  ASSERT_THAT(copy_def.members(), SizeIs(6));
  EXPECT_EQ(copy_def.FindField("zeta"), &copy_def.members()[0].field());
  EXPECT_EQ(copy_def.FindField("choice_z"),
            &copy_def.members()[2].oneof().fields()[0]);
  EXPECT_EQ(copy_def.FindField("choice_m"),
            &copy_def.members()[2].oneof().fields()[2]);
  EXPECT_EQ(copy_def.FindField("omega"), &copy_def.members()[5].field());

  StructTypeDef move_def = std::move(copy_def);
  ASSERT_THAT(move_def.members(), SizeIs(6));
  EXPECT_EQ(move_def.FindField("alpha"), &move_def.members()[1].field());
  EXPECT_EQ(move_def.FindField("choice_a"),
            &move_def.members()[2].oneof().fields()[1]);
  EXPECT_EQ(move_def.FindField("beta"), &move_def.members()[4].field());
}

TEST(TypeDefTest, EnumTypeDefPreservesInsertionOrderAndCopyMoveIntegrity) {
  EnumTypeDef def("com.example.TestEnum");

  ASSERT_THAT(def.AddConstant(EnumTypeDef::EnumConstant("ZETA", 10)), IsOk());
  ASSERT_THAT(def.AddConstant(EnumTypeDef::EnumConstant("ALPHA", 0)), IsOk());
  ASSERT_THAT(def.AddConstant(EnumTypeDef::EnumConstant("MU", 5)), IsOk());
  ASSERT_THAT(def.AddConstant(EnumTypeDef::EnumConstant("BETA", 5)), IsOk());
  ASSERT_THAT(def.AddConstant(EnumTypeDef::EnumConstant("OMEGA", -1)), IsOk());

  absl::Span<const EnumTypeDef::EnumConstant> constants = def.constants();
  ASSERT_THAT(constants, SizeIs(5));
  EXPECT_EQ(constants[0].name(), "ZETA");
  EXPECT_EQ(constants[0].id(), 10);
  EXPECT_EQ(constants[1].name(), "ALPHA");
  EXPECT_EQ(constants[1].id(), 0);
  EXPECT_EQ(constants[2].name(), "MU");
  EXPECT_EQ(constants[2].id(), 5);
  EXPECT_EQ(constants[3].name(), "BETA");
  EXPECT_EQ(constants[3].id(), 5);
  EXPECT_EQ(constants[4].name(), "OMEGA");
  EXPECT_EQ(constants[4].id(), -1);

  EXPECT_EQ(def.FindConstant("ZETA"), &constants[0]);
  EXPECT_EQ(def.FindConstant("ALPHA"), &constants[1]);
  EXPECT_EQ(def.FindConstant("MU"), &constants[2]);
  EXPECT_EQ(def.FindConstant("BETA"), &constants[3]);
  EXPECT_EQ(def.FindConstant("OMEGA"), &constants[4]);
  EXPECT_THAT(def.FindConstant("MISSING"), IsNull());

  EnumTypeDef copy_def = def;
  ASSERT_THAT(copy_def.constants(), SizeIs(5));
  EXPECT_EQ(copy_def.FindConstant("ZETA"), &copy_def.constants()[0]);
  EXPECT_EQ(copy_def.FindConstant("OMEGA"), &copy_def.constants()[4]);

  EnumTypeDef move_def = std::move(copy_def);
  ASSERT_THAT(move_def.constants(), SizeIs(5));
  EXPECT_EQ(move_def.FindConstant("ALPHA"), &move_def.constants()[1]);
  EXPECT_EQ(move_def.FindConstant("MU"), &move_def.constants()[2]);
}

TEST(TypeDefSetTest, ConstructionLookupAndErrors) {
  TypeDefSet set;
  EXPECT_THAT(set.types(), IsEmpty());
  EXPECT_THAT(set.FindTypeDef("com.example.Person"), IsNull());

  // Empty name is rejected.
  EXPECT_THAT(set.AddTypeDef(TypeDef()),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Type name cannot be empty.")));

  StructTypeDef person("com.example.Person");
  ASSERT_THAT(set.AddTypeDef(TypeDef(std::move(person))), IsOk());

  EnumTypeDef status("com.example.Status");
  ASSERT_THAT(set.AddTypeDef(TypeDef(std::move(status))), IsOk());

  ASSERT_EQ(set.types().size(), 2);
  EXPECT_EQ(set.types()[0].struct_type().name(), "com.example.Person");
  EXPECT_EQ(set.types()[1].enum_type().name(), "com.example.Status");

  // Duplicate name is rejected (even if switching between struct and enum).
  EnumTypeDef dup_person("com.example.Person");
  EXPECT_THAT(
      set.AddTypeDef(TypeDef(std::move(dup_person))),
      StatusIs(absl::StatusCode::kAlreadyExists,
               HasSubstr("Type 'com.example.Person' is already defined.")));

  const TypeDef* found_person = set.FindTypeDef("com.example.Person");
  ASSERT_THAT(found_person, NotNull());
  EXPECT_TRUE(found_person->is_struct());
  EXPECT_EQ(found_person->struct_type().name(), "com.example.Person");

  const TypeDef* found_status = set.FindTypeDef("com.example.Status");
  ASSERT_THAT(found_status, NotNull());
  EXPECT_TRUE(found_status->is_enum());
  EXPECT_EQ(found_status->enum_type().name(), "com.example.Status");

  EXPECT_THAT(set.FindTypeDef("com.example.Missing"), IsNull());

  // Copy and move preserve lookup integrity.
  TypeDefSet copy_set = set;
  ASSERT_THAT(copy_set.FindTypeDef("com.example.Person"), NotNull());
  EXPECT_EQ(copy_set.FindTypeDef("com.example.Person"), &copy_set.types()[0]);

  TypeDefSet move_set = std::move(copy_set);
  ASSERT_THAT(move_set.FindTypeDef("com.example.Status"), NotNull());
  EXPECT_EQ(move_set.FindTypeDef("com.example.Status"), &move_set.types()[1]);
}

TEST(TypeDefTest, DefaultAspect) {
  Aspect aspect;
  EXPECT_THAT(aspect.properties(), IsEmpty());
  EXPECT_THAT(aspect.GetProperty("missing"), IsNull());
}

TEST(TypeDefTest, AspectConstructionAndGetProperty) {
  Aspect aspect({
      {"json_name", Constant(StringConstant("displayName"))},
      {"field_number", Constant(int64_t{2})},
      {"deprecated", Constant(true)},
  });

  EXPECT_THAT(aspect.properties(),
              UnorderedElementsAre(
                  Pair("json_name", Constant(StringConstant("displayName"))),
                  Pair("field_number", Constant(int64_t{2})),
                  Pair("deprecated", Constant(true))));
  EXPECT_THAT(aspect.GetProperty("json_name"),
              Pointee(Eq(Constant(StringConstant("displayName")))));
  EXPECT_THAT(aspect.GetProperty("field_number"),
              Pointee(Eq(Constant(int64_t{2}))));
  EXPECT_THAT(aspect.GetProperty("deprecated"), Pointee(Eq(Constant(true))));
  EXPECT_THAT(aspect.GetProperty("nonexistent"), IsNull());
}

}  // namespace
}  // namespace cel
