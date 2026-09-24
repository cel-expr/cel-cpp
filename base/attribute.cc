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

#include "base/attribute.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

#include "absl/base/macros.h"
#include "absl/base/nullability.h"
#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/variant.h"
#include "base/kind.h"
#include "internal/status_macros.h"

namespace cel {

namespace {

// Visitor for appending string representation for different qualifier kinds.
class AttributeStringPrinter {
 public:
  // String representation for the given qualifier is appended to output.
  // output must be non-null.
  explicit AttributeStringPrinter(std::string* output) : output_(*output) {}

  absl::Status operator()(absl::monostate) const {
    return absl::InvalidArgumentError("bad attribute qualifier");
  }

  absl::Status operator()(int64_t index) {
    absl::StrAppend(&output_, "[", index, "]");
    return absl::OkStatus();
  }

  absl::Status operator()(uint64_t index) {
    absl::StrAppend(&output_, "[", index, "]");
    return absl::OkStatus();
  }

  absl::Status operator()(bool bool_key) {
    absl::StrAppend(&output_, "[", (bool_key) ? "true" : "false", "]");
    return absl::OkStatus();
  }

  absl::Status operator()(const std::string& field) {
    absl::StrAppend(&output_, ".", field);
    return absl::OkStatus();
  }

 private:
  std::string& output_;
};

// Visitor for appending string representation for different qualifier kinds.
class AttributeQualifierStringPrinter {
 public:
  // String representation for the given qualifier is appended to output.
  explicit AttributeQualifierStringPrinter(std::string* absl_nonnull output)
      : output_(*output) {}

  absl::Status operator()(absl::monostate) const {
    // Attributes are represented as a variant, with illegal attribute
    // qualifiers represented with their type as the first alternative.
    return absl::InvalidArgumentError("bad attribute qualifier");
  }

  absl::Status operator()(int64_t index) {
    absl::StrAppend(&output_, index);
    return absl::OkStatus();
  }

  absl::Status operator()(uint64_t index) {
    absl::StrAppend(&output_, index);
    return absl::OkStatus();
  }

  absl::Status operator()(bool bool_key) {
    absl::StrAppend(&output_, (bool_key) ? "true" : "false");
    return absl::OkStatus();
  }

  absl::Status operator()(const std::string& field) {
    absl::StrAppend(&output_, field);
    return absl::OkStatus();
  }

 private:
  std::string& output_;
};

struct AttributeQualifierTypeVisitor final {
  Kind operator()(absl::monostate) const { return Kind::kNull; }

  Kind operator()(int64_t) const { return Kind::kInt64; }

  Kind operator()(uint64_t) const { return Kind::kUint64; }

  Kind operator()(const std::string&) const { return Kind::kString; }

