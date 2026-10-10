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

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "common/typedef/proto_schema.h"
#include "common/typedef/proto_schema.pb.h"
#include "common/typedef/schema.h"
#include "common/typedef/schema_proto.h"
#include "common/typedef/typedef.pb.h"
#include "internal/testing.h"
#include "google/protobuf/unknown_field_set.h"

namespace cel {
namespace {

using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::HasSubstr;
using ::testing::IsNull;
using ::testing::NotNull;

struct DummyObjectProperties : public SchemaObjectProperties {};
struct DummyFieldProperties : public SchemaFieldProperties {};
struct DummyEnumProperties : public SchemaEnumProperties {};
struct DummyEnumConstantProperties : public SchemaEnumConstantProperties {};

TEST(ProtoSchemaProtoTest, SchemaNames) {
  Proto2SchemaProto p2;
  Proto3SchemaProto p3;
  EXPECT_EQ(p2.name(), kProto2SchemaName);
  EXPECT_EQ(p3.name(), kProto3SchemaName);
}

TEST(ProtoSchemaProtoTest, DecodeReturnsNullptrWhenExtensionAbsent) {
  Proto2SchemaProto p2;
  Proto3SchemaProto p3;
  types::Object obj_proto;
  types::Object::Field field_proto;
  types::Enum enum_proto;
  types::Enum::EnumConstant const_proto;

  for (const SchemaProto* schema : {static_cast<const SchemaProto*>(&p2),
                                    static_cast<const SchemaProto*>(&p3)}) {
    EXPECT_THAT(schema->DecodeObjectProperties(obj_proto),
                IsOkAndHolds(IsNull()));
    EXPECT_THAT(schema->DecodeFieldProperties(field_proto),
                IsOkAndHolds(IsNull()));
    EXPECT_THAT(schema->DecodeEnumProperties(enum_proto),
                IsOkAndHolds(IsNull()));
    EXPECT_THAT(schema->DecodeEnumConstantProperties(const_proto),
                IsOkAndHolds(IsNull()));
  }
}

TEST(ProtoSchemaProtoTest, EncodeTypeMismatchErrors) {
  Proto2SchemaProto p2;
  Proto3SchemaProto p3;

  for (const SchemaProto* schema : {static_cast<const SchemaProto*>(&p2),
                                    static_cast<const SchemaProto*>(&p3)}) {
    types::Object obj_proto;
    types::Object::Field field_proto;
    types::Enum enum_proto;
    types::Enum::EnumConstant const_proto;

    DummyObjectProperties dummy_obj;
    EXPECT_THAT(schema->EncodeObjectProperties(dummy_obj, &obj_proto),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("Invalid object properties type for '")));

    DummyFieldProperties dummy_field;
    EXPECT_THAT(schema->EncodeFieldProperties(dummy_field, &field_proto),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("Invalid field properties type for '")));

    DummyEnumProperties dummy_enum;
    EXPECT_THAT(schema->EncodeEnumProperties(dummy_enum, &enum_proto),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("Invalid enum properties type for '")));

    DummyEnumConstantProperties dummy_const;
    EXPECT_THAT(
        schema->EncodeEnumConstantProperties(dummy_const, &const_proto),
        StatusIs(absl::StatusCode::kInvalidArgument,
                 HasSubstr("Invalid enum constant properties type for '")));
  }
}

TEST(ProtoSchemaProtoTest, DecodeIgnoresUnknownFields) {
  Proto2SchemaProto p2;
  Proto3SchemaProto p3;

  types::Object obj_proto;
  auto* obj_ext = obj_proto.MutableExtension(types::proto2_object_schema);
  obj_ext->GetReflection()->MutableUnknownFields(obj_ext)->AddVarint(999, 1);
  EXPECT_THAT(p2.DecodeObjectProperties(obj_proto), IsOkAndHolds(NotNull()));

  types::Object::Field field_proto;
  auto* field_ext = field_proto.MutableExtension(types::proto3_field_schema);
  field_ext->GetReflection()->MutableUnknownFields(field_ext)->AddVarint(999,
                                                                         1);
  EXPECT_THAT(p3.DecodeFieldProperties(field_proto), IsOkAndHolds(NotNull()));

  types::Enum enum_proto;
  auto* enum_ext = enum_proto.MutableExtension(types::proto2_enum_schema);
  enum_ext->GetReflection()->MutableUnknownFields(enum_ext)->AddVarint(999, 1);
  EXPECT_THAT(p2.DecodeEnumProperties(enum_proto), IsOkAndHolds(NotNull()));

  types::Enum::EnumConstant const_proto;
  auto* const_ext =
      const_proto.MutableExtension(types::proto3_enum_constant_schema);
  const_ext->GetReflection()->MutableUnknownFields(const_ext)->AddVarint(999,
                                                                         1);
  EXPECT_THAT(p3.DecodeEnumConstantProperties(const_proto),
              IsOkAndHolds(NotNull()));
}

}  // namespace
}  // namespace cel
