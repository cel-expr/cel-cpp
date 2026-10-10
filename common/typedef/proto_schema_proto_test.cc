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

using ::absl_testing::IsOk;
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

TEST(ProtoSchemaProtoTest, EncodeAndDecodeRoundTrip) {
  Proto2SchemaProto p2;
  Proto3SchemaProto p3;

  for (const SchemaProto* schema : {static_cast<const SchemaProto*>(&p2),
                                    static_cast<const SchemaProto*>(&p3)}) {
    ProtoObjectProperties obj_in;
    obj_in.name = "com.example.Obj";
    obj_in.file_name = "com/example/obj.proto";
    obj_in.extension_ranges = {{.start = 100, .end = 200},
                               {.start = 1000, .end = 536870912}};
    types::Object obj_proto;
    ASSERT_THAT(schema->EncodeObjectProperties(obj_in, &obj_proto), IsOk());
    ASSERT_OK_AND_ASSIGN(auto obj_out_base,
                         schema->DecodeObjectProperties(obj_proto));
    const auto* obj_out =
        dynamic_cast<const ProtoObjectProperties*>(obj_out_base.get());
    ASSERT_THAT(obj_out, NotNull());
    EXPECT_EQ(obj_out->name, obj_in.name);
    EXPECT_EQ(obj_out->file_name, obj_in.file_name);
    EXPECT_EQ(obj_out->extension_ranges, obj_in.extension_ranges);

    ProtoFieldProperties field_in;
    field_in.name = "com.example.ExtField";
    field_in.id = 42;
    field_in.json_name = "customJson";
    field_in.type = "sint32";
    field_in.label = "required";
    field_in.oneof = "choice";
    field_in.default_value = "BAR";
    types::Object::Field field_proto;
    ASSERT_THAT(schema->EncodeFieldProperties(field_in, &field_proto), IsOk());
    ASSERT_OK_AND_ASSIGN(auto field_out_base,
                         schema->DecodeFieldProperties(field_proto));
    const auto* field_out =
        dynamic_cast<const ProtoFieldProperties*>(field_out_base.get());
    ASSERT_THAT(field_out, NotNull());
    EXPECT_EQ(field_out->name, field_in.name);
    EXPECT_EQ(field_out->id, field_in.id);
    EXPECT_EQ(field_out->json_name, field_in.json_name);
    EXPECT_EQ(field_out->type, field_in.type);
    EXPECT_EQ(field_out->label, field_in.label);
    EXPECT_EQ(field_out->oneof, field_in.oneof);
    EXPECT_EQ(field_out->default_value, field_in.default_value);

    ProtoEnumProperties enum_in;
    enum_in.name = "com.example.Status";
    enum_in.allow_alias = true;
    enum_in.file_name = "com/example/status.proto";
    types::Enum enum_proto;
    ASSERT_THAT(schema->EncodeEnumProperties(enum_in, &enum_proto), IsOk());
    ASSERT_OK_AND_ASSIGN(auto enum_out_base,
                         schema->DecodeEnumProperties(enum_proto));
    const auto* enum_out =
        dynamic_cast<const ProtoEnumProperties*>(enum_out_base.get());
    ASSERT_THAT(enum_out, NotNull());
    EXPECT_EQ(enum_out->name, enum_in.name);
    EXPECT_EQ(enum_out->allow_alias, enum_in.allow_alias);
    EXPECT_EQ(enum_out->file_name, enum_in.file_name);

    ProtoEnumConstantProperties const_in;
    const_in.name = "STATUS_OK";
    types::Enum::EnumConstant const_proto;
    ASSERT_THAT(schema->EncodeEnumConstantProperties(const_in, &const_proto),
                IsOk());
    ASSERT_OK_AND_ASSIGN(auto const_out_base,
                         schema->DecodeEnumConstantProperties(const_proto));
    const auto* const_out =
        dynamic_cast<const ProtoEnumConstantProperties*>(const_out_base.get());
    ASSERT_THAT(const_out, NotNull());
    EXPECT_EQ(const_out->name, const_in.name);
  }
}

}  // namespace
}  // namespace cel
