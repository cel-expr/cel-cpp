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
#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "base/builtins.h"
#include "common/function_descriptor.h"
#include "common/kind.h"
#include "common/value.h"
#include "common/value_kind.h"
#include "internal/overflow.h"
#include "internal/status_macros.h"
#include "runtime/function.h"
#include "runtime/function_registry.h"
#include "runtime/runtime_options.h"

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

class TimeAccessorFunction final : public Function {
 public:
  enum class Field {
    kFullYear,
    kMonth,
    kDayOfYear,
    kDayOfMonth,
    kDate,
    kDayOfWeek,
    kHours,
    kMinutes,
    kSeconds,
    kMilliseconds,
  };

  explicit TimeAccessorFunction(Field field) : field_(field) {}

  absl::StatusOr<Value> Invoke(absl::Span<const Value> args,
                               const InvokeContext& context) const override {
    if (args.empty() || args.size() > 2) {
      return absl::InvalidArgumentError(
          "unexpected number of arguments for time accessor");
    }
    if (args[0].IsDuration()) {
      if (args.size() != 1) {
        return absl::InvalidArgumentError(
            "unexpected number of arguments for duration accessor");
      }
      absl::Duration d = args[0].GetDuration().ToDuration();
      switch (field_) {
        case Field::kHours:
          return IntValue(absl::ToInt64Hours(d));
        case Field::kMinutes:
          return IntValue(absl::ToInt64Minutes(d));
        case Field::kSeconds:
          return IntValue(absl::ToInt64Seconds(d));
        case Field::kMilliseconds: {
          constexpr int64_t millis_per_second = 1000L;
          return IntValue(absl::ToInt64Milliseconds(d) % millis_per_second);
        }
        default:
          return absl::InvalidArgumentError(
              "unsupported accessor field for duration");
      }
    }

    if (!args[0].IsTimestamp()) {
      return absl::InvalidArgumentError("expected timestamp or duration value");
    }
    std::string tz;
    if (args.size() == 2) {
      if (!args[1].IsString()) {
        return absl::InvalidArgumentError("expected string timezone");
      }
      tz = args[1].GetString().ToString();
    }

    absl::TimeZone::CivilInfo breakdown;
    absl::Status status =
        FindTimeBreakdown(args[0].GetTimestamp().ToTime(), tz, &breakdown);
    if (!status.ok()) {
      return ErrorValue::From(status, context.arena());
    }

    switch (field_) {
      case Field::kFullYear:
        return IntValue(breakdown.cs.year());
      case Field::kMonth:
        return IntValue(breakdown.cs.month() - 1);
      case Field::kDayOfYear:
        return IntValue(absl::GetYearDay(absl::CivilDay(breakdown.cs)) - 1);
      case Field::kDayOfMonth:
        return IntValue(breakdown.cs.day() - 1);
      case Field::kDate:
        return IntValue(breakdown.cs.day());
      case Field::kDayOfWeek: {
        absl::Weekday weekday = absl::GetWeekday(breakdown.cs);
        // get day of week from the date in UTC, zero-based, zero for Sunday,
        // based on GetDayOfWeek CEL function definition.
        int weekday_num = static_cast<int>(weekday);
        weekday_num = (weekday_num == 6) ? 0 : weekday_num + 1;
        return IntValue(weekday_num);
      }
      case Field::kHours:
        return IntValue(breakdown.cs.hour());
      case Field::kMinutes:
        return IntValue(breakdown.cs.minute());
      case Field::kSeconds:
        return IntValue(breakdown.cs.second());
      case Field::kMilliseconds:
        return IntValue(absl::ToInt64Milliseconds(breakdown.subsecond));
    }
  }

 private:
  Field field_;
};

class TimeArithmeticFunction final : public Function {
 public:
  enum class Op { kAdd, kSubtract };

  TimeArithmeticFunction(Op op, bool checked) : op_(op), checked_(checked) {}

