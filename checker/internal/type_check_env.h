// Copyright 2024 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef THIRD_PARTY_CEL_CPP_CHECKER_INTERNAL_TYPE_CHECK_ENV_H_
#define THIRD_PARTY_CEL_CPP_CHECKER_INTERNAL_TYPE_CHECK_ENV_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/functional/function_ref.h"
#include "absl/log/absl_check.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "checker/internal/proto_type_mask.h"
#include "checker/internal/proto_type_mask_registry.h"
#include "common/constant.h"
#include "common/container.h"
#include "common/decl.h"
#include "common/descriptor_pool_type_introspector.h"
#include "common/type.h"
#include "common/type_introspector.h"
#include "internal/status_macros.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"

namespace cel::checker_internal {

class TypeCheckEnv;

// Helper class for managing nested scopes and the local variables they
// implicitly declare.
//
// Nested scopes have a lifetime dependency on any parent scopes and should
// generally be managed by unique_ptrs.
class VariableScope {
 public:
  explicit VariableScope() : parent_(nullptr) {}

  VariableScope(const VariableScope&) = delete;
  VariableScope& operator=(const VariableScope&) = delete;
  VariableScope(VariableScope&&) = default;
  VariableScope& operator=(VariableScope&&) = default;

  bool InsertVariableIfAbsent(VariableDecl decl) {
    return variables_.insert({decl.name(), std::move(decl)}).second;
  }

  // Updates the type of a variable declared directly in this scope. Returns
  // false if absent. The decl is updated in place, so existing pointers to it
  // remain valid.
  bool UpdateLocalVariableType(absl::string_view name, Type type) {
    auto it = variables_.find(name);
    if (it == variables_.end()) {
      return false;
    }
    it->second.set_type(std::move(type));
    return true;
  }

  std::unique_ptr<VariableScope> MakeNestedScope() const
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return absl::WrapUnique(new VariableScope(this));
  }

  const VariableDecl* absl_nullable LookupLocalVariable(
      absl::string_view name) const;

  // Calls `f` for each variable declared in this scope or its parent scopes.
  // Shadowed declarations are included.
  void ForEachVisibleVariable(
      absl::FunctionRef<void(const VariableDecl&)> f) const {
    for (const VariableScope* scope = this; scope != nullptr;
         scope = scope->parent_) {
      for (const auto& entry : scope->variables_) {
        f(entry.second);
      }
    }
  }

 private:
  explicit VariableScope(
      const VariableScope* parent ABSL_ATTRIBUTE_LIFETIME_BOUND)
      : parent_(parent) {}

  const VariableScope* absl_nullable parent_;
  absl::flat_hash_map<std::string, VariableDecl> variables_;
};

// Class managing the state of the type check environment.
//
// Maintains lookup maps for variables and functions and the set of type
// providers.
//
// This class is thread-compatible.
class TypeCheckEnv {
 private:
  using VariableDeclPtr = const VariableDecl* absl_nonnull;
  using FunctionDeclPtr = const FunctionDecl* absl_nonnull;

 public:
  explicit TypeCheckEnv(
      absl_nonnull std::shared_ptr<const google::protobuf::DescriptorPool>
          descriptor_pool)
      : descriptor_pool_(std::move(descriptor_pool)),
        proto_type_introspector_(
            std::make_shared<DescriptorPoolTypeIntrospector>(
                descriptor_pool_.get())) {
    type_providers_.push_back(
        std::make_shared<cel::WellKnownTypeIntrospector>());
    type_providers_.push_back(proto_type_introspector_);
  }

  TypeCheckEnv(const TypeCheckEnv&) = default;
  TypeCheckEnv& operator=(const TypeCheckEnv&) = default;
  TypeCheckEnv(TypeCheckEnv&&) = default;
  TypeCheckEnv& operator=(TypeCheckEnv&&) = default;

  const ExpressionContainer& container() const { return container_; }

  void set_container(ExpressionContainer container) {
    container_ = std::move(container);
  }

  const DescriptorPoolTypeIntrospector& proto_type_introspector() const {
    return *proto_type_introspector_;
  }
  DescriptorPoolTypeIntrospector& proto_type_introspector() {
    return *proto_type_introspector_;
  }

  void set_expected_type(const Type& type) { expected_type_ = std::move(type); }

  const absl::optional<Type>& expected_type() const { return expected_type_; }

  absl::Span<const std::shared_ptr<const TypeIntrospector>> type_providers()
      const {
    return type_providers_;
  }

