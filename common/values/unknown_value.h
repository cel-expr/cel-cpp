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

// IWYU pragma: private, include "common/value.h"
// IWYU pragma: friend "common/value.h"

#ifndef THIRD_PARTY_CEL_CPP_COMMON_VALUES_UNKNOWN_VALUE_H_
#define THIRD_PARTY_CEL_CPP_COMMON_VALUES_UNKNOWN_VALUE_H_

#include <ostream>
#include <string>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "common/type.h"
#include "common/unknown.h"
#include "common/value_kind.h"
#include "common/values/values.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/io/zero_copy_stream.h"
#include "google/protobuf/message.h"

namespace cel {

class Value;
class UnknownValue;

namespace common_internal {
[[nodiscard]]
UnknownValue MakeUnknownValue(Unknown value);
[[nodiscard]]
Unknown GetUnknown(const UnknownValue& value);
[[nodiscard]]
const FunctionResultSet& GetUnknownFunctionResultSet(
    const UnknownValue& value ABSL_ATTRIBUTE_LIFETIME_BOUND);
}  // namespace common_internal

// `UnknownValue` represents values of the primitive `duration` type.
class UnknownValue final : private common_internal::ValueMixin<UnknownValue> {
 public:
  static constexpr ValueKind kKind = ValueKind::kUnknown;

  UnknownValue() = default;
  UnknownValue(const UnknownValue&) = default;
  UnknownValue(UnknownValue&&) = default;
  UnknownValue& operator=(const UnknownValue&) = default;
  UnknownValue& operator=(UnknownValue&&) = default;

  constexpr ValueKind kind() const { return kKind; }

  absl::string_view GetTypeName() const { return UnknownType::kName; }

  std::string DebugString() const { return ""; }

  // See Value::SerializeTo().
  absl::Status SerializeTo(
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::io::ZeroCopyOutputStream* absl_nonnull output) const;

  // See Value::ConvertToJson().
  absl::Status ConvertToJson(
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Message* absl_nonnull json) const;

  absl::Status Equal(const Value& other,
                     const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
                     google::protobuf::MessageFactory* absl_nonnull message_factory,
                     google::protobuf::Arena* absl_nonnull arena,
                     Value* absl_nonnull result) const;
  using ValueMixin::Equal;

  bool IsZeroValue() const { return false; }

  [[nodiscard]]
  AttributeSet ToAttributeSet() const {
    return unknown_.unknown_attributes();
  }

  void swap(UnknownValue& other) noexcept {
    using std::swap;
    swap(unknown_, other.unknown_);
  }

 private:
  friend UnknownValue common_internal::MakeUnknownValue(Unknown value);
  friend Unknown common_internal::GetUnknown(const UnknownValue&);
  friend const FunctionResultSet& common_internal::GetUnknownFunctionResultSet(
      const UnknownValue& value);
  friend class common_internal::ValueMixin<UnknownValue>;

  explicit UnknownValue(Unknown unknown) : unknown_(std::move(unknown)) {}

  Unknown unknown_;
};

inline void swap(UnknownValue& lhs, UnknownValue& rhs) noexcept {
  lhs.swap(rhs);
}

inline std::ostream& operator<<(std::ostream& out, const UnknownValue& value) {
  return out << value.DebugString();
}

namespace common_internal {

[[nodiscard]]
inline UnknownValue MakeUnknownValue(Unknown value) {
  return UnknownValue(std::move(value));
}

[[nodiscard]]
inline Unknown GetUnknown(const UnknownValue& value) {
  return value.unknown_;
}

[[nodiscard]]
inline const FunctionResultSet& GetUnknownFunctionResultSet(
    const UnknownValue& value ABSL_ATTRIBUTE_LIFETIME_BOUND) {
  return value.unknown_.unknown_function_results();
}

}  // namespace common_internal

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMMON_VALUES_UNKNOWN_VALUE_H_
