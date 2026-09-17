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

#ifndef THIRD_PARTY_CEL_CPP_COMMON_VALUES_VALUE_VARIANT_H_
#define THIRD_PARTY_CEL_CPP_COMMON_VALUES_VALUE_VARIANT_H_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/log/absl_check.h"
#include "absl/meta/type_traits.h"
#include "common/value_kind.h"
#include "common/values/bool_value.h"
#include "common/values/bytes_value.h"
#include "common/values/custom_list_value.h"
#include "common/values/custom_map_value.h"
#include "common/values/custom_struct_value.h"
#include "common/values/double_value.h"
#include "common/values/duration_value.h"
#include "common/values/error_value.h"
#include "common/values/int_value.h"
#include "common/values/legacy_list_value.h"
#include "common/values/legacy_map_value.h"
#include "common/values/legacy_struct_value.h"
#include "common/values/list_value.h"
#include "common/values/map_value.h"
#include "common/values/null_value.h"
#include "common/values/opaque_value.h"
#include "common/values/parsed_json_list_value.h"
#include "common/values/parsed_json_map_value.h"
#include "common/values/parsed_map_field_value.h"
#include "common/values/parsed_message_value.h"
#include "common/values/parsed_repeated_field_value.h"
#include "common/values/string_value.h"
#include "common/values/timestamp_value.h"
#include "common/values/type_value.h"
#include "common/values/uint_value.h"
#include "common/values/unknown_value.h"
#include "common/values/values.h"

