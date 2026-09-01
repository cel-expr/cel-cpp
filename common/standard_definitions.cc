// Copyright 2025 Google LLC
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

#include "common/standard_definitions.h"

#include <algorithm>
#include <array>

#include "absl/base/call_once.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace cel {

namespace {

static absl::once_flag all_overloads_sorted_flag;

}  // namespace

absl::Span<const absl::string_view> StandardOverloadIds::GetAllIds() {
  static std::array<absl::string_view, 164> kAll = {
      // Add operator _+_
      kAddInt,
      kAddUint,
      kAddDouble,
      kAddDurationDuration,
      kAddDurationTimestamp,
      kAddTimestampDuration,
      kAddString,
      kAddBytes,
      kAddList,
      // Subtract operator _-_
      kSubtractInt,
      kSubtractUint,
      kSubtractDouble,
      kSubtractDurationDuration,
      kSubtractTimestampDuration,
      kSubtractTimestampTimestamp,
      // Multiply operator _*_
      kMultiplyInt,
      kMultiplyUint,
      kMultiplyDouble,
      // Division operator _/_
      kDivideInt,
      kDivideUint,
      kDivideDouble,
      // Modulo operator _%_
      kModuloInt,
      kModuloUint,
      // Negation operator -_
      kNegateInt,
      kNegateDouble,
      // Logical operators
      kNot,
      kAnd,
      kOr,
      kConditional,
      // Comprehension logic
      kNotStrictlyFalse,
      kNotStrictlyFalseDeprecated,
      // Equality operators
      kEquals,
      kNotEquals,
      // Relational operators
      kLessBool,
      kLessString,
      kLessBytes,
      kLessDuration,
      kLessTimestamp,
      kLessInt,
      kLessIntUint,
      kLessIntDouble,
      kLessDouble,
      kLessDoubleInt,
      kLessDoubleUint,
      kLessUint,
      kLessUintInt,
      kLessUintDouble,
      kGreaterBool,
      kGreaterString,
      kGreaterBytes,
      kGreaterDuration,
      kGreaterTimestamp,
      kGreaterInt,
      kGreaterIntUint,
      kGreaterIntDouble,
      kGreaterDouble,
      kGreaterDoubleInt,
      kGreaterDoubleUint,
      kGreaterUint,
      kGreaterUintInt,
      kGreaterUintDouble,
      kGreaterEqualsBool,
      kGreaterEqualsString,
      kGreaterEqualsBytes,
      kGreaterEqualsDuration,
      kGreaterEqualsTimestamp,
      kGreaterEqualsInt,
      kGreaterEqualsIntUint,
      kGreaterEqualsIntDouble,
      kGreaterEqualsDouble,
      kGreaterEqualsDoubleInt,
      kGreaterEqualsDoubleUint,
      kGreaterEqualsUint,
      kGreaterEqualsUintInt,
      kGreaterEqualsUintDouble,
      kLessEqualsBool,
      kLessEqualsString,
      kLessEqualsBytes,
      kLessEqualsDuration,
      kLessEqualsTimestamp,
      kLessEqualsInt,
      kLessEqualsIntUint,
      kLessEqualsIntDouble,
      kLessEqualsDouble,
      kLessEqualsDoubleInt,
      kLessEqualsDoubleUint,
      kLessEqualsUint,
      kLessEqualsUintInt,
      kLessEqualsUintDouble,
      // Container operators
      kIndexList,
      kIndexMap,
      kInList,
      kInMap,
      kSizeBytes,
      kSizeList,
      kSizeMap,
      kSizeString,
      kSizeBytesMember,
      kSizeListMember,
      kSizeMapMember,
      kSizeStringMember,
      // String functions
      kContainsString,
      kEndsWithString,
      kStartsWithString,
      // String RE2 functions
      kMatches,
      kMatchesMember,
      // Timestamp / duration accessors
      kTimestampToYear,
      kTimestampToYearWithTz,
      kTimestampToMonth,
      kTimestampToMonthWithTz,
      kTimestampToDayOfYear,
      kTimestampToDayOfYearWithTz,
      kTimestampToDayOfMonth,
      kTimestampToDayOfMonthWithTz,
      kTimestampToDayOfWeek,
      kTimestampToDayOfWeekWithTz,
      kTimestampToDate,
      kTimestampToDateWithTz,
      kTimestampToHours,
      kTimestampToHoursWithTz,
      kDurationToHours,
      kTimestampToMinutes,
      kTimestampToMinutesWithTz,
      kDurationToMinutes,
      kTimestampToSeconds,
      kTimestampToSecondsWithTz,
      kDurationToSeconds,
      kTimestampToMilliseconds,
      kTimestampToMillisecondsWithTz,
      kDurationToMilliseconds,
      // Type conversions
      kToDyn,
      // to_uint
      kUintToUint,
      kDoubleToUint,
      kIntToUint,
      kStringToUint,
      // to_int
      kUintToInt,
      kDoubleToInt,
      kIntToInt,
      kStringToInt,
      kTimestampToInt,
      kDurationToInt,
      // to_double
      kDoubleToDouble,
      kUintToDouble,
      kIntToDouble,
      kStringToDouble,
      // to_bool
      kBoolToBool,
      kStringToBool,
      // to_bytes
      kBytesToBytes,
      kStringToBytes,
      // to_string
      kStringToString,
      kBytesToString,
      kBoolToString,
      kDoubleToString,
      kIntToString,
      kUintToString,
      kDurationToString,
      kTimestampToString,
      // to_timestamp
      kTimestampToTimestamp,
      kIntToTimestamp,
      kStringToTimestamp,
      // to_duration
      kDurationToDuration,
      kIntToDuration,
      kStringToDuration,
      // to_type
      kToType,
  };

  absl::call_once(all_overloads_sorted_flag,
                  [] { std::sort(kAll.begin(), kAll.end()); });

  return kAll;
}

bool StandardOverloadIds::HasId(absl::string_view id) {
  absl::Span<const absl::string_view> all_ids = GetAllIds();
  return std::binary_search(all_ids.begin(), all_ids.end(), id);
}

}  // namespace cel
