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

#ifndef THIRD_PARTY_CEL_CPP_COMMON_INTERNAL_VALUE_ARENA_PTR_TAG_H_
#define THIRD_PARTY_CEL_CPP_COMMON_INTERNAL_VALUE_ARENA_PTR_TAG_H_

#include <cstdint>
#include <type_traits>

#include "absl/base/casts.h"
#include "absl/base/nullability.h"
#include "google/protobuf/arena.h"

namespace cel::common_internal {

enum class ValueArenaPtrTag : uint8_t {
  // No tag.
  kNone = 0,
  // Associated underlying representation of the value is static and not owned
  // by any specific arena. It is free to be laundered around as it is valid for
  // the lifetime of the program. The tagged arena pointer must be null.
  kStaticLifetime = 1 << 0,
  // Associated underlying representation of the value has data partially owned
  // by another arena or is unsafe which is not the same as the pointer to the
  // arena this tag is part of. This is used to determine whether or not to
  // perform cloning for composite values such as lists or maps.
  kMixedOwnership = 1 << 1,
};

[[nodiscard]]
constexpr ValueArenaPtrTag operator&(ValueArenaPtrTag lhs,
                                     ValueArenaPtrTag rhs) {
  return static_cast<ValueArenaPtrTag>(
      absl::bit_cast<std::underlying_type_t<ValueArenaPtrTag>>(lhs) &
      absl::bit_cast<std::underlying_type_t<ValueArenaPtrTag>>(rhs));
}

[[nodiscard]]
constexpr ValueArenaPtrTag operator|(ValueArenaPtrTag lhs,
                                     ValueArenaPtrTag rhs) {
  return static_cast<ValueArenaPtrTag>(
      absl::bit_cast<std::underlying_type_t<ValueArenaPtrTag>>(lhs) |
      absl::bit_cast<std::underlying_type_t<ValueArenaPtrTag>>(rhs));
}

[[nodiscard]]
constexpr ValueArenaPtrTag operator^(ValueArenaPtrTag lhs,
                                     ValueArenaPtrTag rhs) {
  return static_cast<ValueArenaPtrTag>(
      absl::bit_cast<std::underlying_type_t<ValueArenaPtrTag>>(lhs) ^
      absl::bit_cast<std::underlying_type_t<ValueArenaPtrTag>>(rhs));
}

[[nodiscard]]
constexpr bool ValueArenaPtrTagIsStaticLifetime(ValueArenaPtrTag tag) {
  return (tag & ValueArenaPtrTag::kStaticLifetime) ==
         ValueArenaPtrTag::kStaticLifetime;
}

[[nodiscard]]
constexpr bool ValueArenaPtrTagIsMixedOwnership(ValueArenaPtrTag tag) {
  return (tag & ValueArenaPtrTag::kMixedOwnership) ==
         ValueArenaPtrTag::kMixedOwnership;
}

[[nodiscard]]
constexpr bool CheckValueArenaPtrTag(google::protobuf::Arena* absl_nullable arena,
                                     ValueArenaPtrTag tag) {
  if (arena == nullptr) {
    return tag == ValueArenaPtrTag::kNone ||
           tag == ValueArenaPtrTag::kStaticLifetime;
  }
  return tag == ValueArenaPtrTag::kNone ||
         tag == ValueArenaPtrTag::kMixedOwnership;
}

}  // namespace cel::common_internal

#endif  // THIRD_PARTY_CEL_CPP_COMMON_INTERNAL_VALUE_ARENA_PTR_TAG_H_
