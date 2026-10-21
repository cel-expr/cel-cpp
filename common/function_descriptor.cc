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
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/functional/overload.h"
#include "absl/types/span.h"
#include "common/ast.h"
#include "common/kind.h"
#include "common/type.h"

namespace cel {

namespace common_internal {

// Structural comparison shared by descriptor equality and ordering. DebugString
// is lossy for nested types and can distinguish equal TypeType instances.
// Empty legacy types must be handled before inspecting names or parameters.
int CompareFunctionTypes(const Type& lhs, const Type& rhs) {
  if (lhs.kind() != rhs.kind()) {
    return lhs.kind() < rhs.kind() ? -1 : 1;
  }
  bool lhs_present = true;
  bool rhs_present = true;
  if (lhs.IsStruct()) {
    lhs_present = static_cast<bool>(lhs.GetStruct());
    rhs_present = static_cast<bool>(rhs.GetStruct());
  } else if (lhs.IsOpaque()) {
    lhs_present = static_cast<bool>(lhs.GetOpaque());
    rhs_present = static_cast<bool>(rhs.GetOpaque());
  } else if (lhs.IsFunction()) {
    lhs_present = static_cast<bool>(lhs.GetFunction());
    rhs_present = static_cast<bool>(rhs.GetFunction());
  } else if (lhs.IsEnum()) {
    lhs_present = static_cast<bool>(lhs.GetEnum());
    rhs_present = static_cast<bool>(rhs.GetEnum());
  }
  if (lhs_present != rhs_present) {
    return lhs_present ? 1 : -1;
  }
  if (!lhs_present || lhs.IsType()) {
    return 0;
  }
  if (lhs.IsStruct() || lhs.IsOpaque() || lhs.IsEnum() || lhs.IsTypeParam()) {
    if (int result = lhs.name().compare(rhs.name()); result != 0) {
      return result;
    }
  }
  if (lhs.IsList() || lhs.IsMap() || lhs.IsOpaque() || lhs.IsFunction()) {
    const auto lhs_parameters = lhs.GetParameters();
    const auto rhs_parameters = rhs.GetParameters();
    for (size_t i = 0;
         i < std::min(lhs_parameters.size(), rhs_parameters.size()); ++i) {
      if (int result =
              CompareFunctionTypes(lhs_parameters[i], rhs_parameters[i]);
          result != 0) {
        return result;
      }
    }
    if (lhs_parameters.size() != rhs_parameters.size()) {
      return lhs_parameters.size() < rhs_parameters.size() ? -1 : 1;
    }
  }
  return 0;
}

namespace {

enum class TypeMatchMode { kOverlap, kRuntime, kPreflight };

size_t NewNode(FunctionTypeMatchState& state, Type type, bool kind_only = false,
               std::vector<size_t> parameters = {}, bool atomic = false) {
  const size_t index = state.nodes.size();
  state.nodes.push_back(
      {type, std::move(parameters), index, kind_only, atomic});
  return index;
}

size_t Build(FunctionTypeMatchState& state, const Type& type, size_t scope,
             TypeMatchMode mode = TypeMatchMode::kOverlap) {
  if (scope >= state.parameter_scopes.size())
    state.parameter_scopes.resize(scope + 1);
  if (type.IsTypeParam()) {
    auto& parameters = state.parameter_scopes[scope];
    const std::string name(type.GetTypeParam().name());
    auto found = parameters.find(name);
    if (found != parameters.end()) {
      return found->second;
    }
    const size_t node = NewNode(state, DynType());
    parameters.emplace(name, node);
    return node;
  }
  if (type.IsDyn() || type.IsAny() ||
      (mode != TypeMatchMode::kRuntime && type.IsError())) {
    return NewNode(state, DynType());
  }
  if ((mode != TypeMatchMode::kRuntime &&
       (type.IsFunction() || type.IsEnum())) ||
      (type.IsStruct() && !type.GetStruct()) ||
      (type.IsOpaque() && !type.GetOpaque())) {
    return NewNode(state, type, true);
  }
  if (mode != TypeMatchMode::kOverlap && type.IsOpaque() &&
      !type.IsOptional()) {
    return AddFunctionTypeAtomic(state, type);
  }
  std::vector<size_t> parameters;
  if (type.IsList() || type.IsMap() || type.IsOpaque()) {
    for (const Type& parameter : type.GetParameters()) {
      parameters.push_back(Build(state, parameter, scope, mode));
    }
  }
  return NewNode(state, type, false, std::move(parameters));
}

std::vector<size_t> BuildSignature(
    FunctionTypeMatchState& state, absl::Span<const Type> types, size_t scope,
    TypeMatchMode mode = TypeMatchMode::kOverlap) {
  std::vector<size_t> signature;
  signature.reserve(types.size());
  for (const Type& type : types) {
    signature.push_back(Build(state, type, scope, mode));
  }
  return signature;
}

size_t Resolve(FunctionTypeMatchState& state, size_t node) {
  if (state.nodes[node].parent != node) {
    state.nodes[node].parent = Resolve(state, state.nodes[node].parent);
  }
  return state.nodes[node].parent;
}

bool Occurs(FunctionTypeMatchState& state, size_t parameter, size_t value) {
  std::vector<size_t> pending{value};
  std::vector<bool> visited(state.nodes.size(), false);
  while (!pending.empty()) {
    const size_t current = Resolve(state, pending.back());
    pending.pop_back();
    if (current == parameter) {
      return true;
    }
    if (!visited[current]) {
      visited[current] = true;
      pending.insert(pending.end(), state.nodes[current].parameters.begin(),
                     state.nodes[current].parameters.end());
    }
  }
  return false;
}

bool Bind(FunctionTypeMatchState& state, size_t from, size_t to) {
  from = Resolve(state, from);
  to = Resolve(state, to);
  if (from == to) {
    return true;
  }
  // A = list<A>, including cycles through other signatures, has no finite
  // solution. Check before merging either variable or structured nodes.
  if (!state.nodes[to].parameters.empty() && Occurs(state, from, to)) {
    return false;
  }
  state.nodes[from].parent = to;
  return true;
}

bool Unify(FunctionTypeMatchState& state, size_t lhs, size_t rhs) {
  lhs = Resolve(state, lhs);
  rhs = Resolve(state, rhs);
  if (lhs == rhs) {
    return true;
  }
  if (state.nodes[lhs].type.IsDyn()) {
    return Bind(state, lhs, rhs);
  }
  if (state.nodes[rhs].type.IsDyn()) {
    return Bind(state, rhs, lhs);
  }
  const Type& a = state.nodes[lhs].type;
  const Type& b = state.nodes[rhs].type;
  if (a.kind() != b.kind()) {
    return false;
  }
  if (state.nodes[lhs].kind_only) {
    return Bind(state, lhs, rhs);
  }
  if (state.nodes[rhs].kind_only) {
    return Bind(state, rhs, lhs);
  }
  // A custom opaque's complete Type cannot stand in for observable Optional
  // structure, even if its nominal Type happens to be identical.
  if (a.IsOpaque() && state.nodes[lhs].atomic != state.nodes[rhs].atomic) {
    return false;
  }
  if (state.nodes[lhs].atomic || state.nodes[rhs].atomic) {
    return CompareFunctionTypes(a, b) == 0 && Bind(state, lhs, rhs);
  }
  if ((a.IsStruct() || a.IsOpaque()) && a.name() != b.name()) {
    return false;
  }
  if (state.nodes[lhs].parameters.size() !=
      state.nodes[rhs].parameters.size()) {
    return false;
  }
  for (size_t i = 0; i < state.nodes[lhs].parameters.size(); ++i) {
    if (!Unify(state, state.nodes[lhs].parameters[i],
               state.nodes[rhs].parameters[i])) {
      return false;
    }
  }
  return Bind(state, lhs, rhs);
}

bool UnifySignatures(FunctionTypeMatchState& state,
                     const std::vector<size_t>& lhs,
                     const std::vector<size_t>& rhs) {
  for (size_t i = 0; i < lhs.size(); ++i) {
    if (!Unify(state, lhs[i], rhs[i])) {
      return false;
    }
  }
  return true;
}

}  // namespace

size_t NewFunctionTypeScope(FunctionTypeMatchState& state) {
  state.parameter_scopes.emplace_back();
  return state.parameter_scopes.size() - 1;
}

size_t AddFunctionTypeUnknown(FunctionTypeMatchState& state) {
  return NewNode(state, DynType());
}

size_t AddFunctionTypeAtomic(FunctionTypeMatchState& state, const Type& type) {
  return NewNode(state, type, false, {}, true);
}

size_t AddFunctionTypeStructure(FunctionTypeMatchState& state, const Type& type,
                                std::vector<size_t> parameters) {
  return NewNode(state, type, false, std::move(parameters));
}

size_t BuildFunctionRuntimeType(FunctionTypeMatchState& state, const Type& type,
                                size_t scope) {
  return Build(state, type, scope, TypeMatchMode::kRuntime);
}

bool ConstrainFunctionTypes(FunctionTypeMatchState& state, size_t lhs,
                            size_t rhs) {
  return Unify(state, lhs, rhs);
}

bool FunctionTypesOverlap(absl::Span<const Type> lhs,
                          absl::Span<const Type> rhs) {
  if (lhs.size() != rhs.size()) return false;
  FunctionTypeMatchState state;
  const auto left = BuildSignature(state, lhs, 0);
  const auto right = BuildSignature(state, rhs, 1);
  return UnifySignatures(state, left, right);
}

bool FunctionTypesOverlap(absl::Span<const Type> lhs,
                          absl::Span<const Type> rhs,
                          absl::Span<const Type> constraint) {
  if (lhs.size() != rhs.size() || lhs.size() != constraint.size()) return false;
  FunctionTypeMatchState state;
  const auto left = BuildSignature(state, lhs, 0);
  const auto right = BuildSignature(state, rhs, 1);
  const auto additional = BuildSignature(state, constraint, 2);
  return UnifySignatures(state, left, right) &&
         UnifySignatures(state, left, additional);
}

bool FunctionTypesMatchPreflight(absl::Span<const Type> implementation,
                                 absl::Span<const Type> query) {
  if (implementation.size() != query.size()) return false;
  FunctionTypeMatchState state;
  const auto declared =
      BuildSignature(state, implementation, 0, TypeMatchMode::kPreflight);
  const auto shallow = BuildSignature(state, query, 1);
  return UnifySignatures(state, declared, shallow);
}

bool FunctionTypesMatchPreflight(absl::Span<const Type> registration,
                                 absl::Span<const Type> implementation,
                                 absl::Span<const Type> query) {
  if (registration.size() != query.size() ||
      implementation.size() != query.size())
    return false;
  FunctionTypeMatchState state;
  const auto registered =
      BuildSignature(state, registration, 0, TypeMatchMode::kPreflight);
  const auto implemented =
      BuildSignature(state, implementation, 1, TypeMatchMode::kPreflight);
  const auto shallow = BuildSignature(state, query, 2);
  return UnifySignatures(state, registered, implemented) &&
         UnifySignatures(state, registered, shallow);
}

}  // namespace common_internal

namespace {

// Converts a span of Kinds to a vector of Types.
// Uses the implicit Type(Kind) constructor for conversion.
std::vector<Type> KindsToTypes(absl::Span<const Kind> kinds) {
  // Uses implicit Type(Kind) constructor for conversion.
  std::vector<Type> types(kinds.begin(), kinds.end());
  return types;
}

}  // namespace

const std::vector<Kind>& FunctionDescriptor::kinds() const { return kinds_; }

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

