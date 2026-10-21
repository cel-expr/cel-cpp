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

#include "runtime/standard/time_functions.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "base/builtins.h"
#include "common/function_descriptor.h"
#include "common/standard_definitions.h"
#include "common/value.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"
#include "internal/testing.h"
#include "runtime/function.h"
#include "runtime/function_overload_reference.h"
#include "runtime/function_registry.h"
#include "runtime/runtime_options.h"

namespace cel {
namespace {

using ::testing::UnorderedElementsAre;

MATCHER_P3(MatchesOperatorDescriptor, name, expected_kind1, expected_kind2,
           "") {
  const FunctionDescriptor& descriptor = *arg;
  std::vector<Kind> kinds{expected_kind1, expected_kind2};
  return descriptor.name() == name && descriptor.receiver_style() == false &&
         descriptor.kinds() == kinds;
}

MATCHER_P2(MatchesTimeAccessor, name, kind, "") {
  const FunctionDescriptor& descriptor = *arg;

  std::vector<Kind> kinds{kind};
  return descriptor.name() == name && descriptor.receiver_style() == true &&
         descriptor.kinds() == kinds;
}

MATCHER_P2(MatchesTimezoneTimeAccessor, name, kind, "") {
  const FunctionDescriptor& descriptor = *arg;

  std::vector<Kind> kinds{kind, Kind::kString};
  return descriptor.name() == name && descriptor.receiver_style() == true &&
         descriptor.kinds() == kinds;
}

TEST(RegisterTimeFunctions, MathOperatorsRegistered) {
  FunctionRegistry registry;
  RuntimeOptions options;

  ASSERT_OK(RegisterTimeFunctions(registry, options));

  auto registered_functions = registry.ListFunctions();

  EXPECT_THAT(registered_functions[builtin::kAdd],
              UnorderedElementsAre(
                  MatchesOperatorDescriptor(builtin::kAdd, Kind::kDuration,
                                            Kind::kDuration),
                  MatchesOperatorDescriptor(builtin::kAdd, Kind::kTimestamp,
                                            Kind::kDuration),
                  MatchesOperatorDescriptor(builtin::kAdd, Kind::kDuration,
                                            Kind::kTimestamp)));

  EXPECT_THAT(registered_functions[builtin::kSubtract],
              UnorderedElementsAre(
                  MatchesOperatorDescriptor(builtin::kSubtract, Kind::kDuration,
                                            Kind::kDuration),
                  MatchesOperatorDescriptor(builtin::kSubtract,
                                            Kind::kTimestamp, Kind::kDuration),
                  MatchesOperatorDescriptor(
                      builtin::kSubtract, Kind::kTimestamp, Kind::kTimestamp)));
}

TEST(RegisterTimeFunctions, ArithmeticIdsAgreeAcrossOverflowOptions) {
  struct Entry {
    absl::string_view name, id;
    std::vector<Kind> kinds;
  };
  const std::vector<Entry> entries = {
      {builtin::kAdd,
       StandardOverloadIds::kAddTimestampDuration,
       {Kind::kTimestamp, Kind::kDuration}},
      {builtin::kAdd,
       StandardOverloadIds::kAddDurationTimestamp,
       {Kind::kDuration, Kind::kTimestamp}},
      {builtin::kAdd,
       StandardOverloadIds::kAddDurationDuration,
       {Kind::kDuration, Kind::kDuration}},
      {builtin::kSubtract,
       StandardOverloadIds::kSubtractTimestampDuration,
       {Kind::kTimestamp, Kind::kDuration}},
      {builtin::kSubtract,
       StandardOverloadIds::kSubtractTimestampTimestamp,
       {Kind::kTimestamp, Kind::kTimestamp}},
      {builtin::kSubtract,
       StandardOverloadIds::kSubtractDurationDuration,
       {Kind::kDuration, Kind::kDuration}}};
  size_t cells = 0;
  for (bool checked : {false, true}) {
    FunctionRegistry registry;
    RuntimeOptions options;
    options.enable_timestamp_duration_overflow_errors = checked;
    ASSERT_OK(RegisterTimeFunctions(registry, options));
    const auto listed = registry.ListFunctions();
    for (const auto& entry : entries) {
      SCOPED_TRACE(checked);
      SCOPED_TRACE(entry.id);
      auto found = registry.FindStaticOverloadById(entry.name, entry.id);
      ASSERT_TRUE(found.has_value());
      const auto& d = found->descriptor;
      EXPECT_EQ(d.overload_id(), entry.id);
      EXPECT_FALSE(d.overload_id().empty());
      EXPECT_EQ(d.name(), entry.name);
      EXPECT_FALSE(d.receiver_style());
      EXPECT_EQ(d.types().size(), 2);
      EXPECT_EQ(d.kinds(), entry.kinds);
      auto list = listed.find(entry.name);
      ASSERT_NE(list, listed.end());
      EXPECT_EQ(std::count_if(list->second.begin(), list->second.end(),
                              [&](const auto* item) {
                                return item->overload_id() == entry.id &&
                                       *item == d;
                              }),
                1);
      ++cells;
    }
    auto subtraction = registry.FindStaticOverloadById(
        builtin::kSubtract, StandardOverloadIds::kSubtractDurationDuration);
    ASSERT_TRUE(subtraction.has_value());
    google::protobuf::Arena arena;
    Function::InvokeContext context(
        google::protobuf::DescriptorPool::generated_pool(),
        google::protobuf::MessageFactory::generated_factory(), &arena);
    ASSERT_OK_AND_ASSIGN(
        Value normal, subtraction->implementation.Invoke(
                          std::vector<Value>{DurationValue(absl::Seconds(5)),
                                             DurationValue(absl::Seconds(2))},
                          context));
    ASSERT_TRUE(normal.IsDuration());
    EXPECT_EQ(normal.GetDuration().NativeValue(), absl::Seconds(3));
    const auto min = absl::Nanoseconds(std::numeric_limits<int64_t>::min()),
               one = absl::Nanoseconds(1);
    ASSERT_OK_AND_ASSIGN(
        Value overflow,
        subtraction->implementation.Invoke(
            std::vector<Value>{DurationValue(min), DurationValue(one)},
            context));
    if (checked) {
      ASSERT_TRUE(overflow.IsError());
      EXPECT_EQ(overflow.GetError().NativeValue(),
                absl::OutOfRangeError("integer overflow"));
    } else {
      ASSERT_TRUE(overflow.IsDuration());
      EXPECT_EQ(overflow.GetDuration().NativeValue(), min - one);
      EXPECT_NE(overflow.GetDuration().NativeValue(), absl::InfiniteDuration());
      EXPECT_NE(overflow.GetDuration().NativeValue(),
                -absl::InfiniteDuration());
    }
  }
  EXPECT_EQ(cells, 12);
}

TEST(RegisterTimeFunctions, AccessorsRegistered) {
  FunctionRegistry registry;
  RuntimeOptions options;

  ASSERT_OK(RegisterTimeFunctions(registry, options));

  auto registered_functions = registry.ListFunctions();
  EXPECT_THAT(
      registered_functions[builtin::kFullYear],
      UnorderedElementsAre(
          MatchesTimeAccessor(builtin::kFullYear, Kind::kTimestamp),
          MatchesTimezoneTimeAccessor(builtin::kFullYear, Kind::kTimestamp)));
  EXPECT_THAT(
      registered_functions[builtin::kDate],
      UnorderedElementsAre(
          MatchesTimeAccessor(builtin::kDate, Kind::kTimestamp),
          MatchesTimezoneTimeAccessor(builtin::kDate, Kind::kTimestamp)));
  EXPECT_THAT(
      registered_functions[builtin::kMonth],
      UnorderedElementsAre(
          MatchesTimeAccessor(builtin::kMonth, Kind::kTimestamp),
          MatchesTimezoneTimeAccessor(builtin::kMonth, Kind::kTimestamp)));
  EXPECT_THAT(
      registered_functions[builtin::kDayOfYear],
      UnorderedElementsAre(
          MatchesTimeAccessor(builtin::kDayOfYear, Kind::kTimestamp),
          MatchesTimezoneTimeAccessor(builtin::kDayOfYear, Kind::kTimestamp)));
  EXPECT_THAT(
      registered_functions[builtin::kDayOfMonth],
      UnorderedElementsAre(
          MatchesTimeAccessor(builtin::kDayOfMonth, Kind::kTimestamp),
          MatchesTimezoneTimeAccessor(builtin::kDayOfMonth, Kind::kTimestamp)));
  EXPECT_THAT(
      registered_functions[builtin::kDayOfWeek],
      UnorderedElementsAre(
          MatchesTimeAccessor(builtin::kDayOfWeek, Kind::kTimestamp),
          MatchesTimezoneTimeAccessor(builtin::kDayOfWeek, Kind::kTimestamp)));

  EXPECT_THAT(
      registered_functions[builtin::kHours],
      UnorderedElementsAre(
          MatchesTimeAccessor(builtin::kHours, Kind::kTimestamp),
          MatchesTimezoneTimeAccessor(builtin::kHours, Kind::kTimestamp),
          MatchesTimeAccessor(builtin::kHours, Kind::kDuration)));

  EXPECT_THAT(
      registered_functions[builtin::kMinutes],
      UnorderedElementsAre(
          MatchesTimeAccessor(builtin::kMinutes, Kind::kTimestamp),
          MatchesTimezoneTimeAccessor(builtin::kMinutes, Kind::kTimestamp),
          MatchesTimeAccessor(builtin::kMinutes, Kind::kDuration)));

  EXPECT_THAT(
      registered_functions[builtin::kSeconds],
      UnorderedElementsAre(
          MatchesTimeAccessor(builtin::kSeconds, Kind::kTimestamp),
          MatchesTimezoneTimeAccessor(builtin::kSeconds, Kind::kTimestamp),
          MatchesTimeAccessor(builtin::kSeconds, Kind::kDuration)));

  EXPECT_THAT(
      registered_functions[builtin::kMilliseconds],
      UnorderedElementsAre(
          MatchesTimeAccessor(builtin::kMilliseconds, Kind::kTimestamp),
          MatchesTimezoneTimeAccessor(builtin::kMilliseconds, Kind::kTimestamp),
          MatchesTimeAccessor(builtin::kMilliseconds, Kind::kDuration)));
}

// TODO(uncreated-issue/41): move functional parsed expr tests when modern APIs for
// evaluator available.

}  // namespace
}  // namespace cel
