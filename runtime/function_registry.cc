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

#include "runtime/function_registry.h"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/node_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "common/function_descriptor.h"
#include "common/kind.h"
#include "common/type.h"
#include "runtime/activation_interface.h"
#include "runtime/function.h"
#include "runtime/function_overload_reference.h"
#include "runtime/function_provider.h"

namespace cel {
namespace {

// Impl for simple provider that looks up functions in an activation function
// registry.
class ActivationFunctionProviderImpl
    : public cel::runtime_internal::FunctionProvider {
 public:
  ActivationFunctionProviderImpl() = default;

  absl::StatusOr<std::optional<FunctionOverloadReference>> GetFunction(
      const cel::FunctionDescriptor& descriptor,
      const cel::ActivationInterface& activation) const override {
    // Branch 1: If descriptor has overload_id, use O(1) precise lookup
    if (descriptor.has_overload_id()) {
      return activation.FindFunctionOverloadById(descriptor.name(),
                                             descriptor.overload_id());
    }

    // Branch 2: No overload_id, fallback to signature matching (legacy logic)
    std::vector<cel::FunctionOverloadReference> overloads =
        activation.FindFunctionOverloads(descriptor.name());

    std::optional<FunctionOverloadReference> matching_overload = std::nullopt;

    for (const auto& overload : overloads) {
      if (overload.descriptor.ShapeMatches(descriptor)) {
        if (matching_overload.has_value()) {
          return absl::Status(absl::StatusCode::kInvalidArgument,
                              "Couldn't resolve function.");
        }
        matching_overload.emplace(overload);
      }
    }

    return matching_overload;
  }
};

// Create a CelFunctionProvider that just looks up the functions inserted in the
// Activation. This is a convenience implementation for a simple, common
// use-case.
std::unique_ptr<cel::runtime_internal::FunctionProvider>
CreateActivationFunctionProvider() {
  return std::make_unique<ActivationFunctionProviderImpl>();
}

}  // namespace

absl::Status FunctionRegistry::Register(
    const cel::FunctionDescriptor& descriptor,
    std::unique_ptr<cel::Function> implementation) {
  if (DescriptorRegistered(descriptor)) {
    return absl::Status(
        absl::StatusCode::kAlreadyExists,
        absl::StrCat("CelFunction with specified parameters already registered: ",
                     descriptor.name()));
  }
  if (!ValidateNonStrictOverload(descriptor)) {
    return absl::Status(absl::StatusCode::kAlreadyExists,
                        absl::StrCat("Only one overload is allowed for non-strict function: ",
                                     descriptor.name()));
  }

  // Check for overload ID conflicts within this function name
  if (descriptor.has_overload_id() &&
      IsOverloadIdConflict(descriptor.name(), descriptor.overload_id())) {
    return absl::Status(
        absl::StatusCode::kAlreadyExists,
        absl::StrCat("Overload ID already registered: ",
                     descriptor.overload_id()));
  }

  auto& overloads = functions_[descriptor.name()];
  overloads.static_overloads.push_back(
      StaticFunctionEntry(descriptor, std::move(implementation)));

  // Add to overload ID index if overload_id is present
  if (descriptor.has_overload_id()) {
    StaticFunctionEntry* entry = &overloads.static_overloads.back();
    overloads.static_overloads_by_id[descriptor.overload_id()] = entry;
  }

  return absl::OkStatus();
}

absl::Status FunctionRegistry::RegisterLazyFunction(
    const cel::FunctionDescriptor& descriptor) {
  if (DescriptorRegistered(descriptor)) {
    return absl::Status(
        absl::StatusCode::kAlreadyExists,
        absl::StrCat("CelFunction with specified parameters already registered: ",
                     descriptor.name()));
  }
  if (!ValidateNonStrictOverload(descriptor)) {
    return absl::Status(absl::StatusCode::kAlreadyExists,
                        absl::StrCat("Only one overload is allowed for non-strict function: ",
                                     descriptor.name()));
  }

  // Check for overload ID conflicts within this function name
  if (descriptor.has_overload_id() &&
      IsOverloadIdConflict(descriptor.name(), descriptor.overload_id())) {
    return absl::Status(
        absl::StatusCode::kAlreadyExists,
        absl::StrCat("Overload ID already registered: ",
                     descriptor.overload_id()));
  }

  auto& overloads = functions_[descriptor.name()];

  overloads.lazy_overloads.push_back(
      LazyFunctionEntry(descriptor, CreateActivationFunctionProvider()));

  // Add to overload ID index if overload_id is present
  if (descriptor.has_overload_id()) {
    LazyFunctionEntry* entry = &overloads.lazy_overloads.back();
    overloads.lazy_overloads_by_id[descriptor.overload_id()] = entry;
  }

  return absl::OkStatus();
}

std::vector<cel::FunctionOverloadReference>
FunctionRegistry::FindStaticOverloads(absl::string_view name,
                                      bool receiver_style,
                                      absl::Span<const cel::Kind> kinds) const {
  std::vector<cel::Type> types;
  types.reserve(kinds.size());
  for (const auto& kind : kinds) {
    types.push_back(cel::Type(kind));
  }
  return FindStaticOverloadsByTypes(name, receiver_style, types);
}

std::vector<cel::FunctionOverloadReference>
FunctionRegistry::FindStaticOverloadsByTypes(absl::string_view name,
                                     bool receiver_style,
                                     absl::Span<const cel::Type> types) const {
  std::vector<cel::FunctionOverloadReference> matched_funcs;

  auto overloads = functions_.find(name);
  if (overloads == functions_.end()) {
    return matched_funcs;
  }

  for (const auto& overload : overloads->second.static_overloads) {
    if (overload.descriptor->ShapeMatches(receiver_style, types)) {
      matched_funcs.push_back({*overload.descriptor, *overload.implementation});
    }
  }

  return matched_funcs;
}

absl::optional<cel::FunctionOverloadReference>
FunctionRegistry::FindStaticOverloadById(absl::string_view name,
                                     absl::string_view overload_id) const {
  auto functions_it = functions_.find(name);
  if (functions_it == functions_.end()) {
    return absl::nullopt;
  }

  const RegistryEntry& entry = functions_it->second;
  auto it = entry.static_overloads_by_id.find(overload_id);
  if (it == entry.static_overloads_by_id.end()) {
    return absl::nullopt;
  }

  const StaticFunctionEntry* func_entry = it->second;
  return cel::FunctionOverloadReference{*func_entry->descriptor,
                                        *func_entry->implementation};
}


std::vector<cel::FunctionOverloadReference>
FunctionRegistry::FindStaticOverloadsByArity(absl::string_view name,
                                             bool receiver_style,
                                             size_t arity) const {
  std::vector<cel::FunctionOverloadReference> matched_funcs;

  auto overloads = functions_.find(name);
  if (overloads == functions_.end()) {
    return matched_funcs;
  }

  for (const auto& overload : overloads->second.static_overloads) {
    if (overload.descriptor->receiver_style() == receiver_style &&
        overload.descriptor->types().size() == arity) {
      matched_funcs.push_back({*overload.descriptor, *overload.implementation});
    }
  }

  return matched_funcs;
}

std::vector<FunctionRegistry::LazyOverload> FunctionRegistry::FindLazyOverloads(
    absl::string_view name, bool receiver_style,
    absl::Span<const cel::Kind> kinds) const {
  std::vector<cel::Type> types;
  types.reserve(kinds.size());
  for (const auto& kind : kinds) {
    types.push_back(cel::Type(kind));
  }
  return FindLazyOverloadsByTypes(name, receiver_style, types);
}

std::vector<FunctionRegistry::LazyOverload> FunctionRegistry::FindLazyOverloadsByTypes(
    absl::string_view name,
    bool receiver_style,
    absl::Span<const cel::Type> types) const {
  std::vector<FunctionRegistry::LazyOverload> matched_funcs;

  auto overloads = functions_.find(name);
  if (overloads == functions_.end()) {
    return matched_funcs;
  }

  for (const auto& entry : overloads->second.lazy_overloads) {
    if (entry.descriptor->ShapeMatches(receiver_style, types)) {
      matched_funcs.push_back({*entry.descriptor, *entry.function_provider});
    }
  }

  return matched_funcs;
}

absl::optional<FunctionRegistry::LazyOverload>
FunctionRegistry::FindLazyOverloadById(absl::string_view name,
                                   absl::string_view overload_id) const {
  auto functions_it = functions_.find(name);
  if (functions_it == functions_.end()) {
    return absl::nullopt;
  }

  const RegistryEntry& entry = functions_it->second;
  auto it = entry.lazy_overloads_by_id.find(overload_id);
  if (it == entry.lazy_overloads_by_id.end()) {
    return absl::nullopt;
  }

  const LazyFunctionEntry* func_entry = it->second;
  return LazyOverload{*func_entry->descriptor, *func_entry->function_provider};
}

std::vector<FunctionRegistry::LazyOverload>
FunctionRegistry::FindLazyOverloadsByArity(absl::string_view name,
                                           bool receiver_style,
                                           size_t arity) const {
  std::vector<FunctionRegistry::LazyOverload> matched_funcs;

  auto overloads = functions_.find(name);
  if (overloads == functions_.end()) {
    return matched_funcs;
  }

  for (const auto& entry : overloads->second.lazy_overloads) {
    if (entry.descriptor->receiver_style() == receiver_style &&
        entry.descriptor->types().size() == arity) {
      matched_funcs.push_back({*entry.descriptor, *entry.function_provider});
    }
  }

  return matched_funcs;
}

absl::node_hash_map<std::string, std::vector<const cel::FunctionDescriptor*>>
FunctionRegistry::ListFunctions() const {
  absl::node_hash_map<std::string, std::vector<const cel::FunctionDescriptor*>>
      descriptor_map;

  for (const auto& entry : functions_) {
    std::vector<const cel::FunctionDescriptor*> descriptors;
    const RegistryEntry& function_entry = entry.second;
    descriptors.reserve(function_entry.static_overloads.size() +
                        function_entry.lazy_overloads.size());
    for (const auto& entry : function_entry.static_overloads) {
      descriptors.push_back(entry.descriptor.get());
    }
    for (const auto& entry : function_entry.lazy_overloads) {
      descriptors.push_back(entry.descriptor.get());
    }
    descriptor_map[entry.first] = std::move(descriptors);
  }

  return descriptor_map;
}

bool FunctionRegistry::DescriptorRegistered(
    const cel::FunctionDescriptor& descriptor) const {
  auto overloads = functions_.find(descriptor.name());
  if (overloads == functions_.end()) {
    return false;
  }
  const RegistryEntry& entry = overloads->second;
  for (const auto& static_ovl : entry.static_overloads) {
    if (static_ovl.descriptor->ShapeMatches(descriptor)) {
      return true;
    }
  }
  for (const auto& lazy_ovl : entry.lazy_overloads) {
    if (lazy_ovl.descriptor->ShapeMatches(descriptor)) {
      return true;
    }
  }
  return false;
}

bool FunctionRegistry::ValidateNonStrictOverload(
    const cel::FunctionDescriptor& descriptor) const {
  auto overloads = functions_.find(descriptor.name());
  if (overloads == functions_.end()) {
    return true;
  }
  const RegistryEntry& entry = overloads->second;
  if (!descriptor.is_strict()) {
    // If the newly added overload is a non-strict function, we require that
    // there are no other overloads, which is not possible here.
    return false;
  }
  // If the newly added overload is a strict function, we need to make sure
  // that no previous overloads are registered non-strict. If the list of
  // overload is not empty, we only need to check the first overload. This is
  // because if the first overload is strict, other overloads must also be
  // strict by the rule.
  return (entry.static_overloads.empty() ||
          entry.static_overloads[0].descriptor->is_strict()) &&
         (entry.lazy_overloads.empty() ||
          entry.lazy_overloads[0].descriptor->is_strict());
}

bool FunctionRegistry::IsOverloadIdConflict(
    absl::string_view name, absl::string_view overload_id) const {
  auto it = functions_.find(name);
  if (it == functions_.end()) {
    return false;
  }
  const RegistryEntry& entry = it->second;
  return entry.static_overloads_by_id.contains(overload_id) ||
         entry.lazy_overloads_by_id.contains(overload_id);
}

}  // namespace cel
