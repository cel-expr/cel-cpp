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

using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::HasSubstr;
using ::testing::Property;

TEST(ProtoSchemaProtoTest, AspectNames) {
  Proto2SchemaProto p2;
  Proto3SchemaProto p3;
  EXPECT_EQ(p2.name(), kProto2AspectName);
  EXPECT_EQ(p3.name(), kProto3AspectName);
}

TEST(ProtoSchemaProtoTest, DecodeReturnsEmptyAnyWhenExtensionAbsent) {
  Proto2SchemaProto p2;
  Proto3SchemaProto p3;
  types::Struct empty_struct;
  types::Struct::Field empty_field;
  types::Enum empty_enum;
  types::Enum::EnumConstant empty_const;

  for (const AspectProto* aspect : {static_cast<const AspectProto*>(&p2),
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

  // Cross-version isolation: Proto3 decoder returns empty std::any when only
  // Proto2 extensions are present.
  types::Struct proto2_struct;
  proto2_struct.MutableExtension(types::proto2_struct_aspect);
  EXPECT_THAT(p3.DecodeStructProperties(proto2_struct),
              IsOkAndHolds(Property(&std::any::has_value, false)));
  types::Struct::Field proto2_field;
  proto2_field.MutableExtension(types::proto2_field_aspect);
  EXPECT_THAT(p3.DecodeFieldProperties(proto2_field),
              IsOkAndHolds(Property(&std::any::has_value, false)));
  types::Enum proto2_enum;
  proto2_enum.MutableExtension(types::proto2_enum_aspect);
  EXPECT_THAT(p3.DecodeEnumProperties(proto2_enum),
              IsOkAndHolds(Property(&std::any::has_value, false)));
  types::Enum::EnumConstant proto2_const;
  proto2_const.MutableExtension(types::proto2_enum_constant_aspect);
  EXPECT_THAT(p3.DecodeEnumConstantProperties(proto2_const),
              IsOkAndHolds(Property(&std::any::has_value, false)));

  // Cross-version isolation: Proto2 decoder returns empty std::any when only
  // Proto3 extensions are present.
  types::Struct proto3_struct;
  proto3_struct.MutableExtension(types::proto3_struct_aspect);
  EXPECT_THAT(p2.DecodeStructProperties(proto3_struct),
              IsOkAndHolds(Property(&std::any::has_value, false)));
  types::Struct::Field proto3_field;
  proto3_field.MutableExtension(types::proto3_field_aspect);
  EXPECT_THAT(p2.DecodeFieldProperties(proto3_field),
              IsOkAndHolds(Property(&std::any::has_value, false)));
  types::Enum proto3_enum;
  proto3_enum.MutableExtension(types::proto3_enum_aspect);
  EXPECT_THAT(p2.DecodeEnumProperties(proto3_enum),
              IsOkAndHolds(Property(&std::any::has_value, false)));
  types::Enum::EnumConstant proto3_const;
  proto3_const.MutableExtension(types::proto3_enum_constant_aspect);
  EXPECT_THAT(p2.DecodeEnumConstantProperties(proto3_const),
              IsOkAndHolds(Property(&std::any::has_value, false)));
}

TEST(ProtoSchemaProtoTest, EncodeTypeMismatchErrors) {
  Proto2SchemaProto p2;
  Proto3SchemaProto p3;

  for (const AspectProto* aspect : {static_cast<const AspectProto*>(&p2),
                                    static_cast<const AspectProto*>(&p3)}) {
    types::Struct struct_proto;
    types::Struct::Field field_proto;
    types::Enum enum_proto;
    types::Enum::EnumConstant const_proto;

    std::any wrong_type = 123;
    EXPECT_THAT(aspect->EncodeStructProperties(wrong_type, &struct_proto),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("Invalid struct properties type for '")));

    EXPECT_THAT(aspect->EncodeFieldProperties(wrong_type, &field_proto),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("Invalid field properties type for '")));

    EXPECT_THAT(aspect->EncodeEnumProperties(wrong_type, &enum_proto),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("Invalid enum properties type for '")));

    EXPECT_THAT(
        aspect->EncodeEnumConstantProperties(wrong_type, &const_proto),
        StatusIs(absl::StatusCode::kInvalidArgument,
                 HasSubstr("Invalid enum constant properties type for '")));
  }
}

TEST(ProtoSchemaProtoTest, DecodeIgnoresUnknownFields) {
  Proto2SchemaProto p2;
  Proto3SchemaProto p3;

  types::Struct struct_proto;
  auto* struct_ext = struct_proto.MutableExtension(types::proto2_struct_aspect);
  struct_ext->GetReflection()
      ->MutableUnknownFields(struct_ext)
      ->AddVarint(999, 1);
  EXPECT_THAT(p2.DecodeStructProperties(struct_proto),
              IsOkAndHolds(Property(&std::any::has_value, true)));

  types::Struct::Field field_proto;
  auto* field_ext = field_proto.MutableExtension(types::proto3_field_aspect);
  field_ext->GetReflection()->MutableUnknownFields(field_ext)->AddVarint(999,
                                                                         1);
  EXPECT_THAT(p3.DecodeFieldProperties(field_proto),
              IsOkAndHolds(Property(&std::any::has_value, true)));

  types::Enum enum_proto;
  auto* enum_ext = enum_proto.MutableExtension(types::proto2_enum_aspect);
  enum_ext->GetReflection()->MutableUnknownFields(enum_ext)->AddVarint(999, 1);
  EXPECT_THAT(p2.DecodeEnumProperties(enum_proto),
              IsOkAndHolds(Property(&std::any::has_value, true)));

  types::Enum::EnumConstant const_proto;
  auto* const_ext =
      const_proto.MutableExtension(types::proto3_enum_constant_aspect);
  const_ext->GetReflection()->MutableUnknownFields(const_ext)->AddVarint(999,
                                                                         1);
  EXPECT_THAT(p3.DecodeEnumConstantProperties(const_proto),
              IsOkAndHolds(Property(&std::any::has_value, true)));
}

}  // namespace
}  // namespace cel