  absl::StatusOr<Value> Invoke(absl::Span<const Value> args,
                               const InvokeContext& context) const override {
    if (args.size() != 2) {
      return absl::InvalidArgumentError(
          "unexpected number of arguments for binary function");
    }
    ValueKind lhs_kind = args[0].kind();
    ValueKind rhs_kind = args[1].kind();
    if (op_ == Op::kAdd) {
      if (lhs_kind == ValueKind::kTimestamp &&
          rhs_kind == ValueKind::kDuration) {
        absl::Time t1 = args[0].GetTimestamp().ToTime();
        absl::Duration d2 = args[1].GetDuration().ToDuration();
        if (!checked_) {
          return UnsafeTimestampValue(t1 + d2);
        }
        auto sum = cel::internal::CheckedAdd(t1, d2);
        if (!sum.ok()) {
          return ErrorValue::From(sum.status(), context.arena());
        }
        return TimestampValue(*sum);
      }
      if (lhs_kind == ValueKind::kDuration &&
          rhs_kind == ValueKind::kTimestamp) {
        absl::Duration d2 = args[0].GetDuration().ToDuration();
        absl::Time t1 = args[1].GetTimestamp().ToTime();
        if (!checked_) {
          return UnsafeTimestampValue(t1 + d2);
        }
        auto sum = cel::internal::CheckedAdd(t1, d2);
        if (!sum.ok()) {
          return ErrorValue::From(sum.status(), context.arena());
        }
        return TimestampValue(*sum);
      }
      if (lhs_kind == ValueKind::kDuration &&
          rhs_kind == ValueKind::kDuration) {
        absl::Duration d1 = args[0].GetDuration().ToDuration();
        absl::Duration d2 = args[1].GetDuration().ToDuration();
        if (!checked_) {
          return UnsafeDurationValue(d1 + d2);
        }
        auto sum = cel::internal::CheckedAdd(d1, d2);
        if (!sum.ok()) {
          return ErrorValue::From(sum.status(), context.arena());
        }
        return DurationValue(*sum);
      }
      return absl::InvalidArgumentError(
          "expected timestamp/duration arguments for add");
    }

    // Op::kSubtract
    if (lhs_kind == ValueKind::kTimestamp && rhs_kind == ValueKind::kDuration) {
      absl::Time t1 = args[0].GetTimestamp().ToTime();
      absl::Duration d2 = args[1].GetDuration().ToDuration();
      if (!checked_) {
        return UnsafeTimestampValue(t1 - d2);
      }
      auto diff = cel::internal::CheckedSub(t1, d2);
      if (!diff.ok()) {
        return ErrorValue::From(diff.status(), context.arena());
      }
      return TimestampValue(*diff);
    }
    if (lhs_kind == ValueKind::kTimestamp &&
        rhs_kind == ValueKind::kTimestamp) {
      absl::Time t1 = args[0].GetTimestamp().ToTime();
      absl::Time t2 = args[1].GetTimestamp().ToTime();
      if (!checked_) {
        return UnsafeDurationValue(t1 - t2);
      }
      auto diff = cel::internal::CheckedSub(t1, t2);
      if (!diff.ok()) {
        return ErrorValue::From(diff.status(), context.arena());
      }
      return DurationValue(*diff);
    }
    if (lhs_kind == ValueKind::kDuration && rhs_kind == ValueKind::kDuration) {
      absl::Duration d1 = args[0].GetDuration().ToDuration();
      absl::Duration d2 = args[1].GetDuration().ToDuration();
      if (!checked_) {
        return UnsafeDurationValue(d1 - d2);
      }
      auto diff = cel::internal::CheckedSub(d1, d2);
      if (!diff.ok()) {
        return ErrorValue::From(diff.status(), context.arena());
      }
      return DurationValue(*diff);
    }
    return absl::InvalidArgumentError(
        "expected timestamp/duration arguments for subtract");
  }

 private:
  Op op_;
  bool checked_;
};

