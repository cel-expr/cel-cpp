// Copyright 2024 Google LLC
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

#include "runtime/internal/legacy_runtime_type_provider.h"

#include <optional>
#include <utility>

#include "cel/expr/conformance/proto3/test_all_types.pb.h"
#include "common/type.h"
#include "common/value.h"
#include "internal/testing.h"
#include "internal/testing_descriptor_pool.h"
#include "internal/testing_message_factory.h"
#include "runtime/internal/runtime_type_provider.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"

namespace cel::runtime_internal {
namespace {

using ::cel::expr::conformance::proto3::TestAllTypes;

TEST(LegacyRuntimeTypeProviderTest, FindType) {
  RuntimeTypeProvider type_provider(cel::internal::GetTestingDescriptorPool());
  LegacyRuntimeTypeProvider provider(cel::internal::GetTestingDescriptorPool(),
                                     &type_provider);
  ASSERT_OK_AND_ASSIGN(std::optional<Type> wrapper_type,
                       provider.FindType("google.protobuf.Int64Value"));
  ASSERT_TRUE(wrapper_type.has_value());
  EXPECT_TRUE(wrapper_type->Is<IntWrapperType>());
  EXPECT_EQ(wrapper_type->name(), "google.protobuf.Int64Value");

  ASSERT_OK_AND_ASSIGN(
      std::optional<Type> msg_type,
      provider.FindType("cel.expr.conformance.proto3.TestAllTypes"));
  ASSERT_TRUE(msg_type.has_value());
  EXPECT_TRUE(msg_type->Is<MessageType>());
  EXPECT_EQ(msg_type->name(), "cel.expr.conformance.proto3.TestAllTypes");
}

TEST(LegacyRuntimeTypeProviderTest, FindTypeNotFound) {
  RuntimeTypeProvider type_provider(google::protobuf::DescriptorPool::generated_pool());
  LegacyRuntimeTypeProvider provider(google::protobuf::DescriptorPool::generated_pool(),
                                     &type_provider);
  ASSERT_OK_AND_ASSIGN(std::optional<Type> type,
                       provider.FindType("UnknownType"));
  EXPECT_FALSE(type.has_value());
}

TEST(LegacyRuntimeTypeProviderTest, FindStructTypeFieldByName) {
  RuntimeTypeProvider type_provider(google::protobuf::DescriptorPool::generated_pool());
  LegacyRuntimeTypeProvider provider(google::protobuf::DescriptorPool::generated_pool(),
                                     &type_provider);
  ASSERT_OK_AND_ASSIGN(std::optional<StructTypeField> field,
                       provider.FindStructTypeFieldByName(
                           "google.protobuf.Int64Value", "value"));
  ASSERT_TRUE(field.has_value());
  EXPECT_EQ(field->name(), "value");
  EXPECT_EQ(field->number(), 1);
  EXPECT_EQ(field->GetType(), IntType());
}

TEST(LegacyRuntimeTypeProviderTest, FindStructTypeFieldByNameNotFound) {
  RuntimeTypeProvider type_provider(google::protobuf::DescriptorPool::generated_pool());
  LegacyRuntimeTypeProvider provider(google::protobuf::DescriptorPool::generated_pool(),
                                     &type_provider);
  ASSERT_OK_AND_ASSIGN(std::optional<StructTypeField> field,
                       provider.FindStructTypeFieldByName(
                           "google.protobuf.Int64Value", "unknown_field"));
  EXPECT_FALSE(field.has_value());

  ASSERT_OK_AND_ASSIGN(
      std::optional<StructTypeField> field2,
      provider.FindStructTypeFieldByName("UnknownType", "value"));
  EXPECT_FALSE(field2.has_value());
}

TEST(LegacyRuntimeTypeProviderTest, NewValueBuilderMessage) {
  RuntimeTypeProvider type_provider(cel::internal::GetTestingDescriptorPool());
  LegacyRuntimeTypeProvider provider(cel::internal::GetTestingDescriptorPool(),
                                     &type_provider);
  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(auto builder,
                       provider.NewValueBuilder(
                           "cel.expr.conformance.proto3.TestAllTypes",
                           cel::internal::GetTestingMessageFactory(), &arena));
  ASSERT_NE(builder, nullptr);

  ASSERT_OK_AND_ASSIGN(auto field_result,
                       builder->SetFieldByName("single_int64", IntValue(42)));
  EXPECT_FALSE(field_result.has_value());

  ASSERT_OK_AND_ASSIGN(auto field_result2,
                       builder->SetFieldByNumber(1, IntValue(100)));
  EXPECT_FALSE(field_result2.has_value());

  ASSERT_OK_AND_ASSIGN(auto value, std::move(*builder).Build());
  EXPECT_TRUE(value.Is<StructValue>());
}

TEST(LegacyRuntimeTypeProviderTest, NewValueBuilderWellKnownType) {
  RuntimeTypeProvider type_provider(cel::internal::GetTestingDescriptorPool());
  LegacyRuntimeTypeProvider provider(cel::internal::GetTestingDescriptorPool(),
                                     &type_provider);
  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(auto builder,
                       provider.NewValueBuilder(
                           "google.protobuf.Int64Value",
                           cel::internal::GetTestingMessageFactory(), &arena));
  ASSERT_NE(builder, nullptr);

  ASSERT_OK_AND_ASSIGN(auto field_result,
                       builder->SetFieldByName("value", IntValue(42)));
  EXPECT_FALSE(field_result.has_value());

  ASSERT_OK_AND_ASSIGN(auto value, std::move(*builder).Build());
  ASSERT_TRUE(value.Is<IntValue>());
  EXPECT_EQ(value.As<IntValue>()->NativeValue(), 42);
}

TEST(LegacyRuntimeTypeProviderTest, NewValueBuilderNotFound) {
  RuntimeTypeProvider type_provider(google::protobuf::DescriptorPool::generated_pool());
  LegacyRuntimeTypeProvider provider(google::protobuf::DescriptorPool::generated_pool(),
                                     &type_provider);
  google::protobuf::LinkMessageReflection<TestAllTypes>();
  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(
      auto builder,
      provider.NewValueBuilder(
          "UnknownType", google::protobuf::MessageFactory::generated_factory(), &arena));
  EXPECT_EQ(builder, nullptr);
}

}  // namespace
}  // namespace cel::runtime_internal
