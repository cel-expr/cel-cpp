// Copyright 2023 Google LLC
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

#include "common/function_descriptor.h"

#include <algorithm>
#include <cstddef>

#include "absl/base/macros.h"
#include "absl/container/flat_hash_map.h"
#include "absl/types/span.h"
#include "common/kind.h"
#include "common/type.h"

namespace cel {

namespace {

// Recursive type matching with TypeParam binding support.
// Returns true if types match (i.e., they conflict and cannot coexist).
//
// Matching semantics:
// - dyn/any act as wildcards that match any type
// - TypeParam requires consistent binding across all arguments
// - Container types (list, map, opaque) are matched recursively
//
// The bindings map tracks TypeParam name -> bound Type mappings to ensure
// consistent binding. For example, (A, A) matches (int, int) but not
// (int, string) because A cannot bind to both int and string.
bool TypeMatches(const Type& a, const Type& b,
                     absl::flat_hash_map<std::string, Type>& bindings) {
  // Wildcard types match anything (consistent with type checker's IsWildCardType)
  // - dyn: dynamic type, defers type checking to runtime
  // - any: legacy wildcard type
  // - error: error type propagates through expressions
  if (a.IsDyn() || b.IsDyn()) {
    return true;
  }
  if (a.IsAny() || b.IsAny()) {
    return true;
  }
  if (a.IsError() || b.IsError()) {
    return true;
  }

  // TypeParam handling - requires consistent binding
  if (a.IsTypeParam()) {
    std::string name(a.GetTypeParam().name());
    auto it = bindings.find(name);
    if (it != bindings.end()) {
      // Already bound - check consistency with the bound type
      return TypeMatches(it->second, b, bindings);
    } else {
      // Not yet bound - bind to b and return match
      bindings[name] = b;
      return true;
    }
  }
  if (b.IsTypeParam()) {
    std::string name(b.GetTypeParam().name());
    auto it = bindings.find(name);
    if (it != bindings.end()) {
      // Already bound - check consistency with the bound type
      return TypeMatches(a, it->second, bindings);
    } else {
      // Not yet bound - bind to a and return match
      bindings[name] = a;
      return true;
    }
  }

  // Different kinds don't match (e.g., int vs string)
  TypeKind a_kind = a.kind();
  TypeKind b_kind = b.kind();
  if (a_kind != b_kind) {
    return false;
  }

  // Same kind - check type-specific details
  switch (a_kind) {
    case TypeKind::kList: {
      // Recursively check element types
      return TypeMatches(a.GetList().GetElement(),
                             b.GetList().GetElement(), bindings);
    }
    case TypeKind::kMap: {
      // Recursively check key and value types
      return TypeMatches(a.GetMap().GetKey(), b.GetMap().GetKey(),
                             bindings) &&
             TypeMatches(a.GetMap().GetValue(), b.GetMap().GetValue(),
                             bindings);
    }
    case TypeKind::kStruct: {
      // Empty StructType acts as wildcard for any struct
      StructType struct_a = a.GetStruct();
      StructType struct_b = b.GetStruct();
      if (!struct_a || !struct_b) {
        // Empty struct matches any struct
        return true;
      }
      // Both have names - must match exactly
      return struct_a.name() == struct_b.name();
    }
    case TypeKind::kOpaque: {
      // Opaque types (including Optional) - must have same name and
      // recursively matching parameters
      OpaqueType opaque_a = a.GetOpaque();
      OpaqueType opaque_b = b.GetOpaque();
      // Empty OpaqueType acts as wildcard for any opaque
      if (!opaque_a || !opaque_b) {
        return true;
      }
      if (opaque_a.name() != opaque_b.name()) {
        return false;
      }
      TypeParameters params_a = opaque_a.GetParameters();
      TypeParameters params_b = opaque_b.GetParameters();
      if (params_a.size() != params_b.size()) {
        return false;
      }
      for (size_t i = 0; i < params_a.size(); i++) {
        if (!TypeMatches(params_a[i], params_b[i], bindings)) {
          return false;
        }
      }
      return true;
    }
    default:
      // Basic types with same kind always match
      return true;
  }
}

// Converts a span of Kinds to a vector of Types.
// Uses the implicit Type(Kind) constructor for conversion.
std::vector<Type> KindsToTypes(absl::Span<const Kind> kinds) {
  // Uses implicit Type(Kind) constructor for conversion.
  std::vector<Type> types(kinds.begin(), kinds.end());
  return types;
}

}  // namespace

const std::vector<Kind>& FunctionDescriptor::kinds() const {
  return impl_->kinds;
}

bool FunctionDescriptor::ShapeMatches(bool receiver_style,
                                      absl::Span<const Kind> types) const {
  // Convert Kinds to Types and delegate to the Type-based version.
  return ShapeMatches(receiver_style, KindsToTypes(types));
}

bool FunctionDescriptor::ShapeMatches(bool receiver_style,
                                      absl::Span<const Type> types) const {
  if (this->receiver_style() != receiver_style) {
    return false;
  }

  if (this->types().size() != types.size()) {
    return false;
  }

  // Use type-level matching with consistent TypeParam binding.
  // The binding context is shared across all arguments to ensure
  // TypeParam consistency (e.g., (A, A) requires both args to be same type).
  absl::flat_hash_map<std::string, Type> bindings;

  for (size_t i = 0; i < this->types().size(); i++) {
    if (!TypeMatches(this->types()[i], types[i], bindings)) {
      return false;
    }
  }
  return true;
}

bool FunctionDescriptor::operator==(const FunctionDescriptor& other) const {
  if (impl_.get() == other.impl_.get()) {
    return true;
  }
  if (name() != other.name() ||
      receiver_style() != other.receiver_style() ||
      types().size() != other.types().size()) {
    return false;
  }
  // Compare using Kind for backward compatibility.
  // Full Type comparison can be added later if needed.
  const auto& lhs_types = types();
  const auto& rhs_types = other.types();
  return std::equal(lhs_types.begin(), lhs_types.end(), rhs_types.begin());
}

bool FunctionDescriptor::operator<(const FunctionDescriptor& other) const {
  if (impl_.get() == other.impl_.get()) {
    return false;
  }
  if (name() < other.name()) {
    return true;
  }
  if (name() != other.name()) {
    return false;
  }
  if (receiver_style() < other.receiver_style()) {
    return true;
  }
  if (receiver_style() != other.receiver_style()) {
    return false;
  }
  auto lhs_begin = types().begin();
  auto lhs_end = types().end();
  auto rhs_begin = other.types().begin();
  auto rhs_end = other.types().end();
  while (lhs_begin != lhs_end && rhs_begin != rhs_end) {
    // Compare types lexicographically using DebugString as stable ordering
    // This ensures consistent ordering for all Type variants
    if (lhs_begin->DebugString() < rhs_begin->DebugString()) {
      return true;
    }
    if (!(*lhs_begin == *rhs_begin)) {
      return false;
    }
    lhs_begin++;
    rhs_begin++;
  }
  if (lhs_begin == lhs_end && rhs_begin == rhs_end) {
    // Neither has any elements left, they are equal.
    return false;
  }
  if (lhs_begin == lhs_end) {
    // Left has no more elements. Right is greater.
    return true;
  }
  // Right has no more elements. Left is greater.
  ABSL_ASSERT(rhs_begin == rhs_end);
  return false;
}

}  // namespace cel
