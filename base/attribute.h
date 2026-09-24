// Copyright 2022 Google LLC
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

#ifndef THIRD_PARTY_CEL_CPP_BASE_ATTRIBUTE_H_
#define THIRD_PARTY_CEL_CPP_BASE_ATTRIBUTE_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/functional/overload.h"
#include "absl/log/absl_check.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/optional_ref.h"
#include "absl/types/span.h"
#include "absl/types/variant.h"
#include "base/kind.h"

namespace cel {

namespace common_internal {
class AttributeMatcherNode;
struct WildcardType {};
using AttributeQualifierVariant =
    absl::variant<absl::monostate, bool, int64_t, uint64_t, std::string>;
using AttributeQualifierPatternVariant =
    absl::variant<absl::monostate, bool, int64_t, uint64_t, std::string,
                  WildcardType>;
using AttributeQualifierViewVariant =
    absl::variant<absl::monostate, bool, int64_t, uint64_t, absl::string_view>;
}  // namespace common_internal

class AttributeQualifier;
class AttributeQualifierPattern;
class Attribute;
class AttributePattern;
class AttributeQualifierView;

namespace common_internal {
[[nodiscard]]
const AttributeQualifierVariant& AsVariant(
    const AttributeQualifier& qualifier ABSL_ATTRIBUTE_LIFETIME_BOUND);
[[nodiscard]]
AttributeQualifierVariant&& AsVariant(
    AttributeQualifier&& qualifier ABSL_ATTRIBUTE_LIFETIME_BOUND);
[[nodiscard]]
const AttributeQualifierPatternVariant& AsVariant(
    const AttributeQualifierPattern& qualifier ABSL_ATTRIBUTE_LIFETIME_BOUND);
[[nodiscard]]
AttributeQualifierPatternVariant&& AsVariant(
    AttributeQualifierPattern&& qualifier ABSL_ATTRIBUTE_LIFETIME_BOUND);
[[nodiscard]]
const AttributeQualifierViewVariant& AsVariant(
    const AttributeQualifierView& qualifier ABSL_ATTRIBUTE_LIFETIME_BOUND);
[[nodiscard]]
AttributeQualifierViewVariant&& AsVariant(
    AttributeQualifierView&& qualifier ABSL_ATTRIBUTE_LIFETIME_BOUND);
}  // namespace common_internal

// AttributeQualifier represents a segment in the
// attribute resolution path. A segment can be qualified by values of
// following types: string/int64_t/uint64_t/bool.
class AttributeQualifier {
 public:
  static AttributeQualifier OfInt(int64_t value) {
    return AttributeQualifier(absl::in_place_type<int64_t>, std::move(value));
  }

  static AttributeQualifier OfUint(uint64_t value) {
    return AttributeQualifier(absl::in_place_type<uint64_t>, std::move(value));
  }

  static AttributeQualifier OfString(const char* value) {
    return OfString(absl::string_view(value));
  }

  static AttributeQualifier OfString(std::string value) {
    return AttributeQualifier(absl::in_place_type<std::string>,
                              std::move(value));
  }

  static AttributeQualifier OfString(absl::string_view value) {
    return AttributeQualifier(absl::in_place_type<std::string>,
                              std::string(value));
  }

  static AttributeQualifier OfBool(bool value) {
    return AttributeQualifier(absl::in_place_type<bool>, std::move(value));
  }

  AttributeQualifier() = default;

  AttributeQualifier(const AttributeQualifier&) = default;
  AttributeQualifier(AttributeQualifier&&) = default;

  AttributeQualifier& operator=(const AttributeQualifier&) = default;
  AttributeQualifier& operator=(AttributeQualifier&&) = default;

  Kind kind() const;

  [[nodiscard]]
  std::string ToString() const;

  ABSL_DEPRECATE_AND_INLINE()
  absl::optional<int64_t> GetInt64Key() const { return AsInt(); }

  ABSL_DEPRECATE_AND_INLINE()
  absl::optional<uint64_t> GetUint64Key() const { return AsUint(); }

  ABSL_DEPRECATED("Use AsString")
  absl::optional<absl::string_view> GetStringKey() const {
    if (auto string = AsString(); string.has_value()) {
      return *string;
    }
    return absl::nullopt;
  }

