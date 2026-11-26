// Copyright 2026 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "env/type_info.h"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "common/ast.h"
#include "common/type.h"
#include "common/type_kind.h"
#include "env/config.h"
#include "internal/status_macros.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"

namespace cel {
namespace {

std::optional<TypeKind> TypeNameToTypeKind(absl::string_view type_name) {
  // Excluded types:
  //   kUnknown
  //   kError
  //   kTypeParam
  //   kFunction
  //   kEnum

  static const absl::NoDestructor<
      absl::flat_hash_map<absl::string_view, TypeKind>>
      kTypeNameToTypeKind({
          {"null", TypeKind::kNull},
          {NullType::kName, TypeKind::kNull},
          {"google.protobuf.NullValue", TypeKind::kNull},
          {"bool", TypeKind::kBool},
          {"int", TypeKind::kInt},
          {"uint", TypeKind::kUint},
          {"double", TypeKind::kDouble},
          {"string", TypeKind::kString},
          {"bytes", TypeKind::kBytes},
          {"timestamp", TypeKind::kTimestamp},
          {TimestampType::kName, TypeKind::kTimestamp},
          {"duration", TypeKind::kDuration},
          {DurationType::kName, TypeKind::kDuration},
          {"list", TypeKind::kList},
          {"map", TypeKind::kMap},
          {"", TypeKind::kDyn},
          {"any", TypeKind::kAny},
          {"dyn", TypeKind::kDyn},
          {BoolWrapperType::kName, TypeKind::kBoolWrapper},
          {"bool_wrapper", TypeKind::kBoolWrapper},
          {IntWrapperType::kName, TypeKind::kIntWrapper},
          {"int_wrapper", TypeKind::kIntWrapper},
          {UintWrapperType::kName, TypeKind::kUintWrapper},
          {"uint_wrapper", TypeKind::kUintWrapper},
          {DoubleWrapperType::kName, TypeKind::kDoubleWrapper},
          {"double_wrapper", TypeKind::kDoubleWrapper},
          {StringWrapperType::kName, TypeKind::kStringWrapper},
          {"string_wrapper", TypeKind::kStringWrapper},
          {BytesWrapperType::kName, TypeKind::kBytesWrapper},
          {"bytes_wrapper", TypeKind::kBytesWrapper},
          {"type", TypeKind::kType},
      });
  if (auto it = kTypeNameToTypeKind->find(type_name);
      it != kTypeNameToTypeKind->end()) {
    return it->second;
  }

  return std::nullopt;
}
}  // namespace

absl::StatusOr<Type> TypeInfoToType(
    const Config::TypeInfo& type_info,
    const google::protobuf::DescriptorPool* descriptor_pool, google::protobuf::Arena* arena) {
  if (type_info.is_type_param) {
    return TypeParamType(type_info.name);
  }

  std::optional<TypeKind> type_kind = TypeNameToTypeKind(type_info.name);
  if (!type_kind.has_value()) {
    if (type_info.params.empty() && descriptor_pool != nullptr) {
      const google::protobuf::Descriptor* type =
          descriptor_pool->FindMessageTypeByName(type_info.name);
      if (type != nullptr) {
        return Type::Message(type);
      }
    }
    // TODO(uncreated-issue/88): use a TypeIntrospector to validate opaque types
    std::vector<Type> parameter_types;
    for (const Config::TypeInfo& param : type_info.params) {
      CEL_ASSIGN_OR_RETURN(Type parameter_type,
                           TypeInfoToType(param, descriptor_pool, arena));
      parameter_types.push_back(parameter_type);
    }

    return OpaqueType(arena, type_info.name, parameter_types);
  }

  switch (*type_kind) {
    case TypeKind::kNull:
      return NullType();
    case TypeKind::kBool:
      return BoolType();
    case TypeKind::kInt:
      return IntType();
    case TypeKind::kUint:
      return UintType();
    case TypeKind::kDouble:
      return DoubleType();
    case TypeKind::kString:
      return StringType();
    case TypeKind::kBytes:
      return BytesType();
    case TypeKind::kDuration:
      return DurationType();
    case TypeKind::kTimestamp:
      return TimestampType();
    case TypeKind::kList: {
      Type element_type;
      if (!type_info.params.empty()) {
        CEL_ASSIGN_OR_RETURN(
            element_type,
            TypeInfoToType(type_info.params[0], descriptor_pool, arena));
      } else {
        element_type = DynType();
      }
      return ListType(arena, element_type);
    }
    case TypeKind::kMap: {
      Type key_type = DynType();
      Type value_type = DynType();
      if (!type_info.params.empty()) {
        CEL_ASSIGN_OR_RETURN(key_type, TypeInfoToType(type_info.params[0],
                                                      descriptor_pool, arena));
      }
      if (type_info.params.size() > 1) {
        CEL_ASSIGN_OR_RETURN(
            value_type,
            TypeInfoToType(type_info.params[1], descriptor_pool, arena));
      }
      return MapType(arena, key_type, value_type);
    }
    case TypeKind::kDyn:
      return DynType();
    case TypeKind::kAny:
      return AnyType();
    case TypeKind::kBoolWrapper:
      return BoolWrapperType();
    case TypeKind::kIntWrapper:
      return IntWrapperType();
    case TypeKind::kUintWrapper:
      return UintWrapperType();
    case TypeKind::kDoubleWrapper:
      return DoubleWrapperType();
    case TypeKind::kStringWrapper:
      return StringWrapperType();
    case TypeKind::kBytesWrapper:
      return BytesWrapperType();
    case TypeKind::kType: {
      if (type_info.params.empty()) {
        return TypeType(arena, DynType());
      }
      CEL_ASSIGN_OR_RETURN(Type type, TypeInfoToType(type_info.params[0],
                                                     descriptor_pool, arena));
      return TypeType(arena, type);
    }
    default:
      return DynType();
  }
}
absl::StatusOr<TypeSpec> TypeInfoToTypeSpec(const Config::TypeInfo& type_info) {
  if (type_info.is_type_param) {
    return TypeSpec(ParamTypeSpec(type_info.name));
  }

  std::optional<TypeKind> type_kind = TypeNameToTypeKind(type_info.name);
  if (!type_kind.has_value()) {
    if (type_info.params.empty()) {
      return TypeSpec(MessageTypeSpec(type_info.name));
    } else {
      std::vector<TypeSpec> param_specs;
      param_specs.reserve(type_info.params.size());
      for (const Config::TypeInfo& param : type_info.params) {
        CEL_ASSIGN_OR_RETURN(TypeSpec param_spec, TypeInfoToTypeSpec(param));
        param_specs.push_back(std::move(param_spec));
      }
      return TypeSpec(AbstractType(type_info.name, std::move(param_specs)));
    }
  }

  switch (*type_kind) {
    case TypeKind::kNull:
      return TypeSpec(NullTypeSpec());
    case TypeKind::kBool:
      return TypeSpec(PrimitiveType::kBool);
    case TypeKind::kInt:
      return TypeSpec(PrimitiveType::kInt64);
    case TypeKind::kUint:
      return TypeSpec(PrimitiveType::kUint64);
    case TypeKind::kDouble:
      return TypeSpec(PrimitiveType::kDouble);
    case TypeKind::kString:
      return TypeSpec(PrimitiveType::kString);
    case TypeKind::kBytes:
      return TypeSpec(PrimitiveType::kBytes);
    case TypeKind::kTimestamp:
      return TypeSpec(WellKnownTypeSpec::kTimestamp);
    case TypeKind::kDuration:
      return TypeSpec(WellKnownTypeSpec::kDuration);
    case TypeKind::kList: {
      if (!type_info.params.empty()) {
        CEL_ASSIGN_OR_RETURN(TypeSpec elem_type,
                             TypeInfoToTypeSpec(type_info.params[0]));
        return TypeSpec(
            ListTypeSpec(std::make_unique<TypeSpec>(std::move(elem_type))));
      } else {
        return TypeSpec(ListTypeSpec());
      }
    }
    case TypeKind::kMap: {
      if (type_info.params.empty()) {
        return TypeSpec(MapTypeSpec());
      }
      CEL_ASSIGN_OR_RETURN(TypeSpec key_type,
                           TypeInfoToTypeSpec(type_info.params[0]));
      if (type_info.params.size() > 1) {
        CEL_ASSIGN_OR_RETURN(TypeSpec value_type,
                             TypeInfoToTypeSpec(type_info.params[1]));
        return TypeSpec(
            MapTypeSpec(std::make_unique<TypeSpec>(std::move(key_type)),
                        std::make_unique<TypeSpec>(std::move(value_type))));
      }
      return TypeSpec(MapTypeSpec(
          std::make_unique<TypeSpec>(std::move(key_type)), nullptr));
    }
    case TypeKind::kDyn:
      return TypeSpec(DynTypeSpec());
    case TypeKind::kAny:
      return TypeSpec(WellKnownTypeSpec::kAny);
    case TypeKind::kBoolWrapper:
      return TypeSpec(PrimitiveTypeWrapper(PrimitiveType::kBool));
    case TypeKind::kIntWrapper:
      return TypeSpec(PrimitiveTypeWrapper(PrimitiveType::kInt64));
    case TypeKind::kUintWrapper:
      return TypeSpec(PrimitiveTypeWrapper(PrimitiveType::kUint64));
    case TypeKind::kDoubleWrapper:
      return TypeSpec(PrimitiveTypeWrapper(PrimitiveType::kDouble));
    case TypeKind::kStringWrapper:
      return TypeSpec(PrimitiveTypeWrapper(PrimitiveType::kString));
    case TypeKind::kBytesWrapper:
      return TypeSpec(PrimitiveTypeWrapper(PrimitiveType::kBytes));
    case TypeKind::kType: {
      if (type_info.params.empty()) {
        return TypeSpec(std::make_unique<TypeSpec>(DynTypeSpec()));
      }
      CEL_ASSIGN_OR_RETURN(TypeSpec type_param,
                           TypeInfoToTypeSpec(type_info.params[0]));
      return TypeSpec(std::make_unique<TypeSpec>(std::move(type_param)));
    }
    default:
      return TypeSpec(DynTypeSpec());
  }
}

absl::StatusOr<Config::TypeInfo> TypeSpecToTypeInfo(const TypeSpec& type_spec) {
  Config::TypeInfo type_info;

  if (type_spec.has_dyn()) {
    type_info.name = "dyn";
  } else if (type_spec.has_null()) {
    type_info.name = "null";
  } else if (type_spec.has_primitive()) {
    switch (type_spec.primitive()) {
      case PrimitiveType::kBool:
        type_info.name = "bool";
        break;
      case PrimitiveType::kInt64:
        type_info.name = "int";
        break;
      case PrimitiveType::kUint64:
        type_info.name = "uint";
        break;
      case PrimitiveType::kDouble:
        type_info.name = "double";
        break;
      case PrimitiveType::kString:
        type_info.name = "string";
        break;
      case PrimitiveType::kBytes:
        type_info.name = "bytes";
        break;
      default:
        return absl::InvalidArgumentError("Unspecified primitive type");
    }
  } else if (type_spec.has_wrapper()) {
    switch (type_spec.wrapper()) {
      case PrimitiveType::kBool:
        type_info.name = "bool_wrapper";
        break;
      case PrimitiveType::kInt64:
        type_info.name = "int_wrapper";
        break;
      case PrimitiveType::kUint64:
        type_info.name = "uint_wrapper";
        break;
      case PrimitiveType::kDouble:
        type_info.name = "double_wrapper";
        break;
      case PrimitiveType::kString:
        type_info.name = "string_wrapper";
        break;
      case PrimitiveType::kBytes:
        type_info.name = "bytes_wrapper";
        break;
      default:
        return absl::InvalidArgumentError("Unspecified wrapper type");
    }
  } else if (type_spec.has_well_known()) {
    switch (type_spec.well_known()) {
      case WellKnownTypeSpec::kAny:
        type_info.name = "any";
        break;
      case WellKnownTypeSpec::kTimestamp:
        type_info.name = "timestamp";
        break;
      case WellKnownTypeSpec::kDuration:
        type_info.name = "duration";
        break;
      default:
        return absl::InvalidArgumentError("Unspecified well known type");
    }
  } else if (type_spec.has_list_type()) {
    type_info.name = "list";
    const ListTypeSpec& list_type = type_spec.list_type();
    if (list_type.has_elem_type() && list_type.elem_type().is_specified()) {
      CEL_ASSIGN_OR_RETURN(Config::TypeInfo param,
                           TypeSpecToTypeInfo(list_type.elem_type()));
      type_info.params.push_back(std::move(param));
    }
  } else if (type_spec.has_map_type()) {
    type_info.name = "map";
    const MapTypeSpec& map_type = type_spec.map_type();
    bool has_key =
        map_type.has_key_type() && map_type.key_type().is_specified();
    bool has_value =
        map_type.has_value_type() && map_type.value_type().is_specified();
    if (has_key || has_value) {
      if (has_key) {
        CEL_ASSIGN_OR_RETURN(Config::TypeInfo param,
                             TypeSpecToTypeInfo(map_type.key_type()));
        type_info.params.push_back(std::move(param));
      } else {
        type_info.params.push_back(Config::TypeInfo{.name = "dyn"});
      }
      if (has_value) {
        CEL_ASSIGN_OR_RETURN(Config::TypeInfo param_value,
                             TypeSpecToTypeInfo(map_type.value_type()));
        type_info.params.push_back(std::move(param_value));
      } else {
        type_info.params.push_back(Config::TypeInfo{.name = "dyn"});
      }
    }
  } else if (type_spec.has_message_type()) {
    type_info.name = type_spec.message_type().type();
  } else if (type_spec.has_type_param()) {
    type_info.name = type_spec.type_param().type();
    type_info.is_type_param = true;
  } else if (type_spec.has_type()) {
    type_info.name = "type";
    CEL_ASSIGN_OR_RETURN(Config::TypeInfo param,
                         TypeSpecToTypeInfo(type_spec.type()));
    type_info.params.push_back(std::move(param));
  } else if (type_spec.has_abstract_type()) {
    type_info.name = type_spec.abstract_type().name();
    for (const TypeSpec& param_spec :
         type_spec.abstract_type().parameter_types()) {
      CEL_ASSIGN_OR_RETURN(Config::TypeInfo param,
                           TypeSpecToTypeInfo(param_spec));
      type_info.params.push_back(std::move(param));
    }
  } else if (type_spec.has_error()) {
    return absl::InvalidArgumentError(
        "ErrorType cannot be converted to TypeInfo");
  } else if (type_spec.has_function()) {
    return absl::InvalidArgumentError(
        "FunctionType cannot be converted to TypeInfo");
  } else {
    return absl::InvalidArgumentError("Unknown TypeSpec kind");
  }

  return type_info;
}

}  // namespace cel
