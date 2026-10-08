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

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cel/expr/checked.pb.h"
#include "google/protobuf/struct.pb.h"
#include "absl/algorithm/container.h"
#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "absl/types/variant.h"
#include "common/ast.h"
#include "common/ast/constant_proto.h"
#include "common/typedef/schema.h"
#include "common/typedef/schema_proto.h"
#include "common/typedef/typedef.h"
#include "common/typedef/typedef.pb.h"
#include "internal/status_macros.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"

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

size_t CountExtensions(const google::protobuf::Message& message) {
  std::vector<const google::protobuf::FieldDescriptor*> fields;
  message.GetReflection()->ListFields(message, &fields);
  size_t extension_count = 0;
  for (const google::protobuf::FieldDescriptor* field : fields) {
    if (field->is_extension()) {
      ++extension_count;
    }
  }
  return extension_count;
}

std::vector<const SchemaProto* absl_nonnull> SortedRegistrySchemas(
    const SchemaProtoRegistry& registry) {
  std::vector<const SchemaProto* absl_nonnull> schemas;
  schemas.reserve(registry.schemas().size());
  for (const auto& [_, schema] : registry.schemas()) {
    if (schema != nullptr) {
      schemas.push_back(schema.get());
    }
  }
  absl::c_sort(schemas, [](const SchemaProto* absl_nonnull lhs,
                           const SchemaProto* absl_nonnull rhs) {
    return lhs->name() < rhs->name();
  });
  return schemas;
}

template <typename PropertiesMap>
std::vector<std::string> SortedPropertyKeys(const PropertiesMap& properties) {
  std::vector<std::string> keys;
  keys.reserve(properties.size());
  for (const auto& [key, props] : properties) {
    if (props != nullptr) {
      keys.push_back(key);
    }
  }
  absl::c_sort(keys);
  return keys;
}

absl::StatusOr<ObjectSchemaSpecificProperties>
DecodeObjectSchemaSpecificProperties(const types::Object& proto,
                                     const SchemaProtoRegistry& registry) {
  const size_t extension_count = CountExtensions(proto);
  ObjectSchemaSpecificProperties result;
  for (const SchemaProto* absl_nonnull schema :
       SortedRegistrySchemas(registry)) {
    CEL_ASSIGN_OR_RETURN(std::unique_ptr<SchemaObjectProperties> props,
                         schema->DecodeObjectProperties(proto));
    if (props != nullptr) {
      result[schema->name()] = std::move(props);
    }
  }
  if (result.size() < extension_count) {
    return absl::InvalidArgumentError(
        "Unsupported schema extension on cel.types.Object");
  }
  return result;
}

absl::StatusOr<FieldSchemaSpecificProperties>
DecodeFieldSchemaSpecificProperties(const types::Object::Field& proto,
                                    const SchemaProtoRegistry& registry) {
  const size_t extension_count = CountExtensions(proto);
  FieldSchemaSpecificProperties result;
  for (const SchemaProto* absl_nonnull schema :
       SortedRegistrySchemas(registry)) {
    CEL_ASSIGN_OR_RETURN(std::unique_ptr<SchemaFieldProperties> props,
                         schema->DecodeFieldProperties(proto));
    if (props != nullptr) {
      result[schema->name()] = std::move(props);
    }
  }
  if (result.size() < extension_count) {
    return absl::InvalidArgumentError(
        "Unsupported schema extension on cel.types.Object.Field");
  }
  return result;
}

absl::StatusOr<EnumSchemaSpecificProperties> DecodeEnumSchemaSpecificProperties(
    const types::Enum& proto, const SchemaProtoRegistry& registry) {
  const size_t extension_count = CountExtensions(proto);
  EnumSchemaSpecificProperties result;
  for (const SchemaProto* absl_nonnull schema :
       SortedRegistrySchemas(registry)) {
    CEL_ASSIGN_OR_RETURN(std::unique_ptr<SchemaEnumProperties> props,
                         schema->DecodeEnumProperties(proto));
    if (props != nullptr) {
      result[schema->name()] = std::move(props);
    }
  }
  if (result.size() < extension_count) {
    return absl::InvalidArgumentError(
        "Unsupported schema extension on cel.types.Enum");
  }
  return result;
}

