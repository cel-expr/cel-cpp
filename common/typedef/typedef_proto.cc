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

#include <any>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cel/expr/checked.pb.h"
#include "google/protobuf/struct.pb.h"
#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "absl/types/variant.h"
#include "common/ast.h"
#include "common/ast/constant_proto.h"
#include "common/constant.h"
#include "common/typedef/aspect_proto.h"
#include "common/typedef/typedef.h"
#include "common/typedef/typedef.pb.h"
#include "internal/status_macros.h"

namespace cel {
namespace {

using TypePb = cel::expr::Type;

absl::StatusOr<TypeSpec> TypeSpecFromProto(const TypePb& type);

absl::StatusOr<PrimitiveType> PrimitiveFromProto(
    TypePb::PrimitiveType primitive_type) {
  switch (primitive_type) {
    case TypePb::PRIMITIVE_TYPE_UNSPECIFIED:
      return PrimitiveType::kPrimitiveTypeUnspecified;
    case TypePb::BOOL:
      return PrimitiveType::kBool;
    case TypePb::INT64:
      return PrimitiveType::kInt64;
    case TypePb::UINT64:
      return PrimitiveType::kUint64;
    case TypePb::DOUBLE:
      return PrimitiveType::kDouble;
    case TypePb::STRING:
      return PrimitiveType::kString;
    case TypePb::BYTES:
      return PrimitiveType::kBytes;
    default:
      return absl::InvalidArgumentError(
          "Illegal type specified for cel::expr::Type::PrimitiveType.");
  }
}

absl::StatusOr<WellKnownTypeSpec> WellKnownFromProto(
    TypePb::WellKnownType well_known_type) {
  switch (well_known_type) {
    case TypePb::WELL_KNOWN_TYPE_UNSPECIFIED:
      return WellKnownTypeSpec::kWellKnownTypeUnspecified;
    case TypePb::ANY:
      return WellKnownTypeSpec::kAny;
    case TypePb::TIMESTAMP:
      return WellKnownTypeSpec::kTimestamp;
    case TypePb::DURATION:
      return WellKnownTypeSpec::kDuration;
    default:
      return absl::InvalidArgumentError(
          "Illegal type specified for cel::expr::Type::WellKnownType.");
  }
}

absl::StatusOr<ListTypeSpec> ListTypeFromProto(
    const TypePb::ListType& list_type) {
  CEL_ASSIGN_OR_RETURN(TypeSpec elem_type,
                       TypeSpecFromProto(list_type.elem_type()));
  return ListTypeSpec(std::make_unique<TypeSpec>(std::move(elem_type)));
}

absl::StatusOr<MapTypeSpec> MapTypeFromProto(const TypePb::MapType& map_type) {
  CEL_ASSIGN_OR_RETURN(TypeSpec key_type,
                       TypeSpecFromProto(map_type.key_type()));
  CEL_ASSIGN_OR_RETURN(TypeSpec value_type,
                       TypeSpecFromProto(map_type.value_type()));
  return MapTypeSpec(std::make_unique<TypeSpec>(std::move(key_type)),
                     std::make_unique<TypeSpec>(std::move(value_type)));
}

absl::StatusOr<FunctionTypeSpec> FunctionTypeFromProto(
    const TypePb::FunctionType& function_type) {
  std::vector<TypeSpec> arg_types;
  arg_types.reserve(function_type.arg_types_size());
  for (const auto& arg_type : function_type.arg_types()) {
    CEL_ASSIGN_OR_RETURN(TypeSpec native_arg, TypeSpecFromProto(arg_type));
    arg_types.push_back(std::move(native_arg));
  }
  CEL_ASSIGN_OR_RETURN(TypeSpec native_result,
                       TypeSpecFromProto(function_type.result_type()));
  return FunctionTypeSpec(std::make_unique<TypeSpec>(std::move(native_result)),
                          std::move(arg_types));
}

absl::StatusOr<AbstractType> AbstractTypeFromProto(
    const TypePb::AbstractType& abstract_type) {
  std::vector<TypeSpec> parameter_types;
  parameter_types.reserve(abstract_type.parameter_types_size());
  for (const auto& parameter_type : abstract_type.parameter_types()) {
    CEL_ASSIGN_OR_RETURN(TypeSpec native_param,
                         TypeSpecFromProto(parameter_type));
    parameter_types.push_back(std::move(native_param));
  }
  return AbstractType(abstract_type.name(), std::move(parameter_types));
}

absl::StatusOr<TypeSpec> TypeSpecFromProto(const TypePb& type) {
  switch (type.type_kind_case()) {
    case TypePb::kDyn:
      return TypeSpec(DynTypeSpec());
    case TypePb::kNull:
      return TypeSpec(NullTypeSpec());
    case TypePb::kPrimitive: {
      CEL_ASSIGN_OR_RETURN(PrimitiveType p,
                           PrimitiveFromProto(type.primitive()));
      return TypeSpec(p);
    }
    case TypePb::kWrapper: {
      CEL_ASSIGN_OR_RETURN(PrimitiveType w, PrimitiveFromProto(type.wrapper()));
      return TypeSpec(PrimitiveTypeWrapper(w));
    }
    case TypePb::kWellKnown: {
      CEL_ASSIGN_OR_RETURN(WellKnownTypeSpec wk,
                           WellKnownFromProto(type.well_known()));
      return TypeSpec(wk);
    }
    case TypePb::kListType: {
      CEL_ASSIGN_OR_RETURN(ListTypeSpec lt,
                           ListTypeFromProto(type.list_type()));
      return TypeSpec(std::move(lt));
    }
    case TypePb::kMapType: {
      CEL_ASSIGN_OR_RETURN(MapTypeSpec mt, MapTypeFromProto(type.map_type()));
      return TypeSpec(std::move(mt));
    }
    case TypePb::kFunction: {
      CEL_ASSIGN_OR_RETURN(FunctionTypeSpec ft,
                           FunctionTypeFromProto(type.function()));
      return TypeSpec(std::move(ft));
    }
    case TypePb::kMessageType:
      return TypeSpec(MessageTypeSpec(type.message_type()));
    case TypePb::kTypeParam:
      return TypeSpec(ParamTypeSpec(type.type_param()));
    case TypePb::kType: {
      if (type.type().type_kind_case() == TypePb::TYPE_KIND_NOT_SET) {
        return TypeSpec(std::unique_ptr<TypeSpec>());
      }
      CEL_ASSIGN_OR_RETURN(TypeSpec inner, TypeSpecFromProto(type.type()));
      return TypeSpec(std::make_unique<TypeSpec>(std::move(inner)));
    }
    case TypePb::kError:
      return TypeSpec(ErrorTypeSpec::kValue);
    case TypePb::kAbstractType: {
      CEL_ASSIGN_OR_RETURN(AbstractType at,
                           AbstractTypeFromProto(type.abstract_type()));
      return TypeSpec(std::move(at));
    }
    case TypePb::TYPE_KIND_NOT_SET:
      return TypeSpec(UnsetTypeSpec());
    default:
      return absl::InvalidArgumentError(
          "Illegal type specified for cel::expr::Type.");
  }
}

absl::Status TypeSpecToProto(const TypeSpec& type, TypePb* absl_nonnull result);

struct TypeSpecKindToProtoVisitor {
  absl::Status operator()(PrimitiveType primitive) const {
    switch (primitive) {
      case PrimitiveType::kPrimitiveTypeUnspecified:
        result->set_primitive(TypePb::PRIMITIVE_TYPE_UNSPECIFIED);
        return absl::OkStatus();
      case PrimitiveType::kBool:
        result->set_primitive(TypePb::BOOL);
        return absl::OkStatus();
      case PrimitiveType::kInt64:
        result->set_primitive(TypePb::INT64);
        return absl::OkStatus();
      case PrimitiveType::kUint64:
        result->set_primitive(TypePb::UINT64);
        return absl::OkStatus();
      case PrimitiveType::kDouble:
        result->set_primitive(TypePb::DOUBLE);
        return absl::OkStatus();
      case PrimitiveType::kString:
        result->set_primitive(TypePb::STRING);
        return absl::OkStatus();
      case PrimitiveType::kBytes:
        result->set_primitive(TypePb::BYTES);
        return absl::OkStatus();
      default:
        return absl::InvalidArgumentError("Unsupported primitive type");
    }
  }

