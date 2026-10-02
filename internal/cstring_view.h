// Copyright 2026 Google LLC
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

#ifndef THIRD_PARTY_CEL_CPP_INTERNAL_CSTRING_VIEW_H_
#define THIRD_PARTY_CEL_CPP_INTERNAL_CSTRING_VIEW_H_

#include <cstddef>
#include <cstring>
#include <string>

#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/base/nullability.h"
#include "absl/strings/string_view.h"

namespace cel::internal {

// Wrapper around C string. Used by UnknownAttributeKey.
class ABSL_ATTRIBUTE_VIEW cstring_view {
 public:
  using value_type = char;
  using reference = const char&;
  using const_reference = reference;
  using pointer = const char*;
  using const_pointer = pointer;
  using size_type = size_t;
  using difference_type = ptrdiff_t;

  cstring_view() = default;
  cstring_view(const cstring_view&) = default;
  cstring_view& operator=(const cstring_view&) = default;

  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr cstring_view(
      const char* absl_nonnull str ABSL_ATTRIBUTE_LIFETIME_BOUND)
      : str_(str) {}

  cstring_view(std::nullptr_t) = delete;

  cstring_view& operator=(
      const char* absl_nonnull str ABSL_ATTRIBUTE_LIFETIME_BOUND) {
    str_ = str;
    return *this;
  }

  cstring_view& operator=(std::nullptr_t) = delete;

  [[nodiscard]]
  constexpr const char* absl_nonnull c_str() const {
    return data();
  }

  [[nodiscard]]
  constexpr const char* absl_nonnull data() const {
    return str_;
  }

  [[nodiscard]]
  constexpr bool empty() const {
    return str_[0] == '\0';
  }

  [[nodiscard]]
  constexpr size_t size() const {
    return std::char_traits<char>::length(c_str());
  }

  [[nodiscard]]
  int compare(const char* absl_nonnull rhs) const {
    return compare(cstring_view(rhs));
  }

  [[nodiscard]]
  int compare(cstring_view rhs) const {
    return ::strcmp(c_str(), rhs.c_str());
  }

  [[nodiscard]]
  int compare(absl::string_view rhs) const {
    if (empty()) {
      return rhs.empty() ? 0 : -1;
    }
    if (rhs.empty()) {
      return 1;
    }
    return ::strncmp(c_str(), rhs.data(), rhs.size());
  }

  [[nodiscard]]
  constexpr const char& operator[](size_t index) const {
    // Okay to access the null terminator.
    ABSL_ASSERT(index < size() + 1);
    return str_[index];
  }

  template <typename H>
  friend H AbslHashValue(H state, cstring_view value) {
    return H::combine(std::move(state),
                      absl::string_view(value.c_str(), value.size()));
  }

  template <typename S>
  friend void AbslStringify(S& sink, cstring_view value) {
    sink.Append(absl::string_view(value.c_str(), value.size()));
  }

 private:
  const char* absl_nonnull str_ = "";
};

[[nodiscard]]
inline bool operator==(cstring_view lhs, cstring_view rhs) {
  return lhs.compare(rhs) == 0;
}

[[nodiscard]]
inline bool operator==(cstring_view lhs, const char* absl_nonnull rhs) {
  return lhs.compare(rhs) == 0;
}

[[nodiscard]]
inline bool operator==(const char* absl_nonnull lhs, cstring_view rhs) {
  return cstring_view(lhs).compare(rhs) == 0;
}

[[nodiscard]]
inline bool operator==(cstring_view lhs, absl::string_view rhs) {
  return lhs.compare(rhs) == 0;
}

[[nodiscard]]
inline bool operator==(absl::string_view lhs, cstring_view rhs) {
  return operator==(rhs, lhs);
}

[[nodiscard]]
inline bool operator!=(cstring_view lhs, cstring_view rhs) {
  return !operator==(lhs, rhs);
}

[[nodiscard]]
inline bool operator!=(cstring_view lhs, const char* absl_nonnull rhs) {
  return !operator==(lhs, rhs);
}

[[nodiscard]]
inline bool operator!=(const char* absl_nonnull lhs, cstring_view rhs) {
  return !operator==(lhs, rhs);
}

[[nodiscard]]
inline bool operator!=(cstring_view lhs, absl::string_view rhs) {
  return !operator==(lhs, rhs);
}

[[nodiscard]]
inline bool operator!=(absl::string_view lhs, cstring_view rhs) {
  return !operator==(lhs, rhs);
}

[[nodiscard]]
inline bool operator<(cstring_view lhs, cstring_view rhs) {
  return lhs.compare(rhs) < 0;
}

[[nodiscard]]
inline bool operator<(cstring_view lhs, const char* absl_nonnull rhs) {
  return lhs.compare(rhs) < 0;
}

[[nodiscard]]
inline bool operator<(const char* absl_nonnull lhs, cstring_view rhs) {
  return rhs.compare(lhs) > 0;
}

[[nodiscard]]
inline bool operator<(cstring_view lhs, absl::string_view rhs) {
  return lhs.compare(rhs) < 0;
}

[[nodiscard]]
inline bool operator<(absl::string_view lhs, cstring_view rhs) {
  return rhs.compare(lhs) > 0;
}

}  // namespace cel::internal

#endif  // THIRD_PARTY_CEL_CPP_INTERNAL_CSTRING_VIEW_H_
