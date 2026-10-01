// Copyright 2026 Google LLC
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

#ifndef THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_TYPE_REF_H_
#define THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_TYPE_REF_H_

#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "common/ast.h"
#include "common/type.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"

namespace cel {

struct TypeRef {
  std::string name;
  std::vector<TypeRef> params;
  bool is_type_param = false;

  bool operator==(const TypeRef& other) const {
    return name == other.name && params == other.params &&
           is_type_param == other.is_type_param;
  }

  bool operator!=(const TypeRef& other) const { return !(*this == other); }
};

// Converts a TypeRef to a cel::Type. Returns an error if the type_ref
// cannot be converted to a known cel::Type, a list configured with more than
// one parameter.
absl::StatusOr<Type> TypeRefToType(
    const TypeRef& type_ref, const google::protobuf::DescriptorPool* descriptor_pool,
    google::protobuf::Arena* arena);

// Converts a TypeRef to a cel::TypeSpec.
absl::StatusOr<TypeSpec> TypeRefToTypeSpec(const TypeRef& type_ref);

// Converts a cel::TypeSpec to a TypeRef.
absl::StatusOr<TypeRef> TypeSpecToTypeRef(const TypeSpec& type_spec);

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_TYPE_REF_H_