namespace cel {

class Value;

namespace common_internal {

// Used by ValueVariant to indicate the active alternative.
enum class ValueIndex : uint8_t {
  kNull = 0,
  kBool,
  kInt,
  kUint,
  kDouble,
  kDuration,
  kTimestamp,
  kType,
  kLegacyList,
  kParsedJsonList,
  kParsedRepeatedField,
  kCustomList,
  kLegacyMap,
  kParsedJsonMap,
  kParsedMapField,
  kCustomMap,
  kLegacyStruct,
  kParsedMessage,
  kCustomStruct,
  kOpaque,
  kBytes,
  kString,
  kError,
  kUnknown,
};

// Traits specialized by each alternative.
//
// ValueIndex ValueAlternative<T>::kIndex
//
//   Indicates the alternative index corresponding to T.
//
// ValueKind ValueAlternative<T>::kKind
//
//  Indicatates the kind corresponding to T.
//
// bool ValueAlternative<T>::kAlwaysTrivial
//
//  True if T is trivially_copyable, false otherwise.
//
// ValueFlags ValueAlternative<T>::Flags(const T* absl_nonnull )
//
//  Returns the flags for the corresponding instance of T.
template <typename T>
struct ValueAlternative;

template <>
struct ValueAlternative<NullValue> {
  static constexpr ValueIndex kIndex = ValueIndex::kNull;
  static constexpr ValueKind kKind = NullValue::kKind;
};

template <>
struct ValueAlternative<BoolValue> {
  static constexpr ValueIndex kIndex = ValueIndex::kBool;
  static constexpr ValueKind kKind = BoolValue::kKind;
};

template <>
struct ValueAlternative<IntValue> {
  static constexpr ValueIndex kIndex = ValueIndex::kInt;
  static constexpr ValueKind kKind = IntValue::kKind;
};

template <>
struct ValueAlternative<UintValue> {
  static constexpr ValueIndex kIndex = ValueIndex::kUint;
  static constexpr ValueKind kKind = UintValue::kKind;
};

template <>
struct ValueAlternative<DoubleValue> {
  static constexpr ValueIndex kIndex = ValueIndex::kDouble;
  static constexpr ValueKind kKind = DoubleValue::kKind;
};

template <>
struct ValueAlternative<DurationValue> {
  static constexpr ValueIndex kIndex = ValueIndex::kDuration;
  static constexpr ValueKind kKind = DurationValue::kKind;
};

template <>
struct ValueAlternative<TimestampValue> {
  static constexpr ValueIndex kIndex = ValueIndex::kTimestamp;
  static constexpr ValueKind kKind = TimestampValue::kKind;
};

template <>
struct ValueAlternative<TypeValue> {
  static constexpr ValueIndex kIndex = ValueIndex::kType;
  static constexpr ValueKind kKind = TypeValue::kKind;
};

template <>
struct ValueAlternative<LegacyListValue> {
  static constexpr ValueIndex kIndex = ValueIndex::kLegacyList;
  static constexpr ValueKind kKind = LegacyListValue::kKind;
};

template <>
struct ValueAlternative<ParsedJsonListValue> {
  static constexpr ValueIndex kIndex = ValueIndex::kParsedJsonList;
  static constexpr ValueKind kKind = ParsedJsonListValue::kKind;
};

template <>
struct ValueAlternative<ParsedRepeatedFieldValue> {
  static constexpr ValueIndex kIndex = ValueIndex::kParsedRepeatedField;
  static constexpr ValueKind kKind = ParsedRepeatedFieldValue::kKind;
};

template <>
struct ValueAlternative<CustomListValue> {
  static constexpr ValueIndex kIndex = ValueIndex::kCustomList;
  static constexpr ValueKind kKind = CustomListValue::kKind;
};

template <>
struct ValueAlternative<LegacyMapValue> {
  static constexpr ValueIndex kIndex = ValueIndex::kLegacyMap;
  static constexpr ValueKind kKind = LegacyMapValue::kKind;
};

template <>
struct ValueAlternative<ParsedJsonMapValue> {
  static constexpr ValueIndex kIndex = ValueIndex::kParsedJsonMap;
  static constexpr ValueKind kKind = ParsedJsonMapValue::kKind;
};

template <>
struct ValueAlternative<ParsedMapFieldValue> {
  static constexpr ValueIndex kIndex = ValueIndex::kParsedMapField;
  static constexpr ValueKind kKind = ParsedMapFieldValue::kKind;
};

template <>
struct ValueAlternative<CustomMapValue> {
  static constexpr ValueIndex kIndex = ValueIndex::kCustomMap;
  static constexpr ValueKind kKind = CustomMapValue::kKind;
  static constexpr bool kAlwaysTrivial = true;
};

template <>
struct ValueAlternative<LegacyStructValue> {
  static constexpr ValueIndex kIndex = ValueIndex::kLegacyStruct;
  static constexpr ValueKind kKind = LegacyStructValue::kKind;
};

template <>
struct ValueAlternative<ParsedMessageValue> {
  static constexpr ValueIndex kIndex = ValueIndex::kParsedMessage;
  static constexpr ValueKind kKind = ParsedMessageValue::kKind;
};

template <>
struct ValueAlternative<CustomStructValue> {
  static constexpr ValueIndex kIndex = ValueIndex::kCustomStruct;
  static constexpr ValueKind kKind = CustomStructValue::kKind;
};

template <>
struct ValueAlternative<OpaqueValue> {
  static constexpr ValueIndex kIndex = ValueIndex::kOpaque;
  static constexpr ValueKind kKind = OpaqueValue::kKind;
};

template <>
struct ValueAlternative<BytesValue> {
  static constexpr ValueIndex kIndex = ValueIndex::kBytes;
  static constexpr ValueKind kKind = BytesValue::kKind;
};

template <>
struct ValueAlternative<StringValue> {
  static constexpr ValueIndex kIndex = ValueIndex::kString;
  static constexpr ValueKind kKind = StringValue::kKind;
};

template <>
struct ValueAlternative<ErrorValue> {
  static constexpr ValueIndex kIndex = ValueIndex::kError;
  static constexpr ValueKind kKind = ErrorValue::kKind;
};

template <>
struct ValueAlternative<UnknownValue> {
  static constexpr ValueIndex kIndex = ValueIndex::kUnknown;
  static constexpr ValueKind kKind = UnknownValue::kKind;
};

template <typename T, typename = void>
struct IsValueAlternative : std::false_type {};

template <typename T>
struct IsValueAlternative<T, std::void_t<decltype(ValueAlternative<T>{})>>
    : std::true_type {};

template <typename T>
inline constexpr bool IsValueAlternativeV = IsValueAlternative<T>::value;

// Alignment and size of the storage inside ValueVariant, not for ValueVariant
// itself.
inline constexpr size_t kValueVariantAlign = 8;
inline constexpr size_t kValueVariantSize = 24;

// Hand-rolled variant used by cel::Value which avoids std::variant and its
// lack of support for an unchecked get.
class alignas(kValueVariantAlign) ValueVariant {
 public:
  ValueVariant() = default;
  ValueVariant(const ValueVariant&) = default;
  ValueVariant& operator=(const ValueVariant&) = default;

  template <typename T, typename... Args>
  explicit ValueVariant(std::in_place_type_t<T>, Args&&... args)
      : index_(ValueAlternative<T>::kIndex), kind_(ValueAlternative<T>::kKind) {
    static_assert(alignof(T) <= kValueVariantAlign);
    static_assert(sizeof(T) <= kValueVariantSize);

    ::new (static_cast<void*>(&raw_[0])) T(std::forward<Args>(args)...);
  }

