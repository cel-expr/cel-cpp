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

#include "common/typedef/typedef_yaml.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "common/ast.h"
#include "common/constant.h"
#include "common/signature.h"
#include "common/typedef/typedef.h"
#include "common/typedef/yaml_helpers.h"
#include "internal/status_macros.h"
#include "internal/strings.h"
#include "yaml-cpp/emitter.h"
#include "yaml-cpp/emittermanip.h"
#include "yaml-cpp/exceptions.h"
#include "yaml-cpp/node/node.h"
#include "yaml-cpp/null.h"
#include "yaml-cpp/yaml.h"  // IWYU pragma: keep

namespace cel {
namespace {

using ::cel::typedef_internal::FormatYamlErrorMessage;
using ::cel::typedef_internal::GetBinary;
using ::cel::typedef_internal::GetBool;
using ::cel::typedef_internal::GetContextNodeForKeyValue;
using ::cel::typedef_internal::GetInt32;
using ::cel::typedef_internal::GetString;
using ::cel::typedef_internal::IsBinary;
using ::cel::typedef_internal::LoadYaml;
using ::cel::typedef_internal::YamlError;

absl::StatusOr<TypeSpec> BuildTypeSpecFromStructured(
    const YAML::Node& node, absl::string_view yaml, std::string name_str,
    bool is_type_param, std::vector<TypeSpec> params) {
  if (is_type_param) {
    if (!params.empty()) {
      return YamlError(yaml, node,
                       "Type parameter cannot have type parameters");
    }
    return TypeSpec(ParamTypeSpec(std::move(name_str)));
  }

  auto read_param_or_dyn = [&params](size_t index) {
    auto spec = std::make_unique<TypeSpec>(DynTypeSpec());
    if (params.size() > index) {
      *spec = std::move(params[index]);
    }
    return spec;
  };

  if (name_str == "type") {
    if (params.size() > 1) {
      return YamlError(yaml, node, "Type 'type' expects at most 1 parameter");
    }
    return TypeSpec(read_param_or_dyn(0));
  }
  if (name_str == "list") {
    if (params.size() > 1) {
      return YamlError(yaml, node, "Type 'list' expects at most 1 parameter");
    }
    return TypeSpec(ListTypeSpec(read_param_or_dyn(0)));
  }
  if (name_str == "map") {
    if (!params.empty() && params.size() != 2) {
      return YamlError(yaml, node, "Type 'map' expects 0 or 2 parameters");
    }
    auto key = read_param_or_dyn(0);
    auto value = read_param_or_dyn(1);
    return TypeSpec(MapTypeSpec(std::move(key), std::move(value)));
  }
  if (name_str == "function") {
    auto result_type = read_param_or_dyn(0);
    std::vector<TypeSpec> arg_types;
    for (size_t i = 1; i < params.size(); ++i) {
      arg_types.push_back(std::move(params[i]));
    }
    return TypeSpec(
        FunctionTypeSpec(std::move(result_type), std::move(arg_types)));
  }

  if (!absl::StartsWith(name_str, "~") &&
      name_str.find_first_of("<>\\ \t\n\r") == std::string::npos) {
    if (absl::StatusOr<TypeSpec> builtin = ParseTypeSpec(name_str);
        builtin.ok() && !builtin->has_abstract_type()) {
      if (!params.empty()) {
        return YamlError(
            yaml, node,
            absl::StrCat("Type '", name_str, "' cannot have type parameters"));
      }
      return *std::move(builtin);
    }
  }

  return TypeSpec(AbstractType(std::move(name_str), std::move(params)));
}

absl::StatusOr<TypeSpec> ParseStructuredTypeSpec(const YAML::Node& node,
                                                 absl::string_view yaml) {
  const YAML::Node type_name = node["type_name"];
  if (!type_name.IsDefined()) {
    return YamlError(yaml, node, "Node 'type_name' is not specified");
  }
  if (!type_name || !type_name.IsScalar()) {
    return YamlError(yaml, type_name, "Node 'type_name' is not a string");
  }
  std::string name_str = GetString(type_name);
  if (name_str.empty()) {
    return YamlError(yaml, type_name, "Node 'type_name' cannot be empty");
  }

  bool is_type_param = false;
  const YAML::Node is_type_param_node = node["is_type_param"];
  if (is_type_param_node.IsDefined()) {
    CEL_ASSIGN_OR_RETURN(is_type_param,
                         GetBool(yaml, "is_type_param", is_type_param_node));
  }

  std::vector<TypeSpec> param_specs;
  const YAML::Node params = node["params"];
  if (params.IsDefined()) {
    if (!params.IsSequence()) {
      return YamlError(yaml, params, "Node 'params' is not a sequence");
    }
    for (const YAML::Node& param : params) {
      if (!param || !param.IsMap()) {
        return YamlError(yaml, param, "Type parameter is not a map");
      }
      CEL_ASSIGN_OR_RETURN(TypeSpec param_spec,
                           ParseStructuredTypeSpec(param, yaml));
      param_specs.push_back(std::move(param_spec));
    }
  }

  return BuildTypeSpecFromStructured(node, yaml, std::move(name_str),
                                     is_type_param, std::move(param_specs));
}

ConstantKindCase GetConstantKindCase(const TypeSpec& type_spec) {
  if (type_spec.has_null()) {
    return ConstantKindCase::kNull;
  }
  if (type_spec.has_primitive()) {
    switch (type_spec.primitive()) {
      case PrimitiveType::kBool:
        return ConstantKindCase::kBool;
      case PrimitiveType::kInt64:
        return ConstantKindCase::kInt;
      case PrimitiveType::kUint64:
        return ConstantKindCase::kUint;
      case PrimitiveType::kDouble:
        return ConstantKindCase::kDouble;
      case PrimitiveType::kString:
        return ConstantKindCase::kString;
      case PrimitiveType::kBytes:
        return ConstantKindCase::kBytes;
      default:
        return ConstantKindCase::kUnspecified;
    }
  }
  if (type_spec.has_well_known()) {
    switch (type_spec.well_known()) {
      case WellKnownTypeSpec::kDuration:
        return ConstantKindCase::kDuration;
      case WellKnownTypeSpec::kTimestamp:
        return ConstantKindCase::kTimestamp;
      default:
        return ConstantKindCase::kUnspecified;
    }
  }
  // What looks like a message or an abstract type with no parameters can only
  // have a default value if it is an enum, in which case the default value
  // is the name of the enum constant.
  if (type_spec.has_message_type() ||
      (type_spec.has_abstract_type() &&
       type_spec.abstract_type().parameter_types().empty())) {
    return ConstantKindCase::kString;
  }
  return ConstantKindCase::kUnspecified;
}

absl::StatusOr<Constant> ParseConstantValue(absl::string_view yaml,
                                            const YAML::Node& node,
                                            ConstantKindCase constant_kind_case,
                                            absl::string_view value) {
  switch (constant_kind_case) {
    case ConstantKindCase::kNull:
      if (!value.empty()) {
        return YamlError(yaml, node, "Failed to parse null constant");
      }
      return Constant(nullptr);
    case ConstantKindCase::kBool:
      if (absl::EqualsIgnoreCase(value, "true")) {
        return Constant(true);
      } else if (absl::EqualsIgnoreCase(value, "false")) {
        return Constant(false);
      } else {
        return YamlError(yaml, node, "Failed to parse bool constant");
      }
    case ConstantKindCase::kInt:
      int64_t int_value;
      if (!absl::SimpleAtoi(value, &int_value)) {
        return YamlError(yaml, node, "Failed to parse int constant");
      }
      return Constant(int_value);
    case ConstantKindCase::kUint:
      uint64_t uint_value;
      if (absl::EndsWith(value, "u") || absl::EndsWith(value, "U")) {
        value = value.substr(0, value.size() - 1);
      }
      if (!absl::SimpleAtoi(value, &uint_value)) {
        return YamlError(yaml, node, "Failed to parse uint constant");
      }
      return Constant(uint_value);
    case ConstantKindCase::kDouble:
      double double_value;
      if (!absl::SimpleAtod(value, &double_value)) {
        return YamlError(yaml, node, "Failed to parse double constant");
      }
      return Constant(double_value);
    case ConstantKindCase::kBytes: {
      if (!IsBinary(node)) {
        absl::StatusOr<std::string> bytes_literal =
            internal::ParseBytesLiteral(value);
        if (bytes_literal.ok()) {
          return Constant(BytesConstant(*bytes_literal));
        }
      }
      return Constant(BytesConstant(value));
    }
    case ConstantKindCase::kString:
      return Constant(StringConstant(value));
    case ConstantKindCase::kDuration: {
      absl::Duration duration_value;
      if (!absl::ParseDuration(value, &duration_value)) {
        return YamlError(yaml, node, "Failed to parse duration constant");
      }
      return Constant(duration_value);
    }
    case ConstantKindCase::kTimestamp: {
      absl::Time timestamp_value;
      std::string error;
      if (!absl::ParseTime("%Y-%m-%d%ET%H:%M:%E*SZ", value, &timestamp_value,
                           &error)) {
        return YamlError(
            yaml, node,
            absl::StrCat("Failed to parse timestamp constant: ", error,
                         " supported format: YYYY-MM-DDThh:mm:ssZ"));
      }
      return Constant(timestamp_value);
    }
    case ConstantKindCase::kUnspecified:
      return YamlError(yaml, node, "Constant type is not supported");
  }
}

absl::StatusOr<Constant> ParseUntypedConstantValue(absl::string_view yaml,
                                                   const YAML::Node& node) {
  if (!node.IsDefined() || node.IsNull()) {
    return Constant(nullptr);
  }
  if (!node.IsScalar()) {
    return YamlError(yaml, node, "Aspect property value is not a scalar");
  }
  if (IsBinary(node)) {
    CEL_ASSIGN_OR_RETURN(std::string bytes, GetBinary(yaml, node));
    return Constant(BytesConstant(std::move(bytes)));
  }

  std::string value = GetString(node);
  const std::string& tag = node.Tag();
  if (tag == "!" || tag == "!!str" || tag == "tag:yaml.org,2002:str") {
    return Constant(StringConstant(std::move(value)));
  }

  if (absl::EqualsIgnoreCase(value, "true")) {
    return Constant(true);
  }
  if (absl::EqualsIgnoreCase(value, "false")) {
    return Constant(false);
  }
  if (absl::StartsWithIgnoreCase(value, "b\"") ||
      absl::StartsWithIgnoreCase(value, "b'")) {
    absl::StatusOr<std::string> bytes_literal =
        internal::ParseBytesLiteral(value);
    if (bytes_literal.ok()) {
      return Constant(BytesConstant(*std::move(bytes_literal)));
    }
  }
  if (value.size() > 1 &&
      (absl::EndsWith(value, "u") || absl::EndsWith(value, "U"))) {
    uint64_t uint_value;
    if (absl::SimpleAtoi(absl::string_view(value).substr(0, value.size() - 1),
                         &uint_value)) {
      return Constant(uint_value);
    }
  }
  int64_t int_value;
  if (absl::SimpleAtoi(value, &int_value)) {
    return Constant(int_value);
  }
  double double_value;
  if (absl::SimpleAtod(value, &double_value)) {
    return Constant(double_value);
  }
  absl::Duration duration_value;
  if (absl::ParseDuration(value, &duration_value)) {
    return Constant(duration_value);
  }
  absl::Time timestamp_value;
  std::string error;
  if (absl::ParseTime("%Y-%m-%d%ET%H:%M:%E*SZ", value, &timestamp_value,
                      &error)) {
    return Constant(timestamp_value);
  }
  return Constant(StringConstant(std::move(value)));
}

void EmitStructuredTypeSpec(const TypeSpec& type_spec, YAML::Emitter& out) {
  out << YAML::BeginMap;
  out << YAML::Key << "type_name" << YAML::Value << YAML::DoubleQuoted;
  if (type_spec.has_null()) {
    out << "null";
  } else if (type_spec.has_primitive()) {
    switch (type_spec.primitive()) {
      case PrimitiveType::kBool:
        out << "bool";
        break;
      case PrimitiveType::kInt64:
        out << "int";
        break;
      case PrimitiveType::kUint64:
        out << "uint";
        break;
      case PrimitiveType::kDouble:
        out << "double";
        break;
      case PrimitiveType::kString:
        out << "string";
        break;
      case PrimitiveType::kBytes:
        out << "bytes";
        break;
      default:
        out << "dyn";
        break;
    }
  } else if (type_spec.has_wrapper()) {
    switch (type_spec.wrapper()) {
      case PrimitiveType::kBool:
        out << "bool_wrapper";
        break;
      case PrimitiveType::kInt64:
        out << "int_wrapper";
        break;
      case PrimitiveType::kUint64:
        out << "uint_wrapper";
        break;
      case PrimitiveType::kDouble:
        out << "double_wrapper";
        break;
      case PrimitiveType::kString:
        out << "string_wrapper";
        break;
      case PrimitiveType::kBytes:
        out << "bytes_wrapper";
        break;
      default:
        out << "dyn";
        break;
    }
  } else if (type_spec.has_well_known()) {
    switch (type_spec.well_known()) {
      case WellKnownTypeSpec::kAny:
        out << "any";
        break;
      case WellKnownTypeSpec::kTimestamp:
        out << "timestamp";
        break;
      case WellKnownTypeSpec::kDuration:
        out << "duration";
        break;
      default:
        out << "dyn";
        break;
    }
  } else if (type_spec.has_list_type()) {
    out << "list";
    if (type_spec.list_type().has_elem_type()) {
      out << YAML::Key << "params" << YAML::Value << YAML::BeginSeq;
      EmitStructuredTypeSpec(type_spec.list_type().elem_type(), out);
      out << YAML::EndSeq;
    }
  } else if (type_spec.has_map_type()) {
    out << "map";
    if (type_spec.map_type().has_key_type() ||
        type_spec.map_type().has_value_type()) {
      out << YAML::Key << "params" << YAML::Value << YAML::BeginSeq;
      EmitStructuredTypeSpec(type_spec.map_type().key_type(), out);
      EmitStructuredTypeSpec(type_spec.map_type().value_type(), out);
      out << YAML::EndSeq;
    }
  } else if (type_spec.has_type()) {
    out << "type";
    if (type_spec.type().is_specified()) {
      out << YAML::Key << "params" << YAML::Value << YAML::BeginSeq;
      EmitStructuredTypeSpec(type_spec.type(), out);
      out << YAML::EndSeq;
    }
  } else if (type_spec.has_type_param()) {
    out << type_spec.type_param().type();
    out << YAML::Key << "is_type_param" << YAML::Value << true;
  } else if (type_spec.has_abstract_type()) {
    out << type_spec.abstract_type().name();
    if (!type_spec.abstract_type().parameter_types().empty()) {
      out << YAML::Key << "params" << YAML::Value << YAML::BeginSeq;
      for (const TypeSpec& param :
           type_spec.abstract_type().parameter_types()) {
        EmitStructuredTypeSpec(param, out);
      }
      out << YAML::EndSeq;
    }
  } else if (type_spec.has_message_type()) {
    out << type_spec.message_type().type();
  } else if (type_spec.has_function()) {
    out << "function";
    out << YAML::Key << "params" << YAML::Value << YAML::BeginSeq;
    EmitStructuredTypeSpec(type_spec.function().result_type(), out);
    for (const TypeSpec& arg : type_spec.function().arg_types()) {
      EmitStructuredTypeSpec(arg, out);
    }
    out << YAML::EndSeq;
  } else {
    out << "dyn";
  }
  out << YAML::EndMap;
}

void EmitTypeSpec(const TypeSpec& type_spec, YAML::Emitter& out,
                  const TypeDefToYamlOptions& options) {
  out << YAML::Key << "type" << YAML::Value;
  if (options.use_type_signatures) {
    absl::StatusOr<std::string> signature = MakeTypeSpecSignature(type_spec);
    if (signature.ok()) {
      out << YAML::DoubleQuoted << *signature;
      return;
    }
  }
  EmitStructuredTypeSpec(type_spec, out);
}

absl::Status EmitConstantValue(absl::string_view key, const Constant& constant,
                               YAML::Emitter& out, bool untyped = false) {
  if (!constant.has_value()) {
    return absl::OkStatus();
  }
  out << YAML::Key << std::string(key) << YAML::Value;
  switch (constant.kind_case()) {
    case ConstantKindCase::kUnspecified:
      break;
    case ConstantKindCase::kNull:
      out << YAML::Null;
      break;
    case ConstantKindCase::kBool:
      out << constant.bool_value();
      break;
    case ConstantKindCase::kInt:
      out << constant.int_value();
      break;
    case ConstantKindCase::kUint:
      if (untyped) {
        out << absl::StrCat(constant.uint_value(), "u");
      } else {
        out << constant.uint_value();
      }
      break;
    case ConstantKindCase::kDouble: {
      std::string double_str =
          absl::StrCat(absl::HighPrecision(constant.double_value()));
      if (untyped && std::isfinite(constant.double_value()) &&
          !absl::StrContains(double_str, '.') &&
          !absl::StrContains(double_str, 'e') &&
          !absl::StrContains(double_str, 'E')) {
        absl::StrAppend(&double_str, ".0");
      }
      out << double_str;
      break;
    }
    case ConstantKindCase::kBytes:
      out << absl::StrCat("b\"",
                          internal::EscapeBytes(constant.bytes_value(),
                                                /*escape_all_bytes=*/true),
                          "\"");
      break;
    case ConstantKindCase::kString:
      out << YAML::DoubleQuoted << constant.string_value();
      break;
    case ConstantKindCase::kDuration: {
      // NOLINTNEXTLINE(clang-diagnostic-deprecated-declarations)
      absl::Duration duration = constant.duration_value();
      if (duration == absl::InfiniteDuration() ||
          duration == -absl::InfiniteDuration()) {
        return absl::InvalidArgumentError(
            "Infinite duration constant is not supported");
      }
      if (untyped && duration == absl::ZeroDuration()) {
        out << "0s";
      } else {
        out << absl::FormatDuration(duration);
      }
      break;
    }
    case ConstantKindCase::kTimestamp: {
      // NOLINTNEXTLINE(clang-diagnostic-deprecated-declarations)
      absl::Time timestamp = constant.timestamp_value();
      if (timestamp == absl::InfiniteFuture() ||
          timestamp == absl::InfinitePast()) {
        return absl::InvalidArgumentError(
            "Infinite timestamp constant is not supported");
      }
      out << absl::FormatTime("%Y-%m-%d%ET%H:%M:%E*SZ", timestamp,
                              absl::UTCTimeZone());
      break;
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<absl::flat_hash_map<std::string, Aspect>> ParseAspects(
    absl::string_view yaml, const YAML::Node& parent_node) {
  absl::flat_hash_map<std::string, Aspect> result;
  const YAML::Node aspects = parent_node["aspects"];
  if (!aspects.IsDefined()) {
    return result;
  }
  if (!aspects.IsMap()) {
    return YamlError(yaml, aspects, "Node 'aspects' is not a map");
  }
  for (const auto& kv : aspects) {
    if (!kv.first.IsScalar()) {
      return YamlError(yaml, kv.first, "Target aspect name is not a string");
    }
    std::string aspect_name = GetString(kv.first);
    if (aspect_name.empty()) {
      return YamlError(yaml, kv.first, "Target aspect name cannot be empty");
    }
    const YAML::Node& aspect_node = kv.second;
    if (!aspect_node.IsDefined() || aspect_node.IsNull()) {
      result.emplace(std::move(aspect_name), Aspect());
      continue;
    }
    if (!aspect_node.IsMap()) {
      return YamlError(yaml, aspect_node,
                       absl::StrCat("Aspect '", aspect_name, "' is not a map"));
    }
    absl::flat_hash_map<std::string, Constant> props;
    props.reserve(aspect_node.size());
    for (const auto& prop_kv : aspect_node) {
      if (!prop_kv.first.IsScalar()) {
        return YamlError(yaml, prop_kv.first,
                         "Aspect property name is not a string");
      }
      std::string prop_name = GetString(prop_kv.first);
      if (prop_name.empty()) {
        return YamlError(yaml, prop_kv.first,
                         "Aspect property name cannot be empty");
      }
      CEL_ASSIGN_OR_RETURN(Constant prop_val,
                           ParseUntypedConstantValue(yaml, prop_kv.second));
      props.emplace(std::move(prop_name), std::move(prop_val));
    }
    result.emplace(std::move(aspect_name), Aspect(std::move(props)));
  }
  return result;
}

absl::Status EmitAspects(
    const absl::flat_hash_map<std::string, Aspect>& aspects,
    YAML::Emitter& out) {
  if (aspects.empty()) {
    return absl::OkStatus();
  }
  std::vector<std::string> sorted_keys;
  sorted_keys.reserve(aspects.size());
  for (const auto& [key, _] : aspects) {
    sorted_keys.push_back(key);
  }
  absl::c_sort(sorted_keys);

  out << YAML::Key << "aspects" << YAML::Value << YAML::BeginMap;
  for (const std::string& aspect_name : sorted_keys) {
    const Aspect& aspect = aspects.at(aspect_name);
    out << YAML::Key << aspect_name << YAML::Value << YAML::BeginMap;
    if (!aspect.properties().empty()) {
      std::vector<std::string> sorted_props;
      sorted_props.reserve(aspect.properties().size());
      for (const auto& [prop_name, prop_val] : aspect.properties()) {
        if (prop_val.has_value()) {
          sorted_props.push_back(prop_name);
        }
      }
      absl::c_sort(sorted_props);
      for (const std::string& prop_name : sorted_props) {
        CEL_RETURN_IF_ERROR(
            EmitConstantValue(prop_name, aspect.properties().at(prop_name), out,
                              /*untyped=*/true));
      }
    }
    out << YAML::EndMap;
  }
  out << YAML::EndMap;
  return absl::OkStatus();
}

absl::StatusOr<StructTypeDef::Field> ParseFieldNode(
    absl::string_view yaml, const YAML::Node& field_node) {
  const YAML::Node field_name = field_node["name"];
  if (!field_name || !field_name.IsScalar()) {
    return YamlError(yaml, field_name.IsDefined() ? field_name : field_node,
                     "Field name is not a string");
  }
  std::string field_name_str = GetString(field_name);
  if (field_name_str.empty()) {
    return YamlError(yaml, field_name, "Field name cannot be empty");
  }

  const YAML::Node type = field_node["type"];
  if (!type.IsDefined()) {
    return YamlError(yaml, field_node, "Field 'type' is not specified");
  }
  TypeSpec field_type;
  if (type.IsScalar()) {
    CEL_ASSIGN_OR_RETURN(field_type, ParseTypeSpec(GetString(type)),
                         YamlError(yaml, type, absl::Status(_).message()));
  } else if (type.IsMap()) {
    CEL_ASSIGN_OR_RETURN(field_type, ParseStructuredTypeSpec(type, yaml));
  } else {
    return YamlError(yaml, type, "Field 'type' is neither a string nor a map");
  }

  std::string field_doc_str;
  const YAML::Node field_doc = field_node["doc"];
  if (field_doc.IsDefined()) {
    if (!field_doc.IsScalar()) {
      return YamlError(yaml, field_doc, "Field 'doc' is not a string");
    }
    field_doc_str = GetString(field_doc);
  }

  Constant default_value;
  const YAML::Node default_node = field_node["default"];
  if (default_node.IsDefined()) {
    ConstantKindCase constant_kind_case = GetConstantKindCase(field_type);
    if (constant_kind_case == ConstantKindCase::kUnspecified) {
      std::string type_name = MakeTypeSpecSignature(field_type)
                                  .value_or(FormatTypeSpec(field_type));
      return YamlError(
          yaml, default_node,
          absl::StrCat("Constant type '", type_name, "' is not supported"));
    }
    if (default_node.IsNull() &&
        constant_kind_case == ConstantKindCase::kNull) {
      default_value = Constant(nullptr);
    } else {
      if (!default_node.IsScalar()) {
        return YamlError(yaml, default_node, "Field 'default' is not a scalar");
      }
      std::string value_str;
      if (IsBinary(default_node)) {
        CEL_ASSIGN_OR_RETURN(value_str, GetBinary(yaml, default_node));
      } else {
        value_str = GetString(default_node);
      }
      CEL_ASSIGN_OR_RETURN(default_value,
                           ParseConstantValue(yaml, default_node,
                                              constant_kind_case, value_str));
    }
  }

  CEL_ASSIGN_OR_RETURN(auto field_aspects, ParseAspects(yaml, field_node));

  return StructTypeDef::Field(std::move(field_name_str), std::move(field_type),
                              std::move(field_doc_str),
                              std::move(default_value),
                              std::move(field_aspects));
}

absl::Status EmitFieldNode(const StructTypeDef::Field& field,
                           YAML::Emitter& out,
                           const TypeDefToYamlOptions& options) {
  out << YAML::BeginMap;
  out << YAML::Key << "name" << YAML::Value << YAML::DoubleQuoted
      << std::string(field.name());
  EmitTypeSpec(field.type(), out, options);
  if (!field.doc().empty()) {
    out << YAML::Key << "doc" << YAML::Value << YAML::DoubleQuoted
        << std::string(field.doc());
  }
  CEL_RETURN_IF_ERROR(EmitConstantValue("default", field.default_value(), out));
  CEL_RETURN_IF_ERROR(EmitAspects(field.aspects(), out));
  out << YAML::EndMap;
  return absl::OkStatus();
}

absl::StatusOr<StructTypeDef> ParseStructTypeDef(absl::string_view yaml,
                                                 const YAML::Node& node) {
  if (!node || !node.IsMap()) {
    return YamlError(yaml, node, "Struct type definition is not a map");
  }

  const YAML::Node name = node["name"];
  if (!name || !name.IsScalar()) {
    return YamlError(yaml, name.IsDefined() ? name : node,
                     "Type definition 'name' is not a string");
  }
  std::string name_str = GetString(name);
  if (name_str.empty()) {
    return YamlError(yaml, name, "Type definition 'name' cannot be empty");
  }

  const YAML::Node doc = node["doc"];
  std::string doc_str;
  if (doc.IsDefined()) {
    if (!doc.IsScalar()) {
      return YamlError(yaml, doc, "Type definition 'doc' is not a string");
    }
    doc_str = GetString(doc);
  }

  const YAML::Node constants = node["constants"];
  if (constants.IsDefined()) {
    return YamlError(yaml, GetContextNodeForKeyValue(node, constants),
                     "Struct type definition cannot have 'constants'");
  }

  CEL_ASSIGN_OR_RETURN(auto struct_aspects, ParseAspects(yaml, node));
  StructTypeDef struct_def(std::move(name_str), std::move(doc_str),
                           std::move(struct_aspects));

  const YAML::Node fields = node["fields"];
  if (!fields.IsDefined()) {
    return struct_def;
  }
  if (!fields.IsSequence()) {
    return YamlError(yaml, fields, "Node 'fields' is not a sequence");
  }

  for (const YAML::Node& member_node : fields) {
    if (!member_node || !member_node.IsMap()) {
      return YamlError(yaml, member_node, "Field is not a map");
    }

    const YAML::Node one_of_fields = member_node["one_of_fields"];
    if (one_of_fields.IsDefined()) {
      if (!one_of_fields.IsSequence()) {
        return YamlError(yaml, one_of_fields,
                         "Node 'one_of_fields' is not a sequence");
      }
      StructTypeDef::Oneof oneof_def;
      for (const YAML::Node& sub_node : one_of_fields) {
        if (!sub_node || !sub_node.IsMap()) {
          return YamlError(yaml, sub_node, "Field is not a map");
        }
        const YAML::Node nested_oneof = sub_node["one_of_fields"];
        if (nested_oneof.IsDefined()) {
          return YamlError(yaml,
                           GetContextNodeForKeyValue(sub_node, nested_oneof),
                           "Nested 'one_of_fields' is not supported");
        }
        CEL_ASSIGN_OR_RETURN(StructTypeDef::Field field,
                             ParseFieldNode(yaml, sub_node));
        if (struct_def.FindField(field.name()) != nullptr) {
          return YamlError(
              yaml, sub_node["name"],
              absl::StrCat("Field '", field.name(), "' is already defined."));
        }
        absl::Status add_status = oneof_def.AddField(std::move(field));
        if (!add_status.ok()) {
          return YamlError(yaml, sub_node["name"], add_status.message());
        }
      }
      CEL_RETURN_IF_ERROR(struct_def.AddOneof(std::move(oneof_def)));
      continue;
    }

    CEL_ASSIGN_OR_RETURN(StructTypeDef::Field field,
                         ParseFieldNode(yaml, member_node));
    absl::Status add_status = struct_def.AddField(std::move(field));
    if (!add_status.ok()) {
      return YamlError(yaml, member_node["name"], add_status.message());
    }
  }

  return struct_def;
}

absl::StatusOr<EnumTypeDef> ParseEnumTypeDef(absl::string_view yaml,
                                             const YAML::Node& node) {
  if (!node || !node.IsMap()) {
    return YamlError(yaml, node, "Enum type definition is not a map");
  }

  const YAML::Node name = node["name"];
  if (!name || !name.IsScalar()) {
    return YamlError(yaml, name.IsDefined() ? name : node,
                     "Type definition 'name' is not a string");
  }
  std::string name_str = GetString(name);
  if (name_str.empty()) {
    return YamlError(yaml, name, "Type definition 'name' cannot be empty");
  }

  const YAML::Node doc = node["doc"];
  std::string doc_str;
  if (doc.IsDefined()) {
    if (!doc.IsScalar()) {
      return YamlError(yaml, doc, "Type definition 'doc' is not a string");
    }
    doc_str = GetString(doc);
  }

  const YAML::Node fields = node["fields"];
  if (fields.IsDefined()) {
    return YamlError(yaml, GetContextNodeForKeyValue(node, fields),
                     "Enum type definition cannot have 'fields'");
  }

  CEL_ASSIGN_OR_RETURN(auto enum_aspects, ParseAspects(yaml, node));
  EnumTypeDef enum_def(std::move(name_str), std::move(doc_str),
                       std::move(enum_aspects));

  const YAML::Node constants = node["constants"];
  if (!constants.IsDefined()) {
    return enum_def;
  }
  if (!constants.IsSequence()) {
    return YamlError(yaml, constants, "Node 'constants' is not a sequence");
  }

  for (const YAML::Node& constant_node : constants) {
    if (!constant_node || !constant_node.IsMap()) {
      return YamlError(yaml, constant_node, "Enum constant is not a map");
    }

    const YAML::Node constant_name = constant_node["name"];
    if (!constant_name || !constant_name.IsScalar()) {
      return YamlError(
          yaml, constant_name.IsDefined() ? constant_name : constant_node,
          "Enum constant name is not a string");
    }
    std::string constant_name_str = GetString(constant_name);
    if (constant_name_str.empty()) {
      return YamlError(yaml, constant_name,
                       "Enum constant name cannot be empty");
    }

    int32_t constant_id_val;
    const YAML::Node constant_id = constant_node["id"];
    if (constant_id.IsDefined()) {
      CEL_ASSIGN_OR_RETURN(constant_id_val, GetInt32(yaml, "id", constant_id));
    } else {
      constant_id_val = static_cast<int32_t>(enum_def.constants().size());
    }

    std::string constant_doc_str;
    const YAML::Node constant_doc = constant_node["doc"];
    if (constant_doc.IsDefined()) {
      if (!constant_doc.IsScalar()) {
        return YamlError(yaml, constant_doc,
                         "Enum constant 'doc' is not a string");
      }
      constant_doc_str = GetString(constant_doc);
    }

    CEL_ASSIGN_OR_RETURN(auto constant_aspects,
                         ParseAspects(yaml, constant_node));

    EnumTypeDef::EnumConstant enum_constant(
        std::move(constant_name_str), constant_id_val,
        std::move(constant_doc_str), std::move(constant_aspects));
    absl::Status add_status = enum_def.AddConstant(std::move(enum_constant));
    if (!add_status.ok()) {
      return YamlError(yaml, constant_name, add_status.message());
    }
  }

  return enum_def;
}

}  // namespace

namespace typedef_internal {

absl::StatusOr<TypeDef> ParseTypeDefNode(absl::string_view yaml,
                                         const YAML::Node& node) {
  if (!node || !node.IsMap()) {
    return YamlError(yaml, node, "Type definition is not a map");
  }

  const YAML::Node struct_node = node["struct"];
  const YAML::Node enum_node = node["enum"];
  if (struct_node.IsDefined() && enum_node.IsDefined()) {
    return YamlError(yaml, GetContextNodeForKeyValue(node, enum_node),
                     "Node 'struct' and 'enum' are mutually exclusive");
  }

  if (struct_node.IsDefined()) {
    CEL_ASSIGN_OR_RETURN(StructTypeDef struct_def,
                         ParseStructTypeDef(yaml, struct_node));
    return TypeDef(std::move(struct_def));
  }
  if (enum_node.IsDefined()) {
    CEL_ASSIGN_OR_RETURN(EnumTypeDef enum_def,
                         ParseEnumTypeDef(yaml, enum_node));
    return TypeDef(std::move(enum_def));
  }
  return YamlError(yaml, node,
                   "Type definition must specify 'struct' or 'enum'");
}

absl::StatusOr<TypeDefSet> ParseTypeDefSetNode(absl::string_view yaml,
                                               const YAML::Node& seq_node) {
  TypeDefSet result;
  if (!seq_node.IsDefined() || seq_node.IsNull()) {
    return result;
  }
  if (!seq_node.IsSequence()) {
    return YamlError(yaml, seq_node, "Node 'types' is not a sequence");
  }
  for (const YAML::Node& item : seq_node) {
    CEL_ASSIGN_OR_RETURN(TypeDef type_def,
                         typedef_internal::ParseTypeDefNode(yaml, item));
    CEL_RETURN_IF_ERROR(result.AddTypeDef(std::move(type_def)));
  }
  return result;
}

absl::Status EmitTypeDefNode(const TypeDef& type_def, YAML::Emitter& out,
                             const TypeDefToYamlOptions& options) {
  out << YAML::BeginMap;
  if (type_def.is_struct()) {
    const StructTypeDef& struct_def = type_def.struct_type();
    out << YAML::Key << "struct" << YAML::Value << YAML::BeginMap;
    if (!struct_def.name().empty()) {
      out << YAML::Key << "name" << YAML::Value << YAML::DoubleQuoted
          << std::string(struct_def.name());
    }
    if (!struct_def.doc().empty()) {
      out << YAML::Key << "doc" << YAML::Value << YAML::DoubleQuoted
          << std::string(struct_def.doc());
    }
    CEL_RETURN_IF_ERROR(EmitAspects(struct_def.aspects(), out));
    if (!struct_def.members().empty()) {
      out << YAML::Key << "fields" << YAML::Value << YAML::BeginSeq;
      for (const StructTypeDef::Member& member : struct_def.members()) {
        switch (member.kind_case()) {
          case StructTypeDef::MemberKindCase::kField:
            CEL_RETURN_IF_ERROR(EmitFieldNode(member.field(), out, options));
            break;
          case StructTypeDef::MemberKindCase::kOneof:
            out << YAML::BeginMap;
            out << YAML::Key << "one_of_fields" << YAML::Value
                << YAML::BeginSeq;
            for (const StructTypeDef::Field& field : member.oneof().fields()) {
              CEL_RETURN_IF_ERROR(EmitFieldNode(field, out, options));
            }
            out << YAML::EndSeq;
            out << YAML::EndMap;
            break;
        }
      }
      out << YAML::EndSeq;
    }
    out << YAML::EndMap;
  } else {
    const EnumTypeDef& enum_def = type_def.enum_type();
    out << YAML::Key << "enum" << YAML::Value << YAML::BeginMap;
    if (!enum_def.name().empty()) {
      out << YAML::Key << "name" << YAML::Value << YAML::DoubleQuoted
          << std::string(enum_def.name());
    }
    if (!enum_def.doc().empty()) {
      out << YAML::Key << "doc" << YAML::Value << YAML::DoubleQuoted
          << std::string(enum_def.doc());
    }
    CEL_RETURN_IF_ERROR(EmitAspects(enum_def.aspects(), out));
    if (!enum_def.constants().empty()) {
      out << YAML::Key << "constants" << YAML::Value << YAML::BeginSeq;
      for (const EnumTypeDef::EnumConstant& constant : enum_def.constants()) {
        out << YAML::BeginMap;
        out << YAML::Key << "name" << YAML::Value << YAML::DoubleQuoted
            << std::string(constant.name());
        out << YAML::Key << "id" << YAML::Value << constant.id();
        if (!constant.doc().empty()) {
          out << YAML::Key << "doc" << YAML::Value << YAML::DoubleQuoted
              << std::string(constant.doc());
        }
        CEL_RETURN_IF_ERROR(EmitAspects(constant.aspects(), out));
        out << YAML::EndMap;
      }
      out << YAML::EndSeq;
    }
    out << YAML::EndMap;
  }
  out << YAML::EndMap;
  return absl::OkStatus();
}

absl::Status EmitTypeDefSetSeq(const TypeDefSet& type_def_set,
                               YAML::Emitter& out,
                               const TypeDefToYamlOptions& options) {
  std::vector<const TypeDef*> sorted_type_defs;
  sorted_type_defs.reserve(type_def_set.types().size());
  for (const TypeDef& td : type_def_set.types()) {
    sorted_type_defs.push_back(&td);
  }
  absl::c_sort(sorted_type_defs, [](const TypeDef* a, const TypeDef* b) {
    return a->name() < b->name();
  });

  out << YAML::BeginSeq;
  for (const TypeDef* type_def : sorted_type_defs) {
    CEL_RETURN_IF_ERROR(
        typedef_internal::EmitTypeDefNode(*type_def, out, options));
  }
  out << YAML::EndSeq;
  return absl::OkStatus();
}

}  // namespace typedef_internal

absl::StatusOr<TypeDef> TypeDefFromYaml(absl::string_view yaml) {
  try {
    CEL_ASSIGN_OR_RETURN(YAML::Node root, LoadYaml(yaml));
    if (!root.IsDefined() || root.IsNull()) {
      return TypeDef();
    }
    if (!root.IsMap()) {
      return absl::InvalidArgumentError(FormatYamlErrorMessage(
          yaml, "Invalid CEL type definition YAML", root.Mark()));
    }
    return typedef_internal::ParseTypeDefNode(yaml, root);
  } catch (const YAML::Exception& e) {
    return absl::InvalidArgumentError(
        FormatYamlErrorMessage(yaml, e.msg, e.mark));
  } catch (const std::exception& e) {
    return absl::InternalError(e.what());
  } catch (...) {
    return absl::InternalError("Unknown exception occurred while parsing YAML");
  }
}

absl::StatusOr<TypeDefSet> TypeDefSetFromYaml(absl::string_view yaml) {
  try {
    CEL_ASSIGN_OR_RETURN(YAML::Node root, LoadYaml(yaml));
    if (!root.IsDefined() || root.IsNull()) {
      return TypeDefSet();
    }
    if (root.IsSequence()) {
      return typedef_internal::ParseTypeDefSetNode(yaml, root);
    }
    if (root.IsMap()) {
      const YAML::Node types = root["types"];
      if (!types.IsDefined()) {
        return absl::InvalidArgumentError(FormatYamlErrorMessage(
            yaml, "Invalid CEL type definitions YAML", root.Mark()));
      }
      return typedef_internal::ParseTypeDefSetNode(yaml, types);
    }
    return absl::InvalidArgumentError(FormatYamlErrorMessage(
        yaml, "Invalid CEL type definitions YAML", root.Mark()));
  } catch (const YAML::Exception& e) {
    return absl::InvalidArgumentError(
        FormatYamlErrorMessage(yaml, e.msg, e.mark));
  } catch (const std::exception& e) {
    return absl::InternalError(e.what());
  } catch (...) {
    return absl::InternalError("Unknown exception occurred while parsing YAML");
  }
}

absl::Status TypeDefToYaml(const TypeDef& type_def, std::ostream& os,
                           const TypeDefToYamlOptions& options) {
  try {
    YAML::Emitter out(os);
    out.SetIndent(2);
    CEL_RETURN_IF_ERROR(
        typedef_internal::EmitTypeDefNode(type_def, out, options));
    if (!out.good()) {
      return absl::InternalError(out.GetLastError());
    }
    return absl::OkStatus();
  } catch (const YAML::Exception& e) {
    return absl::InternalError(e.msg);
  } catch (const std::exception& e) {
    return absl::InternalError(e.what());
  } catch (...) {
    return absl::InternalError(
        "Unknown exception occurred while emitting YAML");
  }
}

absl::Status TypeDefSetToYaml(const TypeDefSet& type_def_set, std::ostream& os,
                              const TypeDefToYamlOptions& options) {
  if (type_def_set.types().empty()) {
    return absl::OkStatus();
  }
  try {
    YAML::Emitter out(os);
    out.SetIndent(2);
    CEL_RETURN_IF_ERROR(
        typedef_internal::EmitTypeDefSetSeq(type_def_set, out, options));
    if (!out.good()) {
      return absl::InternalError(out.GetLastError());
    }
    return absl::OkStatus();
  } catch (const YAML::Exception& e) {
    return absl::InternalError(e.msg);
  } catch (const std::exception& e) {
    return absl::InternalError(e.what());
  } catch (...) {
    return absl::InternalError(
        "Unknown exception occurred while emitting YAML");
  }
}

}  // namespace cel
