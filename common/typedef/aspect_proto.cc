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

#include "common/typedef/aspect_proto.h"

#include <any>
#include <memory>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "common/typedef/typedef.pb.h"

namespace cel {

absl::StatusOr<std::any> AspectProto::DecodeStructProperties(
    const types::Struct& proto) const {
  return std::any();
}

absl::StatusOr<std::any> AspectProto::DecodeFieldProperties(
    const types::Struct::Field& proto) const {
  return std::any();
}

absl::StatusOr<std::any> AspectProto::DecodeEnumProperties(
    const types::Enum& proto) const {
  return std::any();
}

absl::StatusOr<std::any> AspectProto::DecodeEnumConstantProperties(
    const types::Enum::EnumConstant& proto) const {
  return std::any();
}

absl::Status AspectProto::EncodeStructProperties(
    const std::any& properties, types::Struct* absl_nonnull proto) const {
  return absl::UnimplementedError(absl::StrCat(
      "Custom struct properties are not supported for aspect '", name(), "'"));
}

absl::Status AspectProto::EncodeFieldProperties(
    const std::any& properties,
    types::Struct::Field* absl_nonnull proto) const {
  return absl::UnimplementedError(absl::StrCat(
      "Custom field properties are not supported for aspect '", name(), "'"));
}

absl::Status AspectProto::EncodeEnumProperties(
    const std::any& properties, types::Enum* absl_nonnull proto) const {
  return absl::UnimplementedError(absl::StrCat(
      "Custom enum properties are not supported for aspect '", name(), "'"));
}

absl::Status AspectProto::EncodeEnumConstantProperties(
    const std::any& properties,
    types::Enum::EnumConstant* absl_nonnull proto) const {
  return absl::UnimplementedError(absl::StrCat(
      "Custom enum constant properties are not supported for aspect '", name(),
      "'"));
}

AspectProtoRegistry::AspectProtoRegistry(
    std::vector<std::unique_ptr<const AspectProto>> aspects) {
  aspects_.reserve(aspects.size());
  for (std::unique_ptr<const AspectProto>& aspect : aspects) {
    Add(std::move(aspect));
  }
}

const AspectProto* absl_nullable AspectProtoRegistry::Find(
    absl::string_view aspect_name) const {
  auto it = aspects_.find(aspect_name);
  if (it == aspects_.end()) {
    return nullptr;
  }
  return it->second.get();
}

}  // namespace cel