  return common_internal::FunctionTypesOverlap(this->types(), types);
}

bool FunctionDescriptor::operator==(const FunctionDescriptor& other) const {
  if (name() != other.name() || receiver_style() != other.receiver_style() ||
      types().size() != other.types().size()) {
    return false;
  }
  const auto& lhs_types = types();
  const auto& rhs_types = other.types();
  return std::equal(lhs_types.begin(), lhs_types.end(), rhs_types.begin(),
                    [](const Type& lhs, const Type& rhs) {
                      return common_internal::CompareFunctionTypes(lhs, rhs) ==
                             0;
                    });
}

bool FunctionDescriptor::operator<(const FunctionDescriptor& other) const {
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
  return std::lexicographical_compare(
      types().begin(), types().end(), other.types().begin(),
      other.types().end(), [](const Type& lhs, const Type& rhs) {
        return common_internal::CompareFunctionTypes(lhs, rhs) < 0;
      });
}

namespace {

[[nodiscard]]
Kind TypeSpecToKind(const TypeSpec& spec) {
  return std::visit(
      absl::Overload(
          [](UnsetTypeSpec) -> Kind { return Kind::kError; },
          [](const DynTypeSpec& spec) -> Kind { return Kind::kDyn; },
          [](const NullTypeSpec& spec) -> Kind { return Kind::kNull; },
          [](const PrimitiveType& spec) -> Kind {
            switch (spec) {
              case PrimitiveType::kPrimitiveTypeUnspecified:
                return Kind::kError;
              case PrimitiveType::kBool:
                return Kind::kBool;
              case PrimitiveType::kInt64:
                return Kind::kInt;
              case PrimitiveType::kUint64:
                return Kind::kUint;
              case PrimitiveType::kDouble:
                return Kind::kDouble;
              case PrimitiveType::kString:
                return Kind::kString;
              case PrimitiveType::kBytes:
                return Kind::kBytes;
            }
          },
          [](const PrimitiveTypeWrapper& spec) -> Kind {
            switch (spec.type()) {
              case PrimitiveType::kPrimitiveTypeUnspecified:
                return Kind::kError;
              case PrimitiveType::kBool:
                return Kind::kBoolWrapper;
              case PrimitiveType::kInt64:
                return Kind::kIntWrapper;
              case PrimitiveType::kUint64:
                return Kind::kUintWrapper;
              case PrimitiveType::kDouble:
                return Kind::kDoubleWrapper;
              case PrimitiveType::kString:
                return Kind::kStringWrapper;
              case PrimitiveType::kBytes:
                return Kind::kBytesWrapper;
            }
          },
          [](const WellKnownTypeSpec& spec) -> Kind {
            switch (spec) {
              case cel::WellKnownTypeSpec::kWellKnownTypeUnspecified:
                return Kind::kError;
              case cel::WellKnownTypeSpec::kDuration:
                return Kind::kError;
              case cel::WellKnownTypeSpec::kTimestamp:
                return Kind::kTimestamp;
              case cel::WellKnownTypeSpec::kAny:
                return Kind::kAny;
            }
          },
          [](const ListTypeSpec& spec) -> Kind { return Kind::kList; },
          [](const MapTypeSpec& spec) -> Kind { return Kind::kMap; },
          [](const FunctionTypeSpec& spec) -> Kind { return Kind::kFunction; },
          [](const MessageTypeSpec& spec) -> Kind { return Kind::kStruct; },
          [](const ParamTypeSpec& spec) -> Kind { return Kind::kTypeParam; },
          [](const std::unique_ptr<TypeSpec>& spec) -> Kind {
            return Kind::kType;
          },
          [](const ErrorTypeSpec& spec) -> Kind { return Kind::kError; },
          [](const AbstractType& spec) -> Kind { return Kind::kOpaque; }),
      spec.type_kind());
}

}  // namespace

std::vector<Kind> FunctionDescriptor::FunctionTypeSpecToArgumentKinds(
    const FunctionTypeSpec& spec) {
  std::vector<Kind> kinds;
  kinds.reserve(spec.arg_types().size());
  for (const auto& arg : spec.arg_types()) {
    kinds.push_back(TypeSpecToKind(arg));
  }
  return kinds;
}

}  // namespace cel
