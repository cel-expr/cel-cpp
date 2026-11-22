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

#ifndef THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_PROTO_TO_TYPEDEF_H_
#define THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_PROTO_TO_TYPEDEF_H_

#include "google/protobuf/descriptor.pb.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "common/typedef/typedef.h"

namespace cel {

// Canonical aspect name for Protocol Buffers schema metadata on `TypeDef`
// elements.
inline constexpr absl::string_view kProtoAspectName = "proto";

struct ProtoToTypeDefOptions {
  // Whether to extract leading source comments (`SourceCodeInfo`) into `doc`.
  bool include_comments = true;
  // Whether to extract explicit proto2 field default values into
  // `StructTypeDef::Field::default_value`.
  bool include_defaults = true;
};

// Converts all non-well-known message and enum descriptors in a
// `google::protobuf::FileDescriptorSet` into a `cel::TypeDefSet`.
//
// Standard CEL well-known types (e.g. `google.protobuf.Timestamp`,
// `google.protobuf.Duration`, `google.protobuf.Struct`,
// `google.protobuf.Value`, `google.protobuf.ListValue`,
// `google.protobuf.NullValue`, `google.protobuf.Any`, and primitive wrappers)
// are mapped to their built-in CEL `TypeSpec` representations on fields and
// omitted from the emitted `TypeDefSet`. Synthetic protobuf map-entry messages
// are also omitted.
//
// Protocol Buffers schema metadata is populated under the `"proto"` aspect
// (`kProtoAspectName`) using `cel::Constant` properties:
//   - Top-level `StructTypeDef`: `file_name` (string).
//   - `StructTypeDef::Field`: `id` (int64), optional `json_name` (string),
//     optional `type` (string).
//   - `StructTypeDef::Oneof`: `name` (string).
//   - `EnumTypeDef`: optional `file_name` (string, top-level only), optional
//     `allow_alias` (bool).
//
// Because CEL `TypeDef` models logical types rather than wire-level or
// syntax-specific protobuf dialect details, conversion from
// `FileDescriptorSet` to `TypeDefSet` is lossy. The following features of
// `proto2`, `proto3`, and `editions` are ignored:
//   - File syntax & edition level: Whether a `.proto` file uses `proto2`,
//     `proto3`, or `editions` (`edition = "2023"`, `"2024"`, etc.) is not
//     recorded.
//   - Field presence & labels: Proto2 `required` vs. `optional` labels, proto3
//     `optional` (`proto3_optional` / synthetic oneofs), and editions
//     `features.field_presence` (`EXPLICIT`, `IMPLICIT`, `LEGACY_REQUIRED`) are
//     ignored; all singular fields are represented uniformly.
//   - Repeated field packing: Explicit `[packed = true/false]` options and
//     editions `features.repeated_field_encoding` (`PACKED` vs. `EXPANDED`) are
//     ignored.
//   - Group / delimited encoding: Proto2 `group` fields (`TYPE_GROUP`) and
//     editions `features.message_encoding = DELIMITED` are treated as standard
//     message fields (`TYPE_MESSAGE`) referencing the group/message type; the
//     wire-level delimited encoding distinction is not preserved.
//   - Extensions & extension ranges: Proto2/editions `extensions` ranges
//     (`ExtensionRange`) and `extend` declarations (`extension` fields) are
//     ignored.
//   - Enum open/closed semantics: Proto2 closed enum vs. proto3 open enum
//     semantics and editions `features.enum_type` (`OPEN` vs. `CLOSED`) are
//     ignored.
//   - UTF-8 validation: Proto2 vs. proto3 string UTF-8 validation differences
//     and editions `features.utf8_validation` are ignored.
//   - Reserved ranges/names, services, and custom options: `reserved` field
//     numbers/names, RPC `service`/`method` definitions, and
//     custom/unrecognized protobuf options (`FileOptions`, `MessageOptions`,
//     `FieldOptions`, `OneofOptions`, `EnumOptions`, `EnumValueOptions`) are
//     ignored.
absl::StatusOr<TypeDefSet> FileDescriptorSetToTypeDefSet(
    const google::protobuf::FileDescriptorSet& file_descriptor_set,
    const ProtoToTypeDefOptions& options = {});

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_PROTO_TO_TYPEDEF_H_
