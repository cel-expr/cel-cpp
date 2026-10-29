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

#include <optional>
#include <string>
#include <utility>

#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "common/ast.h"
#include "common/constant.h"
#include "common/typedef/typedef.h"
#include "common/typedef/typedef.pb.h"
#include "internal/proto_matchers.h"
#include "internal/runfiles.h"
#include "internal/testing.h"
#include "google/protobuf/text_format.h"

namespace cel {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::cel::internal::test::EqualsProto;
using ::testing::HasSubstr;
using ::testing::NotNull;
using ::testing::SizeIs;

constexpr absl::string_view kTestTypeDefFilePath =
"_main/common/typedef/testdata/";

Constant MakeInvalidDurationConstant() {
  Constant c;
  c.set_duration_value(absl::InfiniteDuration());
  return c;
}

TEST(TypeDefProtoTest, ToProtoClearsExistingOutput) {
  types::TypeDef out_def;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        enum_type {
          name: "com.example.StaleEnum"
          constants { name: "STALE" id: 9 }
        }
      )pb",
      &out_def));

  TypeDef fresh_def(StructTypeDef("com.example.FreshStruct"));
  ASSERT_THAT(TypeDefToProto(fresh_def, &out_def), IsOk());

  types::TypeDef expected_def;
  expected_def.mutable_struct_type()->set_name("com.example.FreshStruct");
  EXPECT_THAT(out_def, EqualsProto(expected_def));
}

struct TypeDefProtoTestCase {
  std::string textproto;
  absl::StatusCode expected_code = absl::StatusCode::kOk;
  std::string expected_error;
  std::optional<std::string> expected_typedef;
};

class TypeDefProtoTest : public testing::TestWithParam<TypeDefProtoTestCase> {};

TEST_P(TypeDefProtoTest, TypeDefProto) {
  const TypeDefProtoTestCase& param = GetParam();

  types::TypeDef proto;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(param.textproto, &proto));

  absl::StatusOr<TypeDef> decoded = TypeDefFromProto(proto);
  ASSERT_THAT(decoded,
              StatusIs(param.expected_code, HasSubstr(param.expected_error)));
  if (!decoded.ok()) {
    return;
  }

  types::TypeDef encoded;
  ASSERT_THAT(TypeDefToProto(*decoded, &encoded), IsOk());

  types::TypeDef expected_proto;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      param.expected_typedef.value_or(param.textproto), &expected_proto));
  EXPECT_THAT(encoded, EqualsProto(expected_proto));
}