  ABSL_DEPRECATE_AND_INLINE()
  absl::optional<bool> GetBoolKey() const { return AsBool(); }

  explicit operator bool() const {
    return !absl::holds_alternative<absl::monostate>(value_);
  }

  [[nodiscard]]
  bool IsBool() const {
    return absl::holds_alternative<bool>(value_);
  }

  [[nodiscard]]
  bool IsInt() const {
    return absl::holds_alternative<int64_t>(value_);
  }

  [[nodiscard]]
  bool IsUint() const {
    return absl::holds_alternative<uint64_t>(value_);
  }

  [[nodiscard]]
  bool IsString() const {
    return absl::holds_alternative<std::string>(value_);
  }

  [[nodiscard]]
  bool GetBool() const {
    ABSL_DCHECK(IsBool());
    return absl::get<bool>(value_);
  }

  [[nodiscard]]
  int64_t GetInt() const {
    ABSL_DCHECK(IsInt());
    return absl::get<int64_t>(value_);
  }

  [[nodiscard]]
  uint64_t GetUint() const {
    ABSL_DCHECK(IsUint());
    return absl::get<uint64_t>(value_);
  }

  [[nodiscard]]
  const std::string& GetString() const {
    ABSL_DCHECK(IsString());
    return absl::get<std::string>(value_);
  }

  [[nodiscard]]
  absl::optional<bool> AsBool() const {
    if (const auto* value = absl::get_if<bool>(&value_); value != nullptr) {
      return *value;
    }
    return absl::nullopt;
  }

  [[nodiscard]]
  absl::optional<int64_t> AsInt() const {
    if (const auto* value = absl::get_if<int64_t>(&value_); value != nullptr) {
      return *value;
    }
    return absl::nullopt;
  }

  [[nodiscard]]
  absl::optional<uint64_t> AsUint() const {
    if (const auto* value = absl::get_if<uint64_t>(&value_); value != nullptr) {
      return *value;
    }
    return absl::nullopt;
  }

  [[nodiscard]]
  absl::optional_ref<const std::string> AsString() const {
    if (const auto* value = absl::get_if<std::string>(&value_);
        value != nullptr) {
      return *value;
    }
    return absl::nullopt;
  }

  [[nodiscard]]
  bool IsMatch(const AttributeQualifier& other) const;

  [[nodiscard]]
  bool IsMatch(absl::string_view other_key) const;

 private:
  friend const common_internal::AttributeQualifierVariant&
  common_internal::AsVariant(const AttributeQualifier& qualifier);
  friend common_internal::AttributeQualifierVariant&&
  common_internal::AsVariant(AttributeQualifier&& qualifier);

  template <typename T, typename... Args>
  explicit AttributeQualifier(absl::in_place_type_t<T> in_place_type,
                              Args&&... args)
      : value_(in_place_type, std::forward<Args>(args)...) {}

  using Variant = common_internal::AttributeQualifierVariant;

  // The previous implementation of Attribute preserved all value
  // instances, regardless of whether they are supported in this context or not.
  // We represented unsupported types by using the first alternative and thus
  // preserve backwards compatibility with the result of `type()` above.
  Variant value_;
};

class AttributeQualifierView {
 public:
  static AttributeQualifierView OfInt(int64_t value) {
    return AttributeQualifierView(absl::in_place_type<int64_t>,
                                  std::move(value));
  }

  static AttributeQualifierView OfUint(uint64_t value) {
    return AttributeQualifierView(absl::in_place_type<uint64_t>,
                                  std::move(value));
  }

  static AttributeQualifierView OfString(const char* value) {
    return OfString(absl::string_view(value));
  }

  static AttributeQualifierView OfString(absl::string_view value) {
    return AttributeQualifierView(absl::in_place_type<absl::string_view>,
                                  std::move(value));
  }

  static AttributeQualifierView OfString(std::string&&) = delete;

  static AttributeQualifierView OfBool(bool value) {
    return AttributeQualifierView(absl::in_place_type<bool>, std::move(value));
  }

