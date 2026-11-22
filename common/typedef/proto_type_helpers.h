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

#ifndef THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_PROTO_TYPE_HELPERS_H_
#define THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_PROTO_TYPE_HELPERS_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "google/protobuf/descriptor.pb.h"
#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "common/ast.h"
#include "common/typedef/typedef.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor_database.h"
#include "google/protobuf/message.h"

namespace cel::typedef_internal {

// Looks up a named `Aspect` on any `TypeDef` element exposing an `aspects()`
// map, returning nullptr if absent.
template <typename HolderT>
const Aspect* absl_nullable FindAspect(const HolderT& holder
                                           ABSL_ATTRIBUTE_LIFETIME_BOUND,
                                       absl::string_view aspect_name) {
  auto it = holder.aspects().find(aspect_name);
  if (it == holder.aspects().end()) {
    return nullptr;
  }
  return &it->second;
}

// Validates and extracts an optional non-empty string property from a `"proto"`
// `Aspect`. The returned view is backed by `*aspect`.
absl::StatusOr<std::optional<absl::string_view>> GetOptionalStringProperty(
    const Aspect* absl_nullable aspect ABSL_ATTRIBUTE_LIFETIME_BOUND,
    absl::string_view key);

// Validates and extracts an optional boolean property from a `"proto"`
// `Aspect`.
absl::StatusOr<std::optional<bool>> GetOptionalBoolProperty(
    const Aspect* absl_nullable aspect, absl::string_view key);

// Validates and extracts an optional positive 32-bit field number property from
// a `"proto"` `Aspect`.
absl::StatusOr<std::optional<int32_t>> GetOptionalFieldIdProperty(
    const Aspect* absl_nullable aspect, absl::string_view key);

// Returns the non-empty `proto.name` override on a struct or enum `TypeDef`,
// if present. The returned view is backed by `td`.
absl::StatusOr<std::optional<absl::string_view>> GetProtoNameOverride(
    const TypeDef& td ABSL_ATTRIBUTE_LIFETIME_BOUND);

// Returns the non-empty `proto.file_name` override on a struct or enum
// `TypeDef`, if present. The returned view is backed by `td`.
absl::StatusOr<std::optional<absl::string_view>> GetProtoFileNameOverride(
    const TypeDef& td ABSL_ATTRIBUTE_LIFETIME_BOUND);

// Maps standard CEL well-known protobuf messages (wrappers, Any, Duration,
// Timestamp, Value, ListValue, Struct) to their built-in CEL `TypeSpec`, or
// returns `std::nullopt` for regular user messages.
std::optional<TypeSpec> WellKnownMessageToTypeSpec(
    const google::protobuf::Descriptor& desc);

bool IsWellKnownMessageType(const google::protobuf::Descriptor& desc);

bool IsWellKnownEnumType(const google::protobuf::EnumDescriptor& enum_desc);

// Returns the canonical `.proto` import path for a standard CEL well-known
// protobuf type (e.g. `"google/protobuf/timestamp.proto"`), or `std::nullopt`
// if `full_type_name` is not a well-known type.
std::optional<absl::string_view> GetWellKnownTypeFileName(
    absl::string_view full_type_name);

// Returns the singular protobuf type keyword or full message/enum name for
// `field` (e.g. `"int32"`, `"sint64"`, or `"pkg.MyMessage"`).
std::string GetSingularProtoTypeName(const google::protobuf::FieldDescriptor& field);

// Parses a scalar protobuf type keyword (e.g. `"int32"`, `"bytes"`) into its
// corresponding `FieldDescriptorProto::Type`, or `std::nullopt` if non-scalar.
std::optional<google::protobuf::FieldDescriptorProto::Type> ParseScalarProtoFieldType(
    absl::string_view proto_type_str);

// Returns the default singular protobuf type name for a CEL `TypeSpec`.
absl::StatusOr<std::string> DefaultSingularProtoTypeName(
    const TypeSpec& type_spec);

// Converts a `snake_case` identifier to `lowerCamelCase` (`upper_first =
// false`) or `UpperCamelCase` (`upper_first = true`).
std::string SnakeToCamelCase(absl::string_view name, bool upper_first);

// Owns a `DescriptorPool` backed by a `FileDescriptorSet` layered over CEL's
// minimal well-known-types `DescriptorDatabase`, collecting build errors.
class TypeDefDescriptorPool final {
 public:
  static absl::StatusOr<std::unique_ptr<TypeDefDescriptorPool>> Create(
      const google::protobuf::FileDescriptorSet& file_descriptor_set);

  TypeDefDescriptorPool(const TypeDefDescriptorPool&) = delete;
  TypeDefDescriptorPool& operator=(const TypeDefDescriptorPool&) = delete;

  const google::protobuf::DescriptorPool& pool() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return pool_;
  }

  // Builds and returns the `FileDescriptor` for `file_name`, or returns
  // `InvalidArgumentError` with collected `DescriptorPool` diagnostics.
  absl::StatusOr<const google::protobuf::FileDescriptor* absl_nonnull> FindFile(
      absl::string_view file_name) const ABSL_ATTRIBUTE_LIFETIME_BOUND;

  // Ensures every `FileDescriptorProto` in `file_descriptor_set` builds cleanly
  // in `pool()`.
  absl::Status BuildAllFiles(
      const google::protobuf::FileDescriptorSet& file_descriptor_set) const;

 private:
  class ErrorCollector final : public google::protobuf::DescriptorPool::ErrorCollector {
   public:
    void RecordError(absl::string_view filename, absl::string_view element_name,
                     const google::protobuf::Message* descriptor, ErrorLocation location,
                     absl::string_view message) override;

    absl::string_view errors() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
      return errors_;
    }

   private:
    std::string errors_;
  };

  explicit TypeDefDescriptorPool(
      std::unique_ptr<google::protobuf::SimpleDescriptorDatabase> ext_db);

  std::unique_ptr<google::protobuf::SimpleDescriptorDatabase> ext_db_;
  google::protobuf::MergedDescriptorDatabase merged_db_;
  mutable ErrorCollector error_collector_;
  google::protobuf::DescriptorPool pool_;
};

// Validates that `file_descriptor_set` has unique non-empty file names and
// builds cleanly in a `DescriptorPool` backed by CEL's minimal WKT database.
absl::Status ValidateFileDescriptorSet(
    const google::protobuf::FileDescriptorSet& file_descriptor_set);

}  // namespace cel::typedef_internal

#endif  // THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_PROTO_TYPE_HELPERS_H_
