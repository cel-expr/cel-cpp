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

#include "runtime/internal/runtime_type_provider.h"

#include <optional>
#include <string>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "common/type.h"
#include "common/type_introspector.h"
#include "common/type_reflector.h"
#include "common/value.h"
#include "common/values/value_builder.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/message.h"

namespace cel::runtime_internal {

absl::Status RuntimeTypeProvider::RegisterType(const OpaqueType& type) {
  auto insertion = types_.insert(std::pair{type.name(), Type(type)});
  if (!insertion.second) {
    return absl::AlreadyExistsError(
        absl::StrCat("type already registered: ", insertion.first->first));
  }
  return absl::OkStatus();
}

absl::StatusOr<absl::optional<Type>> RuntimeTypeProvider::FindTypeImpl(
    absl::string_view name) const {
  auto type = FindWellKnownType(name);
  if (type.has_value()) {
    return type;
  }

  auto result = descriptor_pool_provider_.FindType(name);
  if (!result.ok()) {
    return result;
  }

  if (result->has_value() && !result->value().IsEnum()) {
    return result;
  }

  if (const auto it = types_.find(name); it != types_.end()) {
    return it->second;
  }
  return std::nullopt;
}

absl::StatusOr<absl::optional<TypeIntrospector::EnumConstant>>
RuntimeTypeProvider::FindEnumConstantImpl(absl::string_view type,
                                          absl::string_view value) const {
  auto enum_constant = FindWellKnownTypeEnumConstant(type, value);
  if (enum_constant.has_value()) {
    return enum_constant;
  }
  return descriptor_pool_provider_.FindEnumConstant(type, value);
}

absl::StatusOr<absl::optional<StructTypeField>>
RuntimeTypeProvider::FindStructTypeFieldByNameImpl(
    absl::string_view type, absl::string_view name) const {
  auto field = FindWellKnownTypeFieldByName(type, name);
  if (field.has_value()) {
    return field;
  }
  return descriptor_pool_provider_.FindStructTypeFieldByName(type, name);
}

absl::StatusOr<absl_nullable ValueBuilderPtr>
RuntimeTypeProvider::NewValueBuilder(
    absl::string_view name,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena) const {
  return common_internal::NewValueBuilder(arena, descriptor_pool_,
                                          message_factory, name);
}

TypeReflector::ValueBuilderFactory RuntimeTypeProvider::NewValueBuilderFactory(
    absl::string_view name,
    google::protobuf::MessageFactory* absl_nonnull message_factory) const {
  const google::protobuf::Message* prototype = nullptr;

  if (const auto* descriptor = descriptor_pool_->FindMessageTypeByName(name);
      descriptor != nullptr) {
    prototype = message_factory->GetPrototype(descriptor);
  }

  return [this, name = std::string(name), message_factory, prototype](
             google::protobuf::MessageFactory* absl_nonnull runtime_message_factory,
             google::protobuf::Arena* absl_nonnull arena)
             -> absl::StatusOr<absl_nullable ValueBuilderPtr> {
    if (prototype != nullptr && runtime_message_factory == message_factory) {
      return common_internal::NewValueBuilder(arena, descriptor_pool_,
                                              message_factory, prototype);
    }
    return NewValueBuilder(name, runtime_message_factory, arena);
  };
}

}  // namespace cel::runtime_internal
