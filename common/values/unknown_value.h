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

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "base/attribute_set.h"
#include "common/internal/unknowns.h"
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
UnknownValue MakeUnknownValue(const Unknown& value,
                              google::protobuf::Arena* absl_nonnull arena);
[[nodiscard]]
UnknownValue MakeUnknownValue(
    const common_internal::UnknownRoot* root,
    const common_internal::UnknownAttributeSet* attributes,
    const common_internal::UnknownFunctionSet* functions);
[[nodiscard]]
Unknown ToUnknown(const UnknownValue& value);
using UnknownValueRep = UnknownSet;
[[nodiscard]]
UnknownValue MakeUnknownValue(const UnknownValueRep& rep);
[[nodiscard]]
const UnknownValueRep& GetUnknownValueRep(
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
  AttributeSet ToAttributeSet() const;

  [[nodiscard]]
  FunctionResultSet ToFunctionResultSet() const;

 private:
  friend UnknownValue common_internal::MakeUnknownValue(
      const common_internal::UnknownRoot* root,
      const common_internal::UnknownAttributeSet* attributes,
      const common_internal::UnknownFunctionSet* functions);
  friend Unknown common_internal::ToUnknown(const UnknownValue& value);
  friend const common_internal::UnknownValueRep&
  common_internal::GetUnknownValueRep(const UnknownValue& value);
  friend UnknownValue common_internal::MakeUnknownValue(
      const common_internal::UnknownValueRep& rep);
  friend class common_internal::ValueMixin<UnknownValue>;

  using Rep = common_internal::UnknownValueRep;

  UnknownValue(const common_internal::UnknownRoot* root,
               const common_internal::UnknownAttributeSet* attributes,
               const common_internal::UnknownFunctionSet* functions)
      : rep_{root, attributes, functions} {}

  explicit UnknownValue(const Rep& rep) : rep_(rep) {}

  Rep rep_;
};

inline std::ostream& operator<<(std::ostream& out, const UnknownValue& value) {
  return out << value.DebugString();
}

namespace common_internal {

[[nodiscard]]
inline UnknownValue MakeUnknownValue(
    const common_internal::UnknownRoot* root,
    const common_internal::UnknownAttributeSet* attributes,
    const common_internal::UnknownFunctionSet* functions) {
  return UnknownValue(root, attributes, functions);
}

[[nodiscard]]
inline Unknown ToUnknown(const UnknownValue& value) {
  return Unknown(value.ToAttributeSet(), value.ToFunctionResultSet());
}

[[nodiscard]]
inline UnknownValue MakeUnknownValue(const UnknownValueRep& rep) {
  return UnknownValue(rep);
}

[[nodiscard]]
inline const UnknownValueRep& GetUnknownValueRep(
    const UnknownValue& value ABSL_ATTRIBUTE_LIFETIME_BOUND) {
  return value.rep_;
}

}  // namespace common_internal

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMMON_VALUES_UNKNOWN_VALUE_H_
