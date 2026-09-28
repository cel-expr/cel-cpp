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

#ifndef THIRD_PARTY_CEL_CPP_INTERNAL_TAGGED_ARENA_PTR_H_
#define THIRD_PARTY_CEL_CPP_INTERNAL_TAGGED_ARENA_PTR_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>

#include "absl/base/attributes.h"
#include "absl/base/casts.h"
#include "absl/base/nullability.h"
#include "absl/log/absl_check.h"
#include "absl/numeric/bits.h"
#include "google/protobuf/arena.h"

namespace cel::internal {

template <typename T>
struct TaggedArenaPtrTagTraits;

// TaggedArenaPtr is a non-owning tagged pointer to google::protobuf::Arena. The lower
// unused bits of the pointer hold a tag value.
template <typename T>
class ABSL_ATTRIBUTE_VIEW ABSL_NULLABILITY_COMPATIBLE TaggedArenaPtr {
 private:
  using UnderlyingTagType = std::underlying_type_t<T>;
  using UnsignedUnderlyingTagType = std::make_unsigned_t<UnderlyingTagType>;

  static constexpr size_t kTagBits = absl::countr_zero(alignof(google::protobuf::Arena));
  static constexpr uintptr_t kTagMask =
      static_cast<uintptr_t>(alignof(google::protobuf::Arena)) - 1;
  static constexpr uintptr_t kArenaPtrMask = ~kTagMask;

 public:
  using element_type = google::protobuf::Arena;

  // NOLINTNEXTLINE(google-explicit-constructor)
  TaggedArenaPtr(google::protobuf::Arena* arena, T tag = static_cast<T>(0)) {
    ABSL_DCHECK_LE(absl::bit_width(TagToUnderlyingUnsigned(tag)), kTagBits);
    ABSL_DCHECK(TaggedArenaPtrTagTraits<T>::CheckTag(arena, tag));
    ptr_ = reinterpret_cast<uintptr_t>(arena) | TagToUnderlyingUnsigned(tag);
  }

  // Trivial default constructor which leaves this value in an uninitialized
  // state, so this can be used in unions.
  TaggedArenaPtr() = default;
  TaggedArenaPtr(const TaggedArenaPtr&) = default;
  TaggedArenaPtr& operator=(const TaggedArenaPtr&) = default;

  [[nodiscard]]
  google::protobuf::Arena& operator*() const {
    ABSL_DCHECK(get() != nullptr);
    return *get();
  }

  [[nodiscard]]
  google::protobuf::Arena* operator->() const {
    ABSL_DCHECK(get() != nullptr);
    return get();
  }

  [[nodiscard]]
  google::protobuf::Arena* get() const {
    // Maks off the tag bits.
    return reinterpret_cast<google::protobuf::Arena*>(ptr_ & kArenaPtrMask);
  }

  [[nodiscard]]
  T tag() const {
    // Mask off the pointer bits.
    return TagFromUnderlyingUnsigned(
        static_cast<UnsignedUnderlyingTagType>(ptr_ & kTagMask));
  }

  void tag(T tag) {
    ABSL_DCHECK_LE(absl::bit_width(TagToUnderlyingUnsigned(tag)), kTagBits);
    ABSL_DCHECK(TaggedArenaPtrTagTraits<T>::CheckTag(get(), tag));
    ptr_ = reinterpret_cast<uintptr_t>(get()) | TagToUnderlyingUnsigned(tag);
  }

 private:
  [[nodiscard]]
  static UnderlyingTagType TagToUnderlying(T tags) {
    return absl::bit_cast<UnderlyingTagType>(tags);
  }

  [[nodiscard]]
  static UnsignedUnderlyingTagType TagToUnderlyingUnsigned(T tags) {
    return absl::bit_cast<UnsignedUnderlyingTagType>(TagToUnderlying(tags));
  }

  [[nodiscard]]
  static T TagFromUnderlying(UnderlyingTagType tags) {
    return absl::bit_cast<T>(tags);
  }

  [[nodiscard]]
  static T TagFromUnderlyingUnsigned(UnsignedUnderlyingTagType tags) {
    return TagFromUnderlying(absl::bit_cast<UnderlyingTagType>(tags));
  }

  uintptr_t ptr_;
};

template <typename T>
TaggedArenaPtr(google::protobuf::Arena*, T) -> TaggedArenaPtr<T>;

template <typename T>
[[nodiscard]]
inline bool operator==(TaggedArenaPtr<T> absl_nullable lhs,
                       const google::protobuf::Arena* absl_nullable rhs) {
  return lhs.get() == rhs;
}

template <typename T>
[[nodiscard]]
inline bool operator==(const google::protobuf::Arena* absl_nullable lhs,
                       TaggedArenaPtr<T> absl_nullable rhs) {
  return lhs == rhs.get();
}

template <typename T>
[[nodiscard]]
inline bool operator==(TaggedArenaPtr<T> absl_nullable lhs, std::nullptr_t) {
  return lhs.get() == nullptr;
}

template <typename T>
[[nodiscard]]
inline bool operator==(std::nullptr_t, TaggedArenaPtr<T> absl_nullable rhs) {
  return nullptr == rhs.get();
}

template <typename T>
[[nodiscard]]
inline bool operator!=(TaggedArenaPtr<T> absl_nullable lhs,
                       const google::protobuf::Arena* absl_nullable rhs) {
  return lhs.get() != rhs;
}

template <typename T>
[[nodiscard]]
inline bool operator!=(const google::protobuf::Arena* lhs,
                       TaggedArenaPtr<T> absl_nullable rhs) {
  return lhs != rhs.get();
}

template <typename T>
[[nodiscard]]
inline bool operator!=(TaggedArenaPtr<T> absl_nullable lhs, std::nullptr_t) {
  return lhs.get() != nullptr;
}

template <typename T>
[[nodiscard]]
inline bool operator!=(std::nullptr_t, TaggedArenaPtr<T> absl_nullable rhs) {
  return nullptr != rhs.get();
}

}  // namespace cel::internal

namespace std {

template <typename T>
struct pointer_traits<::cel::internal::TaggedArenaPtr<T>> {
  using pointer = ::cel::internal::TaggedArenaPtr<T>;
  using element_type = ::google::protobuf::Arena;
  using different_type = ptrdiff_t;

  static constexpr element_type* to_address(const pointer& ptr) {
    return ptr.get();
  }
};

}  // namespace std

#endif  // THIRD_PARTY_CEL_CPP_INTERNAL_TAGGED_ARENA_PTR_H_