  AttributeQualifierView() = default;
  AttributeQualifierView(const AttributeQualifierView&) = default;
  AttributeQualifierView& operator=(const AttributeQualifierView&) = default;

  // NOLINTNEXTLINE(google-explicit-constructor)
  AttributeQualifierView(
      const AttributeQualifier& other ABSL_ATTRIBUTE_LIFETIME_BOUND)
      : value_(absl::visit(
            absl::Overload(
                [](absl::monostate value) -> Variant {
                  return Variant(absl::in_place_type<absl::monostate>, value);
                },
                [](bool value) -> Variant {
                  return Variant(absl::in_place_type<bool>, value);
                },
                [](int64_t value) -> Variant {
                  return Variant(absl::in_place_type<int64_t>, value);
                },
                [](uint64_t value) -> Variant {
                  return Variant(absl::in_place_type<uint64_t>, value);
                },
                [](const std::string& value) -> Variant {
                  return Variant(absl::in_place_type<absl::string_view>, value);
                }),
            common_internal::AsVariant(other))) {}

  // NOLINTNEXTLINE(google-explicit-constructor)
  AttributeQualifierView& operator=(
      const AttributeQualifier& other ABSL_ATTRIBUTE_LIFETIME_BOUND) {
    return *this = AttributeQualifierView(other);
  }

  AttributeQualifierView& operator=(AttributeQualifier&&) = delete;

  ABSL_DEPRECATE_AND_INLINE()
  absl::optional<int64_t> GetInt64Key() const { return AsInt(); }

  ABSL_DEPRECATE_AND_INLINE()
  absl::optional<uint64_t> GetUint64Key() const { return AsUint(); }

  ABSL_DEPRECATE_AND_INLINE()
  absl::optional<absl::string_view> GetStringKey() const { return AsString(); }

  ABSL_DEPRECATE_AND_INLINE()
  absl::optional<bool> GetBoolKey() const { return AsBool(); }

  [[nodiscard]]
  bool IsBool() const {
    return absl::holds_alternative<bool>(value_);
  }

  [[nodiscard]]
  bool IsInt() const {
    return absl::holds_alternative<int64_t>(value_);
  }

  [[nodiscard]]
  bool IsUint() const {
    return absl::holds_alternative<uint64_t>(value_);
  }

  [[nodiscard]]
  bool IsString() const {
    return absl::holds_alternative<absl::string_view>(value_);
  }

  [[nodiscard]]
  bool GetBool() const {
    ABSL_DCHECK(IsBool());
    return absl::get<bool>(value_);
  }

  [[nodiscard]]
  int64_t GetInt() const {
    ABSL_DCHECK(IsInt());
    return absl::get<int64_t>(value_);
  }

  [[nodiscard]]
  uint64_t GetUint() const {
    ABSL_DCHECK(IsUint());
    return absl::get<uint64_t>(value_);
  }

  [[nodiscard]]
  absl::string_view GetString() const {
    ABSL_DCHECK(IsString());
    return absl::get<absl::string_view>(value_);
  }

  [[nodiscard]]
  absl::optional<bool> AsBool() const {
    if (const auto* value = absl::get_if<bool>(&value_); value != nullptr) {
      return *value;
    }
    return absl::nullopt;
  }

  [[nodiscard]]
  absl::optional<int64_t> AsInt() const {
    if (const auto* value = absl::get_if<int64_t>(&value_); value != nullptr) {
      return *value;
    }
    return absl::nullopt;
  }

  [[nodiscard]]
  absl::optional<uint64_t> AsUint() const {
    if (const auto* value = absl::get_if<uint64_t>(&value_); value != nullptr) {
      return *value;
    }
    return absl::nullopt;
  }

  [[nodiscard]]
  absl::optional<absl::string_view> AsString() const {
    if (const auto* value = absl::get_if<absl::string_view>(&value_);
        value != nullptr) {
      return *value;
    }
    return absl::nullopt;
  }

 private:
  friend const common_internal::AttributeQualifierViewVariant&
  common_internal::AsVariant(const AttributeQualifierView& qualifier);
  friend common_internal::AttributeQualifierViewVariant&&
  common_internal::AsVariant(AttributeQualifierView&& qualifier);