  absl::Status operator()(PrimitiveTypeWrapper wrapper) const {
    CEL_RETURN_IF_ERROR((*this)(wrapper.type()));
    auto wrapped = result->primitive();
    result->set_wrapper(wrapped);
    return absl::OkStatus();
  }

  absl::Status operator()(UnsetTypeSpec) const {
    result->clear_type_kind();
    return absl::OkStatus();
  }

  absl::Status operator()(DynTypeSpec) const {
    result->mutable_dyn();
    return absl::OkStatus();
  }

  absl::Status operator()(ErrorTypeSpec) const {
    result->mutable_error();
    return absl::OkStatus();
  }

  absl::Status operator()(NullTypeSpec) const {
    result->set_null(google::protobuf::NULL_VALUE);
    return absl::OkStatus();
  }

  absl::Status operator()(const ListTypeSpec& list_type) const {
    return TypeSpecToProto(list_type.elem_type(),
                           result->mutable_list_type()->mutable_elem_type());
  }

  absl::Status operator()(const MapTypeSpec& map_type) const {
    CEL_RETURN_IF_ERROR(TypeSpecToProto(
        map_type.key_type(), result->mutable_map_type()->mutable_key_type()));
    return TypeSpecToProto(map_type.value_type(),
                           result->mutable_map_type()->mutable_value_type());
  }

