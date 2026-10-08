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

#ifndef THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_PROTO_SCHEMA_H_
#define THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_PROTO_SCHEMA_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "common/typedef/schema.h"

namespace cel {

inline constexpr absl::string_view kProto2SchemaName = "proto2";
inline constexpr absl::string_view kProto3SchemaName = "proto3";

struct ProtoExtensionRange {
  int32_t start = 0;
  int32_t end = 0;

  friend bool operator==(const ProtoExtensionRange& a,
                         const ProtoExtensionRange& b) = default;
};

class ProtoObjectProperties final : public SchemaObjectProperties {
 public:
  std::optional<std::string> name;
  std::optional<std::string> file_name;
  std::vector<ProtoExtensionRange> extension_ranges;
};

class ProtoFieldProperties final : public SchemaFieldProperties {
 public:
  std::optional<std::string> name;
  std::optional<int32_t> id;
  std::optional<std::string> json_name;
  std::optional<std::string> type;
  std::optional<std::string> label;
  std::optional<std::string> oneof;
  std::optional<std::string> default_value;
};

class ProtoEnumProperties final : public SchemaEnumProperties {
 public:
  std::optional<std::string> name;
  std::optional<bool> allow_alias;
  std::optional<std::string> file_name;
};

class ProtoEnumConstantProperties final : public SchemaEnumConstantProperties {
 public:
  std::optional<std::string> name;
};

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMMON_TYPEDEF_PROTO_SCHEMA_H_
