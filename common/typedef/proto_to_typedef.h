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
#include "common/typedef/typedef.h"

namespace cel {

struct ProtoToTypeDefOptions {
  // Whether to extract leading source comments (`SourceCodeInfo`) into `doc`.
  bool include_comments = true;
  // Whether to extract explicit proto2 field default values into
  // `ObjectTypeDef::Field::default_value`.
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
// Schema-specific properties (`ProtoObjectProperties`, `ProtoFieldProperties`,
// `ProtoEnumProperties`) are populated under `"proto2"` or `"proto3"` according
// to each file's syntax.
absl::StatusOr<TypeDefSet> FileDescriptorSetToTypeDefSet(
    const google::protobuf::FileDescriptorSet& file_descriptor_set,
    const ProtoToTypeDefOptions& options = {});

struct TypeDefToProtoOptions {
  // Whether to populate `SourceCodeInfo` leading comments from non-empty `doc`
  // fields.
  bool include_comments = true;
  // Whether to include minimal `FileDescriptorProto`s for referenced standard
  // CEL well-known types (e.g. `google/protobuf/timestamp.proto`) when not
  // already defined in the `TypeDefSet`.
  bool include_well_known_type_files = true;
};

// Converts all message and enum `TypeDef`s in a `cel::TypeDefSet` back into a
// valid, topologically ordered `google::protobuf::FileDescriptorSet`.
absl::StatusOr<google::protobuf::FileDescriptorSet> TypeDefSetToFileDescriptorSet(
    const TypeDefSet& type_def_set, const TypeDefToProtoOptions& options = {});

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_PROTO_TO_TYPEDEF_H_
