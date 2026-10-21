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

#ifndef THIRD_PARTY_CEL_CPP_COMMON_FUNCTION_DESCRIPTOR_H_
#define THIRD_PARTY_CEL_CPP_COMMON_FUNCTION_DESCRIPTOR_H_

#include <cstddef>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "common/ast.h"
#include "common/kind.h"
#include "common/type.h"

namespace cel {

struct FunctionDescriptorOptions {
  // If true (strict, default), error or unknown arguments are propagated
  // instead of calling the function. if false (non-strict), the function may
  // receive error or unknown values as arguments.
  bool is_strict = true;

  // Whether the function is impure or context-sensitive.
  //
  // Impure functions depend on state other than the arguments received during
  // the CEL expression evaluation or have visible side effects. This breaks
  // some of the assumptions of the CEL evaluation model. This flag is used as a
  // hint to the planner that some optimizations are not safe or not effective.
  bool is_contextual = false;
};

// Coarsely describes a function for the purpose of runtime resolution of
// overloads.
//
// Internally stores full Type information to enable type-level overload
// resolution.
class FunctionDescriptor {
 public:
  // Creates a FunctionDescriptor for a global function.
  [[nodiscard]]
  static FunctionDescriptor Global(absl::string_view name,
                                   absl::string_view overload_id,
                                   const FunctionTypeSpec& type,
                                   FunctionDescriptorOptions options = {}) {
    return FunctionDescriptor(name, overload_id, type, options,
                              /*is_receiver_style=*/false);
  }

  // Creates a FunctionDescriptor for a member function.
  [[nodiscard]]
  static FunctionDescriptor Member(absl::string_view name,
                                   absl::string_view overload_id,
                                   const FunctionTypeSpec& type,
                                   FunctionDescriptorOptions options = {}) {
    return FunctionDescriptor(name, overload_id, type, options,
                              /*is_receiver_style=*/true);
  }

  FunctionDescriptor(const FunctionDescriptor&) = default;
  FunctionDescriptor(FunctionDescriptor&&) = default;
  FunctionDescriptor& operator=(const FunctionDescriptor&) = default;
  FunctionDescriptor& operator=(FunctionDescriptor&&) = default;

  FunctionDescriptor(absl::string_view name, bool receiver_style,
                     std::vector<Kind> types, bool is_strict)
      : FunctionDescriptor(name, receiver_style, std::move(types),
                           FunctionDescriptorOptions{is_strict,
                                                     /*is_contextual=*/false}) {
  }

  FunctionDescriptor(absl::string_view name, bool receiver_style,
                     std::vector<Kind> types, bool is_strict,
                     bool is_contextual)
      : FunctionDescriptor(
            name, receiver_style, std::move(types),
            FunctionDescriptorOptions{is_strict, is_contextual}) {}

  FunctionDescriptor(absl::string_view name, bool is_receiver_style,
                     std::vector<Kind> types,
                     FunctionDescriptorOptions options = {})
      : name_(name),
        kinds_(std::move(types)),
        is_receiver_style_(is_receiver_style),
        options_(std::move(options)) {
    types_.reserve(kinds_.size());
    for (Kind kind : kinds_) {
      types_.push_back(Type(kind));
    }
  }

  // Constructors for descriptors carrying full argument types.
  FunctionDescriptor(absl::string_view name, absl::string_view overload_id,
                     bool receiver_style, std::vector<Type> types,
                     bool is_strict)
      : FunctionDescriptor(name, overload_id, receiver_style, std::move(types),
                           FunctionDescriptorOptions{is_strict,
                                                     /*is_contextual=*/false}) {
  }

  FunctionDescriptor(absl::string_view name, absl::string_view overload_id,
                     bool receiver_style, std::vector<Type> types,
                     bool is_strict, bool is_contextual)
      : FunctionDescriptor(
            name, overload_id, receiver_style, std::move(types),
            FunctionDescriptorOptions{is_strict, is_contextual}) {}

  FunctionDescriptor(absl::string_view name, absl::string_view overload_id,
                     bool is_receiver_style, std::vector<Type> types,
                     FunctionDescriptorOptions options = {})
      : name_(name),
        overload_id_(overload_id),
        types_(std::move(types)),
        is_receiver_style_(is_receiver_style),
        options_(std::move(options)) {
    kinds_.reserve(types_.size());
    for (const Type& type : types_) {
      kinds_.push_back(TypeKindToKind(type.kind()));
    }
  }

  // Function name.
  const std::string& name() const { return name_; }

  // Function overload ID. This may be empty for legacy code.
  [[nodiscard]]
  absl::string_view overload_id() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return overload_id_;
  }

  // Whether this descriptor has an overload ID.
  bool has_overload_id() const { return !overload_id_.empty(); }

  // Whether function is receiver style i.e. true means arg0.name(args[1:]...).
  bool receiver_style() const { return is_receiver_style_; }

  // The argument types the function accepts, with full Type information.
  // This includes element types for containers (e.g., list<int> vs list<dyn>).
  const std::vector<Type>& types() const { return types_; }

