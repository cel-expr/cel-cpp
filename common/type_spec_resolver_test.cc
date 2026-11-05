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

#include "common/type_spec_resolver.h"

#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "common/ast.h"
#include "common/type.h"
#include "common/type_introspector.h"
#include "common/type_kind.h"
#include "internal/testing.h"
#include "internal/testing_descriptor_pool.h"
#include "google/protobuf/arena.h"

namespace cel {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::cel::internal::GetTestingDescriptorPool;
using ::testing::HasSubstr;
using ::testing::TestWithParam;
using ::testing::Values;

google::protobuf::Arena* GetTestArena() {
  static absl::NoDestructor<google::protobuf::Arena> arena;
  return &*arena;
}

TEST(TypeSpecResolverTest, NullTypeSpec) {
  TypeSpec spec(NullTypeSpec{});
  auto t =
      ConvertTypeSpecToType(spec, *GetTestingDescriptorPool(), GetTestArena());
  ASSERT_THAT(t, IsOk());
  EXPECT_TRUE(t->IsNull());
}

TEST(TypeSpecResolverTest, DynTypeSpec) {
  TypeSpec spec(DynTypeSpec{});
  auto t =
      ConvertTypeSpecToType(spec, *GetTestingDescriptorPool(), GetTestArena());
  ASSERT_THAT(t, IsOk());
  EXPECT_TRUE(t->IsDyn());
}

TEST(TypeSpecResolverTest, UnspecializedConvertsToDyn) {
  EXPECT_THAT(ConvertTypeToTypeSpec(UnspecializedType()),
              IsOkAndHolds(TypeSpec(DynTypeSpec{})));
  EXPECT_THAT(
      ConvertTypeToTypeSpec(ListType(GetTestArena(), UnspecializedType())),
      IsOkAndHolds(
          TypeSpec(ListTypeSpec(std::make_unique<TypeSpec>(DynTypeSpec{})))));
}

using ConversionTest = testing::TestWithParam<std::tuple<TypeSpec, TypeKind>>;

TEST_P(ConversionTest, TestTypeSpecConversion) {
  ASSERT_OK_AND_ASSIGN(
      auto t,
      ConvertTypeSpecToType(std::get<0>(GetParam()),
                            *GetTestingDescriptorPool(), GetTestArena()));
  EXPECT_EQ(t.kind(), std::get<1>(GetParam()));
  EXPECT_THAT(ConvertTypeToTypeSpec(t), IsOkAndHolds(std::get<0>(GetParam())));
}

INSTANTIATE_TEST_SUITE_P(
    TypeSpecResolverTest, ConversionTest,
    testing::Values(
        std::make_tuple(TypeSpec(PrimitiveType::kBool), TypeKind::kBool),
        std::make_tuple(TypeSpec(PrimitiveType::kInt64), TypeKind::kInt),
        std::make_tuple(TypeSpec(PrimitiveType::kUint64), TypeKind::kUint),
        std::make_tuple(TypeSpec(PrimitiveType::kDouble), TypeKind::kDouble),
        std::make_tuple(TypeSpec(PrimitiveType::kString), TypeKind::kString),
        std::make_tuple(TypeSpec(PrimitiveType::kBytes), TypeKind::kBytes),
        std::make_tuple(TypeSpec(WellKnownTypeSpec::kAny), TypeKind::kAny),
        std::make_tuple(TypeSpec(WellKnownTypeSpec::kTimestamp),
                        TypeKind::kTimestamp),
        std::make_tuple(TypeSpec(WellKnownTypeSpec::kDuration),
                        TypeKind::kDuration),
        std::make_tuple(TypeSpec(PrimitiveTypeWrapper(PrimitiveType::kBool)),
                        TypeKind::kBoolWrapper),
        std::make_tuple(TypeSpec(PrimitiveTypeWrapper(PrimitiveType::kInt64)),
                        TypeKind::kIntWrapper),
        std::make_tuple(TypeSpec(PrimitiveTypeWrapper(PrimitiveType::kUint64)),
                        TypeKind::kUintWrapper),
        std::make_tuple(TypeSpec(PrimitiveTypeWrapper(PrimitiveType::kDouble)),
                        TypeKind::kDoubleWrapper),
        std::make_tuple(TypeSpec(PrimitiveTypeWrapper(PrimitiveType::kString)),
                        TypeKind::kStringWrapper),
        std::make_tuple(TypeSpec(PrimitiveTypeWrapper(PrimitiveType::kBytes)),
                        TypeKind::kBytesWrapper)));

TEST(TypeSpecResolverTest, ListTypeConversion) {
  auto elem = std::make_unique<TypeSpec>(PrimitiveType::kInt64);
  TypeSpec spec(ListTypeSpec(std::move(elem)));
  auto t =
      ConvertTypeSpecToType(spec, *GetTestingDescriptorPool(), GetTestArena());
  ASSERT_THAT(t, IsOk());
  EXPECT_TRUE(t->IsList());
  EXPECT_TRUE(t->GetList().element().IsInt());
  ASSERT_OK_AND_ASSIGN(auto spec2, ConvertTypeToTypeSpec(*t));
  EXPECT_EQ(spec2, spec);
}

TEST(TypeSpecResolverTest, MapTypeConversion) {
  auto key = std::make_unique<TypeSpec>(PrimitiveType::kString);
  auto val = std::make_unique<TypeSpec>(PrimitiveType::kBytes);
  TypeSpec spec(MapTypeSpec(std::move(key), std::move(val)));
  auto t =
      ConvertTypeSpecToType(spec, *GetTestingDescriptorPool(), GetTestArena());
  ASSERT_THAT(t, IsOk());
  EXPECT_TRUE(t->IsMap());
  EXPECT_TRUE(t->GetMap().key().IsString());
  EXPECT_TRUE(t->GetMap().value().IsBytes());
  ASSERT_OK_AND_ASSIGN(auto spec2, ConvertTypeToTypeSpec(*t));
  EXPECT_EQ(spec2, spec);
}

TEST(TypeSpecResolverTest, FunctionTypeConversion) {
  auto result = std::make_unique<TypeSpec>(PrimitiveType::kBool);
  std::vector<TypeSpec> args;
  args.push_back(TypeSpec(PrimitiveType::kString));
  TypeSpec spec(FunctionTypeSpec(std::move(result), std::move(args)));
  auto t =
      ConvertTypeSpecToType(spec, *GetTestingDescriptorPool(), GetTestArena());
  ASSERT_THAT(t, IsOk());
  EXPECT_TRUE(t->IsFunction());
  EXPECT_EQ(t->GetFunction().args().size(), 1);
  EXPECT_TRUE(t->GetFunction().result().IsBool());
  ASSERT_OK_AND_ASSIGN(auto spec2, ConvertTypeToTypeSpec(*t));
  EXPECT_EQ(spec2, spec);
}

TEST(TypeSpecResolverTest, TypeParamConversion) {
  TypeSpec spec(ParamTypeSpec("T"));
  auto t =
      ConvertTypeSpecToType(spec, *GetTestingDescriptorPool(), GetTestArena());
  ASSERT_THAT(t, IsOk());
  EXPECT_TRUE(t->IsTypeParam());
  EXPECT_EQ(t->GetTypeParam().name(), "T");
  ASSERT_OK_AND_ASSIGN(auto spec2, ConvertTypeToTypeSpec(*t));
  EXPECT_EQ(spec2, spec);
}

TEST(TypeSpecResolverTest, MessageTypeConversion) {
  TypeSpec spec(
      AbstractType("cel.expr.conformance.proto3.TestAllTypes", /*params=*/{}));
  auto t =
      ConvertTypeSpecToType(spec, *GetTestingDescriptorPool(), GetTestArena());
  ASSERT_THAT(t, IsOk());
  EXPECT_TRUE(t->IsMessage());
  EXPECT_EQ(t->name(), "cel.expr.conformance.proto3.TestAllTypes");
  ASSERT_OK_AND_ASSIGN(auto spec2, ConvertTypeToTypeSpec(*t));
  EXPECT_EQ(
      spec2,
      TypeSpec(MessageTypeSpec("cel.expr.conformance.proto3.TestAllTypes")));
}

TEST(TypeSpecResolverTest, MessageTypeWithParamsError) {
  std::vector<TypeSpec> params;
  params.push_back(TypeSpec(PrimitiveType::kInt64));
  TypeSpec spec(AbstractType("cel.expr.conformance.proto3.TestAllTypes",
                             std::move(params)));
  auto t =
      ConvertTypeSpecToType(spec, *GetTestingDescriptorPool(), GetTestArena());
  EXPECT_THAT(t, StatusIs(absl::StatusCode::kInvalidArgument,
                          HasSubstr("cannot have type parameters")));
}

TEST(TypeSpecResolverTest, UnresolvedAbstractTypeFallbackToOpaque) {
  std::vector<TypeSpec> params;
  params.push_back(TypeSpec(PrimitiveType::kInt64));
  TypeSpec spec(AbstractType("my.custom.OpaqueType", std::move(params)));
  auto t =
      ConvertTypeSpecToType(spec, *GetTestingDescriptorPool(), GetTestArena());
  ASSERT_THAT(t, IsOk());
  EXPECT_TRUE(t->IsOpaque());
  EXPECT_EQ(t->name(), "my.custom.OpaqueType");
  EXPECT_EQ(t->GetParameters().size(), 1);
  EXPECT_TRUE(t->GetParameters()[0].IsInt());
  ASSERT_OK_AND_ASSIGN(auto spec2, ConvertTypeToTypeSpec(*t));
  EXPECT_EQ(spec2, spec);
}

TEST(TypeSpecResolverTest, OptionalType) {
  std::vector<TypeSpec> params;
  params.push_back(TypeSpec(PrimitiveType::kInt64));
  TypeSpec spec(AbstractType("optional_type", std::move(params)));
  auto t =
      ConvertTypeSpecToType(spec, *GetTestingDescriptorPool(), GetTestArena());
  ASSERT_THAT(t, IsOk());
  EXPECT_TRUE(t->IsOpaque());
  EXPECT_EQ(t->name(), "optional_type");
  EXPECT_EQ(t->GetParameters().size(), 1);
  EXPECT_TRUE(t->GetParameters()[0].IsInt());
  EXPECT_TRUE(t->IsOptional());
  ASSERT_OK_AND_ASSIGN(auto spec2, ConvertTypeToTypeSpec(*t));
  EXPECT_EQ(spec2, spec);
}

TEST(TypeSpecResolverTest, TypeTypeConversion) {
  auto nested = std::make_unique<TypeSpec>(PrimitiveType::kInt64);
  TypeSpec spec(std::move(nested));
  auto t =
      ConvertTypeSpecToType(spec, *GetTestingDescriptorPool(), GetTestArena());
  ASSERT_THAT(t, IsOk());
  EXPECT_TRUE(t->IsType());
  EXPECT_TRUE(t->GetType().GetType().IsInt());
  ASSERT_OK_AND_ASSIGN(auto spec2, ConvertTypeToTypeSpec(*t));
  EXPECT_EQ(spec2, spec);
}

TEST(TypeSpecResolverTest, ErrorTypeConversion) {
  TypeSpec spec(ErrorTypeSpec::kValue);
  auto t =
      ConvertTypeSpecToType(spec, *GetTestingDescriptorPool(), GetTestArena());
  ASSERT_THAT(t, IsOk());
  EXPECT_TRUE(t->IsError());
  ASSERT_OK_AND_ASSIGN(auto spec2, ConvertTypeToTypeSpec(*t));
  EXPECT_EQ(spec2, spec);
}

TEST(TypeSpecResolverTest, MessageTypeSpecConversion) {
  TypeSpec spec(MessageTypeSpec("cel.expr.conformance.proto3.TestAllTypes"));
  auto t =
      ConvertTypeSpecToType(spec, *GetTestingDescriptorPool(), GetTestArena());
  ASSERT_THAT(t, IsOk());
  EXPECT_TRUE(t->IsMessage());
  EXPECT_EQ(t->name(), "cel.expr.conformance.proto3.TestAllTypes");
  ASSERT_OK_AND_ASSIGN(auto spec2, ConvertTypeToTypeSpec(*t));
  EXPECT_EQ(spec2, spec);
}

TEST(TypeSpecResolverTest, MessageTypeSpecNotFoundError) {
  TypeSpec spec(MessageTypeSpec("cel.expr.conformance.proto3.NonExistentType"));
  auto t =
      ConvertTypeSpecToType(spec, *GetTestingDescriptorPool(), GetTestArena());
  EXPECT_THAT(t, StatusIs(absl::StatusCode::kInvalidArgument,
                          HasSubstr("not found in descriptor pool")));
}

TEST(TypeSpecResolverTest, EnumTypeConversion) {
  TypeSpec spec(AbstractType(
      "cel.expr.conformance.proto3.TestAllTypes.NestedEnum", /*params=*/{}));
  auto t =
      ConvertTypeSpecToType(spec, *GetTestingDescriptorPool(), GetTestArena());
  ASSERT_THAT(t, IsOk());
  EXPECT_TRUE(t->IsEnum());
  EXPECT_EQ(t->name(), "cel.expr.conformance.proto3.TestAllTypes.NestedEnum");
  ASSERT_OK_AND_ASSIGN(auto spec2, ConvertTypeToTypeSpec(*t));
  EXPECT_EQ(spec2, spec);
}

TEST(TypeSpecResolverTest, EnumTypeWithParamsError) {
  std::vector<TypeSpec> params;
  params.push_back(TypeSpec(PrimitiveType::kInt64));
  TypeSpec spec(
      AbstractType("cel.expr.conformance.proto3.TestAllTypes.NestedEnum",
                   std::move(params)));
  auto t =
      ConvertTypeSpecToType(spec, *GetTestingDescriptorPool(), GetTestArena());
  EXPECT_THAT(t, StatusIs(absl::StatusCode::kInvalidArgument,
                          HasSubstr("cannot have type parameters")));
}

TEST(TypeSpecResolverTest, WellKnownMessageTypeFiltering) {
  TypeSpec message_spec(MessageTypeSpec("google.protobuf.Timestamp"));
  ASSERT_OK_AND_ASSIGN(
      auto message_type,
      ConvertTypeSpecToType(message_spec, *GetTestingDescriptorPool(),
                            GetTestArena()));
  EXPECT_TRUE(message_type.IsTimestamp());

  TypeSpec abstract_spec(
      AbstractType("google.protobuf.Duration", /*params=*/{}));
  ASSERT_OK_AND_ASSIGN(
      auto abstract_type,
      ConvertTypeSpecToType(abstract_spec, *GetTestingDescriptorPool(),
                            GetTestArena()));
  EXPECT_TRUE(abstract_type.IsDuration());

  std::vector<TypeSpec> params;
  params.push_back(TypeSpec(PrimitiveType::kInt64));
  TypeSpec abstract_with_params(
      AbstractType("google.protobuf.Timestamp", std::move(params)));
  EXPECT_THAT(
      ConvertTypeSpecToType(abstract_with_params, *GetTestingDescriptorPool(),
                            GetTestArena()),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("cannot have type parameters")));
}