absl::StatusOr<EnumConstantSchemaSpecificProperties>
DecodeEnumConstantSchemaSpecificProperties(
    const types::Enum::EnumConstant& proto,
    const SchemaProtoRegistry& registry) {
  const size_t extension_count = CountExtensions(proto);
  EnumConstantSchemaSpecificProperties result;
  for (const SchemaProto* absl_nonnull schema :
       SortedRegistrySchemas(registry)) {
    CEL_ASSIGN_OR_RETURN(std::unique_ptr<SchemaEnumConstantProperties> props,
                         schema->DecodeEnumConstantProperties(proto));
    if (props != nullptr) {
      result[schema->name()] = std::move(props);
    }
  }
  if (result.size() < extension_count) {
    return absl::InvalidArgumentError(
        "Unsupported schema extension on cel.types.Enum.EnumConstant");
  }
  return result;
}

absl::Status EncodeObjectSchemaSpecificProperties(
    const ObjectSchemaSpecificProperties& properties,
    types::Object* absl_nonnull out, const SchemaProtoRegistry& registry) {
  for (const std::string& schema_name : SortedPropertyKeys(properties)) {
    const SchemaProto* schema = registry.Find(schema_name);
    if (schema == nullptr) {
      return absl::InvalidArgumentError(
          absl::StrCat("Unsupported schema: '", schema_name, "'"));
    }
    CEL_RETURN_IF_ERROR(
        schema->EncodeObjectProperties(*properties.at(schema_name), out));
  }
  return absl::OkStatus();
}

absl::Status EncodeFieldSchemaSpecificProperties(
    const FieldSchemaSpecificProperties& properties,
    types::Object::Field* absl_nonnull out,
    const SchemaProtoRegistry& registry) {
  for (const std::string& schema_name : SortedPropertyKeys(properties)) {
    const SchemaProto* schema = registry.Find(schema_name);
    if (schema == nullptr) {
      return absl::InvalidArgumentError(
          absl::StrCat("Unsupported schema: '", schema_name, "'"));
    }
    CEL_RETURN_IF_ERROR(
        schema->EncodeFieldProperties(*properties.at(schema_name), out));
  }
  return absl::OkStatus();
}

absl::Status EncodeEnumSchemaSpecificProperties(
    const EnumSchemaSpecificProperties& properties,
    types::Enum* absl_nonnull out, const SchemaProtoRegistry& registry) {
  for (const std::string& schema_name : SortedPropertyKeys(properties)) {
    const SchemaProto* schema = registry.Find(schema_name);
    if (schema == nullptr) {
      return absl::InvalidArgumentError(
          absl::StrCat("Unsupported schema: '", schema_name, "'"));
    }
    CEL_RETURN_IF_ERROR(
        schema->EncodeEnumProperties(*properties.at(schema_name), out));
  }
  return absl::OkStatus();
}