  Kind operator()(bool) const { return Kind::kBool; }
};

}  // namespace

Kind AttributeQualifier::kind() const {
  return absl::visit(AttributeQualifierTypeVisitor{}, value_);
}

bool Attribute::operator==(const Attribute& other) const {
  // We cannot check pointer equality as a short circuit because we have to
  // treat all invalid AttributeQualifier as not equal to each other.
  // TODO(issues/41) we only support Ident-rooted attributes at the moment.
  if (variable_name() != other.variable_name()) {
    return false;
  }

  if (qualifier_path().size() != other.qualifier_path().size()) {
    return false;
  }

  for (size_t i = 0; i < qualifier_path().size(); i++) {
    if (!(qualifier_path()[i] == other.qualifier_path()[i])) {
      return false;
    }
  }

  return true;
}

bool Attribute::operator<(const Attribute& other) const {
  if (impl_.get() == other.impl_.get()) {
    return false;
  }
  auto lhs_begin = qualifier_path().begin();
  auto lhs_end = qualifier_path().end();
  auto rhs_begin = other.qualifier_path().begin();
  auto rhs_end = other.qualifier_path().end();
  while (lhs_begin != lhs_end && rhs_begin != rhs_end) {
    if (*lhs_begin < *rhs_begin) {
      return true;
    }
    if (!(*lhs_begin == *rhs_begin)) {
      return false;
    }
    lhs_begin++;
    rhs_begin++;
  }
  if (lhs_begin == lhs_end && rhs_begin == rhs_end) {
    // Neither has any elements left, they are equal. Compare variable names.
    return variable_name() < other.variable_name();
  }
  if (lhs_begin == lhs_end) {
    // Left has no more elements. Right is greater.
    return true;
  }
  // Right has no more elements. Left is greater.
  ABSL_ASSERT(rhs_begin == rhs_end);
  return false;
}

absl::StatusOr<std::string> Attribute::AsString() const {
  if (variable_name().empty()) {
    return absl::InvalidArgumentError(
        "Only ident rooted attributes are supported.");
  }

  std::string result = std::string(variable_name());

  for (const auto& qualifier : qualifier_path()) {
    CEL_RETURN_IF_ERROR(absl::visit(AttributeStringPrinter(&result),
                                    common_internal::AsVariant(qualifier)));
  }

  return result;
}

std::string AttributeQualifier::ToString() const {
  std::string result;
  absl::Status status =
      absl::visit(AttributeQualifierStringPrinter{&result}, value_);
  ABSL_DCHECK_OK(status) << "bad attribute qualifier";
  status.IgnoreError();
  return result;
}

bool AttributeQualifier::IsMatch(const AttributeQualifier& other) const {
  ABSL_DCHECK(*this) << "bad attribute qualifier";
  ABSL_DCHECK(other) << "bad attribute qualifier";
  if (absl::holds_alternative<absl::monostate>(value_) ||
      absl::holds_alternative<absl::monostate>(other.value_)) {
    return false;
  }
  return *this == other;
}

bool AttributeQualifier::IsMatch(absl::string_view other_key) const {
  ABSL_DCHECK(*this) << "bad attribute qualifier";
  if (auto string = AsString(); string.has_value()) {
    return *string == other_key;
  }
  return false;
}

bool AttributeQualifierPattern::IsMatch(
    const AttributeQualifier& qualifier) const {
  ABSL_DCHECK(*this) << "bad attribute qualifier pattern";
  ABSL_DCHECK(qualifier) << "bad attribute qualifier";
  if (!*this || !qualifier) {
    return false;
  }
  if (IsWildcard()) {
    return true;
  }
  return *this == qualifier;
}

bool AttributeQualifierPattern::IsMatch(absl::string_view other_key) const {
  ABSL_DCHECK(*this) << "bad attribute qualifier pattern";
  if (IsWildcard()) {
    return true;
  }
  if (auto string = AsString(); string.has_value()) {
    return *string == other_key;
  }
  return false;
}

namespace {

struct AttributeQualifierEqualTo {
  bool operator()(const absl::monostate&, const absl::monostate&) const {
    return false;
  }

  template <typename T>
  std::enable_if_t<!std::is_same_v<std::remove_cvref_t<T>, absl::monostate>,
                   bool>
  operator()(const absl::monostate&, const T&) const {
    return false;
  }

  template <typename T>
  std::enable_if_t<!std::is_same_v<std::remove_cvref_t<T>, absl::monostate>,
                   bool>
  operator()(const T&, const absl::monostate&) const {
    return false;
  }

  bool operator()(bool lhs, bool rhs) const { return lhs == rhs; }

  bool operator()(bool, int64_t) const { return false; }

  bool operator()(bool, uint64_t) const { return false; }

  bool operator()(bool, absl::string_view) const { return false; }

  bool operator()(int64_t, bool) const { return false; }

  bool operator()(int64_t lhs, int64_t rhs) const { return lhs == rhs; }

  bool operator()(int64_t, uint64_t) const { return false; }

  bool operator()(int64_t, absl::string_view) const { return false; }

  bool operator()(uint64_t, bool) const { return false; }

  bool operator()(uint64_t, int64_t) const { return false; }

  bool operator()(uint64_t lhs, uint64_t rhs) const { return lhs == rhs; }

  bool operator()(uint64_t, absl::string_view) const { return false; }

