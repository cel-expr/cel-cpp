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
#include <vector>

#include "absl/base/attributes.h"
#include "absl/strings/string_view.h"

namespace cel {

// Aspect identifier for general Protocol Buffers schema properties shared
// across all protobuf flavors ("proto").
inline constexpr absl::string_view kProtoAspectName = "proto";

// Aspect identifier for Protocol Buffers v2 specific properties ("proto2").
inline constexpr absl::string_view kProto2AspectName = "proto2";

// Aspect identifier for Protocol Buffers v3 specific properties ("proto3").
inline constexpr absl::string_view kProto3AspectName = "proto3";

// Note on aspect independence and orthogonal schema roles:
// The "proto", "proto2", and "proto3" aspects are stored, encoded, and decoded
// independently in TypeDef elements (StructTypeDef and EnumTypeDef aspects
// maps) without automatic merging. "proto" captures common properties across
// all protobuf flavors (e.g., field tag id, json_name, or shared message/enum
// names), while "proto2" and "proto3" capture only flavor-differentiating
// properties (e.g., extension ranges in proto2, required in proto2, or explicit
// optional presence in proto3). Client code consuming these aspects is expected
// to inspect both "proto" and the relevant flavor aspect when resolving schema
// metadata.

// Protocol Buffers schema properties attached to a StructTypeDef ("proto"
// aspect).
class ProtoStructProperties final {
 public:
  ProtoStructProperties() = default;
  explicit ProtoStructProperties(std::optional<std::string> name)
      : name_(std::move(name)) {}

  // Overrides the canonical CEL struct name with a target-specific protobuf
  // message name when they differ (e.g., mapping a canonical CEL type
  // "com.example.Order" to "com.example.proto.Order").
  const std::optional<std::string>& name() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return name_;
  }

 private:
  std::optional<std::string> name_;
};

// Protocol Buffers schema properties attached to a StructTypeDef::Field
// ("proto" aspect).
class ProtoFieldProperties final {
 public:
  ProtoFieldProperties() = default;
  explicit ProtoFieldProperties(
      std::optional<std::string> name, std::optional<int32_t> id = std::nullopt,
      std::optional<std::string> json_name = std::nullopt)
      : name_(std::move(name)), id_(id), json_name_(std::move(json_name)) {}

  // Overrides the canonical CEL field name with the protobuf field name when
  // they differ (e.g., mapping a CEL field "order_id" to "order_id_proto",
  // or mapping a camelCase CEL field "userId" to "user_id").
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

// Protocol Buffers schema properties attached to an EnumTypeDef ("proto"
// aspect).
class ProtoEnumProperties final {
 public:
  ProtoEnumProperties() = default;
  explicit ProtoEnumProperties(std::optional<std::string> name,
                               std::optional<bool> allow_alias = std::nullopt)
      : name_(std::move(name)), allow_alias_(allow_alias) {}

  // Overrides the canonical CEL enum name with a target-specific protobuf enum
  // name when they differ (e.g., mapping "com.example.OrderStatus" to
  // "com.example.proto.OrderStatus").
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

// Protocol Buffers schema properties attached to an EnumTypeDef::EnumConstant
// ("proto" aspect).
class ProtoEnumConstantProperties final {
 public:
  ProtoEnumConstantProperties() = default;
  explicit ProtoEnumConstantProperties(std::optional<std::string> name)
      : name_(std::move(name)) {}

  // Overrides the canonical CEL enum constant name with the protobuf enum value
  // name when they differ (e.g., mapping a CEL constant "ORDER_STATUS_ACTIVE"
  // to "STATUS_ACTIVE").
  const std::optional<std::string>& name() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return name_;
  }

 private:
  std::optional<std::string> name_;
};

// Represents an inclusive-to-exclusive `[start, end)` extension tag range on a
// proto2 message (e.g., `extensions 100 to 199;` has `start = 100, end = 200`).
struct ProtoExtensionRange {
  int32_t start = 0;
  int32_t end = 0;
};

// Protocol Buffers v2 schema properties attached to a StructTypeDef ("proto2"
// aspect).
class Proto2StructProperties final {
 public:
  Proto2StructProperties() = default;
  explicit Proto2StructProperties(
      std::vector<ProtoExtensionRange> extension_ranges)
      : extension_ranges_(std::move(extension_ranges)) {}

  // Extension tag ranges declared on a proto2 message (e.g.,
  // `extensions 100 to 199;`), needed to reconstruct extensible messages.
  const std::vector<ProtoExtensionRange>& extension_ranges() const
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return extension_ranges_;
  }

 private:
  std::vector<ProtoExtensionRange> extension_ranges_;
};

// Protocol Buffers v2 schema properties attached to a StructTypeDef::Field
// ("proto2" aspect).
class Proto2FieldProperties final {
 public:
  Proto2FieldProperties() = default;
  explicit Proto2FieldProperties(std::optional<bool> required = std::nullopt,
                                 std::optional<bool> packed = std::nullopt)
      : required_(required), packed_(packed) {}

  // Whether the field is declared with the proto2 `required` label.
  std::optional<bool> required() const { return required_; }

  // Explicit repeated field packing override (`[packed = true/false]`). In
  // proto2, repeated primitive fields default to unpacked (`packed = false`).
  std::optional<bool> packed() const { return packed_; }

 private:
  std::optional<bool> required_;
  std::optional<bool> packed_;
};

// Protocol Buffers v3 schema properties attached to a StructTypeDef::Field
// ("proto3" aspect).
class Proto3FieldProperties final {
 public:
  Proto3FieldProperties() = default;
  explicit Proto3FieldProperties(std::optional<bool> optional = std::nullopt,
                                 std::optional<bool> packed = std::nullopt)
      : optional_(optional), packed_(packed) {}

  // Whether the field has explicit field presence via the proto3 `optional`
  // keyword (`proto3_optional = true`).
  std::optional<bool> optional() const { return optional_; }

  // Explicit repeated field packing override (`[packed = true/false]`). In
  // proto3, repeated primitive fields default to packed (`packed = true`).
  std::optional<bool> packed() const { return packed_; }

 private:
  std::optional<bool> optional_;
  std::optional<bool> packed_;
};

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_PROTO_SCHEMA_H_