absl::Status EncodeEnumConstantSchemaSpecificProperties(
    const EnumConstantSchemaSpecificProperties& properties,
    types::Enum::EnumConstant* absl_nonnull out,
    const SchemaProtoRegistry& registry) {
  for (const std::string& schema_name : SortedPropertyKeys(properties)) {
    const SchemaProto* schema = registry.Find(schema_name);
    if (schema == nullptr) {
      return absl::InvalidArgumentError(
          absl::StrCat("Unsupported schema: '", schema_name, "'"));
    }
    CEL_RETURN_IF_ERROR(
        schema->EncodeEnumConstantProperties(*properties.at(schema_name), out));
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<ObjectTypeDef> ObjectTypeDefFromProto(
    const types::Object& proto, const SchemaProtoRegistry& registry) {
  ObjectTypeDef object_def;
  object_def.name = proto.name();
  object_def.doc = proto.doc();
  CEL_ASSIGN_OR_RETURN(object_def.schema_specific_properties,
                       DecodeObjectSchemaSpecificProperties(proto, registry));

  for (const types::Object::Field& field_proto : proto.fields()) {
    ObjectTypeDef::Field field;
    field.name = field_proto.name();
    if (field_proto.has_type()) {
      CEL_ASSIGN_OR_RETURN(field.type, TypeSpecFromProto(field_proto.type()));
    }
    field.doc = field_proto.doc();
    if (field_proto.has_default_value()) {
      CEL_RETURN_IF_ERROR(ast_internal::ConstantFromProto(
          field_proto.default_value(), field.default_value));
    }
    CEL_ASSIGN_OR_RETURN(
        field.schema_specific_properties,
        DecodeFieldSchemaSpecificProperties(field_proto, registry));
    CEL_RETURN_IF_ERROR(object_def.AddField(std::move(field)));
  }

  return object_def;
}

absl::Status ObjectTypeDefToProto(const ObjectTypeDef& object_def,
                                  types::Object* absl_nonnull out,
                                  const SchemaProtoRegistry& registry) {
  out->Clear();
  if (!object_def.name.empty()) {
    out->set_name(object_def.name);
  }
  if (!object_def.doc.empty()) {
    out->set_doc(object_def.doc);
  }
  CEL_RETURN_IF_ERROR(EncodeObjectSchemaSpecificProperties(
      object_def.schema_specific_properties, out, registry));

  out->mutable_fields()->Reserve(object_def.fields.size());
  for (const ObjectTypeDef::Field& field : object_def.fields) {
    types::Object::Field* field_proto = out->add_fields();
    if (!field.name.empty()) {
      field_proto->set_name(field.name);
    }
    if (field.type.is_specified()) {
      CEL_RETURN_IF_ERROR(
          TypeSpecToProto(field.type, field_proto->mutable_type()));
    }
    if (!field.doc.empty()) {
      field_proto->set_doc(field.doc);
    }
    if (field.default_value.has_value()) {
      CEL_RETURN_IF_ERROR(ast_internal::ConstantToProto(
          field.default_value, field_proto->mutable_default_value()));
    }
    CEL_RETURN_IF_ERROR(EncodeFieldSchemaSpecificProperties(
        field.schema_specific_properties, field_proto, registry));
  }
  return absl::OkStatus();
}

absl::StatusOr<types::Object> ObjectTypeDefToProto(
    const ObjectTypeDef& object_def, const SchemaProtoRegistry& registry) {
  types::Object out;
  CEL_RETURN_IF_ERROR(ObjectTypeDefToProto(object_def, &out, registry));
  return out;
}

absl::StatusOr<EnumTypeDef> EnumTypeDefFromProto(
    const types::Enum& proto, const SchemaProtoRegistry& registry) {
  EnumTypeDef enum_def;
  enum_def.name = proto.name();
  enum_def.doc = proto.doc();
  CEL_ASSIGN_OR_RETURN(enum_def.schema_specific_properties,
                       DecodeEnumSchemaSpecificProperties(proto, registry));

  for (const types::Enum::EnumConstant& constant_proto : proto.constants()) {
    EnumTypeDef::EnumConstant constant;
    constant.name = constant_proto.name();
    constant.id = constant_proto.id();
    constant.doc = constant_proto.doc();
    CEL_ASSIGN_OR_RETURN(
        constant.schema_specific_properties,
        DecodeEnumConstantSchemaSpecificProperties(constant_proto, registry));
    CEL_RETURN_IF_ERROR(enum_def.AddConstant(std::move(constant)));
  }

  return enum_def;
}

absl::Status EnumTypeDefToProto(const EnumTypeDef& enum_def,
                                types::Enum* absl_nonnull out,
                                const SchemaProtoRegistry& registry) {
  out->Clear();
  if (!enum_def.name.empty()) {
    out->set_name(enum_def.name);
  }
  if (!enum_def.doc.empty()) {
    out->set_doc(enum_def.doc);
  }
  CEL_RETURN_IF_ERROR(EncodeEnumSchemaSpecificProperties(
      enum_def.schema_specific_properties, out, registry));

  out->mutable_constants()->Reserve(enum_def.constants.size());
  for (const EnumTypeDef::EnumConstant& constant : enum_def.constants) {
    types::Enum::EnumConstant* constant_proto = out->add_constants();
    if (!constant.name.empty()) {
      constant_proto->set_name(constant.name);
    }
    constant_proto->set_id(constant.id);
    if (!constant.doc.empty()) {
      constant_proto->set_doc(constant.doc);
    }
    CEL_RETURN_IF_ERROR(EncodeEnumConstantSchemaSpecificProperties(
        constant.schema_specific_properties, constant_proto, registry));
  }
  return absl::OkStatus();
}

absl::StatusOr<types::Enum> EnumTypeDefToProto(
    const EnumTypeDef& enum_def, const SchemaProtoRegistry& registry) {
  types::Enum out;
  CEL_RETURN_IF_ERROR(EnumTypeDefToProto(enum_def, &out, registry));
  return out;
}

absl::StatusOr<TypeDef> TypeDefFromProto(const types::TypeDef& proto,
                                         const SchemaProtoRegistry& registry) {
  switch (proto.kind_case()) {
    case types::TypeDef::kObjectType: {
      CEL_ASSIGN_OR_RETURN(
          ObjectTypeDef obj,
          ObjectTypeDefFromProto(proto.object_type(), registry));
      return TypeDef(std::move(obj));
    }
    case types::TypeDef::kEnumType: {
      CEL_ASSIGN_OR_RETURN(EnumTypeDef e,
                           EnumTypeDefFromProto(proto.enum_type(), registry));
      return TypeDef(std::move(e));
    }
    case types::TypeDef::KIND_NOT_SET:
      return absl::InvalidArgumentError(
          "cel.types.TypeDef must specify 'object_type' or 'enum_type'");
  }
  return absl::InvalidArgumentError(
      "cel.types.TypeDef must specify 'object_type' or 'enum_type'");
}

absl::Status TypeDefToProto(const TypeDef& type_def,
                            types::TypeDef* absl_nonnull out,
                            const SchemaProtoRegistry& registry) {
  out->Clear();
  if (type_def.is_object()) {
    return ObjectTypeDefToProto(type_def.object_type(),
                                out->mutable_object_type(), registry);
  }
  return EnumTypeDefToProto(type_def.enum_type(), out->mutable_enum_type(),
                            registry);
}

absl::StatusOr<types::TypeDef> TypeDefToProto(
    const TypeDef& type_def, const SchemaProtoRegistry& registry) {
  types::TypeDef out;
  CEL_RETURN_IF_ERROR(TypeDefToProto(type_def, &out, registry));
  return out;
}

absl::StatusOr<std::vector<TypeDef>> TypeDefsFromProto(
    const types::TypeDefSet& proto, const SchemaProtoRegistry& registry) {
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
                             const SchemaProtoRegistry& registry) {
  out->Clear();
  out->mutable_types()->Reserve(type_defs.size());
  for (const TypeDef& type_def : type_defs) {
    CEL_RETURN_IF_ERROR(TypeDefToProto(type_def, out->add_types(), registry));
  }
  return absl::OkStatus();
}

absl::StatusOr<types::TypeDefSet> TypeDefsToProto(
    absl::Span<const TypeDef> type_defs, const SchemaProtoRegistry& registry) {
  types::TypeDefSet out;
  CEL_RETURN_IF_ERROR(TypeDefsToProto(type_defs, &out, registry));
  return out;
}

absl::StatusOr<TypeDefSet> TypeDefSetFromProto(
    const types::TypeDefSet& proto, const SchemaProtoRegistry& registry) {
  TypeDefSet result;
  for (const types::TypeDef& item : proto.types()) {
    CEL_ASSIGN_OR_RETURN(TypeDef type_def, TypeDefFromProto(item, registry));
    CEL_RETURN_IF_ERROR(result.AddTypeDef(std::move(type_def)));
  }
  return result;
}

absl::Status TypeDefSetToProto(const TypeDefSet& type_def_set,
                               types::TypeDefSet* absl_nonnull out,
                               const SchemaProtoRegistry& registry) {
  return TypeDefsToProto(type_def_set.types(), out, registry);
}

absl::StatusOr<types::TypeDefSet> TypeDefSetToProto(
    const TypeDefSet& type_def_set, const SchemaProtoRegistry& registry) {
  return TypeDefsToProto(type_def_set.types(), registry);
}

}  // namespace cel