  bool operator()(absl::string_view, bool) const { return false; }

  bool operator()(absl::string_view, int64_t) const { return false; }

  bool operator()(absl::string_view, uint64_t) const { return false; }

  bool operator()(absl::string_view lhs, absl::string_view rhs) const {
    return lhs == rhs;
  }

  bool operator()(const common_internal::WildcardType&,
                  const common_internal::WildcardType&) const {
    return true;
  }

  template <typename T>
  std::enable_if_t<
      !std::is_same_v<std::remove_cvref_t<T>, common_internal::WildcardType> &&
          !std::is_same_v<std::remove_cvref_t<T>, absl::monostate>,
      bool>
  operator()(const common_internal::WildcardType&, const T&) const {
    return false;
  }

  template <typename T>
  std::enable_if_t<
      !std::is_same_v<std::remove_cvref_t<T>, common_internal::WildcardType> &&
          !std::is_same_v<std::remove_cvref_t<T>, absl::monostate>,
      bool>
  operator()(const T&, const common_internal::WildcardType&) const {
    return false;
  }
};

struct AttributeQualifierLess {
  bool operator()(const absl::monostate&, const absl::monostate&) const {
    return false;
  }

  template <typename T>
  std::enable_if_t<!std::is_same_v<std::remove_cvref_t<T>, absl::monostate>,
                   bool>
  operator()(const absl::monostate&, const T&) const {
    return true;
  }

  template <typename T>
  std::enable_if_t<!std::is_same_v<std::remove_cvref_t<T>, absl::monostate>,
                   bool>
  operator()(const T&, const absl::monostate&) const {
    return false;
  }

  bool operator()(bool lhs, bool rhs) const { return lhs < rhs; }

  bool operator()(bool, int64_t) const { return false; }

  bool operator()(bool, uint64_t) const { return false; }

  bool operator()(bool, absl::string_view) const { return false; }

  bool operator()(int64_t, bool) const { return true; }

  bool operator()(int64_t lhs, int64_t rhs) const { return lhs < rhs; }

  bool operator()(int64_t, uint64_t) const { return true; }

  bool operator()(int64_t, absl::string_view) const { return true; }

  bool operator()(uint64_t, bool) const { return true; }

  bool operator()(uint64_t, int64_t) const { return false; }

  bool operator()(uint64_t lhs, uint64_t rhs) const { return lhs < rhs; }

  bool operator()(uint64_t, absl::string_view) const { return true; }

  bool operator()(absl::string_view, bool) const { return true; }

  bool operator()(absl::string_view, int64_t) const { return false; }

  bool operator()(absl::string_view, uint64_t) const { return false; }

  bool operator()(absl::string_view lhs, absl::string_view rhs) const {
    return lhs < rhs;
  }

  bool operator()(const common_internal::WildcardType&,
                  const common_internal::WildcardType&) const {
    return false;
  }

  template <typename T>
  std::enable_if_t<
      !std::is_same_v<std::remove_cvref_t<T>, common_internal::WildcardType> &&
          !std::is_same_v<std::remove_cvref_t<T>, absl::monostate>,
      bool>
  operator()(const common_internal::WildcardType&, const T&) const {
    return false;
  }

