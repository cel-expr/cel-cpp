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

#include "common/typedef/proto_schema_proto.h"

#include <any>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "common/typedef/aspect_proto.h"
#include "common/typedef/proto_schema.h"
#include "common/typedef/proto_schema.pb.h"
#include "common/typedef/typedef.pb.h"
#include "internal/testing.h"
#include "google/protobuf/unknown_field_set.h"

namespace cel {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::HasSubstr;
using ::testing::NotNull;
using ::testing::Property;

TEST(ProtoSchemaProtoTest, AspectNames) {
  ProtoSchemaProto p;
  Proto2SchemaProto p2;
  Proto3SchemaProto p3;
  EXPECT_EQ(p.name(), kProtoAspectName);
  EXPECT_EQ(p2.name(), kProto2AspectName);
  EXPECT_EQ(p3.name(), kProto3AspectName);
}

TEST(ProtoSchemaProtoTest, DecodeReturnsEmptyAnyWhenExtensionAbsent) {
  ProtoSchemaProto p;
  Proto2SchemaProto p2;
  Proto3SchemaProto p3;
  types::Struct empty_struct;
  types::Struct::Field empty_field;
  types::Enum empty_enum;
  types::Enum::EnumConstant empty_const;

  for (const AspectProto* aspect : {static_cast<const AspectProto*>(&p),
                                    static_cast<const AspectProto*>(&p2),
                                    static_cast<const AspectProto*>(&p3)}) {
    EXPECT_THAT(aspect->DecodeStructProperties(empty_struct),
                IsOkAndHolds(Property(&std::any::has_value, false)));
    EXPECT_THAT(aspect->DecodeFieldProperties(empty_field),
                IsOkAndHolds(Property(&std::any::has_value, false)));
    EXPECT_THAT(aspect->DecodeEnumProperties(empty_enum),
                IsOkAndHolds(Property(&std::any::has_value, false)));
    EXPECT_THAT(aspect->DecodeEnumConstantProperties(empty_const),
                IsOkAndHolds(Property(&std::any::has_value, false)));
  }

  // Cross-aspect isolation: Proto decoder returns empty std::any when only
  // Proto2 or Proto3 extensions are present.
  types::Struct proto2_struct;
  proto2_struct.MutableExtension(types::proto2_struct_aspect);
  EXPECT_THAT(p.DecodeStructProperties(proto2_struct),
              IsOkAndHolds(Property(&std::any::has_value, false)));
  EXPECT_THAT(p3.DecodeStructProperties(proto2_struct),
              IsOkAndHolds(Property(&std::any::has_value, false)));

  types::Struct::Field proto2_field;
  proto2_field.MutableExtension(types::proto2_field_aspect);
  EXPECT_THAT(p.DecodeFieldProperties(proto2_field),
              IsOkAndHolds(Property(&std::any::has_value, false)));
  EXPECT_THAT(p3.DecodeFieldProperties(proto2_field),
              IsOkAndHolds(Property(&std::any::has_value, false)));

  // Cross-aspect isolation: Proto2 and Proto3 decoders return empty std::any
  // when only general Proto extensions are present.
  types::Struct proto_struct;
  proto_struct.MutableExtension(types::proto_struct_aspect);
  EXPECT_THAT(p2.DecodeStructProperties(proto_struct),
              IsOkAndHolds(Property(&std::any::has_value, false)));
  EXPECT_THAT(p3.DecodeStructProperties(proto_struct),
              IsOkAndHolds(Property(&std::any::has_value, false)));

  types::Struct::Field proto_field;
  proto_field.MutableExtension(types::proto_field_aspect);
  EXPECT_THAT(p2.DecodeFieldProperties(proto_field),
              IsOkAndHolds(Property(&std::any::has_value, false)));
  EXPECT_THAT(p3.DecodeFieldProperties(proto_field),
              IsOkAndHolds(Property(&std::any::has_value, false)));

  types::Enum proto_enum;
  proto_enum.MutableExtension(types::proto_enum_aspect);
  EXPECT_THAT(p2.DecodeEnumProperties(proto_enum),
              IsOkAndHolds(Property(&std::any::has_value, false)));
  EXPECT_THAT(p3.DecodeEnumProperties(proto_enum),
              IsOkAndHolds(Property(&std::any::has_value, false)));

  types::Enum::EnumConstant proto_const;
  proto_const.MutableExtension(types::proto_enum_constant_aspect);
  EXPECT_THAT(p2.DecodeEnumConstantProperties(proto_const),
              IsOkAndHolds(Property(&std::any::has_value, false)));
  EXPECT_THAT(p3.DecodeEnumConstantProperties(proto_const),
              IsOkAndHolds(Property(&std::any::has_value, false)));

  // Cross-aspect isolation: Proto and Proto2 decoders return empty std::any
  // when only Proto3 extensions are present.
  types::Struct::Field proto3_field;
  proto3_field.MutableExtension(types::proto3_field_aspect);
  EXPECT_THAT(p.DecodeFieldProperties(proto3_field),
              IsOkAndHolds(Property(&std::any::has_value, false)));
  EXPECT_THAT(p2.DecodeFieldProperties(proto3_field),
              IsOkAndHolds(Property(&std::any::has_value, false)));
}

TEST(ProtoSchemaProtoTest, EncodeTypeMismatchErrors) {
  ProtoSchemaProto p;
  Proto2SchemaProto p2;
  Proto3SchemaProto p3;

  types::Struct struct_proto;
  types::Struct::Field field_proto;
  types::Enum enum_proto;
  types::Enum::EnumConstant const_proto;
  std::any wrong_type = 123;

  EXPECT_THAT(
      p.EncodeStructProperties(wrong_type, &struct_proto),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Invalid struct properties type for 'proto'")));
  EXPECT_THAT(p.EncodeFieldProperties(wrong_type, &field_proto),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Invalid field properties type for 'proto'")));
  EXPECT_THAT(p.EncodeEnumProperties(wrong_type, &enum_proto),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Invalid enum properties type for 'proto'")));
  EXPECT_THAT(
      p.EncodeEnumConstantProperties(wrong_type, &const_proto),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Invalid enum constant properties type for 'proto'")));

  EXPECT_THAT(
      p2.EncodeStructProperties(wrong_type, &struct_proto),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Invalid struct properties type for 'proto2'")));
  EXPECT_THAT(
      p2.EncodeFieldProperties(wrong_type, &field_proto),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Invalid field properties type for 'proto2'")));

  EXPECT_THAT(
      p3.EncodeFieldProperties(wrong_type, &field_proto),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Invalid field properties type for 'proto3'")));
}

TEST(ProtoSchemaProtoTest, EncodeUnsupportedElements) {
  Proto2SchemaProto p2;
  Proto3SchemaProto p3;
  types::Struct struct_proto;
  types::Enum enum_proto;
  types::Enum::EnumConstant const_proto;

  EXPECT_THAT(p2.EncodeEnumProperties(ProtoEnumProperties(), &enum_proto),
              StatusIs(absl::StatusCode::kUnimplemented,
                       HasSubstr("Custom enum properties are not supported for "
                                 "aspect 'proto2'")));
  EXPECT_THAT(
      p2.EncodeEnumConstantProperties(ProtoEnumConstantProperties(),
                                      &const_proto),
      StatusIs(absl::StatusCode::kUnimplemented,
               HasSubstr("Custom enum constant properties are not supported "
                         "for aspect 'proto2'")));

  EXPECT_THAT(
      p3.EncodeStructProperties(ProtoStructProperties(), &struct_proto),
      StatusIs(absl::StatusCode::kUnimplemented,
               HasSubstr("Custom struct properties are not supported for "
                         "aspect 'proto3'")));
  EXPECT_THAT(p3.EncodeEnumProperties(ProtoEnumProperties(), &enum_proto),
              StatusIs(absl::StatusCode::kUnimplemented,
                       HasSubstr("Custom enum properties are not supported for "
                                 "aspect 'proto3'")));
  EXPECT_THAT(
      p3.EncodeEnumConstantProperties(ProtoEnumConstantProperties(),
                                      &const_proto),
      StatusIs(absl::StatusCode::kUnimplemented,
               HasSubstr("Custom enum constant properties are not supported "
                         "for aspect 'proto3'")));
}

TEST(ProtoSchemaProtoTest, DecodeIgnoresUnknownFields) {
  ProtoSchemaProto p;
  Proto2SchemaProto p2;
  Proto3SchemaProto p3;

  types::Struct struct_proto;
  auto* struct_ext = struct_proto.MutableExtension(types::proto_struct_aspect);
  struct_ext->GetReflection()
      ->MutableUnknownFields(struct_ext)
      ->AddVarint(999, 1);
  EXPECT_THAT(p.DecodeStructProperties(struct_proto),
              IsOkAndHolds(Property(&std::any::has_value, true)));

  auto* struct2_ext =
      struct_proto.MutableExtension(types::proto2_struct_aspect);
  struct2_ext->GetReflection()
      ->MutableUnknownFields(struct2_ext)
      ->AddVarint(999, 1);
  EXPECT_THAT(p2.DecodeStructProperties(struct_proto),
              IsOkAndHolds(Property(&std::any::has_value, true)));

  types::Struct::Field field_proto;
  auto* field3_ext = field_proto.MutableExtension(types::proto3_field_aspect);
  field3_ext->GetReflection()
      ->MutableUnknownFields(field3_ext)
      ->AddVarint(999, 1);
  EXPECT_THAT(p3.DecodeFieldProperties(field_proto),
              IsOkAndHolds(Property(&std::any::has_value, true)));

  auto* field2_ext = field_proto.MutableExtension(types::proto2_field_aspect);
  field2_ext->GetReflection()
      ->MutableUnknownFields(field2_ext)
      ->AddVarint(999, 1);
  EXPECT_THAT(p2.DecodeFieldProperties(field_proto),
              IsOkAndHolds(Property(&std::any::has_value, true)));

  auto* field_ext = field_proto.MutableExtension(types::proto_field_aspect);
  field_ext->GetReflection()->MutableUnknownFields(field_ext)->AddVarint(999,
                                                                         1);
  EXPECT_THAT(p.DecodeFieldProperties(field_proto),
              IsOkAndHolds(Property(&std::any::has_value, true)));

  types::Enum enum_proto;
  auto* enum_ext = enum_proto.MutableExtension(types::proto_enum_aspect);
  enum_ext->GetReflection()->MutableUnknownFields(enum_ext)->AddVarint(999, 1);
  EXPECT_THAT(p.DecodeEnumProperties(enum_proto),
              IsOkAndHolds(Property(&std::any::has_value, true)));

  types::Enum::EnumConstant const_proto;
  auto* const_ext =
      const_proto.MutableExtension(types::proto_enum_constant_aspect);
  const_ext->GetReflection()->MutableUnknownFields(const_ext)->AddVarint(999,
                                                                         1);
  EXPECT_THAT(p.DecodeEnumConstantProperties(const_proto),
              IsOkAndHolds(Property(&std::any::has_value, true)));
}

TEST(ProtoSchemaProtoTest, EncodeAndDecodeRoundTrip) {
  ProtoSchemaProto p;
  Proto2SchemaProto p2;
  Proto3SchemaProto p3;

  // General Proto properties ("proto")
  {
    ProtoStructProperties struct_in("com.example.Order");
    types::Struct struct_proto;
    ASSERT_THAT(p.EncodeStructProperties(struct_in, &struct_proto), IsOk());
    ASSERT_OK_AND_ASSIGN(std::any struct_out_any,
                         p.DecodeStructProperties(struct_proto));
    const auto* struct_out =
        std::any_cast<ProtoStructProperties>(&struct_out_any);
    ASSERT_THAT(struct_out, NotNull());
    EXPECT_EQ(struct_out->name(), struct_in.name());

    ProtoFieldProperties field_in("order_id", 42, "orderId");
    types::Struct::Field field_proto;
    ASSERT_THAT(p.EncodeFieldProperties(field_in, &field_proto), IsOk());
    ASSERT_OK_AND_ASSIGN(std::any field_out_any,
                         p.DecodeFieldProperties(field_proto));
    const auto* field_out = std::any_cast<ProtoFieldProperties>(&field_out_any);
    ASSERT_THAT(field_out, NotNull());
    EXPECT_EQ(field_out->name(), field_in.name());
    EXPECT_EQ(field_out->id(), field_in.id());
    EXPECT_EQ(field_out->json_name(), field_in.json_name());

    ProtoEnumProperties enum_in("com.example.OrderStatus", true);
    types::Enum enum_proto;
    ASSERT_THAT(p.EncodeEnumProperties(enum_in, &enum_proto), IsOk());
    ASSERT_OK_AND_ASSIGN(std::any enum_out_any,
                         p.DecodeEnumProperties(enum_proto));
    const auto* enum_out = std::any_cast<ProtoEnumProperties>(&enum_out_any);
    ASSERT_THAT(enum_out, NotNull());
    EXPECT_EQ(enum_out->name(), enum_in.name());
    EXPECT_EQ(enum_out->allow_alias(), enum_in.allow_alias());

    ProtoEnumConstantProperties const_in("STATUS_OK");
    types::Enum::EnumConstant const_proto;
    ASSERT_THAT(p.EncodeEnumConstantProperties(const_in, &const_proto), IsOk());
    ASSERT_OK_AND_ASSIGN(std::any const_out_any,
                         p.DecodeEnumConstantProperties(const_proto));
    const auto* const_out =
        std::any_cast<ProtoEnumConstantProperties>(&const_out_any);
    ASSERT_THAT(const_out, NotNull());
    EXPECT_EQ(const_out->name(), const_in.name());
  }

  // Proto2 properties ("proto2")
  {
    Proto2StructProperties struct_in({{.start = 100, .end = 200}});
    types::Struct struct_proto;
    ASSERT_THAT(p2.EncodeStructProperties(struct_in, &struct_proto), IsOk());
    ASSERT_OK_AND_ASSIGN(std::any struct_out_any,
                         p2.DecodeStructProperties(struct_proto));
    const auto* struct_out =
        std::any_cast<Proto2StructProperties>(&struct_out_any);
    ASSERT_THAT(struct_out, NotNull());
    EXPECT_THAT(struct_out->extension_ranges(),
                ElementsAre(AllOf(Field(&ProtoExtensionRange::start, 100),
                                  Field(&ProtoExtensionRange::end, 200))));

    Proto2FieldProperties field_in(/*required=*/true, /*packed=*/false);
    types::Struct::Field field_proto;
    ASSERT_THAT(p2.EncodeFieldProperties(field_in, &field_proto), IsOk());
    ASSERT_OK_AND_ASSIGN(std::any field_out_any,
                         p2.DecodeFieldProperties(field_proto));
    const auto* field_out =
        std::any_cast<Proto2FieldProperties>(&field_out_any);
    ASSERT_THAT(field_out, NotNull());
    EXPECT_EQ(field_out->required(), field_in.required());
    EXPECT_EQ(field_out->packed(), field_in.packed());
  }

  // Proto3 properties ("proto3")
  {
    Proto3FieldProperties field_in(/*optional=*/true, /*packed=*/true);
    types::Struct::Field field_proto;
    ASSERT_THAT(p3.EncodeFieldProperties(field_in, &field_proto), IsOk());
    ASSERT_OK_AND_ASSIGN(std::any field_out_any,
                         p3.DecodeFieldProperties(field_proto));
    const auto* field_out =
        std::any_cast<Proto3FieldProperties>(&field_out_any);
    ASSERT_THAT(field_out, NotNull());
    EXPECT_EQ(field_out->optional(), field_in.optional());
    EXPECT_EQ(field_out->packed(), field_in.packed());
  }
}
}  // namespace
}  // namespace cel
