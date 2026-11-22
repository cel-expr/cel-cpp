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

#include <memory>
#include <optional>
#include <string>

#include "google/protobuf/descriptor.pb.h"
#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "common/ast.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor_database.h"
#include "google/protobuf/message.h"

namespace cel::typedef_internal {

// Maps standard CEL well-known protobuf messages (wrappers, Any, Duration,
// Timestamp, Value, ListValue, Struct) to their built-in CEL `TypeSpec`, or
// returns `std::nullopt` for regular user messages.
std::optional<TypeSpec> WellKnownMessageToTypeSpec(
    const google::protobuf::Descriptor& desc);

bool IsWellKnownMessageType(const google::protobuf::Descriptor& desc);

bool IsWellKnownEnumType(const google::protobuf::EnumDescriptor& enum_desc);

// Returns the singular protobuf type keyword or full message/enum name for
// `field` (e.g. `"int32"`, `"sint64"`, or `"pkg.MyMessage"`).
std::string GetSingularProtoTypeName(const google::protobuf::FieldDescriptor& field);

// Returns the default singular protobuf type name for a CEL `TypeSpec`.
absl::StatusOr<std::string> DefaultSingularProtoTypeName(
    const TypeSpec& type_spec);

// Computes the default protobuf `lowerCamelCase` JSON field name for `name`
// (matching `protoc`'s default `json_name` synthesis).
std::string ProtoDefaultJsonName(absl::string_view name);

// Owns a `DescriptorPool` backed by a `FileDescriptorSet` layered over CEL's
// minimal well-known-types `DescriptorDatabase`, collecting build errors.
class TypeDefDescriptorPool final {
 public:
  static absl::StatusOr<std::unique_ptr<TypeDefDescriptorPool>> Create(
      const google::protobuf::FileDescriptorSet& file_descriptor_set);

  TypeDefDescriptorPool(const TypeDefDescriptorPool&) = delete;
  TypeDefDescriptorPool& operator=(const TypeDefDescriptorPool&) = delete;

  // Builds and returns the `FileDescriptor` for `file_name`, or returns
  // `InvalidArgumentError` with collected `DescriptorPool` diagnostics.
  absl::StatusOr<const google::protobuf::FileDescriptor* absl_nonnull> FindFile(
      absl::string_view file_name) ABSL_ATTRIBUTE_LIFETIME_BOUND;

 private:
  class ErrorCollector final : public google::protobuf::DescriptorPool::ErrorCollector {
   public:
    void RecordError(absl::string_view filename, absl::string_view element_name,
                     const google::protobuf::Message* descriptor, ErrorLocation location,
                     absl::string_view message) override;

    void Clear() { errors_.clear(); }

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
  ErrorCollector error_collector_;
  google::protobuf::DescriptorPool pool_;
};

}  // namespace cel::typedef_internal

#endif  // THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_PROTO_TYPE_HELPERS_H_