INSTANTIATE_TEST_SUITE_P(
    TypeDefProtoTest, TypeDefProtoTest,
    ::testing::Values(
        // Success cases.
        TypeDefProtoTestCase{
            .textproto = R"pb(struct_type {})pb",
        },
        TypeDefProtoTestCase{
            .textproto = R"pb(enum_type {})pb",
        },
        TypeDefProtoTestCase{
            .textproto = R"pb(
              struct_type {
                name: "com.example.UnsetFieldTypeNormalized"
                members {
                  field {
                    name: "unset_type_field"
                    type {}
                  }
                }
              }
            )pb",
            .expected_typedef = R"pb(
              struct_type {
                name: "com.example.UnsetFieldTypeNormalized"
                members { field { name: "unset_type_field" } }
              }
            )pb",
        },
        TypeDefProtoTestCase{
            .textproto = R"pb(
              struct_type {
                name: "com.example.UnsetFieldDefault"
                members {
                  field {
                    name: "unset_default_field"
                    default_value {}
                  }
                }
              }
            )pb",
            .expected_typedef = R"pb(
              struct_type {
                name: "com.example.UnsetFieldDefault"
                members { field { name: "unset_default_field" } }
              }
            )pb",
        },
        TypeDefProtoTestCase{
            .textproto = R"pb(
              struct_type {
                name: "com.example.Order"
                doc: "Customer order entity."
                members {
                  field {
                    name: "order_id"
                    type { primitive: STRING }
                    doc: "Unique order ID."
                    aspects {
                      key: "proto"
                      value {
                        properties {
                          key: "id"
                          value { int64_value: 1 }
                        }
                        properties {
                          key: "json_name"
                          value { string_value: "orderId" }
                        }
                        properties {
                          key: "name"
                          value { string_value: "order_id_proto" }
                        }
                        properties {
                          key: "unset"
                          value {}
                        }
                      }
                    }
                  }
                }
                members {
                  field {
                    name: "empty_aspect_field"
                    type { primitive: STRING }
                    aspects {
                      key: "proto"
                      value {}
                    }
                  }
                }
                members {
                  oneof_type {
                    fields {
                      name: "coupon_code"
                      type { primitive: STRING }
                      aspects {
                        key: "proto"
                        value {
                          properties {
                            key: "id"
                            value { int64_value: 3 }
                          }
                        }
                      }
                    }
                    aspects {
                      key: "proto"
                      value {
                        properties {
                          key: "name"
                          value { string_value: "discount" }
                        }
                      }
                    }
                  }
                }
                members { oneof_type {} }
                aspects {
                  key: "custom"
                  value {
                    properties {
                      key: "version"
                      value { int64_value: 2 }
                    }
                  }
                }
                aspects {
                  key: "proto"
                  value {
                    properties {
                      key: "name"
                      value { string_value: "com.example.proto.Order" }
                    }
                  }
                }
              }
            )pb",
        },
        TypeDefProtoTestCase{
            .textproto = R"pb(
              enum_type {
                name: "com.example.OrderStatus"
                doc: "Lifecycle status of an order."
                constants {
                  name: "ORDER_STATUS_ACTIVE"
                  id: 1
                  doc: "Active status."
                  aspects {
                    key: "proto"
                    value {
                      properties {
                        key: "name"
                        value { string_value: "STATUS_ACTIVE" }
                      }
                    }
                  }
                }
                constants {
                  name: "ORDER_STATUS_LEGACY"
                  id: -1
                  aspects {
                    key: "proto"
                    value {}
                  }
                }
                aspects {
                  key: "proto"
                  value {
                    properties {
                      key: "allow_alias"
                      value { bool_value: true }
                    }
                    properties {
                      key: "name"
                      value { string_value: "com.example.proto.OrderStatus" }
                    }
                  }
                }
              }
            )pb",
        },
        // Failure cases.
        TypeDefProtoTestCase{
            .textproto = "",
            .expected_code = absl::StatusCode::kInvalidArgument,
            .expected_error =
                "cel.types.TypeDef must specify 'struct_type' or 'enum_type'",
        },
        TypeDefProtoTestCase{
            .textproto = R"pb(
              struct_type {
                name: "com.example.UnsetMember"
                members {}
              }
            )pb",
            .expected_code = absl::StatusCode::kInvalidArgument,
            .expected_error =
                "cel.types.Struct.Member must specify 'field' or 'oneof_type'",
        },
        TypeDefProtoTestCase{
            .textproto = R"pb(
              struct_type {
                name: "com.example.EmptyFieldName"
                members { field { name: "" } }
              }
            )pb",
            .expected_code = absl::StatusCode::kInvalidArgument,
            .expected_error = "Field name cannot be empty.",
        },
        TypeDefProtoTestCase{
            .textproto = R"pb(
              struct_type {
                name: "com.example.EmptyFieldNameInOneof"
                members { oneof_type { fields {} } }
              }
            )pb",
            .expected_code = absl::StatusCode::kInvalidArgument,
            .expected_error = "Field name cannot be empty.",
        },
        TypeDefProtoTestCase{
            .textproto = R"pb(
              struct_type {
                name: "com.example.DupFields"
                members { field { name: "dup" } }
                members { field { name: "dup" } }
              }
            )pb",
            .expected_code = absl::StatusCode::kAlreadyExists,
            .expected_error = "Field 'dup' is already defined.",
        },
        TypeDefProtoTestCase{
            .textproto = R"pb(
              struct_type {
                name: "com.example.DupInOneof"
                members {
                  oneof_type {
                    fields { name: "dup" }
                    fields { name: "dup" }
                  }
                }
              }
            )pb",
            .expected_code = absl::StatusCode::kAlreadyExists,
            .expected_error = "Field 'dup' is already defined.",
        },
        TypeDefProtoTestCase{
            .textproto = R"pb(
              struct_type {
                name: "com.example.DupAcrossFieldAndOneof"
                members { field { name: "dup" } }
                members { oneof_type { fields { name: "dup" } } }
              }
            )pb",
            .expected_code = absl::StatusCode::kAlreadyExists,
            .expected_error = "Field 'dup' is already defined.",
        },
        TypeDefProtoTestCase{
            .textproto = R"pb(
              struct_type {
                name: "com.example.DupAcrossOneofAndField"
                members { oneof_type { fields { name: "dup" } } }
                members { field { name: "dup" } }
              }
            )pb",
            .expected_code = absl::StatusCode::kAlreadyExists,
            .expected_error = "Field 'dup' is already defined.",
        },
        TypeDefProtoTestCase{
            .textproto = R"pb(
              enum_type {
                name: "com.example.EmptyConstantName"
                constants { name: "" }
              }
            )pb",
            .expected_code = absl::StatusCode::kInvalidArgument,
            .expected_error = "Enum constant name cannot be empty.",
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
        TypeDefProtoTestCase{
            .textproto = R"pb(
              struct_type {
                name: "com.example.BadPrim"
                members {
                  field {
                    name: "f"
                    type { primitive: 999 }
                  }
                }
              }
            )pb",
            .expected_code = absl::StatusCode::kInvalidArgument,
            .expected_error = "PrimitiveType",
        },
        TypeDefProtoTestCase{
            .textproto = R"pb(
              struct_type {
                name: "com.example.BadWk"
                members {
                  field {
                    name: "f"
                    type { well_known: 999 }
                  }
                }
              }
            )pb",
            .expected_code = absl::StatusCode::kInvalidArgument,
            .expected_error = "WellKnownType",
        },
        TypeDefProtoTestCase{
            .textproto = R"pb(
              struct_type {
                name: "com.example.BadPrimInOneof"
                members {
                  oneof_type {
                    fields {
                      name: "f"
                      type { primitive: 999 }
                    }
                  }
                }
              }
            )pb",
            .expected_code = absl::StatusCode::kInvalidArgument,
            .expected_error = "PrimitiveType",
        }));