  void AddTypeProvider(std::unique_ptr<TypeIntrospector> provider) {
    type_providers_.push_back(std::move(provider));
  }

  void AddTypeProvider(std::shared_ptr<const TypeIntrospector> provider) {
    type_providers_.push_back(std::move(provider));
  }

  const absl::flat_hash_map<std::string, VariableDecl>& variables() const {
    return variables_;
  }

  // Inserts a variable declaration into the environment of the current scope if
  // is is not already present. Parent scopes are not searched.
  //
  // Returns true if the variable was inserted, false otherwise.
  bool InsertVariableIfAbsent(VariableDecl decl) {
    return variables_.insert({decl.name(), std::move(decl)}).second;
  }

  // Inserts a variable declaration into the environment of the current scope.
  // Parent scopes are not searched.
  void InsertOrReplaceVariable(VariableDecl decl) {
    variables_[decl.name()] = std::move(decl);
  }

  absl::Status CreateProtoTypeMaskRegistry(
      const std::vector<ProtoTypeMask>& proto_type_masks) {
    CEL_ASSIGN_OR_RETURN(proto_type_mask_registry_,
                         ProtoTypeMaskRegistry::Create(descriptor_pool_.get(),
                                                       proto_type_masks));
    return absl::OkStatus();
  }

  const absl::flat_hash_map<std::string, FunctionDecl>& functions() const {
    return functions_;
  }

  // Inserts a function declaration into the environment of the current scope if
  // is is not already present. Parent scopes are not searched (allowing for
  // shadowing).
  //
  // Returns true if the decl was inserted, false otherwise.
  bool InsertFunctionIfAbsent(FunctionDecl decl) {
    return functions_.insert({decl.name(), std::move(decl)}).second;
  }

  void InsertOrReplaceFunction(FunctionDecl decl) {
    functions_[decl.name()] = std::move(decl);
  }

  // Returns the declaration for the given name if it is found in the current
  // or any parent scope.
  // Note: the returned declaration ptr is only valid as long as no changes are
  // made to the environment.
  const VariableDecl* absl_nullable LookupVariable(
      absl::string_view name) const;
  const FunctionDecl* absl_nullable LookupFunction(
      absl::string_view name) const;

  absl::StatusOr<absl::optional<Type>> LookupTypeName(
      absl::string_view name) const;

  absl::StatusOr<absl::optional<StructTypeField>> LookupStructField(
      absl::string_view type_name, absl::string_view field_name) const;

  absl::StatusOr<absl::optional<VariableDecl>> LookupTypeConstant(
      google::protobuf::Arena* absl_nonnull arena, absl::string_view type_name) const;

  const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool() const {
    return descriptor_pool_.get();
  }

  // Used to keep an arena alive if one was needed to allocate types.
  //
  // Expected to be called exactly once if at all.
  void set_arena(std::shared_ptr<google::protobuf::Arena> arena) {
    ABSL_DCHECK(arena_ == nullptr || arena == arena_);
    arena_ = std::move(arena);
  }

  // Returns the arena if one was set, nullptr otherwise.
  std::shared_ptr<google::protobuf::Arena> arena() const { return arena_; }

 private:
  absl::StatusOr<absl::optional<VariableDecl>> LookupEnumConstant(
      absl::string_view type, absl::string_view value) const;

  absl_nonnull std::shared_ptr<const google::protobuf::DescriptorPool> descriptor_pool_;

  // If set, an arena was needed to allocate types in the environment.
  //
  // The TypeCheckEnv does not otherwise use the arena, though it may be used by
  // derived TypeCheckerBuilders.
  absl_nullable std::shared_ptr<google::protobuf::Arena> arena_;
  ExpressionContainer container_;

  // Used to resolve fields on message types.
  std::shared_ptr<DescriptorPoolTypeIntrospector> proto_type_introspector_;

  // Maps fully qualified names to declarations.
  absl::flat_hash_map<std::string, VariableDecl> variables_;
  absl::flat_hash_map<std::string, FunctionDecl> functions_;

  std::shared_ptr<ProtoTypeMaskRegistry> proto_type_mask_registry_;

  // Type providers for custom types.
  std::vector<std::shared_ptr<const TypeIntrospector>> type_providers_;

  absl::optional<Type> expected_type_;
};

}  // namespace cel::checker_internal

#endif  // THIRD_PARTY_CEL_CPP_CHECKER_INTERNAL_TYPE_CHECK_ENV_H_
