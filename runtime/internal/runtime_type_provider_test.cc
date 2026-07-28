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

#include "runtime/internal/runtime_type_provider.h"

#include "absl/status/status_matchers.h"
#include "internal/testing.h"
#include "cel/expr/conformance/proto3/test_all_types.pb.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/dynamic_message.h"
#include "google/protobuf/message.h"

namespace cel::runtime_internal {
namespace {

using ::absl_testing::IsOk;
using ::cel::expr::conformance::proto3::TestAllTypes;

TEST(RuntimeTypeProviderTest, NewValueBuilderFactorySameFactory) {
  RuntimeTypeProvider type_provider(google::protobuf::DescriptorPool::generated_pool());
  auto factory = type_provider.NewValueBuilderFactory(
      TestAllTypes::descriptor()->full_name(),
      google::protobuf::MessageFactory::generated_factory());

  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(
      auto builder,
      factory(google::protobuf::MessageFactory::generated_factory(), &arena));
  ASSERT_NE(builder, nullptr);
}

TEST(RuntimeTypeProviderTest, NewValueBuilderFactoryDifferentFactory) {
  RuntimeTypeProvider type_provider(google::protobuf::DescriptorPool::generated_pool());
  auto factory = type_provider.NewValueBuilderFactory(
      TestAllTypes::descriptor()->full_name(),
      google::protobuf::MessageFactory::generated_factory());

  google::protobuf::DynamicMessageFactory custom_factory;
  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(auto builder, factory(&custom_factory, &arena));
  ASSERT_NE(builder, nullptr);
}

}  // namespace
}  // namespace cel::runtime_internal