  // The argument types the function accepts, as Kinds.
  const std::vector<Kind>& kinds() const;

  // if true (strict, default), error or unknown arguments are propagated
  // instead of calling the function. if false (non-strict), the function may
  // receive error or unknown values as arguments.
  bool is_strict() const { return options_.is_strict; }

  // Whether the function is contextual (impure).
  //
  // Contextual functions depend on state other than the arguments received in
  // the CEL expression evaluation or have visible side effects. This breaks
  // some of the assumptions of CEL. This flag is used as a hint to the planner
  // that some optimizations are not safe or not effective.
  bool is_contextual() const { return options_.is_contextual; }

  // Helper for matching a descriptor. This tests that the shape is the same --
  // |other| accepts the same number and types of arguments and is the same call
  // style).
  bool ShapeMatches(const FunctionDescriptor& other) const {
    return ShapeMatches(other.receiver_style(), other.types());
  }
  bool ShapeMatches(bool receiver_style, absl::Span<const Kind> types) const;
  // Preserves empty and Kind-element brace calls. Pointer/length braces need an
  // explicitly typed Span to distinguish Kind from Type elements.
  bool ShapeMatches(bool receiver_style,
                    std::initializer_list<Kind> types) const {
    return ShapeMatches(receiver_style,
                        absl::Span<const Kind>(types.begin(), types.size()));
  }
  bool ShapeMatches(bool receiver_style, absl::Span<const Type> types) const;

  // Descriptor identity compares the name, call style, and argument types.
  // Legacy empty types compare equal to the same empty type, and TypeType
  // parameters are ignored, consistent with Type equality. IDs and options
  // do not participate in identity or ordering.
  bool operator==(const FunctionDescriptor& other) const;

  bool operator<(const FunctionDescriptor& other) const;

 private:
  [[nodiscard]]
  static std::vector<Kind> FunctionTypeSpecToArgumentKinds(
      const FunctionTypeSpec& spec);

  FunctionDescriptor(absl::string_view name, absl::string_view overload_id,
                     const FunctionTypeSpec& type,
                     FunctionDescriptorOptions options, bool is_receiver_style)
      : FunctionDescriptor(name, is_receiver_style,
                           FunctionTypeSpecToArgumentKinds(type), options) {
    overload_id_ = overload_id;
  }

  std::string name_;
  std::string overload_id_;
  std::vector<Type> types_;
  std::vector<Kind> kinds_;
  bool is_receiver_style_;
  FunctionDescriptorOptions options_;
};

namespace common_internal {

// Runtime kind guards intentionally do not interpret generic parameters.
constexpr bool FunctionKindMatches(Kind expected, Kind actual) {
  return expected == actual || expected == Kind::kAny ||
         expected == Kind::kDyn || expected == Kind::kTypeParam;
}

// Structural Type identity, including safe empty legacy placeholders.
int CompareFunctionTypes(const Type& lhs, const Type& rhs);

// Each call uses fresh constraints and independently scoped type parameters.
bool FunctionTypesOverlap(absl::Span<const Type> lhs,
                          absl::Span<const Type> rhs);
bool FunctionTypesOverlap(absl::Span<const Type> lhs,
                          absl::Span<const Type> rhs,
                          absl::Span<const Type> constraint);

// Declarations preserve atomic opaque metadata. The shallow, kind-derived query
// carries no observed names or payload constraints.
bool FunctionTypesMatchPreflight(absl::Span<const Type> implementation,
                                 absl::Span<const Type> query);
bool FunctionTypesMatchPreflight(absl::Span<const Type> registration,
                                 absl::Span<const Type> implementation,
                                 absl::Span<const Type> query);

// Scratch data owned by one matching call. Signatures and observed evidence
// share these nodes while retaining independent type-parameter scopes.
struct FunctionTypeMatchState {
  struct Node {
    Type type;
    std::vector<size_t> parameters;
    size_t parent;
    // Empty struct/opaque placeholders constrain the kind but not the name
    // or parameters. Function and enum matching also only checks the kind.
    bool kind_only;
    bool atomic;
  };

  std::vector<absl::flat_hash_map<std::string, size_t>> parameter_scopes;
  std::vector<Node> nodes;
};

// Incremental runtime operations mutate only the supplied per-call state.
size_t NewFunctionTypeScope(FunctionTypeMatchState& state);
size_t AddFunctionTypeUnknown(FunctionTypeMatchState& state);
size_t AddFunctionTypeAtomic(FunctionTypeMatchState& state, const Type& type);
size_t AddFunctionTypeStructure(FunctionTypeMatchState& state, const Type& type,
                                std::vector<size_t> parameters);
size_t BuildFunctionRuntimeType(FunctionTypeMatchState& state, const Type& type,
                                size_t scope);
bool ConstrainFunctionTypes(FunctionTypeMatchState& state, size_t lhs,
                            size_t rhs);

}  // namespace common_internal

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMMON_FUNCTION_DESCRIPTOR_H_