  using Variant = common_internal::AttributeQualifierViewVariant;

  template <typename T>
  AttributeQualifierView(absl::in_place_type_t<T> in_place_type, T&& value)
      : value_(in_place_type, std::forward<T>(value)) {}

  Variant value_;
};

// AttributeQualifierPattern matches a segment in
// attribute resolutuion path. AttributeQualifierPattern is capable of
// matching path elements of types string/int64/uint64/bool.
class AttributeQualifierPattern {
 public:
  static AttributeQualifierPattern OfInt(int64_t value) {
    return AttributeQualifierPattern(absl::in_place_type<int64_t>, value);
  }

  static AttributeQualifierPattern OfUint(uint64_t value) {
    return AttributeQualifierPattern(absl::in_place_type<uint64_t>, value);
  }

  static AttributeQualifierPattern OfString(const char* value) {
    return OfString(absl::string_view(value));
  }

  static AttributeQualifierPattern OfString(std::string value) {
    return AttributeQualifierPattern(absl::in_place_type<std::string>,
                                     std::move(value));
  }

  static AttributeQualifierPattern OfString(absl::string_view value) {
    return AttributeQualifierPattern(absl::in_place_type<std::string>,
                                     std::string(value));
  }

  static AttributeQualifierPattern OfBool(bool value) {
    return AttributeQualifierPattern(absl::in_place_type<bool>, value);
  }

  ABSL_DEPRECATE_AND_INLINE()
  static AttributeQualifierPattern CreateWildcard() { return Wildcard(); }

  static AttributeQualifierPattern Wildcard() {
    return AttributeQualifierPattern(absl::in_place_type<WildcardType>);
  }

  // NOLINTNEXTLINE(google-explicit-constructor)
  AttributeQualifierPattern(const AttributeQualifier& value)
      : value_(absl::visit(
            absl::Overload(
                [](absl::monostate value) -> Variant {
                  return Variant(absl::in_place_type<absl::monostate>, value);
                },
                [](bool value) -> Variant {
                  return Variant(absl::in_place_type<bool>, value);
                },
                [](int64_t value) -> Variant {
                  return Variant(absl::in_place_type<int64_t>, value);
                },
                [](uint64_t value) -> Variant {
                  return Variant(absl::in_place_type<uint64_t>, value);
                },
                [](const std::string& value) -> Variant {
                  return Variant(absl::in_place_type<std::string>, value);
                }),
            common_internal::AsVariant(value))) {}

  // NOLINTNEXTLINE(google-explicit-constructor)
  AttributeQualifierPattern(AttributeQualifier&& value)
      : value_(absl::visit(
            absl::Overload(
                [](absl::monostate value) -> Variant {
                  return Variant(absl::in_place_type<absl::monostate>, value);
                },
                [](bool value) -> Variant {
                  return Variant(absl::in_place_type<bool>, value);
                },
                [](int64_t value) -> Variant {
                  return Variant(absl::in_place_type<int64_t>, value);
                },
                [](uint64_t value) -> Variant {
                  return Variant(absl::in_place_type<uint64_t>, value);
                },
                [](std::string&& value) -> Variant {
                  return Variant(absl::in_place_type<std::string>,
                                 std::move(value));
                }),
            common_internal::AsVariant(std::move(value)))) {}

  explicit operator bool() const {
    return !absl::holds_alternative<absl::monostate>(value_);
  }

  [[nodiscard]]
  bool IsBool() const {
    return absl::holds_alternative<bool>(value_);
  }

  [[nodiscard]]
  bool IsInt() const {
    return absl::holds_alternative<int64_t>(value_);
  }

  [[nodiscard]]
  bool IsUint() const {
    return absl::holds_alternative<uint64_t>(value_);
  }

  [[nodiscard]]
  bool IsString() const {
    return absl::holds_alternative<std::string>(value_);
  }

  [[nodiscard]]
  bool IsWildcard() const {
    return absl::holds_alternative<WildcardType>(value_);
  }

  [[nodiscard]]
  bool GetBool() const {
    ABSL_DCHECK(IsBool());
    return absl::get<bool>(value_);
  }

