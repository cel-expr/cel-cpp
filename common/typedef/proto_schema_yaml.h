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

#include <any>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "common/typedef/aspect_yaml.h"
#include "common/typedef/proto_schema.h"

namespace YAML {
class Emitter;
class Node;
}  // namespace YAML

namespace cel {

// AspectYaml implementation for general Protocol Buffers schemas ("proto").
class ProtoSchemaYaml final : public AspectYaml {
 public:
  static constexpr absl::string_view kName = kProtoAspectName;

  ProtoSchemaYaml() = default;

  absl::string_view name() const override { return kName; }

  absl::StatusOr<std::any> DecodeStructProperties(
      absl::string_view yaml, const YAML::Node& node) const override;

  absl::StatusOr<std::any> DecodeFieldProperties(
      absl::string_view yaml, const YAML::Node& node) const override;

  absl::StatusOr<std::any> DecodeEnumProperties(
      absl::string_view yaml, const YAML::Node& node) const override;

  absl::StatusOr<std::any> DecodeEnumConstantProperties(
      absl::string_view yaml, const YAML::Node& node) const override;

  absl::Status EncodeStructProperties(const std::any& properties,
                                      YAML::Emitter& out) const override;

  absl::Status EncodeFieldProperties(const std::any& properties,
                                     YAML::Emitter& out) const override;

  absl::Status EncodeEnumProperties(const std::any& properties,
                                    YAML::Emitter& out) const override;

  absl::Status EncodeEnumConstantProperties(const std::any& properties,
                                            YAML::Emitter& out) const override;
};

// AspectYaml implementation for Protocol Buffers v2 ("proto2").
class Proto2SchemaYaml final : public AspectYaml {
 public:
  static constexpr absl::string_view kName = kProto2AspectName;

  Proto2SchemaYaml() = default;

  absl::string_view name() const override { return kName; }

  absl::StatusOr<std::any> DecodeStructProperties(
      absl::string_view yaml, const YAML::Node& node) const override;

  absl::StatusOr<std::any> DecodeFieldProperties(
      absl::string_view yaml, const YAML::Node& node) const override;

  absl::Status EncodeStructProperties(const std::any& properties,
                                      YAML::Emitter& out) const override;

  absl::Status EncodeFieldProperties(const std::any& properties,
                                     YAML::Emitter& out) const override;
};

// AspectYaml implementation for Protocol Buffers v3 ("proto3").
class Proto3SchemaYaml final : public AspectYaml {
 public:
  static constexpr absl::string_view kName = kProto3AspectName;

  Proto3SchemaYaml() = default;

  absl::string_view name() const override { return kName; }

  absl::StatusOr<std::any> DecodeFieldProperties(
      absl::string_view yaml, const YAML::Node& node) const override;

  absl::Status EncodeFieldProperties(const std::any& properties,
                                     YAML::Emitter& out) const override;
};

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_PROTO_SCHEMA_YAML_H_