absl::Status RegisterTimestampFunctions(FunctionRegistry& registry,
                                        const RuntimeOptions& options) {
  static constexpr struct {
    absl::string_view name;
    TimeAccessorFunction::Field field;
  } kTimestampAccessors[] = {
      {builtin::kFullYear, TimeAccessorFunction::Field::kFullYear},
      {builtin::kMonth, TimeAccessorFunction::Field::kMonth},
      {builtin::kDayOfYear, TimeAccessorFunction::Field::kDayOfYear},
      {builtin::kDayOfMonth, TimeAccessorFunction::Field::kDayOfMonth},
      {builtin::kDate, TimeAccessorFunction::Field::kDate},
      {builtin::kDayOfWeek, TimeAccessorFunction::Field::kDayOfWeek},
      {builtin::kHours, TimeAccessorFunction::Field::kHours},
      {builtin::kMinutes, TimeAccessorFunction::Field::kMinutes},
      {builtin::kSeconds, TimeAccessorFunction::Field::kSeconds},
      {builtin::kMilliseconds, TimeAccessorFunction::Field::kMilliseconds},
  };
  for (const auto& entry : kTimestampAccessors) {
    CEL_RETURN_IF_ERROR(registry.Register(
        FunctionDescriptor(entry.name, /*is_receiver_style=*/true,
                           {Kind::kTimestamp, Kind::kString}),
        std::make_unique<TimeAccessorFunction>(entry.field)));
    CEL_RETURN_IF_ERROR(registry.Register(
        FunctionDescriptor(entry.name, /*is_receiver_style=*/true,
                           {Kind::kTimestamp}),
        std::make_unique<TimeAccessorFunction>(entry.field)));
  }
  return absl::OkStatus();
}

absl::Status RegisterTimeArithmeticFunctions(FunctionRegistry& registry,
                                             bool checked) {
  static constexpr struct {
    absl::string_view name;
    TimeArithmeticFunction::Op op;
    Kind lhs;
    Kind rhs;
  } kOverloads[] = {
      {builtin::kAdd, TimeArithmeticFunction::Op::kAdd, Kind::kTimestamp,
       Kind::kDuration},
      {builtin::kAdd, TimeArithmeticFunction::Op::kAdd, Kind::kDuration,
       Kind::kTimestamp},
      {builtin::kAdd, TimeArithmeticFunction::Op::kAdd, Kind::kDuration,
       Kind::kDuration},
      {builtin::kSubtract, TimeArithmeticFunction::Op::kSubtract,
       Kind::kTimestamp, Kind::kDuration},
      {builtin::kSubtract, TimeArithmeticFunction::Op::kSubtract,
       Kind::kTimestamp, Kind::kTimestamp},
      {builtin::kSubtract, TimeArithmeticFunction::Op::kSubtract,
       Kind::kDuration, Kind::kDuration},
  };
  for (const auto& entry : kOverloads) {
    CEL_RETURN_IF_ERROR(registry.Register(
        FunctionDescriptor(entry.name, /*is_receiver_style=*/false,
                           {entry.lhs, entry.rhs}),
        std::make_unique<TimeArithmeticFunction>(entry.op, checked)));
  }
  return absl::OkStatus();
}

absl::Status RegisterDurationFunctions(FunctionRegistry& registry) {
  static constexpr struct {
    absl::string_view name;
    TimeAccessorFunction::Field field;
  } kDurationAccessors[] = {
      {builtin::kHours, TimeAccessorFunction::Field::kHours},
      {builtin::kMinutes, TimeAccessorFunction::Field::kMinutes},
      {builtin::kSeconds, TimeAccessorFunction::Field::kSeconds},
      {builtin::kMilliseconds, TimeAccessorFunction::Field::kMilliseconds},
  };
  for (const auto& entry : kDurationAccessors) {
    CEL_RETURN_IF_ERROR(registry.Register(
        FunctionDescriptor(entry.name, /*is_receiver_style=*/true,
                           {Kind::kDuration}),
        std::make_unique<TimeAccessorFunction>(entry.field)));
  }
  return absl::OkStatus();
}

}  // namespace

absl::Status RegisterTimeFunctions(FunctionRegistry& registry,
                                   const RuntimeOptions& options) {
  CEL_RETURN_IF_ERROR(RegisterTimestampFunctions(registry, options));
  CEL_RETURN_IF_ERROR(RegisterDurationFunctions(registry));

  // Special arithmetic operators for Timestamp and Duration
  // TODO(uncreated-issue/37): deprecate unchecked time math functions when clients no
  // longer depend on them.
  return RegisterTimeArithmeticFunctions(
      registry, options.enable_timestamp_duration_overflow_errors);
}

}  // namespace cel