  absl::Status operator()(const MessageTypeSpec& message_type) const {
    result->set_message_type(message_type.type());
    return absl::OkStatus();
  }

  absl::Status operator()(const WellKnownTypeSpec& well_known_type) const {
    switch (well_known_type) {
      case WellKnownTypeSpec::kWellKnownTypeUnspecified:
        result->set_well_known(TypePb::WELL_KNOWN_TYPE_UNSPECIFIED);
        return absl::OkStatus();
      case WellKnownTypeSpec::kAny:
        result->set_well_known(TypePb::ANY);
        return absl::OkStatus();
      case WellKnownTypeSpec::kDuration:
        result->set_well_known(TypePb::DURATION);
        return absl::OkStatus();
      case WellKnownTypeSpec::kTimestamp:
        result->set_well_known(TypePb::TIMESTAMP);
        return absl::OkStatus();
      default:
        return absl::InvalidArgumentError("Unsupported well-known type");
    }
  }

  absl::Status operator()(const FunctionTypeSpec& function_type) const {
    CEL_RETURN_IF_ERROR(
        TypeSpecToProto(function_type.result_type(),
                        result->mutable_function()->mutable_result_type()));
    for (const TypeSpec& arg_type : function_type.arg_types()) {
      CEL_RETURN_IF_ERROR(TypeSpecToProto(
          arg_type, result->mutable_function()->add_arg_types()));
    }
    return absl::OkStatus();
  }

  absl::Status operator()(const AbstractType& type) const {
    auto* abstract_type_pb = result->mutable_abstract_type();
    abstract_type_pb->set_name(type.name());
    for (const TypeSpec& type_param : type.parameter_types()) {
      CEL_RETURN_IF_ERROR(
          TypeSpecToProto(type_param, abstract_type_pb->add_parameter_types()));
    }
    return absl::OkStatus();
  }

  absl::Status operator()(const std::unique_ptr<TypeSpec>& type_type) const {
    return TypeSpecToProto((type_type != nullptr) ? *type_type : TypeSpec(),
                           result->mutable_type());
  }

  absl::Status operator()(const ParamTypeSpec& param_type) const {
    result->set_type_param(param_type.type());
    return absl::OkStatus();
  }

