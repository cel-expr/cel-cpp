// Copyright 2023 Google LLC
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

#include "runtime/standard/container_membership_functions.h"

#include <array>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "base/builtins.h"
#include "common/function_descriptor.h"
#include "common/kind.h"
#include "common/value.h"
#include "common/value_testing.h"
#include "internal/testing.h"
#include "internal/testing_descriptor_pool.h"
#include "internal/testing_message_factory.h"
#include "runtime/function.h"
#include "runtime/function_overload_reference.h"
#include "runtime/function_registry.h"
#include "runtime/runtime_options.h"
#include "google/protobuf/arena.h"

namespace cel {
namespace {

using ::absl_testing::IsOk;
using ::cel::test::BoolValueIs;
using ::testing::SizeIs;
using ::testing::UnorderedElementsAre;

MATCHER_P3(MatchesDescriptor, name, receiver, expected_kinds, "") {
  const FunctionDescriptor& descriptor = *arg;
  const std::vector<Kind>& types = expected_kinds;
  return descriptor.name() == name && descriptor.receiver_style() == receiver &&
         descriptor.types() == types;
}

static constexpr std::array<absl::string_view, 3> kInOperators = {
    builtin::kIn, builtin::kInDeprecated, builtin::kInFunction};

TEST(RegisterContainerMembershipFunctions, RegistersHomogeneousInOperator) {
  FunctionRegistry registry;
  RuntimeOptions options;
  options.enable_heterogeneous_equality = false;

  ASSERT_OK(RegisterContainerMembershipFunctions(registry, options));

  auto overloads = registry.ListFunctions();

  for (absl::string_view operator_name : kInOperators) {
    EXPECT_THAT(
        overloads[operator_name],
        UnorderedElementsAre(
            MatchesDescriptor(operator_name, false,
                              std::vector<Kind>{Kind::kInt, Kind::kList}),
            MatchesDescriptor(operator_name, false,
                              std::vector<Kind>{Kind::kUint, Kind::kList}),
            MatchesDescriptor(operator_name, false,
                              std::vector<Kind>{Kind::kDouble, Kind::kList}),
            MatchesDescriptor(operator_name, false,
                              std::vector<Kind>{Kind::kString, Kind::kList}),
            MatchesDescriptor(operator_name, false,
                              std::vector<Kind>{Kind::kBytes, Kind::kList}),
            MatchesDescriptor(operator_name, false,
                              std::vector<Kind>{Kind::kBool, Kind::kList}),
            MatchesDescriptor(operator_name, false,
                              std::vector<Kind>{Kind::kInt, Kind::kMap}),
            MatchesDescriptor(operator_name, false,
                              std::vector<Kind>{Kind::kUint, Kind::kMap}),
            MatchesDescriptor(operator_name, false,
                              std::vector<Kind>{Kind::kString, Kind::kMap}),
            MatchesDescriptor(operator_name, false,
                              std::vector<Kind>{Kind::kBool, Kind::kMap})));
  }
}

TEST(RegisterContainerMembershipFunctions, RegistersHeterogeneousInOperation) {
  FunctionRegistry registry;
  RuntimeOptions options;
  options.enable_heterogeneous_equality = true;

  ASSERT_OK(RegisterContainerMembershipFunctions(registry, options));

  auto overloads = registry.ListFunctions();

  for (absl::string_view operator_name : kInOperators) {
    EXPECT_THAT(
        overloads[operator_name],
        UnorderedElementsAre(
            MatchesDescriptor(operator_name, false,
                              std::vector<Kind>{Kind::kAny, Kind::kList}),
            MatchesDescriptor(operator_name, false,
                              std::vector<Kind>{Kind::kInt, Kind::kMap}),
            MatchesDescriptor(operator_name, false,
                              std::vector<Kind>{Kind::kUint, Kind::kMap}),
            MatchesDescriptor(operator_name, false,
                              std::vector<Kind>{Kind::kDouble, Kind::kMap}),
            MatchesDescriptor(operator_name, false,
                              std::vector<Kind>{Kind::kString, Kind::kMap}),
            MatchesDescriptor(operator_name, false,
                              std::vector<Kind>{Kind::kBool, Kind::kMap})));
  }
}

TEST(RegisterContainerMembershipFunctions, RegistersInOperatorListsDisabled) {
  FunctionRegistry registry;
  RuntimeOptions options;
  options.enable_list_contains = false;

  ASSERT_OK(RegisterContainerMembershipFunctions(registry, options));

  auto overloads = registry.ListFunctions();

  for (absl::string_view operator_name : kInOperators) {
    EXPECT_THAT(
        overloads[operator_name],
        UnorderedElementsAre(

            MatchesDescriptor(operator_name, false,
                              std::vector<Kind>{Kind::kInt, Kind::kMap}),
            MatchesDescriptor(operator_name, false,
                              std::vector<Kind>{Kind::kUint, Kind::kMap}),
            MatchesDescriptor(operator_name, false,
                              std::vector<Kind>{Kind::kDouble, Kind::kMap}),
            MatchesDescriptor(operator_name, false,
                              std::vector<Kind>{Kind::kString, Kind::kMap}),
            MatchesDescriptor(operator_name, false,
                              std::vector<Kind>{Kind::kBool, Kind::kMap})));
  }
}

// When enable_fast_builtins is true, the planner routes heterogeneous `in`
// through a dedicated step; invoke the registered overload directly to cover
// the uint -> int key fallback used when enable_fast_builtins is false.
TEST(RegisterContainerMembershipFunctions, HeterogeneousUintKeyMatchesIntKey) {
  FunctionRegistry registry;
  RuntimeOptions options;
  options.enable_heterogeneous_equality = true;
  ASSERT_THAT(RegisterContainerMembershipFunctions(registry, options), IsOk());

  google::protobuf::Arena arena;
  auto builder = NewMapValueBuilder(&arena);
  ASSERT_THAT(builder->Put(IntValue(1), IntValue(2)), IsOk());
  ASSERT_THAT(builder->Put(IntValue(-1), IntValue(3)), IsOk());
  MapValue map_value = std::move(*builder).Build();

  for (absl::string_view operator_name : kInOperators) {
    std::vector<FunctionOverloadReference> overloads =
        registry.FindStaticOverloads(operator_name, /*receiver_style=*/false,
                                     {Kind::kUint, Kind::kMap});
    ASSERT_THAT(overloads, SizeIs(1));
    const Function::InvokeContext context(internal::GetTestingDescriptorPool(),
                                          internal::GetTestingMessageFactory(),
                                          &arena);

    ASSERT_OK_AND_ASSIGN(Value found, overloads[0].implementation.Invoke(
                                          {UintValue(1), map_value}, context));
    EXPECT_THAT(found, BoolValueIs(true));

    ASSERT_OK_AND_ASSIGN(
        Value missing,
        overloads[0].implementation.Invoke({UintValue(2), map_value}, context));
    EXPECT_THAT(missing, BoolValueIs(false));

    ASSERT_OK_AND_ASSIGN(
        Value unconvertible,
        overloads[0].implementation.Invoke(
            {UintValue(std::numeric_limits<uint64_t>::max()), map_value},
            context));
    EXPECT_THAT(unconvertible, BoolValueIs(false));
  }
}

// TODO(uncreated-issue/41): move functional parsed expr tests when modern APIs for
// evaluator available.

}  // namespace
}  // namespace cel
