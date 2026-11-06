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

// IWYU pragma: private, include "common/type.h"
// IWYU pragma: friend "common/type.h"

#ifndef THIRD_PARTY_CEL_CPP_COMMON_TYPES_UNSPECIALIZED_TYPE_H_
#define THIRD_PARTY_CEL_CPP_COMMON_TYPES_UNSPECIALIZED_TYPE_H_

#include <ostream>
#include <string>
#include <utility>

#include "absl/strings/string_view.h"
#include "common/type_kind.h"

namespace cel {

class Type;
class TypeParameters;

// `UnspecializedType` is a special type used by the type checker as a
// placeholder for an unconstrained type parameter that remains after
// specializing a parameterized (container) type, e.g. the element type of an
// empty list literal `[]` or of an empty optional `optional.none()`.
//
// It is assignable to and from any type like `dyn`, but unlike `dyn` it does
// not widen inferred type bindings: when unified with a more specific type,
// the more specific type is preferred. It is converted to `dyn` when
// serialized (e.g. to `TypeSpec` or `google.api.expr.Type`) for backward
// compatibility.
//
// IMPORTANT: This type has special meaning to the type checker and should
// generally not be used by clients. Prefer `DynType` when declaring types.
class UnspecializedType final {
 public:
  static constexpr TypeKind kKind = TypeKind::kUnspecialized;
  static constexpr absl::string_view kName = "*unspecialized*";

  UnspecializedType() = default;
  UnspecializedType(const UnspecializedType&) = default;
  UnspecializedType(UnspecializedType&&) = default;
  UnspecializedType& operator=(const UnspecializedType&) = default;
  UnspecializedType& operator=(UnspecializedType&&) = default;

  static TypeKind kind() { return kKind; }

  static absl::string_view name() { return kName; }

  static TypeParameters GetParameters();

  static std::string DebugString() { return std::string(name()); }
};

constexpr bool operator==(UnspecializedType, UnspecializedType) { return true; }

constexpr bool operator!=(UnspecializedType lhs, UnspecializedType rhs) {
  return !operator==(lhs, rhs);
}

template <typename H>
H AbslHashValue(H state, UnspecializedType) {
  // UnspecializedType is really a singleton and all instances are equal.
  // Nothing to hash.
  return std::move(state);
}

inline std::ostream& operator<<(std::ostream& out,
                                const UnspecializedType& type) {
  return out << type.DebugString();
}

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMMON_TYPES_UNSPECIALIZED_TYPE_H_