  TypePb* absl_nonnull result;
};

absl::Status TypeSpecToProto(const TypeSpec& type,
                             TypePb* absl_nonnull result) {
  return absl::visit(TypeSpecKindToProtoVisitor{result}, type.type_kind());
}

absl::StatusOr<absl::flat_hash_map<std::string, std::any>> DecodeStructAspects(
    const types::Struct& proto, const AspectProtoRegistry& registry) {
  absl::flat_hash_map<std::string, std::any> result;
  for (const auto& [name, aspect] : registry.aspects()) {
    CEL_ASSIGN_OR_RETURN(std::any props, aspect->DecodeStructProperties(proto));
    if (props.has_value()) {
      result[aspect->name()] = std::move(props);
    }
  }
  return result;
}

absl::StatusOr<absl::flat_hash_map<std::string, std::any>> DecodeFieldAspects(
    const types::Struct::Field& proto, const AspectProtoRegistry& registry) {
  absl::flat_hash_map<std::string, std::any> result;
  for (const auto& [name, aspect] : registry.aspects()) {
    CEL_ASSIGN_OR_RETURN(std::any props, aspect->DecodeFieldProperties(proto));
    if (props.has_value()) {
      result[aspect->name()] = std::move(props);
    }
  }
  return result;
}

absl::StatusOr<absl::flat_hash_map<std::string, std::any>> DecodeEnumAspects(
    const types::Enum& proto, const AspectProtoRegistry& registry) {
  absl::flat_hash_map<std::string, std::any> result;
  for (const auto& [name, aspect] : registry.aspects()) {
    CEL_ASSIGN_OR_RETURN(std::any props, aspect->DecodeEnumProperties(proto));
    if (props.has_value()) {
      result[aspect->name()] = std::move(props);
    }
  }
  return result;
}

absl::StatusOr<absl::flat_hash_map<std::string, std::any>>
DecodeEnumConstantAspects(const types::Enum::EnumConstant& proto,
                          const AspectProtoRegistry& registry) {
  absl::flat_hash_map<std::string, std::any> result;
  for (const auto& [name, aspect] : registry.aspects()) {
    CEL_ASSIGN_OR_RETURN(std::any props,
                         aspect->DecodeEnumConstantProperties(proto));
    if (props.has_value()) {
      result[aspect->name()] = std::move(props);
    }
  }
  return result;
}

absl::Status EncodeStructAspects(
    const absl::flat_hash_map<std::string, std::any>& properties,
    types::Struct* absl_nonnull out, const AspectProtoRegistry& registry) {
  for (const auto& [aspect_name, props] : properties) {
    if (!props.has_value()) {
      continue;
    }
    const AspectProto* aspect = registry.Find(aspect_name);
    if (aspect == nullptr) {
      continue;
    }
    CEL_RETURN_IF_ERROR(aspect->EncodeStructProperties(props, out));
  }
  return absl::OkStatus();
}

absl::Status EncodeFieldAspects(
    const absl::flat_hash_map<std::string, std::any>& properties,
    types::Struct::Field* absl_nonnull out,
    const AspectProtoRegistry& registry) {
  for (const auto& [aspect_name, props] : properties) {
    if (!props.has_value()) {
      continue;
    }
    const AspectProto* aspect = registry.Find(aspect_name);
    if (aspect == nullptr) {
      continue;
    }
    CEL_RETURN_IF_ERROR(aspect->EncodeFieldProperties(props, out));
  }
  return absl::OkStatus();
}

absl::Status EncodeEnumAspects(
    const absl::flat_hash_map<std::string, std::any>& properties,
    types::Enum* absl_nonnull out, const AspectProtoRegistry& registry) {
  for (const auto& [aspect_name, props] : properties) {
    if (!props.has_value()) {
      continue;
    }
    const AspectProto* aspect = registry.Find(aspect_name);
    if (aspect == nullptr) {
      continue;
    }
    CEL_RETURN_IF_ERROR(aspect->EncodeEnumProperties(props, out));
  }
  return absl::OkStatus();
}

absl::Status EncodeEnumConstantAspects(
    const absl::flat_hash_map<std::string, std::any>& properties,
    types::Enum::EnumConstant* absl_nonnull out,
    const AspectProtoRegistry& registry) {
  for (const auto& [aspect_name, props] : properties) {
    if (!props.has_value()) {
      continue;
    }
    const AspectProto* aspect = registry.Find(aspect_name);
    if (aspect == nullptr) {
      continue;
    }
    CEL_RETURN_IF_ERROR(aspect->EncodeEnumConstantProperties(props, out));
  }
  return absl::OkStatus();
}

absl::StatusOr<StructTypeDef::Field> FieldFromProto(
    const types::Struct::Field& field_proto,
    const AspectProtoRegistry& registry) {
  TypeSpec field_type;
  if (field_proto.has_type()) {
    CEL_ASSIGN_OR_RETURN(field_type, TypeSpecFromProto(field_proto.type()));
  }
  Constant default_value;
  if (field_proto.has_default_value()) {
    CEL_RETURN_IF_ERROR(ast_internal::ConstantFromProto(
        field_proto.default_value(), default_value));
  }
  CEL_ASSIGN_OR_RETURN(auto field_aspect_props,
                       DecodeFieldAspects(field_proto, registry));
  return StructTypeDef::Field(field_proto.name(), std::move(field_type),
                              field_proto.doc(), std::move(default_value),
                              std::move(field_aspect_props));
}

absl::Status FieldToProto(const StructTypeDef::Field& field,
                          types::Struct::Field* absl_nonnull field_proto,
                          const AspectProtoRegistry& registry) {
  if (!field.name().empty()) {
    field_proto->set_name(field.name());
  }
  if (field.type().is_specified()) {
    CEL_RETURN_IF_ERROR(
        TypeSpecToProto(field.type(), field_proto->mutable_type()));
  }
  if (!field.doc().empty()) {
    field_proto->set_doc(field.doc());
  }
  if (field.default_value().has_value()) {
    CEL_RETURN_IF_ERROR(ast_internal::ConstantToProto(
        field.default_value(), field_proto->mutable_default_value()));
  }
  return EncodeFieldAspects(field.aspects(), field_proto, registry);
}

}  // namespace

absl::StatusOr<StructTypeDef> StructTypeDefFromProto(
    const types::Struct& proto, const AspectProtoRegistry& registry) {
  CEL_ASSIGN_OR_RETURN(auto aspect_props, DecodeStructAspects(proto, registry));
  StructTypeDef struct_def(proto.name(), proto.doc(), std::move(aspect_props));

  for (const types::Struct::Member& member_proto : proto.members()) {
    switch (member_proto.kind_case()) {
      case types::Struct::Member::kField: {
        CEL_ASSIGN_OR_RETURN(StructTypeDef::Field field,
                             FieldFromProto(member_proto.field(), registry));
        CEL_RETURN_IF_ERROR(struct_def.AddField(std::move(field)));
        break;
      }
      case types::Struct::Member::kOneofType: {
        StructTypeDef::Oneof oneof_def;
        for (const types::Struct::Field& field_proto :
             member_proto.oneof_type().fields()) {
          CEL_ASSIGN_OR_RETURN(StructTypeDef::Field field,
                               FieldFromProto(field_proto, registry));
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
                                  types::Struct* absl_nonnull out,
                                  const AspectProtoRegistry& registry) {
  out->Clear();
  if (!struct_def.name().empty()) {
    out->set_name(struct_def.name());
  }
  if (!struct_def.doc().empty()) {
    out->set_doc(struct_def.doc());
  }
  CEL_RETURN_IF_ERROR(EncodeStructAspects(struct_def.aspects(), out, registry));

  out->mutable_members()->Reserve(struct_def.members().size());
  for (const StructTypeDef::Member& member : struct_def.members()) {
    types::Struct::Member* member_proto = out->add_members();
    switch (member.kind_case()) {
      case StructTypeDef::MemberKindCase::kField:
        CEL_RETURN_IF_ERROR(FieldToProto(
            member.field(), member_proto->mutable_field(), registry));
        break;
      case StructTypeDef::MemberKindCase::kOneof: {
        types::Struct::Oneof* oneof_proto = member_proto->mutable_oneof_type();
        oneof_proto->mutable_fields()->Reserve(member.oneof().fields().size());
        for (const StructTypeDef::Field& field : member.oneof().fields()) {
          CEL_RETURN_IF_ERROR(
              FieldToProto(field, oneof_proto->add_fields(), registry));
        }
        break;
      }
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<types::Struct> StructTypeDefToProto(
    const StructTypeDef& struct_def, const AspectProtoRegistry& registry) {
  types::Struct out;
  CEL_RETURN_IF_ERROR(StructTypeDefToProto(struct_def, &out, registry));
  return out;
}

absl::StatusOr<EnumTypeDef> EnumTypeDefFromProto(
    const types::Enum& proto, const AspectProtoRegistry& registry) {
  CEL_ASSIGN_OR_RETURN(auto aspect_props, DecodeEnumAspects(proto, registry));
  EnumTypeDef enum_def(proto.name(), proto.doc(), std::move(aspect_props));

  for (const types::Enum::EnumConstant& constant_proto : proto.constants()) {
    CEL_ASSIGN_OR_RETURN(auto constant_aspect_props,
                         DecodeEnumConstantAspects(constant_proto, registry));
    CEL_RETURN_IF_ERROR(enum_def.AddConstant(EnumTypeDef::EnumConstant(
        constant_proto.name(), constant_proto.id(), constant_proto.doc(),
        std::move(constant_aspect_props))));
  }

  return enum_def;
}

absl::Status EnumTypeDefToProto(const EnumTypeDef& enum_def,
                                types::Enum* absl_nonnull out,
                                const AspectProtoRegistry& registry) {
  out->Clear();
  if (!enum_def.name().empty()) {
    out->set_name(enum_def.name());
  }
  if (!enum_def.doc().empty()) {
    out->set_doc(enum_def.doc());
  }
  CEL_RETURN_IF_ERROR(EncodeEnumAspects(enum_def.aspects(), out, registry));

  out->mutable_constants()->Reserve(enum_def.constants().size());
  for (const EnumTypeDef::EnumConstant& constant : enum_def.constants()) {
    types::Enum::EnumConstant* constant_proto = out->add_constants();
    if (!constant.name().empty()) {
      constant_proto->set_name(constant.name());
    }
    constant_proto->set_id(constant.id());
    if (!constant.doc().empty()) {
      constant_proto->set_doc(constant.doc());
    }
    CEL_RETURN_IF_ERROR(EncodeEnumConstantAspects(constant.aspects(),
                                                  constant_proto, registry));
  }
  return absl::OkStatus();
}

absl::StatusOr<types::Enum> EnumTypeDefToProto(
    const EnumTypeDef& enum_def, const AspectProtoRegistry& registry) {
  types::Enum out;
  CEL_RETURN_IF_ERROR(EnumTypeDefToProto(enum_def, &out, registry));
  return out;
}

absl::StatusOr<TypeDef> TypeDefFromProto(const types::TypeDef& proto,
                                         const AspectProtoRegistry& registry) {
  switch (proto.kind_case()) {
    case types::TypeDef::kStructType: {
      CEL_ASSIGN_OR_RETURN(StructTypeDef s, StructTypeDefFromProto(
                                                proto.struct_type(), registry));
      return TypeDef(std::move(s));
    }
    case types::TypeDef::kEnumType: {
      CEL_ASSIGN_OR_RETURN(EnumTypeDef e,
                           EnumTypeDefFromProto(proto.enum_type(), registry));
      return TypeDef(std::move(e));
    }
    case types::TypeDef::KIND_NOT_SET:
      break;
  }
  return absl::InvalidArgumentError(
      "cel.types.TypeDef must specify 'struct_type' or 'enum_type'");
}

absl::Status TypeDefToProto(const TypeDef& type_def,
                            types::TypeDef* absl_nonnull out,
                            const AspectProtoRegistry& registry) {
  out->Clear();
  if (type_def.is_struct()) {
    return StructTypeDefToProto(type_def.struct_type(),
                                out->mutable_struct_type(), registry);
  }
  return EnumTypeDefToProto(type_def.enum_type(), out->mutable_enum_type(),
                            registry);
}

absl::StatusOr<types::TypeDef> TypeDefToProto(
    const TypeDef& type_def, const AspectProtoRegistry& registry) {
  types::TypeDef out;
  CEL_RETURN_IF_ERROR(TypeDefToProto(type_def, &out, registry));
  return out;
}

absl::StatusOr<std::vector<TypeDef>> TypeDefsFromProto(
    const types::TypeDefSet& proto, const AspectProtoRegistry& registry) {
  std::vector<TypeDef> result;
  result.reserve(proto.types_size());
  for (const types::TypeDef& item : proto.types()) {
    CEL_ASSIGN_OR_RETURN(TypeDef type_def, TypeDefFromProto(item, registry));
    result.push_back(std::move(type_def));
  }
  return result;
}

absl::Status TypeDefsToProto(absl::Span<const TypeDef> type_defs,
                             types::TypeDefSet* absl_nonnull out,
                             const AspectProtoRegistry& registry) {
  out->Clear();
  out->mutable_types()->Reserve(type_defs.size());
  for (const TypeDef& type_def : type_defs) {
    CEL_RETURN_IF_ERROR(TypeDefToProto(type_def, out->add_types(), registry));
  }
  return absl::OkStatus();
}

absl::StatusOr<types::TypeDefSet> TypeDefsToProto(
    absl::Span<const TypeDef> type_defs, const AspectProtoRegistry& registry) {
  types::TypeDefSet out;
  CEL_RETURN_IF_ERROR(TypeDefsToProto(type_defs, &out, registry));
  return out;
}

}  // namespace cel