  template <typename T, typename = std::enable_if_t<
                            IsValueAlternativeV<absl::remove_cvref_t<T>>>>
  explicit ValueVariant(T&& value)
      : ValueVariant(std::in_place_type<absl::remove_cvref_t<T>>,
                     std::forward<T>(value)) {}

  [[nodiscard]]
  ValueKind kind() const {
    return kind_;
  }

  template <typename T>
  void Assign(T&& value) {
    using U = absl::remove_cvref_t<T>;

    static_assert(alignof(U) <= kValueVariantAlign);
    static_assert(sizeof(U) <= kValueVariantSize);

    index_ = ValueAlternative<U>::kIndex;
    kind_ = ValueAlternative<U>::kKind;
    ::new (static_cast<void*>(&raw_[0])) U(std::forward<T>(value));
  }

  template <typename T>
  [[nodiscard]]
  bool Is() const {
    return index_ == ValueAlternative<T>::kIndex;
  }

  template <typename T>
  [[nodiscard]]
  T& Get() ABSL_ATTRIBUTE_LIFETIME_BOUND {
    ABSL_DCHECK(Is<T>());

    return *At<T>();
  }

  template <typename T>
  [[nodiscard]]
  const T& Get() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    ABSL_DCHECK(Is<T>());

    return *At<T>();
  }

  template <typename T>
  [[nodiscard]]
  T* absl_nullable As() ABSL_ATTRIBUTE_LIFETIME_BOUND {
    if (Is<T>()) {
      return At<T>();
    }
    return nullptr;
  }

  template <typename T>
  [[nodiscard]]
  const T* absl_nullable As() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    if (Is<T>()) {
      return At<T>();
    }
    return nullptr;
  }

  template <typename Visitor>
  decltype(auto) Visit(Visitor&& visitor) const {
    switch (index_) {
      case ValueIndex::kNull:
        return std::forward<Visitor>(visitor)(*At<NullValue>());
      case ValueIndex::kBool:
        return std::forward<Visitor>(visitor)(*At<BoolValue>());
      case ValueIndex::kInt:
        return std::forward<Visitor>(visitor)(*At<IntValue>());
      case ValueIndex::kUint:
        return std::forward<Visitor>(visitor)(*At<UintValue>());
      case ValueIndex::kDouble:
        return std::forward<Visitor>(visitor)(*At<DoubleValue>());
      case ValueIndex::kDuration:
        return std::forward<Visitor>(visitor)(*At<DurationValue>());
      case ValueIndex::kTimestamp:
        return std::forward<Visitor>(visitor)(*At<TimestampValue>());
      case ValueIndex::kType:
        return std::forward<Visitor>(visitor)(*At<TypeValue>());
      case ValueIndex::kLegacyList:
        return std::forward<Visitor>(visitor)(*At<LegacyListValue>());
      case ValueIndex::kParsedJsonList:
        return std::forward<Visitor>(visitor)(*At<ParsedJsonListValue>());
      case ValueIndex::kParsedRepeatedField:
        return std::forward<Visitor>(visitor)(*At<ParsedRepeatedFieldValue>());
      case ValueIndex::kCustomList:
        return std::forward<Visitor>(visitor)(*At<CustomListValue>());
      case ValueIndex::kLegacyMap:
        return std::forward<Visitor>(visitor)(*At<LegacyMapValue>());
      case ValueIndex::kParsedJsonMap:
        return std::forward<Visitor>(visitor)(*At<ParsedJsonMapValue>());
      case ValueIndex::kParsedMapField:
        return std::forward<Visitor>(visitor)(*At<ParsedMapFieldValue>());
      case ValueIndex::kCustomMap:
        return std::forward<Visitor>(visitor)(*At<CustomMapValue>());
      case ValueIndex::kLegacyStruct:
        return std::forward<Visitor>(visitor)(*At<LegacyStructValue>());
      case ValueIndex::kParsedMessage:
        return std::forward<Visitor>(visitor)(*At<ParsedMessageValue>());
      case ValueIndex::kCustomStruct:
        return std::forward<Visitor>(visitor)(*At<CustomStructValue>());
      case ValueIndex::kOpaque:
        return std::forward<Visitor>(visitor)(*At<OpaqueValue>());
      case ValueIndex::kBytes:
        return std::forward<Visitor>(visitor)(*At<BytesValue>());
      case ValueIndex::kString:
        return std::forward<Visitor>(visitor)(*At<StringValue>());
      case ValueIndex::kError:
        return std::forward<Visitor>(visitor)(*At<ErrorValue>());
      case ValueIndex::kUnknown:
        return std::forward<Visitor>(visitor)(*At<UnknownValue>());
    }
  }

