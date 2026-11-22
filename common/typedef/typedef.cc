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

#include "common/typedef/typedef.h"

#include <utility>
#include <variant>

#include "absl/base/nullability.h"
#include "absl/functional/overload.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace cel {

absl::Status ObjectTypeDef::AddField(Field field) {
  if (FindField(field.name) != nullptr) {
    return absl::AlreadyExistsError(
        absl::StrCat("Field '", field.name, "' is already defined."));
  }
  fields.push_back(std::move(field));
  return absl::OkStatus();
}

const ObjectTypeDef::Field* absl_nullable ObjectTypeDef::FindField(
    absl::string_view field_name) const {
  for (const Field& field : fields) {
    if (field.name == field_name) {
      return &field;
    }
  }
  return nullptr;
}

absl::Status EnumTypeDef::AddConstant(EnumConstant constant) {
  if (FindConstant(constant.name) != nullptr) {
    return absl::AlreadyExistsError(absl::StrCat(
        "Enum constant '", constant.name, "' is already defined."));
  }
  constants.push_back(std::move(constant));
  return absl::OkStatus();
}

const EnumTypeDef::EnumConstant* absl_nullable EnumTypeDef::FindConstant(
    absl::string_view constant_name) const {
  for (const EnumConstant& constant : constants) {
    if (constant.name == constant_name) {
      return &constant;
    }
  }
  return nullptr;
}

TypeDefKindCase TypeDef::kind_case() const {
  return std::visit(
      absl::Overload{
          [](const ObjectTypeDef&) { return TypeDefKindCase::kObject; },
          [](const EnumTypeDef&) { return TypeDefKindCase::kEnum; },
      },
      variant_);
}

absl::string_view TypeDef::name() const {
  return std::visit(
      absl::Overload{
          [](const ObjectTypeDef& obj) -> absl::string_view {
            return obj.name;
          },
          [](const EnumTypeDef& e) -> absl::string_view { return e.name; },
      },
      variant_);
}

absl::string_view TypeDef::doc() const {
  return std::visit(
      absl::Overload{
          [](const ObjectTypeDef& obj) -> absl::string_view { return obj.doc; },
          [](const EnumTypeDef& e) -> absl::string_view { return e.doc; },
      },
      variant_);
}

}  // namespace cel