  [[nodiscard]]
  int64_t GetInt() const {
    ABSL_DCHECK(IsInt());
    return absl::get<int64_t>(value_);
  }

  [[nodiscard]]
  uint64_t GetUint() const {
    ABSL_DCHECK(IsUint());
    return absl::get<uint64_t>(value_);
  }

  [[nodiscard]]
  const std::string& GetString() const {
    ABSL_DCHECK(IsString());
    return absl::get<std::string>(value_);
  }

  [[nodiscard]]
  absl::optional<bool> AsBool() const {
    if (const auto* value = absl::get_if<bool>(&value_); value != nullptr) {
      return *value;
    }
    return absl::nullopt;
  }

  [[nodiscard]]
  absl::optional<int64_t> AsInt() const {
    if (const auto* value = absl::get_if<int64_t>(&value_); value != nullptr) {
      return *value;
    }
    return absl::nullopt;
  }

  [[nodiscard]]
  absl::optional<uint64_t> AsUint() const {
    if (const auto* value = absl::get_if<uint64_t>(&value_); value != nullptr) {
      return *value;
    }
    return absl::nullopt;
  }

  [[nodiscard]]
  absl::optional_ref<const std::string> AsString() const {
    if (const auto* value = absl::get_if<std::string>(&value_);
        value != nullptr) {
      return *value;
    }
    return absl::nullopt;
  }

  [[nodiscard]]
  absl::optional<AttributeQualifier> ToQualifier() const {
    return absl::visit(
        absl::Overload(
            [](absl::monostate) -> absl::optional<AttributeQualifier> {
              return AttributeQualifier();
            },
            [](bool value) -> absl::optional<AttributeQualifier> {
              return AttributeQualifier::OfBool(value);
            },
            [](int64_t value) -> absl::optional<AttributeQualifier> {
              return AttributeQualifier::OfInt(value);
            },
            [](uint64_t value) -> absl::optional<AttributeQualifier> {
              return AttributeQualifier::OfUint(value);
            },
            [](const std::string& value) -> absl::optional<AttributeQualifier> {
              return AttributeQualifier::OfString(value);
            },
            [](common_internal::WildcardType value)
                -> absl::optional<AttributeQualifier> {
              return absl::nullopt;
            }),
        value_);
  }

  [[nodiscard]]
  absl::optional<AttributeQualifierView> ToQualifierView() const
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return absl::visit(
        absl::Overload(
            [](absl::monostate) -> absl::optional<AttributeQualifierView> {
              return AttributeQualifierView();
            },
            [](bool value) -> absl::optional<AttributeQualifierView> {
              return AttributeQualifierView::OfBool(value);
            },
            [](int64_t value) -> absl::optional<AttributeQualifierView> {
              return AttributeQualifierView::OfInt(value);
            },
            [](uint64_t value) -> absl::optional<AttributeQualifierView> {
              return AttributeQualifierView::OfUint(value);
            },
            [](const std::string& value)
                -> absl::optional<AttributeQualifierView> {
              return AttributeQualifierView::OfString(value);
            },
            [](common_internal::WildcardType value)
                -> absl::optional<AttributeQualifierView> {
              return absl::nullopt;
            }),
        value_);
  }

  [[nodiscard]]
  bool IsMatch(const AttributeQualifier& qualifier) const;

  [[nodiscard]]
  bool IsMatch(absl::string_view other_key) const;

 private:
  friend const common_internal::AttributeQualifierPatternVariant&
  common_internal::AsVariant(const AttributeQualifierPattern& qualifier);
  friend common_internal::AttributeQualifierPatternVariant&&
  common_internal::AsVariant(AttributeQualifierPattern&& qualifier);

  using Variant = common_internal::AttributeQualifierPatternVariant;
  using WildcardType = common_internal::WildcardType;

  template <typename T, typename... Args>
  explicit AttributeQualifierPattern(absl::in_place_type_t<T> in_place_type,
                                     Args&&... args)
      : value_(in_place_type, std::forward<Args>(args)...) {}

  // Qualifier value. If not set, treated as wildcard.
  common_internal::AttributeQualifierPatternVariant value_;
};

[[nodiscard]]
bool operator==(const AttributeQualifier& lhs, const AttributeQualifier& rhs);

