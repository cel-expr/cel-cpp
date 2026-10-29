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

#ifndef THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_ASPECT_PROTO_H_
#define THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_ASPECT_PROTO_H_

#include <any>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "common/typedef/typedef.pb.h"

namespace cel {

// Interface representing a Protobuf codec for an aspect (e.g. "json",
// "proto2", "proto3") capable of decoding and encoding aspect property
// objects via proto extensions on cel.types messages.
class AspectProto {
 public:
  virtual ~AspectProto() = default;

  // Returns the unique aspect identifier (e.g. "json", "proto2", "proto3").
  virtual absl::string_view name() const = 0;

  // Decodes struct aspect properties from extensions on `proto`.
  // Returns an empty std::any if this aspect has no properties present on
  // `proto`.
  virtual absl::StatusOr<std::any> DecodeStructProperties(
      const types::Struct& proto) const;

  // Decodes field aspect properties from extensions on `proto`.
  // Returns an empty std::any if this aspect has no properties present on
  // `proto`.
  virtual absl::StatusOr<std::any> DecodeFieldProperties(
      const types::Struct::Field& proto) const;

  // Decodes enum aspect properties from extensions on `proto`.
  // Returns an empty std::any if this aspect has no properties present on
  // `proto`.
  virtual absl::StatusOr<std::any> DecodeEnumProperties(
      const types::Enum& proto) const;

  // Decodes enum constant aspect properties from extensions on
  // `proto`. Returns an empty std::any if this aspect has no properties present
  // on `proto`.
  virtual absl::StatusOr<std::any> DecodeEnumConstantProperties(
      const types::Enum::EnumConstant& proto) const;

  virtual absl::Status EncodeStructProperties(
      const std::any& properties, types::Struct* absl_nonnull proto) const;

  virtual absl::Status EncodeFieldProperties(
      const std::any& properties,
      types::Struct::Field* absl_nonnull proto) const;

  virtual absl::Status EncodeEnumProperties(
      const std::any& properties, types::Enum* absl_nonnull proto) const;

  virtual absl::Status EncodeEnumConstantProperties(
      const std::any& properties,
      types::Enum::EnumConstant* absl_nonnull proto) const;
};

// Read-only registry of AspectProto codecs keyed by aspect name.
class AspectProtoRegistry {
 public:
  AspectProtoRegistry() = default;

  explicit AspectProtoRegistry(
      std::vector<std::unique_ptr<const AspectProto>> aspects);

  template <
      typename... Aspects,
      typename = std::enable_if_t<
          (sizeof...(Aspects) > 0) &&
          (std::is_convertible_v<Aspects, std::unique_ptr<const AspectProto>> &&
           ...)>>
  explicit AspectProtoRegistry(Aspects&&... aspects) {
    aspects_.reserve(sizeof...(Aspects));
    (Add(std::forward<Aspects>(aspects)), ...);
  }

  // Finds a registered AspectProto by name, or returns nullptr if not found.
  const AspectProto* absl_nullable Find(absl::string_view aspect_name) const
      ABSL_ATTRIBUTE_LIFETIME_BOUND;

  const absl::flat_hash_map<std::string, std::unique_ptr<const AspectProto>>&
  aspects() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return aspects_;
  }

 private:
  void Add(std::unique_ptr<const AspectProto> aspect) {
    if (aspect != nullptr) {
      absl::string_view name = aspect->name();
      aspects_[name] = std::move(aspect);
    }
  }

  absl::flat_hash_map<std::string, std::unique_ptr<const AspectProto>> aspects_;
};

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_ASPECT_PROTO_H_