  template <typename Visitor>
  decltype(auto) Visit(Visitor&& visitor) {
    switch (index_) {
      case ValueIndex::kNull:
        return std::forward<Visitor>(visitor)(*At<NullValue>());
      case ValueIndex::kBool:
        return std::forward<Visitor>(visitor)(*At<BoolValue>());
      case ValueIndex::kInt:
        return std::forward<Visitor>(visitor)(*At<IntValue>());
      case ValueIndex::kUint:
        return std::forward<Visitor>(visitor)(*At<UintValue>());
      case ValueIndex::kDouble:
        return std::forward<Visitor>(visitor)(*At<DoubleValue>());
      case ValueIndex::kDuration:
        return std::forward<Visitor>(visitor)(*At<DurationValue>());
      case ValueIndex::kTimestamp:
        return std::forward<Visitor>(visitor)(*At<TimestampValue>());
      case ValueIndex::kType:
        return std::forward<Visitor>(visitor)(*At<TypeValue>());
      case ValueIndex::kLegacyList:
        return std::forward<Visitor>(visitor)(*At<LegacyListValue>());
      case ValueIndex::kParsedJsonList:
        return std::forward<Visitor>(visitor)(*At<ParsedJsonListValue>());
      case ValueIndex::kParsedRepeatedField:
        return std::forward<Visitor>(visitor)(*At<ParsedRepeatedFieldValue>());
      case ValueIndex::kCustomList:
        return std::forward<Visitor>(visitor)(*At<CustomListValue>());
      case ValueIndex::kLegacyMap:
        return std::forward<Visitor>(visitor)(*At<LegacyMapValue>());
      case ValueIndex::kParsedJsonMap:
        return std::forward<Visitor>(visitor)(*At<ParsedJsonMapValue>());
      case ValueIndex::kParsedMapField:
        return std::forward<Visitor>(visitor)(*At<ParsedMapFieldValue>());
      case ValueIndex::kCustomMap:
        return std::forward<Visitor>(visitor)(*At<CustomMapValue>());
      case ValueIndex::kLegacyStruct:
        return std::forward<Visitor>(visitor)(*At<LegacyStructValue>());
      case ValueIndex::kParsedMessage:
        return std::forward<Visitor>(visitor)(*At<ParsedMessageValue>());
      case ValueIndex::kCustomStruct:
        return std::forward<Visitor>(visitor)(*At<CustomStructValue>());
      case ValueIndex::kOpaque:
        return std::forward<Visitor>(visitor)(*At<OpaqueValue>());
      case ValueIndex::kBytes:
        return std::forward<Visitor>(visitor)(*At<BytesValue>());
      case ValueIndex::kString:
        return std::forward<Visitor>(visitor)(*At<StringValue>());
      case ValueIndex::kError:
        return std::forward<Visitor>(visitor)(*At<ErrorValue>());
      case ValueIndex::kUnknown:
        return std::forward<Visitor>(visitor)(*At<UnknownValue>());
    }
  }

 private:
  template <typename T>
  [[nodiscard]]
  ABSL_ATTRIBUTE_ALWAYS_INLINE T* absl_nonnull At()
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    static_assert(IsValueAlternativeV<T>);
    static_assert(alignof(T) <= kValueVariantAlign);
    static_assert(sizeof(T) <= kValueVariantSize);

    return std::launder(reinterpret_cast<T*>(&raw_[0]));
  }

  template <typename T>
  [[nodiscard]]
  ABSL_ATTRIBUTE_ALWAYS_INLINE const T* absl_nonnull At() const
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    static_assert(IsValueAlternativeV<T>);
    static_assert(alignof(T) <= kValueVariantAlign);
    static_assert(sizeof(T) <= kValueVariantSize);

    return std::launder(reinterpret_cast<const T*>(&raw_[0]));
  }

  ValueIndex index_ = ValueIndex::kNull;
  ValueKind kind_ = ValueKind::kNull;
  alignas(kValueVariantAlign) std::byte raw_[kValueVariantSize];
};

}  // namespace common_internal

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMMON_VALUES_VALUE_VARIANT_H_
