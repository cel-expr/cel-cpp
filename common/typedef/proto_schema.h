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

#ifndef THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_PROTO_SCHEMA_H_
#define THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_PROTO_SCHEMA_H_

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/strings/string_view.h"

namespace cel {

inline constexpr absl::string_view kProto2AspectName = "proto2";
inline constexpr absl::string_view kProto3AspectName = "proto3";

// Protocol Buffers schema properties attached to a StructTypeDef.
class ProtoStructProperties final {
 public:
  ProtoStructProperties() = default;
  explicit ProtoStructProperties(std::optional<std::string> name)
      : name_(std::move(name)) {}

  // Overrides the canonical CEL struct name with a target-specific protobuf
  // message name when they differ (e.g., mapping a canonical CEL type
  // "com.example.Order" to "com.example.proto2.Order" in proto2 and
  // "com.example.proto3.Order" in proto3).
  const std::optional<std::string>& name() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return name_;
  }

 private:
  std::optional<std::string> name_;
};

// Protocol Buffers schema properties attached to a StructTypeDef::Field.
class ProtoFieldProperties final {
 public:
  ProtoFieldProperties() = default;
  explicit ProtoFieldProperties(
      std::optional<std::string> name, std::optional<int32_t> id = std::nullopt,
      std::optional<std::string> json_name = std::nullopt)
      : name_(std::move(name)), id_(id), json_name_(std::move(json_name)) {}

  // Overrides the canonical CEL field name with the protobuf field name when
  // they differ (e.g., mapping a CEL field "order_id" to a legacy proto2 field
  // "order_id_v2", or mapping a camelCase CEL field "userId" to "user_id").
  const std::optional<std::string>& name() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return name_;
  }

  // The protobuf field number (tag) in the message definition (e.g., `1` in
  // `string order_id = 1;`), required for wire serialization and descriptor
  // generation.
  std::optional<int32_t> id() const { return id_; }

  // Custom JSON field name specified via the `[json_name = "..."]` field
  // option when it differs from the default lowerCamelCase of the proto field
  // name (e.g., `[json_name = "order_id_legacy"]` on field `order_id`).
  const std::optional<std::string>& json_name() const
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return json_name_;
  }

 private:
  std::optional<std::string> name_;
  std::optional<int32_t> id_;
  std::optional<std::string> json_name_;
};

// Protocol Buffers schema properties attached to an EnumTypeDef.
class ProtoEnumProperties final {
 public:
  ProtoEnumProperties() = default;
  explicit ProtoEnumProperties(std::optional<std::string> name,
                               std::optional<bool> allow_alias = std::nullopt)
      : name_(std::move(name)), allow_alias_(allow_alias) {}

  // Overrides the canonical CEL enum name with a target-specific protobuf enum
  // name when they differ (e.g., mapping "com.example.OrderStatus" to
  // "com.example.proto2.OrderStatus").
  const std::optional<std::string>& name() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return name_;
  }

  // Whether the protobuf enum sets `option allow_alias = true;`, permitting
  // multiple enum constants to share the same numeric value (e.g.,
  // `STARTED = 1;` and `RUNNING = 1;`).
  std::optional<bool> allow_alias() const { return allow_alias_; }

 private:
  std::optional<std::string> name_;
  std::optional<bool> allow_alias_;
};

// Protocol Buffers schema properties attached to an EnumTypeDef::EnumConstant.
class ProtoEnumConstantProperties final {
 public:
  ProtoEnumConstantProperties() = default;
  explicit ProtoEnumConstantProperties(std::optional<std::string> name)
      : name_(std::move(name)) {}

  // Overrides the canonical CEL enum constant name with the protobuf enum value
  // name when they differ (e.g., mapping a CEL constant "ORDER_STATUS_ACTIVE"
  // to "STATUS_ACTIVE_V2" in a legacy proto2 enum).
  const std::optional<std::string>& name() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return name_;
  }

 private:
  std::optional<std::string> name_;
};

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_PROTO_SCHEMA_H_