[[nodiscard]]
bool operator==(const AttributeQualifierView& lhs,
                const AttributeQualifierView& rhs);

[[nodiscard]]
bool operator==(const AttributeQualifierPattern& lhs,
                const AttributeQualifierPattern& rhs);

[[nodiscard]]
bool operator==(const AttributeQualifier& lhs,
                const AttributeQualifierView& rhs);

[[nodiscard]]
bool operator==(const AttributeQualifier& lhs,
                const AttributeQualifierPattern& rhs);

[[nodiscard]]
bool operator==(const AttributeQualifierView& lhs,
                const AttributeQualifier& rhs);

[[nodiscard]]
bool operator==(const AttributeQualifierView& lhs,
                const AttributeQualifierPattern& rhs);

[[nodiscard]]
bool operator==(const AttributeQualifierPattern& lhs,
                const AttributeQualifier& rhs);

[[nodiscard]]
bool operator==(const AttributeQualifierPattern& lhs,
                const AttributeQualifierView& rhs);

[[nodiscard]]
inline bool operator!=(const AttributeQualifier& lhs,
                       const AttributeQualifier& rhs) {
  return !operator==(lhs, rhs);
}

[[nodiscard]]
inline bool operator!=(const AttributeQualifierView& lhs,
                       const AttributeQualifierView& rhs) {
  return !operator==(lhs, rhs);
}

[[nodiscard]]
inline bool operator!=(const AttributeQualifierPattern& lhs,
                       const AttributeQualifierPattern& rhs) {
  return !operator==(lhs, rhs);
}

[[nodiscard]]
inline bool operator!=(const AttributeQualifier& lhs,
                       const AttributeQualifierView& rhs) {
  return !operator==(lhs, rhs);
}

[[nodiscard]]
inline bool operator!=(const AttributeQualifier& lhs,
                       const AttributeQualifierPattern& rhs) {
  return !operator==(lhs, rhs);
}

[[nodiscard]]
inline bool operator!=(const AttributeQualifierView& lhs,
                       const AttributeQualifier& rhs) {
  return !operator==(lhs, rhs);
}

[[nodiscard]]
inline bool operator!=(const AttributeQualifierView& lhs,
                       const AttributeQualifierPattern& rhs) {
  return !operator==(lhs, rhs);
}

[[nodiscard]]
inline bool operator!=(const AttributeQualifierPattern& lhs,
                       const AttributeQualifier& rhs) {
  return !operator==(lhs, rhs);
}

[[nodiscard]]
inline bool operator!=(const AttributeQualifierPattern& lhs,
                       const AttributeQualifierView& rhs) {
  return !operator==(lhs, rhs);
}

[[nodiscard]]
bool operator<(const AttributeQualifier& lhs, const AttributeQualifier& rhs);

[[nodiscard]]
bool operator<(const AttributeQualifierView& lhs,
               const AttributeQualifierView& rhs);

[[nodiscard]]
bool operator<(const AttributeQualifierPattern& lhs,
               const AttributeQualifierPattern& rhs);

[[nodiscard]]
bool operator<(const AttributeQualifier& lhs,
               const AttributeQualifierView& rhs);

[[nodiscard]]
bool operator<(const AttributeQualifier& lhs,
               const AttributeQualifierPattern& rhs);

[[nodiscard]]
bool operator<(const AttributeQualifierView& lhs,
               const AttributeQualifier& rhs);

[[nodiscard]]
bool operator<(const AttributeQualifierView& lhs,
               const AttributeQualifierPattern& rhs);

[[nodiscard]]
bool operator<(const AttributeQualifierPattern& lhs,
               const AttributeQualifier& rhs);

[[nodiscard]]
bool operator<(const AttributeQualifierPattern& lhs,
               const AttributeQualifierView& rhs);

