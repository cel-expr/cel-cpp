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

#ifndef THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_TYPEDEF_TO_PROTO_H_
#define THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_TYPEDEF_TO_PROTO_H_

#include "google/protobuf/descriptor.pb.h"
#include "absl/status/statusor.h"
#include "common/typedef/typedef.h"

namespace cel {

enum class ProtoSyntax {
  kProto2,
  kProto3,
};

struct TypeDefToProtoOptions {
  // Target protobuf syntax for synthesized `FileDescriptorProto`s.
  ProtoSyntax syntax = ProtoSyntax::kProto2;
  // Whether to populate `SourceCodeInfo` leading comments from non-empty `doc`
  // fields.
  bool include_comments = true;
  // Whether to include minimal `FileDescriptorProto`s for referenced standard
  // CEL well-known types (e.g. `google/protobuf/timestamp.proto`) when not
  // already defined in the `TypeDefSet`.
  bool include_well_known_type_files = true;
};

// Converts all message and enum `TypeDef`s in a `cel::TypeDefSet` into a
// valid, topologically ordered `google::protobuf::FileDescriptorSet`.
//
// Honors `"proto"` aspect (`kProtoAspectName`) properties produced by
// `FileDescriptorSetToTypeDefSet`, as well as optional `name` (string)
// overrides on structs, fields, oneofs, enums, and enum constants. Top-level
// types without an explicit `proto.file_name` aspect property are grouped by
// package into `<pkg>/types.proto` (or `"types.proto"` in the root package).
// When `options.syntax == ProtoSyntax::kProto3`, field `default_value`s are
// ignored (since `proto3` forbids explicit default values) and enums must have
// `id == 0` for their first constant.
absl::StatusOr<google::protobuf::FileDescriptorSet> TypeDefSetToFileDescriptorSet(
    const TypeDefSet& type_def_set, const TypeDefToProtoOptions& options = {});

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_TYPEDEF_TO_PROTO_H_
