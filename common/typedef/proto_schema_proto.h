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

#ifndef THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_PROTO_SCHEMA_PROTO_H_
#define THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_PROTO_SCHEMA_PROTO_H_

#include <any>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "common/typedef/aspect_proto.h"
#include "common/typedef/proto_schema.h"
#include "common/typedef/typedef.pb.h"

namespace cel {

// AspectProto implementation for general Protocol Buffers schemas ("proto").
class ProtoSchemaProto final : public AspectProto {
 public:
  static constexpr absl::string_view kName = kProtoAspectName;

  ProtoSchemaProto() = default;

  absl::string_view name() const override { return kName; }

  absl::StatusOr<std::any> DecodeStructProperties(
      const types::Struct& proto) const override;

  absl::StatusOr<std::any> DecodeFieldProperties(
      const types::Struct::Field& proto) const override;

  absl::StatusOr<std::any> DecodeEnumProperties(
      const types::Enum& proto) const override;

  absl::StatusOr<std::any> DecodeEnumConstantProperties(
      const types::Enum::EnumConstant& proto) const override;

  absl::Status EncodeStructProperties(
      const std::any& properties,
      types::Struct* absl_nonnull proto) const override;

  absl::Status EncodeFieldProperties(
      const std::any& properties,
      types::Struct::Field* absl_nonnull proto) const override;

  absl::Status EncodeEnumProperties(
      const std::any& properties,
      types::Enum* absl_nonnull proto) const override;

  absl::Status EncodeEnumConstantProperties(
      const std::any& properties,
      types::Enum::EnumConstant* absl_nonnull proto) const override;
};

// AspectProto implementation for Protocol Buffers v2 ("proto2").
class Proto2SchemaProto final : public AspectProto {
 public:
  static constexpr absl::string_view kName = kProto2AspectName;

  Proto2SchemaProto() = default;

  absl::string_view name() const override { return kName; }

  absl::StatusOr<std::any> DecodeStructProperties(
      const types::Struct& proto) const override;

  absl::StatusOr<std::any> DecodeFieldProperties(
      const types::Struct::Field& proto) const override;

  absl::Status EncodeStructProperties(
      const std::any& properties,
      types::Struct* absl_nonnull proto) const override;

  absl::Status EncodeFieldProperties(
      const std::any& properties,
      types::Struct::Field* absl_nonnull proto) const override;
};

// AspectProto implementation for Protocol Buffers v3 ("proto3").
class Proto3SchemaProto final : public AspectProto {
 public:
  static constexpr absl::string_view kName = kProto3AspectName;

  Proto3SchemaProto() = default;

  absl::string_view name() const override { return kName; }

  absl::StatusOr<std::any> DecodeFieldProperties(
      const types::Struct::Field& proto) const override;

  absl::Status EncodeFieldProperties(
      const std::any& properties,
      types::Struct::Field* absl_nonnull proto) const override;
};

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_PROTO_SCHEMA_PROTO_H_
