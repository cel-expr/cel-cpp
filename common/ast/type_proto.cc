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

#include "common/ast/type_proto.h"

#include <memory>
#include <utility>
#include <vector>

#include "cel/expr/checked.pb.h"
#include "google/protobuf/struct.pb.h"
#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/variant.h"
#include "common/ast/metadata.h"
#include "internal/status_macros.h"

namespace cel::ast_internal {
namespace {

using TypePb = cel::expr::Type;

absl::StatusOr<PrimitiveType> ToNative(TypePb::PrimitiveType primitive_type) {
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
          "Illegal type specified for "
          "cel::expr::Type::PrimitiveType.");
  }
}

absl::StatusOr<WellKnownTypeSpec> ToNative(
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
          "Illegal type specified for "
          "cel::expr::Type::WellKnownType.");
  }
}

absl::StatusOr<ListTypeSpec> ToNative(const TypePb::ListType& list_type) {
  CEL_ASSIGN_OR_RETURN(TypeSpec native_elem_type,
                       TypeFromProto(list_type.elem_type()));
  return ListTypeSpec(std::make_unique<TypeSpec>(std::move(native_elem_type)));
}

absl::StatusOr<MapTypeSpec> ToNative(const TypePb::MapType& map_type) {
  CEL_ASSIGN_OR_RETURN(TypeSpec native_key_type,
                       TypeFromProto(map_type.key_type()));
  CEL_ASSIGN_OR_RETURN(TypeSpec native_value_type,
                       TypeFromProto(map_type.value_type()));
  return MapTypeSpec(std::make_unique<TypeSpec>(std::move(native_key_type)),
                     std::make_unique<TypeSpec>(std::move(native_value_type)));
}

absl::StatusOr<FunctionTypeSpec> ToNative(
    const TypePb::FunctionType& function_type) {
  std::vector<TypeSpec> arg_types;
  arg_types.reserve(function_type.arg_types_size());
  for (const auto& arg_type : function_type.arg_types()) {
    CEL_ASSIGN_OR_RETURN(TypeSpec native_arg, TypeFromProto(arg_type));
    arg_types.push_back(std::move(native_arg));
  }
  CEL_ASSIGN_OR_RETURN(TypeSpec native_result,
                       TypeFromProto(function_type.result_type()));
  return FunctionTypeSpec(std::make_unique<TypeSpec>(std::move(native_result)),
                          std::move(arg_types));
}

absl::StatusOr<AbstractType> ToNative(
    const TypePb::AbstractType& abstract_type) {
  std::vector<TypeSpec> parameter_types;
  parameter_types.reserve(abstract_type.parameter_types_size());
  for (const auto& parameter_type : abstract_type.parameter_types()) {
    CEL_ASSIGN_OR_RETURN(TypeSpec native_parameter_type,
                         TypeFromProto(parameter_type));
    parameter_types.push_back(std::move(native_parameter_type));
  }
  return AbstractType(abstract_type.name(), std::move(parameter_types));
}

struct TypeKindToProtoVisitor {
  absl::Status operator()(PrimitiveType primitive) {
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
        break;
    }
    return absl::InvalidArgumentError("Unsupported primitive type");
  }

  absl::Status operator()(PrimitiveTypeWrapper wrapper) {
    CEL_RETURN_IF_ERROR(this->operator()(wrapper.type()));
    auto wrapped = result->primitive();
    result->set_wrapper(wrapped);
    return absl::OkStatus();
  }

  absl::Status operator()(UnsetTypeSpec) {
    result->clear_type_kind();
    return absl::OkStatus();
  }

  absl::Status operator()(DynTypeSpec) {
    result->mutable_dyn();
    return absl::OkStatus();
  }

  absl::Status operator()(ErrorTypeSpec) {
    result->mutable_error();
    return absl::OkStatus();
  }

  absl::Status operator()(NullTypeSpec) {
    result->set_null(google::protobuf::NULL_VALUE);
    return absl::OkStatus();
  }

  absl::Status operator()(const ListTypeSpec& list_type) {
    return TypeToProto(list_type.elem_type(),
                       result->mutable_list_type()->mutable_elem_type());
  }

  absl::Status operator()(const MapTypeSpec& map_type) {
    CEL_RETURN_IF_ERROR(TypeToProto(
        map_type.key_type(), result->mutable_map_type()->mutable_key_type()));
    return TypeToProto(map_type.value_type(),
                       result->mutable_map_type()->mutable_value_type());
  }

  absl::Status operator()(const MessageTypeSpec& message_type) {
    result->set_message_type(message_type.type());
    return absl::OkStatus();
  }

  absl::Status operator()(const WellKnownTypeSpec& well_known_type) {
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
        break;
    }
    return absl::InvalidArgumentError("Unsupported well-known type");
  }

  absl::Status operator()(const FunctionTypeSpec& function_type) {
    auto* function_pb = result->mutable_function();
    CEL_RETURN_IF_ERROR(TypeToProto(function_type.result_type(),
                                    function_pb->mutable_result_type()));

    function_pb->mutable_arg_types()->Reserve(function_type.arg_types().size());
    for (const TypeSpec& arg_type : function_type.arg_types()) {
      CEL_RETURN_IF_ERROR(TypeToProto(arg_type, function_pb->add_arg_types()));
    }
    return absl::OkStatus();
  }

  absl::Status operator()(const AbstractType& type) {
    auto* abstract_type_pb = result->mutable_abstract_type();
    abstract_type_pb->set_name(type.name());
    abstract_type_pb->mutable_parameter_types()->Reserve(
        type.parameter_types().size());
    for (const TypeSpec& type_param : type.parameter_types()) {
      CEL_RETURN_IF_ERROR(
          TypeToProto(type_param, abstract_type_pb->add_parameter_types()));
    }
    return absl::OkStatus();
  }

  absl::Status operator()(const std::unique_ptr<TypeSpec>& type_type) {
    return TypeToProto((type_type != nullptr) ? *type_type : TypeSpec(),
                       result->mutable_type());
  }

  absl::Status operator()(const ParamTypeSpec& param_type) {
    result->set_type_param(param_type.type());
    return absl::OkStatus();
  }

  TypePb* absl_nonnull result;
};

}  // namespace

