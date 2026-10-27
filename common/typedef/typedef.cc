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

absl::Status StructTypeDef::Oneof::AddField(Field field) {
  if (field.name().empty()) {
    return absl::InvalidArgumentError("Field name cannot be empty.");
  }
  if (FindField(field.name()) != nullptr) {
    return absl::AlreadyExistsError(
        absl::StrCat("Field '", field.name(), "' is already defined."));
  }
  fields_.push_back(std::move(field));
  return absl::OkStatus();
}

const StructTypeDef::Field* absl_nullable StructTypeDef::Oneof::FindField(
    absl::string_view field_name) const {
  for (const Field& field : fields_) {
    if (field.name() == field_name) {
      return &field;
    }
  }
  return nullptr;
}

StructTypeDef::MemberKindCase StructTypeDef::Member::kind_case() const {
  return std::visit(absl::Overload{
                        [](const Field&) { return MemberKindCase::kField; },
                        [](const Oneof&) { return MemberKindCase::kOneof; },
                    },
                    variant_);
}

absl::Status StructTypeDef::AddField(Field field) {
  if (field.name().empty()) {
    return absl::InvalidArgumentError("Field name cannot be empty.");
  }
  if (FindField(field.name()) != nullptr) {
    return absl::AlreadyExistsError(
        absl::StrCat("Field '", field.name(), "' is already defined."));
  }
  members_.push_back(Member(std::move(field)));
  return absl::OkStatus();
}

absl::Status StructTypeDef::AddOneof(Oneof oneof) {
  for (const Field& field : oneof.fields()) {
    if (FindField(field.name()) != nullptr) {
      return absl::AlreadyExistsError(
          absl::StrCat("Field '", field.name(), "' is already defined."));
    }
  }
  members_.push_back(Member(std::move(oneof)));
  return absl::OkStatus();
}

const StructTypeDef::Field* absl_nullable StructTypeDef::FindField(
    absl::string_view field_name) const {
  for (const Member& member : members_) {
    switch (member.kind_case()) {
      case MemberKindCase::kField:
        if (member.field().name() == field_name) {
          return &member.field();
        }
        break;
      case MemberKindCase::kOneof:
        if (const Field* found = member.oneof().FindField(field_name);
            found != nullptr) {
          return found;
        }
        break;
    }
  }
  return nullptr;
}

absl::Status EnumTypeDef::AddConstant(EnumConstant constant) {
  if (constant.name().empty()) {
    return absl::InvalidArgumentError("Enum constant name cannot be empty.");
  }
  if (FindConstant(constant.name()) != nullptr) {
    return absl::AlreadyExistsError(absl::StrCat(
        "Enum constant '", constant.name(), "' is already defined."));
  }
  constants_.push_back(std::move(constant));
  return absl::OkStatus();
}

const EnumTypeDef::EnumConstant* absl_nullable EnumTypeDef::FindConstant(
    absl::string_view constant_name) const {
  for (const EnumConstant& constant : constants_) {
    if (constant.name() == constant_name) {
      return &constant;
    }
  }
  return nullptr;
}

TypeDefKindCase TypeDef::kind_case() const {
  return std::visit(
      absl::Overload{
          [](const StructTypeDef&) { return TypeDefKindCase::kStruct; },
          [](const EnumTypeDef&) { return TypeDefKindCase::kEnum; },
      },
      variant_);
}

}  // namespace cel
