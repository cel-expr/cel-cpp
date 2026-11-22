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

#ifndef THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_PROTO_SCHEMA_YAML_H_
#define THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_PROTO_SCHEMA_YAML_H_

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "common/typedef/proto_schema.h"
#include "common/typedef/schema.h"
#include "common/typedef/schema_yaml.h"

namespace cel {

// SchemaYaml implementation for Protocol Buffers v2 ("proto2").
class Proto2SchemaYaml : public SchemaYaml {
 public:
  static constexpr absl::string_view kName = kProto2SchemaName;

  Proto2SchemaYaml() = default;

  absl::string_view name() const override { return kName; }

  absl::StatusOr<std::unique_ptr<SchemaObjectProperties>>
  DecodeObjectProperties(absl::string_view yaml,
                         const YAML::Node& node) const override;

  absl::StatusOr<std::unique_ptr<SchemaFieldProperties>> DecodeFieldProperties(
      absl::string_view yaml, const YAML::Node& node) const override;

  absl::StatusOr<std::unique_ptr<SchemaEnumProperties>> DecodeEnumProperties(
      absl::string_view yaml, const YAML::Node& node) const override;

  absl::StatusOr<std::unique_ptr<SchemaEnumConstantProperties>>
  DecodeEnumConstantProperties(absl::string_view yaml,
                               const YAML::Node& node) const override;

  absl::Status EncodeObjectProperties(const SchemaObjectProperties& properties,
                                      YAML::Emitter& out) const override;

  absl::Status EncodeFieldProperties(const SchemaFieldProperties& properties,
                                     YAML::Emitter& out) const override;

  absl::Status EncodeEnumProperties(const SchemaEnumProperties& properties,
                                    YAML::Emitter& out) const override;

  absl::Status EncodeEnumConstantProperties(
      const SchemaEnumConstantProperties& properties,
      YAML::Emitter& out) const override;
};

// SchemaYaml implementation for Protocol Buffers v3 ("proto3").
class Proto3SchemaYaml : public SchemaYaml {
 public:
  static constexpr absl::string_view kName = kProto3SchemaName;

  Proto3SchemaYaml() = default;

  absl::string_view name() const override { return kName; }

  absl::StatusOr<std::unique_ptr<SchemaObjectProperties>>
  DecodeObjectProperties(absl::string_view yaml,
                         const YAML::Node& node) const override;

  absl::StatusOr<std::unique_ptr<SchemaFieldProperties>> DecodeFieldProperties(
      absl::string_view yaml, const YAML::Node& node) const override;

  absl::StatusOr<std::unique_ptr<SchemaEnumProperties>> DecodeEnumProperties(
      absl::string_view yaml, const YAML::Node& node) const override;

  absl::StatusOr<std::unique_ptr<SchemaEnumConstantProperties>>
  DecodeEnumConstantProperties(absl::string_view yaml,
                               const YAML::Node& node) const override;

  absl::Status EncodeObjectProperties(const SchemaObjectProperties& properties,
                                      YAML::Emitter& out) const override;

  absl::Status EncodeFieldProperties(const SchemaFieldProperties& properties,
                                     YAML::Emitter& out) const override;

  absl::Status EncodeEnumProperties(const SchemaEnumProperties& properties,
                                    YAML::Emitter& out) const override;

  absl::Status EncodeEnumConstantProperties(
      const SchemaEnumConstantProperties& properties,
      YAML::Emitter& out) const override;
};

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_PROTO_SCHEMA_YAML_H_