struct TypeDefToProtoErrorCase {
  TypeDef type_def;
  absl::StatusCode expected_code = absl::StatusCode::kInvalidArgument;
  std::string expected_error;
};

class TypeDefToProtoErrorTest
    : public testing::TestWithParam<TypeDefToProtoErrorCase> {};

TEST_P(TypeDefToProtoErrorTest, TypeDefToProtoError) {
  const TypeDefToProtoErrorCase& param = GetParam();

  types::TypeDef encoded;
  EXPECT_THAT(TypeDefToProto(param.type_def, &encoded),
              StatusIs(param.expected_code, HasSubstr(param.expected_error)));
}

INSTANTIATE_TEST_SUITE_P(
    TypeDefToProtoErrorTest, TypeDefToProtoErrorTest,
    ::testing::Values(
        TypeDefToProtoErrorCase{
            .type_def =
                [] {
                  StructTypeDef s("com.example.BadOneofField");
                  StructTypeDef::Oneof oneof_def;
                  ABSL_CHECK_OK(oneof_def.AddField(StructTypeDef::Field(
                      "f", TypeSpec(static_cast<PrimitiveType>(999)))));
                  ABSL_CHECK_OK(s.AddOneof(std::move(oneof_def)));
                  return TypeDef(std::move(s));
                }(),
            .expected_code = absl::StatusCode::kInvalidArgument,
            .expected_error = "Unsupported primitive type",
        },
        TypeDefToProtoErrorCase{
            .type_def =
                [] {
                  StructTypeDef s("com.example.BadPrim");
                  ABSL_CHECK_OK(s.AddField(StructTypeDef::Field(
                      "f", TypeSpec(static_cast<PrimitiveType>(999)))));
                  return TypeDef(std::move(s));
                }(),
            .expected_code = absl::StatusCode::kInvalidArgument,
            .expected_error = "Unsupported primitive type",
        },
        TypeDefToProtoErrorCase{
            .type_def =
                [] {
                  StructTypeDef s("com.example.BadWk");
                  ABSL_CHECK_OK(s.AddField(StructTypeDef::Field(
                      "f", TypeSpec(static_cast<WellKnownTypeSpec>(999)))));
                  return TypeDef(std::move(s));
                }(),
            .expected_code = absl::StatusCode::kInvalidArgument,
            .expected_error = "Unsupported well-known type",
        },
        TypeDefToProtoErrorCase{
            .type_def =
                [] {
                  StructTypeDef s("com.example.BadFieldDefault");
                  ABSL_CHECK_OK(s.AddField(StructTypeDef::Field(
                      "f", TypeSpec(WellKnownTypeSpec::kDuration), "",
                      MakeInvalidDurationConstant())));
                  return TypeDef(std::move(s));
                }(),
            .expected_code = absl::StatusCode::kInvalidArgument,
        },
        TypeDefToProtoErrorCase{
            .type_def = TypeDef(StructTypeDef(
                "com.example.BadStructAspect", "",
                {{"proto", Aspect({{"d", MakeInvalidDurationConstant()}})}})),
            .expected_code = absl::StatusCode::kInvalidArgument,
        },
        TypeDefToProtoErrorCase{
            .type_def =
                [] {
                  StructTypeDef s("com.example.BadFieldAspect");
                  ABSL_CHECK_OK(s.AddField(StructTypeDef::Field(
                      "f", TypeSpec(), "", {},
                      {{"proto",
                        Aspect({{"d", MakeInvalidDurationConstant()}})}})));
                  return TypeDef(std::move(s));
                }(),
            .expected_code = absl::StatusCode::kInvalidArgument,
        },
        TypeDefToProtoErrorCase{
            .type_def =
                [] {
                  StructTypeDef s("com.example.BadOneofAspect");
                  StructTypeDef::Oneof oneof_def(
                      {{"proto",
                        Aspect({{"d", MakeInvalidDurationConstant()}})}});
                  ABSL_CHECK_OK(s.AddOneof(std::move(oneof_def)));
                  return TypeDef(std::move(s));
                }(),
            .expected_code = absl::StatusCode::kInvalidArgument,
        },
        TypeDefToProtoErrorCase{
            .type_def = TypeDef(EnumTypeDef(
                "com.example.BadEnumAspect", "",
                {{"proto", Aspect({{"d", MakeInvalidDurationConstant()}})}})),
            .expected_code = absl::StatusCode::kInvalidArgument,
        },
        TypeDefToProtoErrorCase{
            .type_def =
                [] {
                  EnumTypeDef e("com.example.BadEnumConstAspect");
                  ABSL_CHECK_OK(e.AddConstant(EnumTypeDef::EnumConstant(
                      "C", 0, "",
                      {{"proto",
                        Aspect({{"d", MakeInvalidDurationConstant()}})}})));
                  return TypeDef(std::move(e));
                }(),
            .expected_code = absl::StatusCode::kInvalidArgument,
        }));

