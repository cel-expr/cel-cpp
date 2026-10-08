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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "common/ast.h"
#include "common/constant.h"
#include "common/signature.h"
#include "common/typedef/schema.h"
#include "common/typedef/schema_yaml.h"
#include "common/typedef/typedef.h"
#include "common/typedef/typedef_yaml_internal.h"
#include "common/typedef/yaml_helpers.h"
#include "internal/status_macros.h"
#include "internal/strings.h"
#include "yaml-cpp/emitter.h"
#include "yaml-cpp/emittermanip.h"
#include "yaml-cpp/node/node.h"
#include "yaml-cpp/yaml.h"  // IWYU pragma: keep

namespace cel {
namespace {

using ::cel::internal::FormatYamlErrorMessage;
using ::cel::internal::GetBinary;
using ::cel::internal::GetBool;
using ::cel::internal::GetContextNodeForKeyValue;
using ::cel::internal::GetInt32;
using ::cel::internal::GetString;
using ::cel::internal::IsBinary;
using ::cel::internal::LoadYaml;
using ::cel::internal::YamlError;

absl::StatusOr<TypeSpec> BuildTypeSpecFromStructured(
    const YAML::Node& node, absl::string_view yaml, std::string name_str,
    bool is_type_param, std::vector<TypeSpec> params) {
  if (is_type_param) {
    return TypeSpec(ParamTypeSpec(std::move(name_str)));
  }

  auto read_param_or_dyn = [&params](size_t index) {
    auto spec = std::make_unique<TypeSpec>(DynTypeSpec());
    if (params.size() > index) {
      *spec = std::move(params[index]);
    }
    return spec;
  };

  if (params.empty()) {
    if (name_str == "null") return TypeSpec(NullTypeSpec());
    if (name_str == "bool") return TypeSpec(PrimitiveType::kBool);
    if (name_str == "int") return TypeSpec(PrimitiveType::kInt64);
    if (name_str == "uint") return TypeSpec(PrimitiveType::kUint64);
    if (name_str == "double") return TypeSpec(PrimitiveType::kDouble);
    if (name_str == "string") return TypeSpec(PrimitiveType::kString);
    if (name_str == "bytes") return TypeSpec(PrimitiveType::kBytes);
    if (name_str == "any" || name_str == "google.protobuf.Any") {
      return TypeSpec(WellKnownTypeSpec::kAny);
    }
    if (name_str == "timestamp" || name_str == "google.protobuf.Timestamp") {
      return TypeSpec(WellKnownTypeSpec::kTimestamp);
    }
    if (name_str == "duration" || name_str == "google.protobuf.Duration") {
      return TypeSpec(WellKnownTypeSpec::kDuration);
    }
    if (name_str == "dyn" || name_str == "google.protobuf.Value") {
      return TypeSpec(DynTypeSpec());
    }
    if (name_str == "bool_wrapper" || name_str == "google.protobuf.BoolValue") {
      return TypeSpec(PrimitiveTypeWrapper(PrimitiveType::kBool));
    }
    if (name_str == "int_wrapper" || name_str == "google.protobuf.Int64Value" ||
        name_str == "google.protobuf.Int32Value") {
      return TypeSpec(PrimitiveTypeWrapper(PrimitiveType::kInt64));
    }
    if (name_str == "uint_wrapper" ||
        name_str == "google.protobuf.UInt64Value" ||
        name_str == "google.protobuf.UInt32Value") {
      return TypeSpec(PrimitiveTypeWrapper(PrimitiveType::kUint64));
    }
    if (name_str == "double_wrapper" ||
        name_str == "google.protobuf.DoubleValue" ||
        name_str == "google.protobuf.FloatValue") {
      return TypeSpec(PrimitiveTypeWrapper(PrimitiveType::kDouble));
    }
    if (name_str == "string_wrapper" ||
        name_str == "google.protobuf.StringValue") {
      return TypeSpec(PrimitiveTypeWrapper(PrimitiveType::kString));
    }
    if (name_str == "bytes_wrapper" ||
        name_str == "google.protobuf.BytesValue") {
      return TypeSpec(PrimitiveTypeWrapper(PrimitiveType::kBytes));
    }
    if (name_str == "google.protobuf.ListValue") {
      return TypeSpec(ListTypeSpec(std::make_unique<TypeSpec>(DynTypeSpec())));
    }
    if (name_str == "google.protobuf.Struct") {
      return TypeSpec(
          MapTypeSpec(std::make_unique<TypeSpec>(PrimitiveType::kString),
                      std::make_unique<TypeSpec>(DynTypeSpec())));
    }
  }

  if (name_str == "type") {
    return TypeSpec(read_param_or_dyn(0));
  }
  if (name_str == "list") {
    return TypeSpec(ListTypeSpec(read_param_or_dyn(0)));
  }
  if (name_str == "map") {
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
  std::string name_str = GetString(yaml, type_name);
  if (name_str.empty()) {
    return YamlError(yaml, node, "Field 'type' is not specified");
  }

  bool is_type_param = false;
  const YAML::Node is_type_param_node = node["is_type_param"];
  if (is_type_param_node.IsDefined()) {
    if (!is_type_param_node.IsScalar()) {
      return YamlError(yaml, is_type_param_node,
                       "Node 'is_type_param' is not a boolean");
    }
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
      if (absl::EndsWith(value, "u")) {
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
    default:
      return YamlError(yaml, node, "Constant type is not supported");
  }
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

void EmitConstantValue(absl::string_view key, const Constant& constant,
                       YAML::Emitter& out) {
  if (!constant.has_value()) {
    return;
  }
  switch (constant.kind_case()) {
    case ConstantKindCase::kUnspecified:
    case ConstantKindCase::kNull:
      break;
    case ConstantKindCase::kBool:
      out << YAML::Key << std::string(key) << YAML::Value
          << constant.bool_value();
      break;
    case ConstantKindCase::kInt:
      out << YAML::Key << std::string(key) << YAML::Value
          << constant.int_value();
      break;
    case ConstantKindCase::kUint:
      out << YAML::Key << std::string(key) << YAML::Value
          << constant.uint_value();
      break;
    case ConstantKindCase::kDouble:
      out << YAML::Key << std::string(key) << YAML::Value
          << constant.double_value();
      break;
    case ConstantKindCase::kBytes: {
      out << YAML::Key << std::string(key);
      const std::string& bytes_value = constant.bytes_value();
      std::string hex_escaped = "b\"";
      for (unsigned char byte : bytes_value) {
        absl::StrAppend(&hex_escaped, "\\x");
        absl::StrAppendFormat(&hex_escaped, "%02x", byte);
      }
      absl::StrAppend(&hex_escaped, "\"");
      out << YAML::Value << hex_escaped;
      break;
    }
    case ConstantKindCase::kString:
      out << YAML::Key << std::string(key);
      out << YAML::Value << YAML::DoubleQuoted << constant.string_value();
      break;
    case ConstantKindCase::kDuration:
      out << YAML::Key << std::string(key) << YAML::Value;
      // NOLINTNEXTLINE(clang-diagnostic-deprecated-declarations)
      out << absl::FormatDuration(constant.duration_value());
      break;
    case ConstantKindCase::kTimestamp:
      out << YAML::Key << std::string(key) << YAML::Value;
      out << absl::FormatTime(
          "%Y-%m-%d%ET%H:%M:%E*SZ",
          // NOLINTNEXTLINE(clang-diagnostic-deprecated-declarations)
          constant.timestamp_value(), absl::UTCTimeZone());
      break;
  }
}

absl::StatusOr<ObjectSchemaSpecificProperties>
ParseObjectSchemaSpecificProperties(absl::string_view yaml,
                                    const YAML::Node& parent_node,
                                    const SchemaYamlRegistry& registry) {
  ObjectSchemaSpecificProperties result;
  const YAML::Node schemas = parent_node["schemas"];
  if (!schemas.IsDefined()) {
    return result;
  }
  if (!schemas.IsMap()) {
    return YamlError(yaml, schemas, "Node 'schemas' is not a map");
  }
  for (const auto& kv : schemas) {
    if (!kv.first.IsScalar()) {
      return YamlError(yaml, kv.first, "Target schema name is not a string");
    }
    std::string schema_name = GetString(yaml, kv.first);
    const SchemaYaml* schema = registry.Find(schema_name);
    if (schema == nullptr) {
      return YamlError(yaml, kv.first,
                       absl::StrCat("Unsupported schema: '", schema_name, "'"));
    }
    CEL_ASSIGN_OR_RETURN(std::unique_ptr<SchemaObjectProperties> props,
                         schema->DecodeObjectProperties(yaml, kv.second));
    if (props != nullptr) {
      result[schema_name] = std::move(props);
    }
  }
  return result;
}

absl::StatusOr<FieldSchemaSpecificProperties>
ParseFieldSchemaSpecificProperties(absl::string_view yaml,
                                   const YAML::Node& parent_node,
                                   const SchemaYamlRegistry& registry) {
  FieldSchemaSpecificProperties result;
  const YAML::Node schemas = parent_node["schemas"];
  if (!schemas.IsDefined()) {
    return result;
  }
  if (!schemas.IsMap()) {
    return YamlError(yaml, schemas, "Node 'schemas' is not a map");
  }
  for (const auto& kv : schemas) {
    if (!kv.first.IsScalar()) {
      return YamlError(yaml, kv.first, "Target schema name is not a string");
    }
    std::string schema_name = GetString(yaml, kv.first);
    const SchemaYaml* schema = registry.Find(schema_name);
    if (schema == nullptr) {
      return YamlError(yaml, kv.first,
                       absl::StrCat("Unsupported schema: '", schema_name, "'"));
    }
    CEL_ASSIGN_OR_RETURN(std::unique_ptr<SchemaFieldProperties> props,
                         schema->DecodeFieldProperties(yaml, kv.second));
    if (props != nullptr) {
      result[schema_name] = std::move(props);
    }
  }
  return result;
}

absl::StatusOr<EnumSchemaSpecificProperties> ParseEnumSchemaSpecificProperties(
    absl::string_view yaml, const YAML::Node& parent_node,
    const SchemaYamlRegistry& registry) {
  EnumSchemaSpecificProperties result;
  const YAML::Node schemas = parent_node["schemas"];
  if (!schemas.IsDefined()) {
    return result;
  }
  if (!schemas.IsMap()) {
    return YamlError(yaml, schemas, "Node 'schemas' is not a map");
  }
  for (const auto& kv : schemas) {
    if (!kv.first.IsScalar()) {
      return YamlError(yaml, kv.first, "Target schema name is not a string");
    }
    std::string schema_name = GetString(yaml, kv.first);
    const SchemaYaml* schema = registry.Find(schema_name);
    if (schema == nullptr) {
      return YamlError(yaml, kv.first,
                       absl::StrCat("Unsupported schema: '", schema_name, "'"));
    }
    CEL_ASSIGN_OR_RETURN(std::unique_ptr<SchemaEnumProperties> props,
                         schema->DecodeEnumProperties(yaml, kv.second));
    if (props != nullptr) {
      result[schema_name] = std::move(props);
    }
  }
  return result;
}

absl::StatusOr<EnumConstantSchemaSpecificProperties>
ParseEnumConstantSchemaSpecificProperties(absl::string_view yaml,
                                          const YAML::Node& parent_node,
                                          const SchemaYamlRegistry& registry) {
  EnumConstantSchemaSpecificProperties result;
  const YAML::Node schemas = parent_node["schemas"];
  if (!schemas.IsDefined()) {
    return result;
  }
  if (!schemas.IsMap()) {
    return YamlError(yaml, schemas, "Node 'schemas' is not a map");
  }
  for (const auto& kv : schemas) {
    if (!kv.first.IsScalar()) {
      return YamlError(yaml, kv.first, "Target schema name is not a string");
    }
    std::string schema_name = GetString(yaml, kv.first);
    const SchemaYaml* schema = registry.Find(schema_name);
    if (schema == nullptr) {
      return YamlError(yaml, kv.first,
                       absl::StrCat("Unsupported schema: '", schema_name, "'"));
    }
    CEL_ASSIGN_OR_RETURN(std::unique_ptr<SchemaEnumConstantProperties> props,
                         schema->DecodeEnumConstantProperties(yaml, kv.second));
    if (props != nullptr) {
      result[schema_name] = std::move(props);
    }
  }
  return result;
}

absl::Status EmitObjectSchemaSpecificProperties(
    const ObjectSchemaSpecificProperties& properties, YAML::Emitter& out,
    const SchemaYamlRegistry& registry) {
  if (properties.empty()) {
    return absl::OkStatus();
  }
  std::vector<std::string> sorted_keys;
  sorted_keys.reserve(properties.size());
  for (const auto& [key, props] : properties) {
    if (props != nullptr) {
      sorted_keys.push_back(key);
    }
  }
  if (sorted_keys.empty()) {
    return absl::OkStatus();
  }
  absl::c_sort(sorted_keys);

  out << YAML::Key << "schemas" << YAML::Value << YAML::BeginMap;
  for (const std::string& schema_name : sorted_keys) {
    const SchemaYaml* schema = registry.Find(schema_name);
    if (schema == nullptr) {
      return absl::InvalidArgumentError(
          absl::StrCat("Unsupported schema: '", schema_name, "'"));
    }
    out << YAML::Key << schema_name << YAML::Value;
    CEL_RETURN_IF_ERROR(
        schema->EncodeObjectProperties(*properties.at(schema_name), out));
  }
  out << YAML::EndMap;
  return absl::OkStatus();
}

absl::Status EmitFieldSchemaSpecificProperties(
    const FieldSchemaSpecificProperties& properties, YAML::Emitter& out,
    const SchemaYamlRegistry& registry) {
  if (properties.empty()) {
    return absl::OkStatus();
  }
  std::vector<std::string> sorted_keys;
  sorted_keys.reserve(properties.size());
  for (const auto& [key, props] : properties) {
    if (props != nullptr) {
      sorted_keys.push_back(key);
    }
  }
  if (sorted_keys.empty()) {
    return absl::OkStatus();
  }
  absl::c_sort(sorted_keys);

  out << YAML::Key << "schemas" << YAML::Value << YAML::BeginMap;
  for (const std::string& schema_name : sorted_keys) {
    const SchemaYaml* schema = registry.Find(schema_name);
    if (schema == nullptr) {
      return absl::InvalidArgumentError(
          absl::StrCat("Unsupported schema: '", schema_name, "'"));
    }
    out << YAML::Key << schema_name << YAML::Value;
    CEL_RETURN_IF_ERROR(
        schema->EncodeFieldProperties(*properties.at(schema_name), out));
  }
  out << YAML::EndMap;
  return absl::OkStatus();
}

absl::Status EmitEnumSchemaSpecificProperties(
    const EnumSchemaSpecificProperties& properties, YAML::Emitter& out,
    const SchemaYamlRegistry& registry) {
  if (properties.empty()) {
    return absl::OkStatus();
  }
  std::vector<std::string> sorted_keys;
  sorted_keys.reserve(properties.size());
  for (const auto& [key, props] : properties) {
    if (props != nullptr) {
      sorted_keys.push_back(key);
    }
  }
  if (sorted_keys.empty()) {
    return absl::OkStatus();
  }
  absl::c_sort(sorted_keys);

  out << YAML::Key << "schemas" << YAML::Value << YAML::BeginMap;
  for (const std::string& schema_name : sorted_keys) {
    const SchemaYaml* schema = registry.Find(schema_name);
    if (schema == nullptr) {
      return absl::InvalidArgumentError(
          absl::StrCat("Unsupported schema: '", schema_name, "'"));
    }
    out << YAML::Key << schema_name << YAML::Value;
    CEL_RETURN_IF_ERROR(
        schema->EncodeEnumProperties(*properties.at(schema_name), out));
  }
  out << YAML::EndMap;
  return absl::OkStatus();
}

absl::Status EmitEnumConstantSchemaSpecificProperties(
    const EnumConstantSchemaSpecificProperties& properties, YAML::Emitter& out,
    const SchemaYamlRegistry& registry) {
  if (properties.empty()) {
    return absl::OkStatus();
  }
  std::vector<std::string> sorted_keys;
  sorted_keys.reserve(properties.size());
  for (const auto& [key, props] : properties) {
    if (props != nullptr) {
      sorted_keys.push_back(key);
    }
  }
  if (sorted_keys.empty()) {
    return absl::OkStatus();
  }
  absl::c_sort(sorted_keys);

  out << YAML::Key << "schemas" << YAML::Value << YAML::BeginMap;
  for (const std::string& schema_name : sorted_keys) {
    const SchemaYaml* schema = registry.Find(schema_name);
    if (schema == nullptr) {
      return absl::InvalidArgumentError(
          absl::StrCat("Unsupported schema: '", schema_name, "'"));
    }
    out << YAML::Key << schema_name << YAML::Value;
    CEL_RETURN_IF_ERROR(
        schema->EncodeEnumConstantProperties(*properties.at(schema_name), out));
  }
  out << YAML::EndMap;
  return absl::OkStatus();
}

absl::StatusOr<ObjectTypeDef> ParseObjectTypeDef(
    absl::string_view yaml, const YAML::Node& node,
    const SchemaYamlRegistry& registry) {
  if (!node || !node.IsMap()) {
    return YamlError(yaml, node, "Object type definition is not a map");
  }

  const YAML::Node name = node["name"];
  if (!name || !name.IsScalar()) {
    return YamlError(yaml, name.IsDefined() ? name : node,
                     "Type definition 'name' is not a string");
  }
  std::string name_str = GetString(yaml, name);

  const YAML::Node doc = node["doc"];
  std::string doc_str;
  if (doc.IsDefined()) {
    if (!doc.IsScalar()) {
      return YamlError(yaml, doc, "Type definition 'doc' is not a string");
    }
    doc_str = GetString(yaml, doc);
  }

  const YAML::Node constants = node["constants"];
  if (constants.IsDefined()) {
    return YamlError(yaml, GetContextNodeForKeyValue(node, constants),
                     "Object type definition cannot have 'constants'");
  }

  ObjectTypeDef object_def;
  object_def.name = std::move(name_str);
  object_def.doc = std::move(doc_str);
  CEL_ASSIGN_OR_RETURN(
      object_def.schema_specific_properties,
      ParseObjectSchemaSpecificProperties(yaml, node, registry));

  const YAML::Node fields = node["fields"];
  if (!fields.IsDefined()) {
    return object_def;
  }
  if (!fields.IsSequence()) {
    return YamlError(yaml, fields, "Node 'fields' is not a sequence");
  }

  for (const YAML::Node& field_node : fields) {
    if (!field_node || !field_node.IsMap()) {
      return YamlError(yaml, field_node, "Field is not a map");
    }
    ObjectTypeDef::Field field;

    const YAML::Node field_name = field_node["name"];
    if (!field_name || !field_name.IsScalar()) {
      return YamlError(yaml, field_name.IsDefined() ? field_name : field_node,
                       "Field name is not a string");
    }
    field.name = GetString(yaml, field_name);

    const YAML::Node type = field_node["type"];
    if (!type.IsDefined()) {
      return YamlError(yaml, field_node, "Field 'type' is not specified");
    }
    if (type.IsScalar()) {
      CEL_ASSIGN_OR_RETURN(field.type, ParseTypeSpec(GetString(yaml, type)),
                           YamlError(yaml, type, absl::Status(_).message()));
    } else if (type.IsMap()) {
      CEL_ASSIGN_OR_RETURN(field.type, ParseStructuredTypeSpec(type, yaml));
    } else {
      return YamlError(yaml, type,
                       "Field 'type' is neither a string nor a map");
    }

    const YAML::Node field_doc = field_node["doc"];
    if (field_doc.IsDefined()) {
      if (!field_doc.IsScalar()) {
        return YamlError(yaml, field_doc, "Field 'doc' is not a string");
      }
      field.doc = GetString(yaml, field_doc);
    }

    const YAML::Node default_node = field_node["default"];
    if (default_node.IsDefined()) {
      ConstantKindCase constant_kind_case = GetConstantKindCase(field.type);
      if (constant_kind_case == ConstantKindCase::kUnspecified) {
        std::string type_name = MakeTypeSpecSignature(field.type)
                                    .value_or(FormatTypeSpec(field.type));
        return YamlError(
            yaml, default_node,
            absl::StrCat("Constant type '", type_name, "' is not supported"));
      }
      if (default_node.IsNull() &&
          constant_kind_case == ConstantKindCase::kNull) {
        field.default_value = Constant(nullptr);
      } else {
        if (!default_node.IsScalar()) {
          return YamlError(yaml, default_node,
                           "Field 'default' is not a scalar");
        }
        std::string value_str;
        if (IsBinary(default_node)) {
          CEL_ASSIGN_OR_RETURN(value_str, GetBinary(yaml, default_node));
        } else {
          value_str = GetString(yaml, default_node);
        }
        CEL_ASSIGN_OR_RETURN(field.default_value,
                             ParseConstantValue(yaml, default_node,
                                                constant_kind_case, value_str));
      }
    }

    CEL_ASSIGN_OR_RETURN(
        field.schema_specific_properties,
        ParseFieldSchemaSpecificProperties(yaml, field_node, registry));

    absl::Status add_status = object_def.AddField(std::move(field));
    if (!add_status.ok()) {
      return YamlError(yaml, field_name, add_status.message());
    }
  }

  return object_def;
}

absl::StatusOr<EnumTypeDef> ParseEnumTypeDef(
    absl::string_view yaml, const YAML::Node& node,
    const SchemaYamlRegistry& registry) {
  if (!node || !node.IsMap()) {
    return YamlError(yaml, node, "Enum type definition is not a map");
  }

  const YAML::Node name = node["name"];
  if (!name || !name.IsScalar()) {
    return YamlError(yaml, name.IsDefined() ? name : node,
                     "Type definition 'name' is not a string");
  }
  std::string name_str = GetString(yaml, name);

  const YAML::Node doc = node["doc"];
  std::string doc_str;
  if (doc.IsDefined()) {
    if (!doc.IsScalar()) {
      return YamlError(yaml, doc, "Type definition 'doc' is not a string");
    }
    doc_str = GetString(yaml, doc);
  }

  const YAML::Node fields = node["fields"];
  if (fields.IsDefined()) {
    return YamlError(yaml, GetContextNodeForKeyValue(node, fields),
                     "Enum type definition cannot have 'fields'");
  }

  EnumTypeDef enum_def;
  enum_def.name = std::move(name_str);
  enum_def.doc = std::move(doc_str);
  CEL_ASSIGN_OR_RETURN(enum_def.schema_specific_properties,
                       ParseEnumSchemaSpecificProperties(yaml, node, registry));

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
    EnumTypeDef::EnumConstant enum_constant;

    const YAML::Node constant_name = constant_node["name"];
    if (!constant_name || !constant_name.IsScalar()) {
      return YamlError(
          yaml, constant_name.IsDefined() ? constant_name : constant_node,
          "Enum constant name is not a string");
    }
    enum_constant.name = GetString(yaml, constant_name);

    const YAML::Node constant_id = constant_node["id"];
    if (constant_id.IsDefined()) {
      CEL_ASSIGN_OR_RETURN(enum_constant.id, GetInt32(yaml, "id", constant_id));
    } else {
      enum_constant.id = static_cast<int32_t>(enum_def.constants.size());
    }

    const YAML::Node constant_doc = constant_node["doc"];
    if (constant_doc.IsDefined()) {
      if (!constant_doc.IsScalar()) {
        return YamlError(yaml, constant_doc,
                         "Enum constant 'doc' is not a string");
      }
      enum_constant.doc = GetString(yaml, constant_doc);
    }

    CEL_ASSIGN_OR_RETURN(enum_constant.schema_specific_properties,
                         ParseEnumConstantSchemaSpecificProperties(
                             yaml, constant_node, registry));

    absl::Status add_status = enum_def.AddConstant(std::move(enum_constant));
    if (!add_status.ok()) {
      return YamlError(yaml, constant_name, add_status.message());
    }
  }

  return enum_def;
}

}  // namespace

namespace internal {

absl::StatusOr<TypeDef> ParseTypeDefNode(absl::string_view yaml,
                                         const YAML::Node& node,
                                         const SchemaYamlRegistry& registry) {
  if (!node || !node.IsMap()) {
    return YamlError(yaml, node, "Type definition is not a map");
  }

  const YAML::Node object_node = node["object"];
  const YAML::Node enum_node = node["enum"];
  if (object_node.IsDefined() && enum_node.IsDefined()) {
    return YamlError(yaml, GetContextNodeForKeyValue(node, enum_node),
                     "Node 'object' and 'enum' are mutually exclusive");
  }

  for (const auto& kv : node) {
    if (!kv.first.IsScalar()) {
      return YamlError(yaml, kv.first, "Type definition kind is not a string");
    }
    std::string kind = GetString(yaml, kv.first);
    if (kind != "object" && kind != "enum") {
      return YamlError(
          yaml, kv.first,
          absl::StrCat("Unsupported type definition kind: '", kind, "'"));
    }
  }

  if (object_node.IsDefined()) {
    CEL_ASSIGN_OR_RETURN(ObjectTypeDef object_def,
                         ParseObjectTypeDef(yaml, object_node, registry));
    return TypeDef(std::move(object_def));
  }
  if (enum_node.IsDefined()) {
    CEL_ASSIGN_OR_RETURN(EnumTypeDef enum_def,
                         ParseEnumTypeDef(yaml, enum_node, registry));
    return TypeDef(std::move(enum_def));
  }
  return YamlError(yaml, node,
                   "Type definition must specify 'object' or 'enum'");
}

absl::StatusOr<std::vector<TypeDef>> ParseTypeDefsNode(
    absl::string_view yaml, const YAML::Node& seq_node,
    const SchemaYamlRegistry& registry) {
  std::vector<TypeDef> result;
  if (!seq_node.IsDefined() || seq_node.IsNull()) {
    return result;
  }
  if (!seq_node.IsSequence()) {
    return YamlError(yaml, seq_node, "Node 'types' is not a sequence");
  }
  for (const YAML::Node& item : seq_node) {
    CEL_ASSIGN_OR_RETURN(TypeDef type_def,
                         ParseTypeDefNode(yaml, item, registry));
    result.push_back(std::move(type_def));
  }
  return result;
}

absl::Status EmitTypeDefNode(const TypeDef& type_def, YAML::Emitter& out,
                             const TypeDefToYamlOptions& options,
                             const SchemaYamlRegistry& registry) {
  out << YAML::BeginMap;
  if (type_def.is_object()) {
    const ObjectTypeDef& object_def = type_def.object_type();
    out << YAML::Key << "object" << YAML::Value << YAML::BeginMap;
    if (!object_def.name.empty()) {
      out << YAML::Key << "name" << YAML::Value << YAML::DoubleQuoted
          << object_def.name;
    }
    if (!object_def.doc.empty()) {
      out << YAML::Key << "doc" << YAML::Value << YAML::DoubleQuoted
          << object_def.doc;
    }
    CEL_RETURN_IF_ERROR(EmitObjectSchemaSpecificProperties(
        object_def.schema_specific_properties, out, registry));
    if (!object_def.fields.empty()) {
      out << YAML::Key << "fields" << YAML::Value << YAML::BeginSeq;
      for (const ObjectTypeDef::Field& field : object_def.fields) {
        out << YAML::BeginMap;
        out << YAML::Key << "name" << YAML::Value << YAML::DoubleQuoted
            << field.name;
        EmitTypeSpec(field.type, out, options);
        if (!field.doc.empty()) {
          out << YAML::Key << "doc" << YAML::Value << YAML::DoubleQuoted
              << field.doc;
        }
        EmitConstantValue("default", field.default_value, out);
        CEL_RETURN_IF_ERROR(EmitFieldSchemaSpecificProperties(
            field.schema_specific_properties, out, registry));
        out << YAML::EndMap;
      }
      out << YAML::EndSeq;
    }
    out << YAML::EndMap;
  } else {
    const EnumTypeDef& enum_def = type_def.enum_type();
    out << YAML::Key << "enum" << YAML::Value << YAML::BeginMap;
    if (!enum_def.name.empty()) {
      out << YAML::Key << "name" << YAML::Value << YAML::DoubleQuoted
          << enum_def.name;
    }
    if (!enum_def.doc.empty()) {
      out << YAML::Key << "doc" << YAML::Value << YAML::DoubleQuoted
          << enum_def.doc;
    }
    CEL_RETURN_IF_ERROR(EmitEnumSchemaSpecificProperties(
        enum_def.schema_specific_properties, out, registry));
    if (!enum_def.constants.empty()) {
      out << YAML::Key << "constants" << YAML::Value << YAML::BeginSeq;
      for (const EnumTypeDef::EnumConstant& constant : enum_def.constants) {
        out << YAML::BeginMap;
        out << YAML::Key << "name" << YAML::Value << YAML::DoubleQuoted
            << constant.name;
        out << YAML::Key << "id" << YAML::Value << constant.id;
        if (!constant.doc.empty()) {
          out << YAML::Key << "doc" << YAML::Value << YAML::DoubleQuoted
              << constant.doc;
        }
        CEL_RETURN_IF_ERROR(EmitEnumConstantSchemaSpecificProperties(
            constant.schema_specific_properties, out, registry));
        out << YAML::EndMap;
      }
      out << YAML::EndSeq;
    }
    out << YAML::EndMap;
  }
  out << YAML::EndMap;
  return absl::OkStatus();
}

absl::Status EmitTypeDefsSeq(absl::Span<const TypeDef> type_defs,
                             YAML::Emitter& out,
                             const TypeDefToYamlOptions& options,
                             const SchemaYamlRegistry& registry) {
  std::vector<TypeDef> sorted_type_defs(type_defs.begin(), type_defs.end());
  absl::c_sort(sorted_type_defs, [](const TypeDef& a, const TypeDef& b) {
    return a.name() < b.name();
  });

  out << YAML::BeginSeq;
  for (const TypeDef& type_def : sorted_type_defs) {
    CEL_RETURN_IF_ERROR(EmitTypeDefNode(type_def, out, options, registry));
  }
  out << YAML::EndSeq;
  return absl::OkStatus();
}

}  // namespace internal

absl::StatusOr<TypeDef> TypeDefFromYaml(absl::string_view yaml,
                                        const SchemaYamlRegistry& registry) {
  CEL_ASSIGN_OR_RETURN(YAML::Node root, LoadYaml(yaml));
  if (!root.IsDefined() || root.IsNull()) {
    return TypeDef();
  }
  if (!root.IsMap()) {
    return absl::InvalidArgumentError(FormatYamlErrorMessage(
        yaml, "Invalid CEL type definition YAML", root.Mark()));
  }
  return internal::ParseTypeDefNode(yaml, root, registry);
}

absl::StatusOr<std::vector<TypeDef>> TypeDefsFromYaml(
    absl::string_view yaml, const SchemaYamlRegistry& registry) {
  CEL_ASSIGN_OR_RETURN(YAML::Node root, LoadYaml(yaml));
  if (!root.IsDefined() || root.IsNull()) {
    return std::vector<TypeDef>();
  }
  if (root.IsSequence()) {
    return internal::ParseTypeDefsNode(yaml, root, registry);
  }
  if (root.IsMap()) {
    const YAML::Node types = root["types"];
    if (!types.IsDefined()) {
      return absl::InvalidArgumentError(FormatYamlErrorMessage(
          yaml, "Invalid CEL type definitions YAML", root.Mark()));
    }
    return internal::ParseTypeDefsNode(yaml, types, registry);
  }
  return absl::InvalidArgumentError(FormatYamlErrorMessage(
      yaml, "Invalid CEL type definitions YAML", root.Mark()));
}

absl::Status TypeDefToYaml(const TypeDef& type_def, std::ostream& os,
                           const TypeDefToYamlOptions& options,
                           const SchemaYamlRegistry& registry) {
  YAML::Emitter out(os);
  out.SetIndent(2);
  return internal::EmitTypeDefNode(type_def, out, options, registry);
}

absl::Status TypeDefToYaml(const TypeDef& type_def,
                           const SchemaYamlRegistry& registry, std::ostream& os,
                           const TypeDefToYamlOptions& options) {
  return TypeDefToYaml(type_def, os, options, registry);
}

absl::Status TypeDefsToYaml(absl::Span<const TypeDef> type_defs,
                            std::ostream& os,
                            const TypeDefToYamlOptions& options,
                            const SchemaYamlRegistry& registry) {
  if (type_defs.empty()) {
    return absl::OkStatus();
  }
  YAML::Emitter out(os);
  out.SetIndent(2);
  return internal::EmitTypeDefsSeq(type_defs, out, options, registry);
}

absl::Status TypeDefsToYaml(absl::Span<const TypeDef> type_defs,
                            const SchemaYamlRegistry& registry,
                            std::ostream& os,
                            const TypeDefToYamlOptions& options) {
  return TypeDefsToYaml(type_defs, os, options, registry);
}

}  // namespace cel
