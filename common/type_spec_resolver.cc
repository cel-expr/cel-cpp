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

#include "common/type_spec_resolver.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "common/ast.h"
#include "common/descriptor_pool_type_introspector.h"
#include "common/type.h"
#include "common/type_introspector.h"
#include "common/type_kind.h"
#include "internal/status_macros.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"

#undef GetMessage

namespace cel {
namespace {

absl::StatusOr<absl::optional<Type>> ResolveNamedType(
    absl::string_view name, const TypeIntrospector& type_introspector) {
  absl::optional<Type> type = FindWellKnownType(name);
  if (type.has_value()) {
    return type;
  }
  return type_introspector.FindType(name);
}

bool TypeAcceptsParameters(const Type& type) {
  return type.IsOpaque() || type.IsType() || type.IsList() || type.IsMap();
}
}  // namespace

absl::StatusOr<Type> ConvertTypeSpecToType(
    const TypeSpec& type_spec, const TypeIntrospector& type_introspector,
    google::protobuf::Arena* arena) {
  if (type_spec.has_null()) return Type(NullType{});
  if (type_spec.has_dyn()) return Type(DynType{});

  if (type_spec.has_primitive()) {
    switch (type_spec.primitive()) {
      case PrimitiveType::kBool:
        return Type(BoolType{});
      case PrimitiveType::kInt64:
        return Type(IntType{});
      case PrimitiveType::kUint64:
        return Type(UintType{});
      case PrimitiveType::kDouble:
        return Type(DoubleType{});
      case PrimitiveType::kString:
        return Type(StringType{});
      case PrimitiveType::kBytes:
        return Type(BytesType{});
      default:
        return absl::InvalidArgumentError("Unsupported primitive type");
    }
  }

  if (type_spec.has_well_known()) {
    switch (type_spec.well_known()) {
      case WellKnownTypeSpec::kAny:
        return Type(AnyType{});
      case WellKnownTypeSpec::kTimestamp:
        return Type(TimestampType{});
      case WellKnownTypeSpec::kDuration:
        return Type(DurationType{});
      default:
        return absl::InvalidArgumentError("Unsupported well-known type");
    }
  }

  if (type_spec.has_wrapper()) {
    switch (type_spec.wrapper()) {
      case PrimitiveType::kBool:
        return Type(BoolWrapperType{});
      case PrimitiveType::kInt64:
        return Type(IntWrapperType{});
      case PrimitiveType::kUint64:
        return Type(UintWrapperType{});
      case PrimitiveType::kDouble:
        return Type(DoubleWrapperType{});
      case PrimitiveType::kString:
        return Type(StringWrapperType{});
      case PrimitiveType::kBytes:
        return Type(BytesWrapperType{});
      default:
        return absl::InvalidArgumentError("Unsupported wrapper type");
    }
  }

  if (type_spec.has_list_type()) {
    Type elem_type;
    if (type_spec.list_type().elem_type().is_specified()) {
      CEL_ASSIGN_OR_RETURN(
          elem_type, ConvertTypeSpecToType(type_spec.list_type().elem_type(),
                                           type_introspector, arena));
    }
    return Type(ListType(arena, elem_type));
  }

  if (type_spec.has_map_type()) {
    Type key_type;
    if (type_spec.map_type().key_type().is_specified()) {
      CEL_ASSIGN_OR_RETURN(
          key_type, ConvertTypeSpecToType(type_spec.map_type().key_type(),
                                          type_introspector, arena));
    }

    Type value_type;
    if (type_spec.map_type().value_type().is_specified()) {
      CEL_ASSIGN_OR_RETURN(
          value_type, ConvertTypeSpecToType(type_spec.map_type().value_type(),
                                            type_introspector, arena));
    }
    return Type(MapType(arena, key_type, value_type));
  }

  if (type_spec.has_function()) {
    const auto& func_spec = type_spec.function();
    Type result_type;
    if (func_spec.result_type().is_specified()) {
      CEL_ASSIGN_OR_RETURN(result_type,
                           ConvertTypeSpecToType(func_spec.result_type(),
                                                 type_introspector, arena));
    }
    std::vector<Type> arg_types;
    arg_types.reserve(func_spec.arg_types().size());
    for (const auto& arg_spec : func_spec.arg_types()) {
      CEL_ASSIGN_OR_RETURN(
          auto arg_type,
          ConvertTypeSpecToType(arg_spec, type_introspector, arena));
      arg_types.push_back(std::move(arg_type));
    }
    return Type(FunctionType(arena, result_type, arg_types));
  }

  if (type_spec.has_type_param()) {
    const std::string& name = type_spec.type_param().type();
    auto* allocated_name = google::protobuf::Arena::Create<std::string>(arena, name);
    return Type(TypeParamType(absl::string_view(*allocated_name)));
  }

  if (type_spec.has_message_type()) {
    const std::string& name = type_spec.message_type().type();
    CEL_ASSIGN_OR_RETURN(absl::optional<Type> type,
                         ResolveNamedType(name, type_introspector));
    if (!type.has_value()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Message type '", name, "' not found in descriptor pool"));
    }
    return *type;
  }

  if (type_spec.has_abstract_type()) {
    const std::string& name = type_spec.abstract_type().name();

    CEL_ASSIGN_OR_RETURN(absl::optional<Type> type,
                         ResolveNamedType(name, type_introspector));
    if (type.has_value()) {
      if (!TypeAcceptsParameters(*type) &&
          !type_spec.abstract_type().parameter_types().empty()) {
        return absl::InvalidArgumentError(
            absl::StrCat("Type '", name, "' cannot have type parameters"));
      }
      return *type;
    }

    // Otherwise fallback to OpaqueType
    std::vector<Type> params;
    params.reserve(type_spec.abstract_type().parameter_types().size());
    for (const auto& param_spec : type_spec.abstract_type().parameter_types()) {
      CEL_ASSIGN_OR_RETURN(
          auto param,
          ConvertTypeSpecToType(param_spec, type_introspector, arena));
      params.push_back(std::move(param));
    }
    auto* allocated_name = google::protobuf::Arena::Create<std::string>(arena, name);
    return Type(OpaqueType(arena, absl::string_view(*allocated_name), params));
  }

  if (type_spec.has_type()) {
    CEL_ASSIGN_OR_RETURN(
        auto contained_type,
        ConvertTypeSpecToType(type_spec.type(), type_introspector, arena));
    return Type(TypeType(arena, contained_type));
  }

  if (type_spec.has_error()) {
    return Type(ErrorType{});
  }

  return absl::InvalidArgumentError("Unknown TypeSpec kind");
}

absl::StatusOr<Type> ConvertTypeSpecToType(const TypeSpec& type_spec,
                                           const google::protobuf::DescriptorPool& pool,
                                           google::protobuf::Arena* arena) {
  // In this configuration we will only ever reference objects backed by the
  // given DescriptorPool or global constants so it is safe for `introspector`
  // to go out of scope, but this is not sound for arbitrary TypeIntrospectors.
  DescriptorPoolTypeIntrospector introspector(&pool);
  return ConvertTypeSpecToType(type_spec, introspector, arena);
}

absl::StatusOr<TypeSpec> ConvertTypeToTypeSpec(const Type& type) {
  switch (type.kind()) {
    case TypeKind::kNull:
      return TypeSpec(NullTypeSpec{});
    case TypeKind::kDyn:
      return TypeSpec(DynTypeSpec{});
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
    case TypeKind::kAny:
      return TypeSpec(WellKnownTypeSpec::kAny);
    case TypeKind::kTimestamp:
      return TypeSpec(WellKnownTypeSpec::kTimestamp);
    case TypeKind::kDuration:
      return TypeSpec(WellKnownTypeSpec::kDuration);
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
    case TypeKind::kList: {
      CEL_ASSIGN_OR_RETURN(auto elem_type,
                           ConvertTypeToTypeSpec(type.GetList().element()));
      return TypeSpec(
          ListTypeSpec(std::make_unique<TypeSpec>(std::move(elem_type))));
    }
    case TypeKind::kMap: {
      CEL_ASSIGN_OR_RETURN(auto key_type,
                           ConvertTypeToTypeSpec(type.GetMap().key()));
      CEL_ASSIGN_OR_RETURN(auto value_type,
                           ConvertTypeToTypeSpec(type.GetMap().value()));
      return TypeSpec(
          MapTypeSpec(std::make_unique<TypeSpec>(std::move(key_type)),
                      std::make_unique<TypeSpec>(std::move(value_type))));
    }
    case TypeKind::kFunction: {
      auto func_type = type.GetFunction();
      CEL_ASSIGN_OR_RETURN(auto result_type,
                           ConvertTypeToTypeSpec(func_type.result()));
      std::vector<TypeSpec> arg_types;
      arg_types.reserve(func_type.args().size());
      for (const auto& arg : func_type.args()) {
        CEL_ASSIGN_OR_RETURN(auto arg_type, ConvertTypeToTypeSpec(arg));
        arg_types.push_back(std::move(arg_type));
      }
      return TypeSpec(
          FunctionTypeSpec(std::make_unique<TypeSpec>(std::move(result_type)),
                           std::move(arg_types)));
    }
    case TypeKind::kTypeParam:
      return TypeSpec(ParamTypeSpec(std::string(type.GetTypeParam().name())));
    case TypeKind::kStruct: {
      if (type.IsMessage()) {
        return TypeSpec(MessageTypeSpec(std::string(type.GetMessage().name())));
      }
      return absl::InvalidArgumentError("Unsupported struct type");
    }
    case TypeKind::kOpaque: {
      auto opaque_type = type.GetOpaque();
      std::vector<TypeSpec> params;
      params.reserve(opaque_type.GetParameters().size());
      for (const auto& param : opaque_type.GetParameters()) {
        CEL_ASSIGN_OR_RETURN(auto param_type, ConvertTypeToTypeSpec(param));
        params.push_back(std::move(param_type));
      }
      return TypeSpec(
          AbstractType(std::string(opaque_type.name()), std::move(params)));
    }
    case TypeKind::kType: {
      CEL_ASSIGN_OR_RETURN(auto nested_type,
                           ConvertTypeToTypeSpec(type.GetType().GetType()));
      return TypeSpec(std::make_unique<TypeSpec>(std::move(nested_type)));
    }
    case TypeKind::kError:
      return TypeSpec(ErrorTypeSpec::kValue);
    case TypeKind::kEnum:
      return TypeSpec(
          AbstractType(std::string(type.GetEnum().name()), /*params=*/{}));
    default:
      return absl::InvalidArgumentError(absl::StrCat(
          "Unsupported Type kind: ", TypeKindToString(type.kind())));
  }
}

}  // namespace cel
