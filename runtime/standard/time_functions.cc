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

#include <cstdint>
#include <functional>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "base/builtins.h"
#include "base/function_adapter.h"
#include "common/standard_definitions.h"
#include "common/value.h"
#include "internal/overflow.h"
#include "internal/status_macros.h"
#include "runtime/function.h"
#include "runtime/function_registry.h"
#include "runtime/runtime_options.h"
#include "google/protobuf/arena.h"

namespace cel {
namespace {

// Timestamp
absl::Status FindTimeBreakdown(absl::Time timestamp, absl::string_view tz,
                               absl::TimeZone::CivilInfo* breakdown) {
  absl::TimeZone time_zone;

  // Early return if there is no timezone.
  if (tz.empty()) {
    *breakdown = time_zone.At(timestamp);
    return absl::OkStatus();
  }

  // Check to see whether the timezone is an IANA timezone.
  if (absl::LoadTimeZone(tz, &time_zone)) {
    *breakdown = time_zone.At(timestamp);
    return absl::OkStatus();
  }

  // Check for times of the format: [+-]HH:MM and convert them into durations
  // specified as [+-]HHhMMm.
  if (absl::StrContains(tz, ":")) {
    std::string dur = absl::StrCat(tz, "m");
    absl::StrReplaceAll({{":", "h"}}, &dur);
    absl::Duration d;
    if (absl::ParseDuration(dur, &d)) {
      timestamp += d;
      *breakdown = time_zone.At(timestamp);
      return absl::OkStatus();
    }
  }

  // Otherwise, error.
  return absl::InvalidArgumentError("Invalid timezone");
}

Value GetTimeBreakdownPart(
    absl::Time timestamp, absl::string_view tz,
    const std::function<int64_t(const absl::TimeZone::CivilInfo&)>&
        extractor_func,
    google::protobuf::Arena* arena) {
  absl::TimeZone::CivilInfo breakdown;
  auto status = FindTimeBreakdown(timestamp, tz, &breakdown);

  if (!status.ok()) {
    return ErrorValue::From(status, arena);
  }

  return IntValue(extractor_func(breakdown));
}

Value GetFullYear(absl::Time timestamp, absl::string_view tz,
                  const Function::InvokeContext& context) {
  return GetTimeBreakdownPart(
      timestamp, tz,
      [](const absl::TimeZone::CivilInfo& breakdown) {
        return breakdown.cs.year();
      },
      context.arena());
}

Value GetMonth(absl::Time timestamp, absl::string_view tz,
               const Function::InvokeContext& context) {
  return GetTimeBreakdownPart(
      timestamp, tz,
      [](const absl::TimeZone::CivilInfo& breakdown) {
        return breakdown.cs.month() - 1;
      },
      context.arena());
}

Value GetDayOfYear(absl::Time timestamp, absl::string_view tz,
                   const Function::InvokeContext& context) {
  return GetTimeBreakdownPart(
      timestamp, tz,
      [](const absl::TimeZone::CivilInfo& breakdown) {
        return absl::GetYearDay(absl::CivilDay(breakdown.cs)) - 1;
      },
      context.arena());
}

Value GetDayOfMonth(absl::Time timestamp, absl::string_view tz,
                    const Function::InvokeContext& context) {
  return GetTimeBreakdownPart(
      timestamp, tz,
      [](const absl::TimeZone::CivilInfo& breakdown) {
        return breakdown.cs.day() - 1;
      },
      context.arena());
}

Value GetDate(absl::Time timestamp, absl::string_view tz,
              const Function::InvokeContext& context) {
  return GetTimeBreakdownPart(
      timestamp, tz,
      [](const absl::TimeZone::CivilInfo& breakdown) {
        return breakdown.cs.day();
      },
      context.arena());
}

Value GetDayOfWeek(absl::Time timestamp, absl::string_view tz,
                   const Function::InvokeContext& context) {
  return GetTimeBreakdownPart(
      timestamp, tz,
      [](const absl::TimeZone::CivilInfo& breakdown) {
        absl::Weekday weekday = absl::GetWeekday(breakdown.cs);

        // get day of week from the date in UTC, zero-based, zero for Sunday,
        // based on GetDayOfWeek CEL function definition.
        int weekday_num = static_cast<int>(weekday);
        weekday_num = (weekday_num == 6) ? 0 : weekday_num + 1;
        return weekday_num;
      },
      context.arena());
}

Value GetHours(absl::Time timestamp, absl::string_view tz,
               const Function::InvokeContext& context) {
  return GetTimeBreakdownPart(
      timestamp, tz,
      [](const absl::TimeZone::CivilInfo& breakdown) {
        return breakdown.cs.hour();
      },
      context.arena());
}

Value GetMinutes(absl::Time timestamp, absl::string_view tz,
                 const Function::InvokeContext& context) {
  return GetTimeBreakdownPart(
      timestamp, tz,
      [](const absl::TimeZone::CivilInfo& breakdown) {
        return breakdown.cs.minute();
      },
      context.arena());
}

Value GetSeconds(absl::Time timestamp, absl::string_view tz,
                 const Function::InvokeContext& context) {
  return GetTimeBreakdownPart(
      timestamp, tz,
      [](const absl::TimeZone::CivilInfo& breakdown) {
        return breakdown.cs.second();
      },
      context.arena());
}

Value GetMilliseconds(absl::Time timestamp, absl::string_view tz,
                      const Function::InvokeContext& context) {
  return GetTimeBreakdownPart(
      timestamp, tz,
      [](const absl::TimeZone::CivilInfo& breakdown) {
        return absl::ToInt64Milliseconds(breakdown.subsecond);
      },
      context.arena());
}

absl::Status RegisterTimestampFunctions(FunctionRegistry& registry,
                                        const RuntimeOptions& options) {
  CEL_RETURN_IF_ERROR(registry.Register(
      BinaryFunctionAdapter<Value, absl::Time, const StringValue&>::
          CreateDescriptor(builtin::kFullYear,
                           StandardOverloadIds::kTimestampToYearWithTz, true),
      BinaryFunctionAdapter<Value, absl::Time, const StringValue&>::
          WrapFunction([](absl::Time ts, const StringValue& tz,
                          const Function::InvokeContext& context) -> Value {
            return GetFullYear(ts, tz.ToString(), context);
          })));

  CEL_RETURN_IF_ERROR(registry.Register(
      UnaryFunctionAdapter<Value, absl::Time>::CreateDescriptor(
          builtin::kFullYear, StandardOverloadIds::kTimestampToYear, true),
      UnaryFunctionAdapter<Value, absl::Time>::WrapFunction(
          [](absl::Time ts, const Function::InvokeContext& context) -> Value {
            return GetFullYear(ts, "", context);
          })));

  CEL_RETURN_IF_ERROR(registry.Register(
      BinaryFunctionAdapter<Value, absl::Time, const StringValue&>::
          CreateDescriptor(builtin::kMonth,
                           StandardOverloadIds::kTimestampToMonthWithTz, true),
      BinaryFunctionAdapter<Value, absl::Time, const StringValue&>::
          WrapFunction([](absl::Time ts, const StringValue& tz,
                          const Function::InvokeContext& context) -> Value {
            return GetMonth(ts, tz.ToString(), context);
          })));

  CEL_RETURN_IF_ERROR(registry.Register(
      UnaryFunctionAdapter<Value, absl::Time>::CreateDescriptor(
          builtin::kMonth, StandardOverloadIds::kTimestampToMonth, true),
      UnaryFunctionAdapter<Value, absl::Time>::WrapFunction(
          [](absl::Time ts, const Function::InvokeContext& context) -> Value {
            return GetMonth(ts, "", context);
          })));

  CEL_RETURN_IF_ERROR(registry.Register(
      BinaryFunctionAdapter<Value, absl::Time, const StringValue&>::
          CreateDescriptor(builtin::kDayOfYear,
                           StandardOverloadIds::kTimestampToDayOfYearWithTz,
                           true),
      BinaryFunctionAdapter<Value, absl::Time, const StringValue&>::
          WrapFunction([](absl::Time ts, const StringValue& tz,
                          const Function::InvokeContext& context) -> Value {
            return GetDayOfYear(ts, tz.ToString(), context);
          })));

  CEL_RETURN_IF_ERROR(registry.Register(
      UnaryFunctionAdapter<Value, absl::Time>::CreateDescriptor(
          builtin::kDayOfYear, StandardOverloadIds::kTimestampToDayOfYear,
          true),
      UnaryFunctionAdapter<Value, absl::Time>::WrapFunction(
          [](absl::Time ts, const Function::InvokeContext& context) -> Value {
            return GetDayOfYear(ts, "", context);
          })));

  CEL_RETURN_IF_ERROR(registry.Register(
      BinaryFunctionAdapter<Value, absl::Time, const StringValue&>::
          CreateDescriptor(builtin::kDayOfMonth,
                           StandardOverloadIds::kTimestampToDayOfMonthWithTz,
                           true),
      BinaryFunctionAdapter<Value, absl::Time, const StringValue&>::
          WrapFunction([](absl::Time ts, const StringValue& tz,
                          const Function::InvokeContext& context) -> Value {
            return GetDayOfMonth(ts, tz.ToString(), context);
          })));

  CEL_RETURN_IF_ERROR(registry.Register(
      UnaryFunctionAdapter<Value, absl::Time>::CreateDescriptor(
          builtin::kDayOfMonth, StandardOverloadIds::kTimestampToDayOfMonth,
          true),
      UnaryFunctionAdapter<Value, absl::Time>::WrapFunction(
          [](absl::Time ts, const Function::InvokeContext& context) -> Value {
            return GetDayOfMonth(ts, "", context);
          })));

  CEL_RETURN_IF_ERROR(registry.Register(
      BinaryFunctionAdapter<Value, absl::Time, const StringValue&>::
          CreateDescriptor(builtin::kDate,
                           StandardOverloadIds::kTimestampToDateWithTz, true),
      BinaryFunctionAdapter<Value, absl::Time, const StringValue&>::
          WrapFunction([](absl::Time ts, const StringValue& tz,
                          const Function::InvokeContext& context) -> Value {
            return GetDate(ts, tz.ToString(), context);
          })));

  CEL_RETURN_IF_ERROR(registry.Register(
      UnaryFunctionAdapter<Value, absl::Time>::CreateDescriptor(
          builtin::kDate, StandardOverloadIds::kTimestampToDate, true),
      UnaryFunctionAdapter<Value, absl::Time>::WrapFunction(
          [](absl::Time ts, const Function::InvokeContext& context) -> Value {
            return GetDate(ts, "", context);
          })));

  CEL_RETURN_IF_ERROR(registry.Register(
      BinaryFunctionAdapter<Value, absl::Time, const StringValue&>::
          CreateDescriptor(builtin::kDayOfWeek,
                           StandardOverloadIds::kTimestampToDayOfWeekWithTz,
                           true),
      BinaryFunctionAdapter<Value, absl::Time, const StringValue&>::
          WrapFunction([](absl::Time ts, const StringValue& tz,
                          const Function::InvokeContext& context) -> Value {
            return GetDayOfWeek(ts, tz.ToString(), context);
          })));

  CEL_RETURN_IF_ERROR(registry.Register(
      UnaryFunctionAdapter<Value, absl::Time>::CreateDescriptor(
          builtin::kDayOfWeek, StandardOverloadIds::kTimestampToDayOfWeek,
          true),
      UnaryFunctionAdapter<Value, absl::Time>::WrapFunction(
          [](absl::Time ts, const Function::InvokeContext& context) -> Value {
            return GetDayOfWeek(ts, "", context);
          })));

  CEL_RETURN_IF_ERROR(registry.Register(
      BinaryFunctionAdapter<Value, absl::Time, const StringValue&>::
          CreateDescriptor(builtin::kHours,
                           StandardOverloadIds::kTimestampToHoursWithTz, true),
      BinaryFunctionAdapter<Value, absl::Time, const StringValue&>::
          WrapFunction([](absl::Time ts, const StringValue& tz,
                          const Function::InvokeContext& context) -> Value {
            return GetHours(ts, tz.ToString(), context);
          })));

  CEL_RETURN_IF_ERROR(registry.Register(
      UnaryFunctionAdapter<Value, absl::Time>::CreateDescriptor(
          builtin::kHours, StandardOverloadIds::kTimestampToHours, true),
      UnaryFunctionAdapter<Value, absl::Time>::WrapFunction(
          [](absl::Time ts, const Function::InvokeContext& context) -> Value {
            return GetHours(ts, "", context);
          })));

  CEL_RETURN_IF_ERROR(registry.Register(
      BinaryFunctionAdapter<Value, absl::Time, const StringValue&>::
          CreateDescriptor(builtin::kMinutes,
                           StandardOverloadIds::kTimestampToMinutesWithTz,
                           true),
      BinaryFunctionAdapter<Value, absl::Time, const StringValue&>::
          WrapFunction([](absl::Time ts, const StringValue& tz,
                          const Function::InvokeContext& context) -> Value {
            return GetMinutes(ts, tz.ToString(), context);
          })));

  CEL_RETURN_IF_ERROR(registry.Register(
      UnaryFunctionAdapter<Value, absl::Time>::CreateDescriptor(
          builtin::kMinutes, StandardOverloadIds::kTimestampToMinutes, true),
      UnaryFunctionAdapter<Value, absl::Time>::WrapFunction(
          [](absl::Time ts, const Function::InvokeContext& context) -> Value {
            return GetMinutes(ts, "", context);
          })));

  CEL_RETURN_IF_ERROR(registry.Register(
      BinaryFunctionAdapter<Value, absl::Time, const StringValue&>::
          CreateDescriptor(builtin::kSeconds,
                           StandardOverloadIds::kTimestampToSecondsWithTz,
                           true),
      BinaryFunctionAdapter<Value, absl::Time, const StringValue&>::
          WrapFunction([](absl::Time ts, const StringValue& tz,
                          const Function::InvokeContext& context) -> Value {
            return GetSeconds(ts, tz.ToString(), context);
          })));

  CEL_RETURN_IF_ERROR(registry.Register(
      UnaryFunctionAdapter<Value, absl::Time>::CreateDescriptor(
          builtin::kSeconds, StandardOverloadIds::kTimestampToSeconds, true),
      UnaryFunctionAdapter<Value, absl::Time>::WrapFunction(
          [](absl::Time ts, const Function::InvokeContext& context) -> Value {
            return GetSeconds(ts, "", context);
          })));

  CEL_RETURN_IF_ERROR(registry.Register(
      BinaryFunctionAdapter<Value, absl::Time, const StringValue&>::
          CreateDescriptor(builtin::kMilliseconds,
                           StandardOverloadIds::kTimestampToMillisecondsWithTz,
                           true),
      BinaryFunctionAdapter<Value, absl::Time, const StringValue&>::
          WrapFunction([](absl::Time ts, const StringValue& tz,
                          const Function::InvokeContext& context) -> Value {
            return GetMilliseconds(ts, tz.ToString(), context);
          })));

  return registry.Register(
      UnaryFunctionAdapter<Value, absl::Time>::CreateDescriptor(
          builtin::kMilliseconds, StandardOverloadIds::kTimestampToMilliseconds,
          true),
      UnaryFunctionAdapter<Value, absl::Time>::WrapFunction(
          [](absl::Time ts, const Function::InvokeContext& context) -> Value {
            return GetMilliseconds(ts, "", context);
          }));
}

absl::Status RegisterCheckedTimeArithmeticFunctions(
    FunctionRegistry& registry) {
  CEL_RETURN_IF_ERROR(registry.Register(
      BinaryFunctionAdapter<Value, absl::Time, absl::Duration>::
          CreateDescriptor(builtin::kAdd,
                           StandardOverloadIds::kAddTimestampDuration, false),
      BinaryFunctionAdapter<absl::StatusOr<Value>, absl::Time, absl::Duration>::
          WrapFunction([](absl::Time t1, absl::Duration d2,
                          const Function::InvokeContext& context)
                           -> absl::StatusOr<Value> {
            auto sum = cel::internal::CheckedAdd(t1, d2);
            if (!sum.ok()) {
              return ErrorValue::From(sum.status(), context.arena());
            }
            return TimestampValue(*sum);
          })));

  CEL_RETURN_IF_ERROR(registry.Register(
      BinaryFunctionAdapter<absl::StatusOr<Value>, absl::Duration, absl::Time>::
          CreateDescriptor(builtin::kAdd,
                           StandardOverloadIds::kAddDurationTimestamp, false),
      BinaryFunctionAdapter<absl::StatusOr<Value>, absl::Duration, absl::Time>::
          WrapFunction([](absl::Duration d2, absl::Time t1,
                          const Function::InvokeContext& context)
                           -> absl::StatusOr<Value> {
            auto sum = cel::internal::CheckedAdd(t1, d2);
            if (!sum.ok()) {
              return ErrorValue::From(sum.status(), context.arena());
            }
            return TimestampValue(*sum);
          })));

  CEL_RETURN_IF_ERROR(registry.Register(
      BinaryFunctionAdapter<absl::StatusOr<Value>, absl::Duration,
                            absl::Duration>::
          CreateDescriptor(builtin::kAdd,
                           StandardOverloadIds::kAddDurationDuration, false),
      BinaryFunctionAdapter<absl::StatusOr<Value>, absl::Duration,
                            absl::Duration>::
          WrapFunction([](absl::Duration d1, absl::Duration d2,
                          const Function::InvokeContext& context)
                           -> absl::StatusOr<Value> {
            auto sum = cel::internal::CheckedAdd(d1, d2);
            if (!sum.ok()) {
              return ErrorValue::From(sum.status(), context.arena());
            }
            return DurationValue(*sum);
          })));

  CEL_RETURN_IF_ERROR(registry.Register(
      BinaryFunctionAdapter<absl::StatusOr<Value>, absl::Time, absl::Duration>::
          CreateDescriptor(builtin::kSubtract,
                           StandardOverloadIds::kSubtractTimestampDuration,
                           false),
      BinaryFunctionAdapter<absl::StatusOr<Value>, absl::Time, absl::Duration>::
          WrapFunction([](absl::Time t1, absl::Duration d2,
                          const Function::InvokeContext& context)
                           -> absl::StatusOr<Value> {
            auto diff = cel::internal::CheckedSub(t1, d2);
            if (!diff.ok()) {
              return ErrorValue::From(diff.status(), context.arena());
            }
            return TimestampValue(*diff);
          })));

  CEL_RETURN_IF_ERROR(registry.Register(
      BinaryFunctionAdapter<absl::StatusOr<Value>, absl::Time, absl::Time>::
          CreateDescriptor(builtin::kSubtract,
                           StandardOverloadIds::kSubtractTimestampTimestamp,
                           false),
      BinaryFunctionAdapter<absl::StatusOr<Value>, absl::Time, absl::Time>::
          WrapFunction([](absl::Time t1, absl::Time t2,
                          const Function::InvokeContext& context)
                           -> absl::StatusOr<Value> {
            auto diff = cel::internal::CheckedSub(t1, t2);
            if (!diff.ok()) {
              return ErrorValue::From(diff.status(), context.arena());
            }
            return DurationValue(*diff);
          })));

  CEL_RETURN_IF_ERROR(registry.Register(
      BinaryFunctionAdapter<absl::StatusOr<Value>, absl::Duration,
                            absl::Duration>::
          CreateDescriptor(builtin::kSubtract,
                           StandardOverloadIds::kSubtractDurationDuration,
                           false),
      BinaryFunctionAdapter<absl::StatusOr<Value>, absl::Duration,
                            absl::Duration>::
          WrapFunction([](absl::Duration d1, absl::Duration d2,
                          const Function::InvokeContext& context)
                           -> absl::StatusOr<Value> {
            auto diff = cel::internal::CheckedSub(d1, d2);
            if (!diff.ok()) {
              return ErrorValue::From(diff.status(), context.arena());
            }
            return DurationValue(*diff);
          })));

  return absl::OkStatus();
}

absl::Status RegisterUncheckedTimeArithmeticFunctions(
    FunctionRegistry& registry) {
  CEL_RETURN_IF_ERROR(registry.Register(
      BinaryFunctionAdapter<Value, absl::Time, absl::Duration>::
          CreateDescriptor(builtin::kAdd,
                           StandardOverloadIds::kAddTimestampDuration, false),
      BinaryFunctionAdapter<Value, absl::Time, absl::Duration>::WrapFunction(
          [](absl::Time t1, absl::Duration d2) -> Value {
            return UnsafeTimestampValue(t1 + d2);
          })));

  CEL_RETURN_IF_ERROR(registry.Register(
      BinaryFunctionAdapter<Value, absl::Duration, absl::Time>::
          CreateDescriptor(builtin::kAdd,
                           StandardOverloadIds::kAddDurationTimestamp, false),
      BinaryFunctionAdapter<Value, absl::Duration, absl::Time>::WrapFunction(
          [](absl::Duration d2, absl::Time t1) -> Value {
            return UnsafeTimestampValue(t1 + d2);
          })));

  CEL_RETURN_IF_ERROR(registry.Register(
      BinaryFunctionAdapter<Value, absl::Duration, absl::Duration>::
          CreateDescriptor(builtin::kAdd,
                           StandardOverloadIds::kAddDurationDuration, false),
      BinaryFunctionAdapter<Value, absl::Duration, absl::Duration>::
          WrapFunction([](absl::Duration d1, absl::Duration d2) -> Value {
            return UnsafeDurationValue(d1 + d2);
          })));

  CEL_RETURN_IF_ERROR(registry.Register(
      BinaryFunctionAdapter<Value, absl::Time, absl::Duration>::
          CreateDescriptor(builtin::kSubtract,
                           StandardOverloadIds::kSubtractTimestampDuration,
                           false),

      BinaryFunctionAdapter<Value, absl::Time, absl::Duration>::WrapFunction(

          [](absl::Time t1, absl::Duration d2) -> Value {
            return UnsafeTimestampValue(t1 - d2);
          })));

  CEL_RETURN_IF_ERROR(registry.Register(
      BinaryFunctionAdapter<Value, absl::Time, absl::Time>::CreateDescriptor(
          builtin::kSubtract, StandardOverloadIds::kSubtractTimestampTimestamp,
          false),
      BinaryFunctionAdapter<Value, absl::Time, absl::Time>::WrapFunction(

          [](absl::Time t1, absl::Time t2) -> Value {
            return UnsafeDurationValue(t1 - t2);
          })));

  CEL_RETURN_IF_ERROR(registry.Register(
      BinaryFunctionAdapter<Value, absl::Duration, absl::Duration>::
          CreateDescriptor(builtin::kSubtract,
                           StandardOverloadIds::kSubtractDurationDuration,
                           false),
      BinaryFunctionAdapter<Value, absl::Duration, absl::Duration>::
          WrapFunction([](absl::Duration d1, absl::Duration d2) -> Value {
            return UnsafeDurationValue(d1 - d2);
          })));

  return absl::OkStatus();
}

absl::Status RegisterDurationFunctions(FunctionRegistry& registry) {
  // duration breakdown accessor functions
  using DurationAccessorFunction =
      UnaryFunctionAdapter<int64_t, absl::Duration>;
  CEL_RETURN_IF_ERROR(registry.Register(
      DurationAccessorFunction::CreateDescriptor(
          builtin::kHours, StandardOverloadIds::kDurationToHours, true),
      DurationAccessorFunction::WrapFunction(
          [](absl::Duration d) -> int64_t { return absl::ToInt64Hours(d); })));

  CEL_RETURN_IF_ERROR(registry.Register(
      DurationAccessorFunction::CreateDescriptor(
          builtin::kMinutes, StandardOverloadIds::kDurationToMinutes, true),
      DurationAccessorFunction::WrapFunction([](absl::Duration d) -> int64_t {
        return absl::ToInt64Minutes(d);
      })));

  CEL_RETURN_IF_ERROR(registry.Register(
      DurationAccessorFunction::CreateDescriptor(
          builtin::kSeconds, StandardOverloadIds::kDurationToSeconds, true),
      DurationAccessorFunction::WrapFunction([](absl::Duration d) -> int64_t {
        return absl::ToInt64Seconds(d);
      })));

  return registry.Register(
      DurationAccessorFunction::CreateDescriptor(
          builtin::kMilliseconds, StandardOverloadIds::kDurationToMilliseconds,
          true),
      DurationAccessorFunction::WrapFunction([](absl::Duration d) -> int64_t {
        constexpr int64_t millis_per_second = 1000L;
        return absl::ToInt64Milliseconds(d) % millis_per_second;
      }));
}

}  // namespace

absl::Status RegisterTimeFunctions(FunctionRegistry& registry,
                                   const RuntimeOptions& options) {
  CEL_RETURN_IF_ERROR(RegisterTimestampFunctions(registry, options));
  CEL_RETURN_IF_ERROR(RegisterDurationFunctions(registry));

  // Special arithmetic operators for Timestamp and Duration
  // TODO(uncreated-issue/37): deprecate unchecked time math functions when clients no
  // longer depend on them.
  if (options.enable_timestamp_duration_overflow_errors) {
    return RegisterCheckedTimeArithmeticFunctions(registry);
  }

  return RegisterUncheckedTimeArithmeticFunctions(registry);
}

}  // namespace cel
