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

#include "common/typedef/typedef_proto.h"

#include <string>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "common/ast.h"
#include "common/ast/constant_proto.h"
#include "common/ast/type_proto.h"
#include "common/constant.h"
#include "common/typedef/typedef.h"
#include "common/typedef/typedef.pb.h"
#include "internal/status_macros.h"
#include "google/protobuf/map.h"

namespace cel {
namespace {

absl::StatusOr<Aspect> AspectFromProto(const types::Aspect& proto) {
  absl::flat_hash_map<std::string, Constant> properties;
  properties.reserve(proto.properties_size());
  for (const auto& [key, value_proto] : proto.properties()) {
    Constant value;
    CEL_RETURN_IF_ERROR(ast_internal::ConstantFromProto(value_proto, value));
    properties.emplace(key, std::move(value));
  }
  return Aspect(std::move(properties));
}

absl::Status AspectToProto(const Aspect& aspect,
                           types::Aspect* absl_nonnull out) {
  auto* out_props = out->mutable_properties();
  for (const auto& [key, value] : aspect.properties()) {
    CEL_RETURN_IF_ERROR(
        ast_internal::ConstantToProto(value, &(*out_props)[key]));
  }
  return absl::OkStatus();
}

absl::StatusOr<absl::flat_hash_map<std::string, Aspect>> DecodeAspects(
    const google::protobuf::Map<std::string, types::Aspect>& aspects) {
  absl::flat_hash_map<std::string, Aspect> result;
  result.reserve(aspects.size());
  for (const auto& [aspect_name, aspect_proto] : aspects) {
    CEL_ASSIGN_OR_RETURN(Aspect aspect, AspectFromProto(aspect_proto));
    result.emplace(aspect_name, std::move(aspect));
  }
  return result;
}

absl::Status EncodeAspects(
    const absl::flat_hash_map<std::string, Aspect>& aspects,
    google::protobuf::Map<std::string, types::Aspect>* absl_nonnull out) {
  for (const auto& [aspect_name, aspect] : aspects) {
    CEL_RETURN_IF_ERROR(AspectToProto(aspect, &(*out)[aspect_name]));
  }
  return absl::OkStatus();
}

absl::StatusOr<StructTypeDef::Field> FieldFromProto(
    const types::Struct::Field& field_proto) {
  TypeSpec field_type;
  if (field_proto.has_type()) {
    CEL_ASSIGN_OR_RETURN(field_type,
                         ast_internal::TypeFromProto(field_proto.type()));
  }
  Constant default_value;
  if (field_proto.has_default_value()) {
    CEL_RETURN_IF_ERROR(ast_internal::ConstantFromProto(
        field_proto.default_value(), default_value));
  }
  CEL_ASSIGN_OR_RETURN(auto field_aspects,
                       DecodeAspects(field_proto.aspects()));
  return StructTypeDef::Field(field_proto.name(), std::move(field_type),
                              field_proto.doc(), std::move(default_value),
                              std::move(field_aspects));
}

absl::Status FieldToProto(const StructTypeDef::Field& field,
                          types::Struct::Field* absl_nonnull field_proto) {
  field_proto->set_name(field.name());
  if (field.type().is_specified()) {
    CEL_RETURN_IF_ERROR(
        ast_internal::TypeToProto(field.type(), field_proto->mutable_type()));
  }
  if (!field.doc().empty()) {
    field_proto->set_doc(field.doc());
  }
  if (field.default_value().has_value()) {
    CEL_RETURN_IF_ERROR(ast_internal::ConstantToProto(
        field.default_value(), field_proto->mutable_default_value()));
  }
  return EncodeAspects(field.aspects(), field_proto->mutable_aspects());
}

absl::StatusOr<StructTypeDef> StructTypeDefFromProto(
    const types::Struct& proto) {
  CEL_ASSIGN_OR_RETURN(auto aspects, DecodeAspects(proto.aspects()));
  StructTypeDef struct_def(proto.name(), proto.doc(), std::move(aspects));

  for (const types::Struct::Member& member_proto : proto.members()) {
    switch (member_proto.kind_case()) {
      case types::Struct::Member::kField: {
        CEL_ASSIGN_OR_RETURN(StructTypeDef::Field field,
                             FieldFromProto(member_proto.field()));
        CEL_RETURN_IF_ERROR(struct_def.AddField(std::move(field)));
        break;
      }
      case types::Struct::Member::kOneofType: {
        CEL_ASSIGN_OR_RETURN(
            auto oneof_aspects,
            DecodeAspects(member_proto.oneof_type().aspects()));
        StructTypeDef::Oneof oneof_def(std::move(oneof_aspects));
        for (const types::Struct::Field& field_proto :
             member_proto.oneof_type().fields()) {
          CEL_ASSIGN_OR_RETURN(StructTypeDef::Field field,
                               FieldFromProto(field_proto));
          CEL_RETURN_IF_ERROR(oneof_def.AddField(std::move(field)));
        }
        CEL_RETURN_IF_ERROR(struct_def.AddOneof(std::move(oneof_def)));
        break;
      }
      case types::Struct::Member::KIND_NOT_SET:
        return absl::InvalidArgumentError(
            "cel.types.Struct.Member must specify 'field' or 'oneof_type'");
    }
  }

  return struct_def;
}

absl::Status StructTypeDefToProto(const StructTypeDef& struct_def,
                                  types::Struct* absl_nonnull out) {
  if (!struct_def.name().empty()) {
    out->set_name(struct_def.name());
  }
  if (!struct_def.doc().empty()) {
    out->set_doc(struct_def.doc());
  }
  CEL_RETURN_IF_ERROR(
      EncodeAspects(struct_def.aspects(), out->mutable_aspects()));

  out->mutable_members()->Reserve(struct_def.members().size());
  for (const StructTypeDef::Member& member : struct_def.members()) {
    types::Struct::Member* member_proto = out->add_members();
    switch (member.kind_case()) {
      case StructTypeDef::MemberKindCase::kField:
        CEL_RETURN_IF_ERROR(
            FieldToProto(member.field(), member_proto->mutable_field()));
        break;
      case StructTypeDef::MemberKindCase::kOneof: {
        types::Struct::Oneof* oneof_proto = member_proto->mutable_oneof_type();
        oneof_proto->mutable_fields()->Reserve(member.oneof().fields().size());
        for (const StructTypeDef::Field& field : member.oneof().fields()) {
          CEL_RETURN_IF_ERROR(FieldToProto(field, oneof_proto->add_fields()));
        }
        CEL_RETURN_IF_ERROR(EncodeAspects(member.oneof().aspects(),
                                          oneof_proto->mutable_aspects()));
        break;
      }
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<EnumTypeDef> EnumTypeDefFromProto(const types::Enum& proto) {
  CEL_ASSIGN_OR_RETURN(auto aspects, DecodeAspects(proto.aspects()));
  EnumTypeDef enum_def(proto.name(), proto.doc(), std::move(aspects));

  for (const types::Enum::EnumConstant& constant_proto : proto.constants()) {
    CEL_ASSIGN_OR_RETURN(auto constant_aspects,
                         DecodeAspects(constant_proto.aspects()));
    CEL_RETURN_IF_ERROR(enum_def.AddConstant(EnumTypeDef::EnumConstant(
        constant_proto.name(), constant_proto.id(), constant_proto.doc(),
        std::move(constant_aspects))));
  }

  return enum_def;
}

absl::Status EnumTypeDefToProto(const EnumTypeDef& enum_def,
                                types::Enum* absl_nonnull out) {
  if (!enum_def.name().empty()) {
    out->set_name(enum_def.name());
  }
  if (!enum_def.doc().empty()) {
    out->set_doc(enum_def.doc());
  }
  CEL_RETURN_IF_ERROR(
      EncodeAspects(enum_def.aspects(), out->mutable_aspects()));

  out->mutable_constants()->Reserve(enum_def.constants().size());
  for (const EnumTypeDef::EnumConstant& constant : enum_def.constants()) {
    types::Enum::EnumConstant* constant_proto = out->add_constants();
    constant_proto->set_name(constant.name());
    constant_proto->set_id(constant.id());
    if (!constant.doc().empty()) {
      constant_proto->set_doc(constant.doc());
    }
    CEL_RETURN_IF_ERROR(
        EncodeAspects(constant.aspects(), constant_proto->mutable_aspects()));
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<TypeDef> TypeDefFromProto(const types::TypeDef& proto) {
  switch (proto.kind_case()) {
    case types::TypeDef::kStructType: {
      CEL_ASSIGN_OR_RETURN(StructTypeDef s,
                           StructTypeDefFromProto(proto.struct_type()));
      return TypeDef(std::move(s));
    }
    case types::TypeDef::kEnumType: {
      CEL_ASSIGN_OR_RETURN(EnumTypeDef e,
                           EnumTypeDefFromProto(proto.enum_type()));
      return TypeDef(std::move(e));
    }
    case types::TypeDef::KIND_NOT_SET:
      break;
  }
  return absl::InvalidArgumentError(
      "cel.types.TypeDef must specify 'struct_type' or 'enum_type'");
}

absl::Status TypeDefToProto(const TypeDef& type_def,
                            types::TypeDef* absl_nonnull out) {
  out->Clear();
  if (type_def.is_struct()) {
    return StructTypeDefToProto(type_def.struct_type(),
                                out->mutable_struct_type());
  }
  return EnumTypeDefToProto(type_def.enum_type(), out->mutable_enum_type());
}

absl::StatusOr<TypeDefSet> TypeDefSetFromProto(const types::TypeDefSet& proto) {
  TypeDefSet result;
  for (const types::TypeDef& item : proto.types()) {
    CEL_ASSIGN_OR_RETURN(TypeDef type_def, TypeDefFromProto(item));
    CEL_RETURN_IF_ERROR(result.AddTypeDef(std::move(type_def)));
  }
  return result;
}

absl::Status TypeDefSetToProto(const TypeDefSet& type_def_set,
                               types::TypeDefSet* absl_nonnull out) {
  out->Clear();
  out->mutable_types()->Reserve(type_def_set.types().size());
  for (const TypeDef& type_def : type_def_set.types()) {
    CEL_RETURN_IF_ERROR(TypeDefToProto(type_def, out->add_types()));
  }
  return absl::OkStatus();
}

}  // namespace cel