  template <typename T>
  std::enable_if_t<
      !std::is_same_v<std::remove_cvref_t<T>, common_internal::WildcardType> &&
          !std::is_same_v<std::remove_cvref_t<T>, absl::monostate>,
      bool>
  operator()(const T&, const common_internal::WildcardType&) const {
    return true;
  }
};

}  // namespace

bool operator==(const AttributeQualifier& lhs, const AttributeQualifier& rhs) {
  return absl::visit(AttributeQualifierEqualTo{},
                     common_internal::AsVariant(lhs),
                     common_internal::AsVariant(rhs));
}

bool operator==(const AttributeQualifierView& lhs,
                const AttributeQualifierView& rhs) {
  return absl::visit(AttributeQualifierEqualTo{},
                     common_internal::AsVariant(lhs),
                     common_internal::AsVariant(rhs));
}

bool operator==(const AttributeQualifierPattern& lhs,
                const AttributeQualifierPattern& rhs) {
  return absl::visit(AttributeQualifierEqualTo{},
                     common_internal::AsVariant(lhs),
                     common_internal::AsVariant(rhs));
}

bool operator==(const AttributeQualifier& lhs,
                const AttributeQualifierView& rhs) {
  return absl::visit(AttributeQualifierEqualTo{},
                     common_internal::AsVariant(lhs),
                     common_internal::AsVariant(rhs));
}

bool operator==(const AttributeQualifier& lhs,
                const AttributeQualifierPattern& rhs) {
  return absl::visit(AttributeQualifierEqualTo{},
                     common_internal::AsVariant(lhs),
                     common_internal::AsVariant(rhs));
}

bool operator==(const AttributeQualifierView& lhs,
                const AttributeQualifier& rhs) {
  return absl::visit(AttributeQualifierEqualTo{},
                     common_internal::AsVariant(lhs),
                     common_internal::AsVariant(rhs));
}

bool operator==(const AttributeQualifierView& lhs,
                const AttributeQualifierPattern& rhs) {
  return absl::visit(AttributeQualifierEqualTo{},
                     common_internal::AsVariant(lhs),
                     common_internal::AsVariant(rhs));
}

bool operator==(const AttributeQualifierPattern& lhs,
                const AttributeQualifier& rhs) {
  return absl::visit(AttributeQualifierEqualTo{},
                     common_internal::AsVariant(lhs),
                     common_internal::AsVariant(rhs));
}

bool operator==(const AttributeQualifierPattern& lhs,
                const AttributeQualifierView& rhs) {
  return absl::visit(AttributeQualifierEqualTo{},
                     common_internal::AsVariant(lhs),
                     common_internal::AsVariant(rhs));
}

bool operator<(const AttributeQualifier& lhs, const AttributeQualifier& rhs) {
  return absl::visit(AttributeQualifierLess{}, common_internal::AsVariant(lhs),
                     common_internal::AsVariant(rhs));
}

bool operator<(const AttributeQualifierView& lhs,
               const AttributeQualifierView& rhs) {
  return absl::visit(AttributeQualifierLess{}, common_internal::AsVariant(lhs),
                     common_internal::AsVariant(rhs));
}

bool operator<(const AttributeQualifierPattern& lhs,
               const AttributeQualifierPattern& rhs) {
  return absl::visit(AttributeQualifierLess{}, common_internal::AsVariant(lhs),
                     common_internal::AsVariant(rhs));
}

bool operator<(const AttributeQualifier& lhs,
               const AttributeQualifierView& rhs) {
  return absl::visit(AttributeQualifierLess{}, common_internal::AsVariant(lhs),
                     common_internal::AsVariant(rhs));
}

bool operator<(const AttributeQualifier& lhs,
               const AttributeQualifierPattern& rhs) {
  return absl::visit(AttributeQualifierLess{}, common_internal::AsVariant(lhs),
                     common_internal::AsVariant(rhs));
}

bool operator<(const AttributeQualifierView& lhs,
               const AttributeQualifier& rhs) {
  return absl::visit(AttributeQualifierLess{}, common_internal::AsVariant(lhs),
                     common_internal::AsVariant(rhs));
}

bool operator<(const AttributeQualifierView& lhs,
               const AttributeQualifierPattern& rhs) {
  return absl::visit(AttributeQualifierLess{}, common_internal::AsVariant(lhs),
                     common_internal::AsVariant(rhs));
}

bool operator<(const AttributeQualifierPattern& lhs,
               const AttributeQualifier& rhs) {
  return absl::visit(AttributeQualifierLess{}, common_internal::AsVariant(lhs),
                     common_internal::AsVariant(rhs));
}

bool operator<(const AttributeQualifierPattern& lhs,
               const AttributeQualifierView& rhs) {
  return absl::visit(AttributeQualifierLess{}, common_internal::AsVariant(lhs),
                     common_internal::AsVariant(rhs));
}

}  // namespace cel