absl::StatusOr<TypeSpec> TypeFromProto(const cel::expr::Type& proto) {
  switch (proto.type_kind_case()) {
    case TypePb::kDyn:
      return TypeSpec(DynTypeSpec());
    case TypePb::kNull:
      return TypeSpec(NullTypeSpec());
    case TypePb::kPrimitive: {
      CEL_ASSIGN_OR_RETURN(PrimitiveType native_primitive,
                           ToNative(proto.primitive()));
      return TypeSpec(native_primitive);
    }
    case TypePb::kWrapper: {
      CEL_ASSIGN_OR_RETURN(PrimitiveType native_wrapper,
                           ToNative(proto.wrapper()));
      return TypeSpec(PrimitiveTypeWrapper(native_wrapper));
    }
    case TypePb::kWellKnown: {
      CEL_ASSIGN_OR_RETURN(WellKnownTypeSpec native_well_known,
                           ToNative(proto.well_known()));
      return TypeSpec(native_well_known);
    }
    case TypePb::kListType: {
      CEL_ASSIGN_OR_RETURN(ListTypeSpec native_list_type,
                           ToNative(proto.list_type()));
      return TypeSpec(std::move(native_list_type));
    }
    case TypePb::kMapType: {
      CEL_ASSIGN_OR_RETURN(MapTypeSpec native_map_type,
                           ToNative(proto.map_type()));
      return TypeSpec(std::move(native_map_type));
    }
    case TypePb::kFunction: {
      CEL_ASSIGN_OR_RETURN(FunctionTypeSpec native_function,
                           ToNative(proto.function()));
      return TypeSpec(std::move(native_function));
    }
    case TypePb::kMessageType:
      return TypeSpec(MessageTypeSpec(proto.message_type()));
    case TypePb::kTypeParam:
      return TypeSpec(ParamTypeSpec(proto.type_param()));
    case TypePb::kType: {
      if (proto.type().type_kind_case() == TypePb::TYPE_KIND_NOT_SET) {
        return TypeSpec(std::unique_ptr<TypeSpec>());
      }
      CEL_ASSIGN_OR_RETURN(TypeSpec native_type, TypeFromProto(proto.type()));
      return TypeSpec(std::make_unique<TypeSpec>(std::move(native_type)));
    }
    case TypePb::kError:
      return TypeSpec(ErrorTypeSpec::kValue);
    case TypePb::kAbstractType: {
      CEL_ASSIGN_OR_RETURN(AbstractType native_abstract,
                           ToNative(proto.abstract_type()));
      return TypeSpec(std::move(native_abstract));
    }
    case TypePb::TYPE_KIND_NOT_SET:
      return TypeSpec(UnsetTypeSpec());
    default:
      return absl::InvalidArgumentError(
          "Illegal type specified for cel::expr::Type.");
  }
}

absl::Status TypeToProto(const TypeSpec& type,
                         cel::expr::Type* absl_nonnull proto) {
  proto->Clear();
  return absl::visit(TypeKindToProtoVisitor{proto}, type.type_kind());
}

}  // namespace cel::ast_internal