TEST(TypeSpecResolverTest, WellKnownTypeIntrospector) {
  WellKnownTypeIntrospector introspector;

  TypeSpec message_spec(MessageTypeSpec("google.protobuf.Timestamp"));
  ASSERT_OK_AND_ASSIGN(
      auto message_type,
      ConvertTypeSpecToType(message_spec, introspector, GetTestArena()));
  EXPECT_TRUE(message_type.IsTimestamp());

  TypeSpec abstract_spec(
      AbstractType("google.protobuf.Duration", /*params=*/{}));
  ASSERT_OK_AND_ASSIGN(
      auto abstract_type,
      ConvertTypeSpecToType(abstract_spec, introspector, GetTestArena()));
  EXPECT_TRUE(abstract_type.IsDuration());

  TypeSpec primitive_spec(PrimitiveType::kInt64);
  ASSERT_OK_AND_ASSIGN(
      auto primitive_type,
      ConvertTypeSpecToType(primitive_spec, introspector, GetTestArena()));
  EXPECT_TRUE(primitive_type.IsInt());

  TypeSpec custom_spec(MessageTypeSpec("custom.UnknownMessage"));
  EXPECT_THAT(
      ConvertTypeSpecToType(custom_spec, introspector, GetTestArena()),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Message type 'custom.UnknownMessage' not found")));
}

TEST(TypeSpecResolverTest, UnknownTypeSpecKindError) {
  TypeSpec spec;
  auto t =
      ConvertTypeSpecToType(spec, *GetTestingDescriptorPool(), GetTestArena());
  EXPECT_THAT(t, StatusIs(absl::StatusCode::kInvalidArgument,
                          HasSubstr("Unknown TypeSpec kind")));
}

}  // namespace
}  // namespace cel
