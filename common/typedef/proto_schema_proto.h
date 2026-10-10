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

#include <memory>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "common/typedef/proto_schema.h"
#include "common/typedef/schema.h"
#include "common/typedef/schema_proto.h"
#include "common/typedef/typedef.pb.h"

namespace cel {

// Base SchemaProto implementation for Protocol Buffers schemas.
class ProtoSchemaProto : public SchemaProto {
 public:
  absl::StatusOr<std::unique_ptr<SchemaObjectProperties>>
  DecodeObjectProperties(const types::Object& proto) const override;

  absl::StatusOr<std::unique_ptr<SchemaFieldProperties>> DecodeFieldProperties(
      const types::Object::Field& proto) const override;

  absl::StatusOr<std::unique_ptr<SchemaEnumProperties>> DecodeEnumProperties(
      const types::Enum& proto) const override;

  absl::StatusOr<std::unique_ptr<SchemaEnumConstantProperties>>
  DecodeEnumConstantProperties(
      const types::Enum::EnumConstant& proto) const override;

  absl::Status EncodeObjectProperties(
      const SchemaObjectProperties& properties,
      types::Object* absl_nonnull proto) const override;

  absl::Status EncodeFieldProperties(
      const SchemaFieldProperties& properties,
      types::Object::Field* absl_nonnull proto) const override;

  absl::Status EncodeEnumProperties(
      const SchemaEnumProperties& properties,
      types::Enum* absl_nonnull proto) const override;

  absl::Status EncodeEnumConstantProperties(
      const SchemaEnumConstantProperties& properties,
      types::Enum::EnumConstant* absl_nonnull proto) const override;

 protected:
  enum class ProtoVersion {
    kProto2,
    kProto3,
  };

  explicit ProtoSchemaProto(ProtoVersion version) : version_(version) {}

 private:
  ProtoVersion version_;
};

// SchemaProto implementation for Protocol Buffers v2 ("proto2").
class Proto2SchemaProto : public ProtoSchemaProto {
 public:
  static constexpr absl::string_view kName = kProto2SchemaName;

  Proto2SchemaProto() : ProtoSchemaProto(ProtoVersion::kProto2) {}

  absl::string_view name() const override { return kName; }
};

// SchemaProto implementation for Protocol Buffers v3 ("proto3").
class Proto3SchemaProto : public ProtoSchemaProto {
 public:
  static constexpr absl::string_view kName = kProto3SchemaName;

  Proto3SchemaProto() : ProtoSchemaProto(ProtoVersion::kProto3) {}

  absl::string_view name() const override { return kName; }
};

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_PROTO_SCHEMA_PROTO_H_
