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

#ifndef THIRD_PARTY_CEL_CPP_OPTIONAL_REF_H_
#define THIRD_PARTY_CEL_CPP_OPTIONAL_REF_H_

#include <optional>
#include <type_traits>

#include "absl/base/macros.h"
#include "absl/types/optional_ref.h"

namespace cel {

// `optional_ref<T>` looks and feels like `absl::optional<T>`, but instead of
// owning the underlying value, it retains a reference to the value it accepts
// in its constructor.
template <typename T>
using optional_ref ABSL_DEPRECATE_AND_INLINE() = absl::optional_ref<T>;

namespace common_internal {

template <typename T>
[[nodiscard]]
std::optional<std::decay_t<T>> AsOptional(absl::optional_ref<T> ref) {
  if (ref.has_value()) {
    return *ref;
  }
  return std::nullopt;
}

template <typename T>
[[nodiscard]]
std::optional<T> AsOptional(std::optional<T> opt) {
  return opt;
}

}  // namespace common_internal

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_OPTIONAL_REF_H_