namespace common_internal {

[[nodiscard]]
inline const AttributeQualifierVariant& AsVariant(
    const AttributeQualifier& qualifier ABSL_ATTRIBUTE_LIFETIME_BOUND) {
  return qualifier.value_;
}

[[nodiscard]]
inline AttributeQualifierVariant&& AsVariant(
    AttributeQualifier&& qualifier ABSL_ATTRIBUTE_LIFETIME_BOUND) {
  return std::move(qualifier.value_);
}

[[nodiscard]]
inline const AttributeQualifierPatternVariant& AsVariant(
    const AttributeQualifierPattern& qualifier ABSL_ATTRIBUTE_LIFETIME_BOUND) {
  return qualifier.value_;
}

[[nodiscard]]
inline AttributeQualifierPatternVariant&& AsVariant(
    AttributeQualifierPattern&& qualifier ABSL_ATTRIBUTE_LIFETIME_BOUND) {
  return std::move(qualifier.value_);
}

[[nodiscard]]
inline const AttributeQualifierViewVariant& AsVariant(
    const AttributeQualifierView& qualifier ABSL_ATTRIBUTE_LIFETIME_BOUND) {
  return qualifier.value_;
}

[[nodiscard]]
inline AttributeQualifierViewVariant&& AsVariant(
    AttributeQualifierView&& qualifier ABSL_ATTRIBUTE_LIFETIME_BOUND) {
  return std::move(qualifier.value_);
}

}  // namespace common_internal

// Attribute represents resolved attribute path.
class Attribute {
 public:
  explicit Attribute(std::string variable_name)
      : Attribute(std::move(variable_name), {}) {}

  Attribute(std::string variable_name,
            std::vector<AttributeQualifier> qualifier_path)
      : impl_(std::make_shared<Impl>(std::move(variable_name),
                                     std::move(qualifier_path))) {}

  absl::string_view variable_name() const { return impl_->variable_name; }

  bool has_variable_name() const { return !impl_->variable_name.empty(); }

  absl::Span<const AttributeQualifier> qualifier_path() const {
    return impl_->qualifier_path;
  }

  bool operator==(const Attribute& other) const;

  bool operator<(const Attribute& other) const;

  absl::StatusOr<std::string> AsString() const;

 private:
  struct Impl final {
    Impl(std::string variable_name,
         std::vector<AttributeQualifier> qualifier_path)
        : variable_name(std::move(variable_name)),
          qualifier_path(std::move(qualifier_path)) {}

    std::string variable_name;
    std::vector<AttributeQualifier> qualifier_path;
  };

  std::shared_ptr<const Impl> impl_;
};

// AttributePattern is a fully-qualified absolute attribute path pattern.
// Supported segments steps in the path are:
// - field selection;
// - map lookup by key;
// - list access by index.
class AttributePattern {
 public:
  // MatchType enum specifies how closely pattern is matching the attribute:
  enum class MatchType {
    NONE,     // Pattern does not match attribute itself nor its children
    PARTIAL,  // Pattern matches an entity nested within attribute;
    FULL      // Pattern matches an attribute itself.
  };

  AttributePattern(std::string variable,
                   std::vector<AttributeQualifierPattern> qualifier_path)
      : variable_(std::move(variable)),
        qualifier_path_(std::move(qualifier_path)) {}

  absl::string_view variable() const { return variable_; }

  absl::Span<const AttributeQualifierPattern> qualifier_path() const {
    return qualifier_path_;
  }

  // Matches the pattern to an attribute.
  // Distinguishes between no-match, partial match and full match cases.
  MatchType IsMatch(const Attribute& attribute) const {
    MatchType result = MatchType::NONE;
    if (attribute.variable_name() != variable_) {
      return result;
    }

    auto max_index = qualifier_path().size();
    result = MatchType::FULL;
    if (qualifier_path().size() > attribute.qualifier_path().size()) {
      max_index = attribute.qualifier_path().size();
      result = MatchType::PARTIAL;
    }

    for (size_t i = 0; i < max_index; i++) {
      if (!(qualifier_path()[i].IsMatch(attribute.qualifier_path()[i]))) {
        return MatchType::NONE;
      }
    }
    return result;
  }

 private:
  std::string variable_;
  std::vector<AttributeQualifierPattern> qualifier_path_;
};

struct FieldSpecifier {
  int64_t number;
  std::string name;
};

using SelectQualifier = absl::variant<FieldSpecifier, AttributeQualifier>;

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_BASE_ATTRIBUTE_H_
