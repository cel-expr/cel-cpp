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

#ifndef THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_TYPEDEF_PROTO_H_
#define THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_TYPEDEF_PROTO_H_

#include <vector>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "common/typedef/aspect_proto.h"
#include "common/typedef/typedef.h"
#include "common/typedef/typedef.pb.h"

namespace cel {

// Converts a cel.types.Struct proto message to a native StructTypeDef.
//
// Aspects on the struct and its fields are decoded
// by delegating to the corresponding `AspectProto` codecs in `registry`.
absl::StatusOr<StructTypeDef> StructTypeDefFromProto(
    const types::Struct& proto, const AspectProtoRegistry& registry = {});

// Converts a native StructTypeDef to a cel.types.Struct proto message.
absl::Status StructTypeDefToProto(const StructTypeDef& struct_def,
                                  types::Struct* absl_nonnull out,
                                  const AspectProtoRegistry& registry = {});

absl::StatusOr<types::Struct> StructTypeDefToProto(
    const StructTypeDef& struct_def, const AspectProtoRegistry& registry = {});

// Converts a cel.types.Enum proto message to a native EnumTypeDef.
//
// Aspects on the enum and its constants are
// decoded by delegating to the corresponding `AspectProto` codecs in
// `registry`.
absl::StatusOr<EnumTypeDef> EnumTypeDefFromProto(
    const types::Enum& proto, const AspectProtoRegistry& registry = {});

// Converts a native EnumTypeDef to a cel.types.Enum proto message.
absl::Status EnumTypeDefToProto(const EnumTypeDef& enum_def,
                                types::Enum* absl_nonnull out,
                                const AspectProtoRegistry& registry = {});

absl::StatusOr<types::Enum> EnumTypeDefToProto(
    const EnumTypeDef& enum_def, const AspectProtoRegistry& registry = {});

// Converts a cel.types.TypeDef proto message to a native TypeDef.
//
// Aspects on struct, field, enum, and enum
// constant messages are decoded by delegating to the corresponding
// `AspectProto` codecs in `registry`.
absl::StatusOr<TypeDef> TypeDefFromProto(
    const types::TypeDef& proto, const AspectProtoRegistry& registry = {});

// Converts a native TypeDef to a cel.types.TypeDef proto message.
//
// Aspects are encoded as proto extensions by delegating to
// the corresponding `AspectProto` codecs looked up in `registry`.
absl::Status TypeDefToProto(const TypeDef& type_def,
                            types::TypeDef* absl_nonnull out,
                            const AspectProtoRegistry& registry = {});

absl::StatusOr<types::TypeDef> TypeDefToProto(
    const TypeDef& type_def, const AspectProtoRegistry& registry = {});

// Converts a cel.types.TypeDefSet proto message to a sequence of native
// TypeDefs.
//
// Aspects are decoded by delegating to the
// corresponding `AspectProto` codecs in `registry`.
absl::StatusOr<std::vector<TypeDef>> TypeDefsFromProto(
    const types::TypeDefSet& proto, const AspectProtoRegistry& registry = {});

// Converts a sequence of native TypeDefs to a cel.types.TypeDefSet proto
// message.
//
// Aspects are encoded as proto extensions by delegating to
// the corresponding `AspectProto` codecs looked up in `registry`.
absl::Status TypeDefsToProto(absl::Span<const TypeDef> type_defs,
                             types::TypeDefSet* absl_nonnull out,
                             const AspectProtoRegistry& registry = {});

absl::StatusOr<types::TypeDefSet> TypeDefsToProto(
    absl::Span<const TypeDef> type_defs,
    const AspectProtoRegistry& registry = {});

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_TYPEDEF_PROTO_H_