TEST(TypeDefSetProtoTest, ToProtoClearsExistingOutput) {
  types::TypeDefSet out_set;
  out_set.add_types()->mutable_struct_type()->set_name("com.example.Stale1");
  out_set.add_types()->mutable_struct_type()->set_name("com.example.Stale2");

  TypeDefSet fresh_set;
  ASSERT_THAT(
      fresh_set.AddTypeDef(TypeDef(StructTypeDef("com.example.FreshStruct"))),
      IsOk());
  ASSERT_THAT(TypeDefSetToProto(fresh_set, &out_set), IsOk());

  types::TypeDefSet expected_set;
  expected_set.add_types()->mutable_struct_type()->set_name(
      "com.example.FreshStruct");
  EXPECT_THAT(out_set, EqualsProto(expected_set));
}

TEST(TypeDefSetProtoTest, TextProtoRoundTrip) {
  std::string contents;
  std::string test_file = cel::internal::ResolveRunfilesPath(
      absl::StrCat(kTestTypeDefFilePath, "cel_types.txtproto"));
  ASSERT_THAT(cel::internal::GetFileContents(test_file, &contents), IsOk());

  types::TypeDefSet proto_set;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(contents, &proto_set));
  ASSERT_OK_AND_ASSIGN(TypeDefSet type_def_set, TypeDefSetFromProto(proto_set));
  EXPECT_THAT(type_def_set.types(), SizeIs(proto_set.types_size()));
  EXPECT_THAT(type_def_set.FindTypeDef("com.example.store.Order"), NotNull());

  types::TypeDefSet roundtrip_proto_set;
  ASSERT_THAT(TypeDefSetToProto(type_def_set, &roundtrip_proto_set), IsOk());
  EXPECT_THAT(roundtrip_proto_set, EqualsProto(proto_set));
}

TEST(TypeDefSetProtoTest, Errors) {
  types::TypeDefSet unnamed_set_proto;
  unnamed_set_proto.add_types()->mutable_struct_type();
  EXPECT_THAT(TypeDefSetFromProto(unnamed_set_proto),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Type name cannot be empty.")));

  types::TypeDefSet duplicate_set_proto;
  duplicate_set_proto.add_types()->mutable_struct_type()->set_name(
      "com.example.Dup");
  duplicate_set_proto.add_types()->mutable_enum_type()->set_name(
      "com.example.Dup");
  EXPECT_THAT(
      TypeDefSetFromProto(duplicate_set_proto),
      StatusIs(absl::StatusCode::kAlreadyExists,
               HasSubstr("Type 'com.example.Dup' is already defined.")));

  types::TypeDefSet invalid_elem_proto;
  invalid_elem_proto.add_types()->mutable_struct_type()->set_name(
      "com.example.Valid");
  invalid_elem_proto.add_types();
  EXPECT_THAT(
      TypeDefSetFromProto(invalid_elem_proto),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("TypeDef must specify 'struct_type' or 'enum_type'")));

  TypeDefSet invalid_set;
  ASSERT_THAT(
      invalid_set.AddTypeDef(TypeDef(StructTypeDef("com.example.Valid"))),
      IsOk());
  ASSERT_THAT(
      invalid_set.AddTypeDef(TypeDef(StructTypeDef(
          "com.example.BadAspect", "",
          {{"proto", Aspect({{"d", MakeInvalidDurationConstant()}})}}))),
      IsOk());
  types::TypeDefSet out_set;
  EXPECT_THAT(TypeDefSetToProto(invalid_set, &out_set),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

}  // namespace
}  // namespace cel
