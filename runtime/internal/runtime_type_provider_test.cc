// Copyright 2026 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "runtime/internal/runtime_type_provider.h"

#include <optional>
#include <utility>

#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "cel/expr/conformance/proto3/test_all_types.pb.h"
#include "common/value.h"
#include "internal/testing.h"
#include "internal/testing_descriptor_pool.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/dynamic_message.h"
#include "google/protobuf/message.h"

namespace cel::runtime_internal {
namespace {

using ::absl_testing::IsOkAndHolds;
using ::cel::expr::conformance::proto3::TestAllTypes;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsNull;
using ::testing::NotNull;

const google::protobuf::Message* BuildMessage(ValueBuilder& builder) {
  absl::StatusOr<Value> value = std::move(builder).Build();
  if (!value.ok() || !value->IsParsedMessage()) {
    return nullptr;
  }
  return value->GetParsedMessage().message();
}

TEST(RuntimeTypeProviderTest, NewValueBuilderFactorySameFactory) {
  RuntimeTypeProvider type_provider(google::protobuf::DescriptorPool::generated_pool());
  auto factory = type_provider.NewValueBuilderFactory(
      TestAllTypes::descriptor()->full_name(),
      google::protobuf::MessageFactory::generated_factory());

  google::protobuf::Arena arena;
  for (int i = 0; i < 2; ++i) {
    ASSERT_OK_AND_ASSIGN(
        auto builder,
        factory(google::protobuf::MessageFactory::generated_factory(), &arena));
    ASSERT_THAT(builder, NotNull());
    EXPECT_THAT(builder->SetFieldByName("single_int32", IntValue(1)),
                IsOkAndHolds(Eq(std::nullopt)));
    const google::protobuf::Message* msg = BuildMessage(*builder);
    ASSERT_THAT(msg, NotNull());
    EXPECT_EQ(msg->GetDescriptor(), TestAllTypes::descriptor());
    EXPECT_EQ(msg->GetReflection()->GetMessageFactory(),
              google::protobuf::MessageFactory::generated_factory());
    EXPECT_THAT(msg->DebugString(), HasSubstr("single_int32: 1"));
  }
}

TEST(RuntimeTypeProviderTest, NewValueBuilderFactoryDifferentFactory) {
  RuntimeTypeProvider type_provider(google::protobuf::DescriptorPool::generated_pool());
  auto factory = type_provider.NewValueBuilderFactory(
      TestAllTypes::descriptor()->full_name(),
      google::protobuf::MessageFactory::generated_factory());

  google::protobuf::DynamicMessageFactory custom_factory;
  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(auto builder, factory(&custom_factory, &arena));
  ASSERT_THAT(builder, NotNull());
  const google::protobuf::Message* msg = BuildMessage(*builder);
  ASSERT_THAT(msg, NotNull());
  EXPECT_EQ(msg->GetReflection()->GetMessageFactory(), &custom_factory);
}

TEST(RuntimeTypeProviderTest, NewValueBuilderFactoryDynamicPool) {
  const google::protobuf::DescriptorPool* pool = internal::GetTestingDescriptorPool();
  RuntimeTypeProvider type_provider(pool);
  google::protobuf::DynamicMessageFactory message_factory;
  auto factory = type_provider.NewValueBuilderFactory(
      "cel.expr.conformance.proto3.TestAllTypes", &message_factory);

  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(auto builder, factory(&message_factory, &arena));
  ASSERT_THAT(builder, NotNull());
  EXPECT_THAT(builder->SetFieldByName("single_int64", IntValue(2)),
              IsOkAndHolds(Eq(std::nullopt)));
  const google::protobuf::Message* msg = BuildMessage(*builder);
  ASSERT_THAT(msg, NotNull());
  EXPECT_EQ(
      msg->GetDescriptor(),
      pool->FindMessageTypeByName("cel.expr.conformance.proto3.TestAllTypes"));
  EXPECT_EQ(msg->GetReflection()->GetMessageFactory(), &message_factory);
  EXPECT_THAT(msg->DebugString(), HasSubstr("single_int64: 2"));
}

TEST(RuntimeTypeProviderTest, NewValueBuilderFactoryUnknownType) {
  RuntimeTypeProvider type_provider(google::protobuf::DescriptorPool::generated_pool());
  auto factory = type_provider.NewValueBuilderFactory(
      "does.not.Exist", google::protobuf::MessageFactory::generated_factory());

  google::protobuf::Arena arena;
  EXPECT_THAT(factory(google::protobuf::MessageFactory::generated_factory(), &arena),
              IsOkAndHolds(IsNull()));
}

}  // namespace
}  // namespace cel::runtime_internal
