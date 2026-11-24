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

#include "common/signature.h"

#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/types/optional.h"
#include "common/ast.h"
#include "common/type.h"
#include "common/type_spec_resolver.h"
#include "internal/status_macros.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"

namespace cel {

// Signature generator helper functions.
namespace {

void AppendEscaped(std::string* result, std::string_view str, bool escape_dot) {
  for (char c : str) {
    switch (c) {
      case '\\':
      case '(':
      case ')':
      case '<':
      case '>':
      case '"':
      case ',':
      case '~':
        result->push_back('\\');
        break;
      case '.':
        if (escape_dot) {
          result->push_back('\\');
        }
        break;
    }
    result->push_back(c);
  }
}

absl::Status AppendTypeDesc(std::string* result, const TypeSpec& type_spec);

absl::Status AppendTypeSpecList(std::string* result,
                                const std::vector<TypeSpec>& params) {
  if (!params.empty()) {
    result->push_back('<');
    for (size_t i = 0; i < params.size(); ++i) {
      CEL_RETURN_IF_ERROR(AppendTypeDesc(result, params[i]));
      if (i < params.size() - 1) {
        result->push_back(',');
      }
    }
    result->push_back('>');
  }
  return absl::OkStatus();
}

absl::Status AppendTypeDesc(std::string* result, const TypeSpec& type_spec) {
  if (type_spec.has_null()) {
    absl::StrAppend(result, "null");
  } else if (type_spec.has_dyn()) {
    absl::StrAppend(result, "dyn");
  } else if (type_spec.has_primitive()) {
    switch (type_spec.primitive()) {
      case PrimitiveType::kBool:
        absl::StrAppend(result, "bool");
        break;
      case PrimitiveType::kInt64:
        absl::StrAppend(result, "int");
        break;
      case PrimitiveType::kUint64:
        absl::StrAppend(result, "uint");
        break;
      case PrimitiveType::kDouble:
        absl::StrAppend(result, "double");
        break;
      case PrimitiveType::kString:
        absl::StrAppend(result, "string");
        break;
      case PrimitiveType::kBytes:
        absl::StrAppend(result, "bytes");
        break;
      default:
        return absl::InvalidArgumentError("Unsupported primitive type");
    }
  } else if (type_spec.has_well_known()) {
    switch (type_spec.well_known()) {
      case WellKnownTypeSpec::kAny:
        absl::StrAppend(result, "any");
        break;
      case WellKnownTypeSpec::kTimestamp:
        absl::StrAppend(result, "timestamp");
        break;
      case WellKnownTypeSpec::kDuration:
        absl::StrAppend(result, "duration");
        break;
      default:
        return absl::InvalidArgumentError("Unsupported well-known type");
    }
  } else if (type_spec.has_wrapper()) {
    switch (type_spec.wrapper()) {
      case PrimitiveType::kBool:
        absl::StrAppend(result, "bool_wrapper");
        break;
      case PrimitiveType::kInt64:
        absl::StrAppend(result, "int_wrapper");
        break;
      case PrimitiveType::kUint64:
        absl::StrAppend(result, "uint_wrapper");
        break;
      case PrimitiveType::kDouble:
        absl::StrAppend(result, "double_wrapper");
        break;
      case PrimitiveType::kString:
        absl::StrAppend(result, "string_wrapper");
        break;
      case PrimitiveType::kBytes:
        absl::StrAppend(result, "bytes_wrapper");
        break;
      default:
        return absl::InvalidArgumentError("Unsupported wrapper type");
    }
  } else if (type_spec.has_list_type()) {
    absl::StrAppend(result, "list<");
    if (type_spec.list_type().elem_type().is_specified()) {
      CEL_RETURN_IF_ERROR(
          AppendTypeDesc(result, type_spec.list_type().elem_type()));
    } else {
      absl::StrAppend(result, "dyn");
    }
    result->push_back('>');
  } else if (type_spec.has_map_type()) {
    absl::StrAppend(result, "map<");
    if (type_spec.map_type().key_type().is_specified()) {
      CEL_RETURN_IF_ERROR(
          AppendTypeDesc(result, type_spec.map_type().key_type()));
    } else {
      absl::StrAppend(result, "dyn");
    }
    result->push_back(',');
    if (type_spec.map_type().value_type().is_specified()) {
      CEL_RETURN_IF_ERROR(
          AppendTypeDesc(result, type_spec.map_type().value_type()));
    } else {
      absl::StrAppend(result, "dyn");
    }
    result->push_back('>');
  } else if (type_spec.has_function()) {
    absl::StrAppend(result, "function<");
    if (type_spec.function().result_type().is_specified()) {
      CEL_RETURN_IF_ERROR(
          AppendTypeDesc(result, type_spec.function().result_type()));
    } else {
      absl::StrAppend(result, "dyn");
    }
    for (const auto& arg : type_spec.function().arg_types()) {
      result->push_back(',');
      CEL_RETURN_IF_ERROR(AppendTypeDesc(result, arg));
    }
    result->push_back('>');
  } else if (type_spec.has_type()) {
    absl::StrAppend(result, "type");
    result->push_back('<');
    CEL_RETURN_IF_ERROR(AppendTypeDesc(result, type_spec.type()));
    result->push_back('>');
  } else if (type_spec.has_type_param()) {
    absl::StrAppend(result, "~");
    AppendEscaped(result, type_spec.type_param().type(), /*escape_dot=*/true);
  } else if (type_spec.has_abstract_type()) {
    AppendEscaped(result, type_spec.abstract_type().name(),
                  /*escape_dot=*/false);
    CEL_RETURN_IF_ERROR(AppendTypeSpecList(
        result, type_spec.abstract_type().parameter_types()));
  } else if (type_spec.has_message_type()) {
    AppendEscaped(result, type_spec.message_type().type(),
                  /*escape_dot=*/false);
  } else {
    return absl::InvalidArgumentError(absl::StrCat(
        "Unsupported type in signature: ", FormatTypeSpec(type_spec)));
  }
  return absl::OkStatus();
}
}  // namespace

absl::StatusOr<std::string> MakeTypeSignature(const Type& type) {
  std::string result;
  CEL_ASSIGN_OR_RETURN(TypeSpec type_spec, ConvertTypeToTypeSpec(type));
  CEL_RETURN_IF_ERROR(AppendTypeDesc(&result, type_spec));
  return result;
}

absl::StatusOr<std::string> MakeTypeSpecSignature(const TypeSpec& type_spec) {
  std::string result;
  CEL_RETURN_IF_ERROR(AppendTypeDesc(&result, type_spec));
  return result;
}

absl::StatusOr<std::string> MakeOverloadSignature(
    std::string_view function_name, const std::vector<Type>& args,
    bool is_member) {
  std::vector<TypeSpec> arg_type_specs;
  arg_type_specs.reserve(args.size());
  for (const auto& arg : args) {
    CEL_ASSIGN_OR_RETURN(TypeSpec type_spec, ConvertTypeToTypeSpec(arg));
    arg_type_specs.push_back(type_spec);
  }
  return MakeOverloadSignature(function_name, arg_type_specs, is_member);
}

absl::StatusOr<std::string> MakeOverloadSignature(
    std::string_view function_name, const std::vector<TypeSpec>& args,
    bool is_member) {
  std::string result;
  if (is_member) {
    if (!args.empty()) {
      CEL_RETURN_IF_ERROR(AppendTypeDesc(&result, args[0]));
    } else {
      return absl::InvalidArgumentError("Member function with no receiver");
    }
    result.push_back('.');
  }
  AppendEscaped(&result, function_name, /*escape_dot=*/true);
  result.push_back('(');
  for (size_t i = is_member ? 1 : 0; i < args.size(); ++i) {
    CEL_RETURN_IF_ERROR(AppendTypeDesc(&result, args[i]));
    if (i < args.size() - 1) {
      result.push_back(',');
    }
  }
  result.push_back(')');

  return result;
}

// Signature parser helper functions.
namespace {

std::string StripUnescapedWhitespace(std::string_view str) {
  std::string result;
  result.reserve(str.size());
  bool escaped = false;
  for (char c : str) {
    if (escaped) {
      result.push_back(c);
      escaped = false;
      continue;
    }
    if (c == '\\') {
      result.push_back(c);
      escaped = true;
      continue;
    }
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      continue;
    }
    result.push_back(c);
  }
  return result;
}

absl::optional<TypeSpec> ParseBuiltinOrWrapper(std::string_view name_str) {
  if (name_str == "null" || name_str == "null_type" ||
      name_str == "google.protobuf.NullValue")
    return TypeSpec(NullTypeSpec());
  if (name_str == "bool") return TypeSpec(PrimitiveType::kBool);
  if (name_str == "int") return TypeSpec(PrimitiveType::kInt64);
  if (name_str == "uint") return TypeSpec(PrimitiveType::kUint64);
  if (name_str == "double") return TypeSpec(PrimitiveType::kDouble);
  if (name_str == "string") return TypeSpec(PrimitiveType::kString);
  if (name_str == "bytes") return TypeSpec(PrimitiveType::kBytes);
  if (name_str == "any" || name_str == "google.protobuf.Any")
    return TypeSpec(WellKnownTypeSpec::kAny);
  if (name_str == "timestamp" || name_str == "google.protobuf.Timestamp")
    return TypeSpec(WellKnownTypeSpec::kTimestamp);
  if (name_str == "duration" || name_str == "google.protobuf.Duration")
    return TypeSpec(WellKnownTypeSpec::kDuration);
  if (name_str == "dyn" || name_str == "google.protobuf.Value")
    return TypeSpec(DynTypeSpec());

  // Handle standard Protobuf well-known wrapper types to preserve
  // backward compatibility for users migrating YAML configuration files.
  if (name_str == "bool_wrapper" || name_str == "google.protobuf.BoolValue")
    return TypeSpec(PrimitiveTypeWrapper(PrimitiveType::kBool));
  if (name_str == "int_wrapper" || name_str == "google.protobuf.Int64Value" ||
      name_str == "google.protobuf.Int32Value")
    return TypeSpec(PrimitiveTypeWrapper(PrimitiveType::kInt64));
  if (name_str == "uint_wrapper" || name_str == "google.protobuf.UInt64Value" ||
      name_str == "google.protobuf.UInt32Value")
    return TypeSpec(PrimitiveTypeWrapper(PrimitiveType::kUint64));
  if (name_str == "double_wrapper" ||
      name_str == "google.protobuf.DoubleValue" ||
      name_str == "google.protobuf.FloatValue")
    return TypeSpec(PrimitiveTypeWrapper(PrimitiveType::kDouble));
  if (name_str == "string_wrapper" || name_str == "google.protobuf.StringValue")
    return TypeSpec(PrimitiveTypeWrapper(PrimitiveType::kString));
  if (name_str == "bytes_wrapper" || name_str == "google.protobuf.BytesValue")
    return TypeSpec(PrimitiveTypeWrapper(PrimitiveType::kBytes));

  if (name_str == "google.protobuf.ListValue") {
    return TypeSpec(ListTypeSpec(std::make_unique<TypeSpec>(DynTypeSpec())));
  }
  if (name_str == "google.protobuf.Struct") {
    return TypeSpec(
        MapTypeSpec(std::make_unique<TypeSpec>(PrimitiveType::kString),
                    std::make_unique<TypeSpec>(DynTypeSpec())));
  }

  return std::nullopt;
}

std::string Unescape(std::string_view str) {
  size_t first_escape = str.find('\\');
  if (first_escape == std::string_view::npos) {
    return std::string(str);
  }
  std::string result;
  result.reserve(str.size());
  result.append(str.substr(0, first_escape));
  bool escaped = false;
  for (size_t i = first_escape; i < str.size(); ++i) {
    char c = str[i];
    if (escaped) {
      result.push_back(c);
      escaped = false;
    } else if (c == '\\') {
      escaped = true;
    } else {
      result.push_back(c);
    }
  }
  if (escaped) {
    result.push_back('\\');
  }
  return result;
}

class SignatureScanner {
 public:
  explicit SignatureScanner(std::string_view input,
                            std::string_view error_prefix = "Invalid signature")
      : input_(input), error_prefix_(error_prefix) {}

  absl::StatusOr<size_t> FindTopLevelChar(char target, bool find_last = false) {
    size_t found_idx = std::string_view::npos;
    int nesting = 0;
    bool escaped = false;
    // Scanning str for delimiter boundaries while ensuring
    // brackets are balanced and escape backslashes are bypassed.
    for (size_t i = 0; i < input_.size(); ++i) {
      char c = input_[i];
      if (escaped) {
        escaped = false;
        continue;
      }
      if (c == '\\') {
        escaped = true;
        continue;
      }
      if (c == target && nesting == 0) {
        if (find_last || found_idx == std::string_view::npos) {
          found_idx = i;
        }
      }
      if (c == '<') {
        nesting++;
      } else if (c == '>') {
        nesting--;
        if (nesting < 0) {
          return absl::InvalidArgumentError(
              absl::StrCat(error_prefix_, ": mismatched brackets"));
        }
      }
    }
    if (nesting != 0) {
      return absl::InvalidArgumentError(
          absl::StrCat(error_prefix_, ": mismatched brackets"));
    }
    return found_idx;
  }

  absl::StatusOr<std::vector<std::string_view>> SplitTopLevel(char delimiter) {
    std::vector<std::string_view> result;
    int nesting = 0;
    bool escaped = false;
    size_t start = 0;
    // Scanning str for delimiter while ensuring brackets are balanced and
    // escape backslashes are bypassed.
    for (size_t i = 0; i < input_.size(); ++i) {
      char c = input_[i];
      if (escaped) {
        escaped = false;
        continue;
      }
      if (c == '\\') {
        escaped = true;
        continue;
      }
      if (c == delimiter && nesting == 0) {
        result.push_back(input_.substr(start, i - start));
        start = i + 1;
      }
      if (c == '<') {
        nesting++;
      } else if (c == '>') {
        nesting--;
        if (nesting < 0) {
          return absl::InvalidArgumentError(
              absl::StrCat(error_prefix_, ": mismatched brackets"));
        }
      }
    }
    if (nesting != 0) {
      return absl::InvalidArgumentError(
          absl::StrCat(error_prefix_, ": mismatched brackets"));
    }
    result.push_back(input_.substr(start));
    return result;
  }

 private:
  std::string_view input_;
  std::string_view error_prefix_;
};

absl::StatusOr<std::vector<std::string_view>> SplitTypeList(
    std::string_view params) {
  return SignatureScanner(params, "Invalid type signature").SplitTopLevel(',');
}

absl::StatusOr<TypeSpec> ParseTypeSignature(std::string_view signature) {
  if (signature.empty()) {
    return absl::InvalidArgumentError("Empty type signature");
  }

  if (signature[0] == '~') {
    std::string_view param_name = signature.substr(1);
    if (param_name.empty()) {
      return absl::InvalidArgumentError(
          "Invalid type signature: invalid type parameter name");
    }
    CEL_ASSIGN_OR_RETURN(size_t less_idx,
                         SignatureScanner(param_name)
                             .FindTopLevelChar('<', /*find_last=*/false));
    CEL_ASSIGN_OR_RETURN(size_t comma_idx,
                         SignatureScanner(param_name)
                             .FindTopLevelChar(',', /*find_last=*/false));
    if (less_idx != std::string_view::npos ||
        comma_idx != std::string_view::npos) {
      return absl::InvalidArgumentError(
          "Invalid type signature: invalid type parameter name");
    }
    return TypeSpec(ParamTypeSpec(Unescape(param_name)));
  }

  CEL_ASSIGN_OR_RETURN(size_t less_idx,
                       SignatureScanner(signature, "Invalid type signature")
                           .FindTopLevelChar('<', /*find_last=*/false));

  std::string name_str;
  std::vector<TypeSpec> params;

  if (less_idx != std::string_view::npos) {
    // If the signature contains a '<', it must also contain a matching '>'.
    if (signature.back() != '>') {
      return absl::InvalidArgumentError(
          "Invalid type signature: missing closing >");
    }
    name_str = Unescape(signature.substr(0, less_idx));
    std::string_view params_str =
        signature.substr(less_idx + 1, signature.size() - less_idx - 2);
    CEL_ASSIGN_OR_RETURN(auto param_list, SplitTypeList(params_str));
    for (std::string_view param_str : param_list) {
      CEL_ASSIGN_OR_RETURN(auto param, ParseTypeSignature(param_str));
      params.push_back(std::move(param));
    }
  } else {
    name_str = Unescape(signature);
  }

  auto read_param_or_dyn = [&params](size_t index) {
    auto spec = std::make_unique<TypeSpec>(DynTypeSpec());
    if (params.size() > index) {
      *spec = std::move(params[index]);
    }
    return spec;
  };

  if (!params.empty()) {
    if (ParseBuiltinOrWrapper(name_str).has_value()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Invalid type signature: ", name_str,
                       " cannot have type parameters"));
    }
  } else {
    if (auto builtin = ParseBuiltinOrWrapper(name_str); builtin.has_value()) {
      return *builtin;
    }
  }

  if (name_str == "type") {
    if (params.size() > 1) {
      return absl::InvalidArgumentError(
          "Invalid type signature: type expects at most 1 parameter");
    }
    return TypeSpec(read_param_or_dyn(0));
  }

  if (name_str == "list") {
    if (params.size() > 1) {
      return absl::InvalidArgumentError(
          "Invalid type signature: list expects at most 1 parameter");
    }
    return TypeSpec(ListTypeSpec(read_param_or_dyn(0)));
  }

  if (name_str == "map") {
    if (!params.empty() && params.size() != 2) {
      return absl::InvalidArgumentError(
          "Invalid type signature: map expects 0 or 2 parameters");
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

  if (name_str.empty() || absl::StrContains(name_str, "..")) {
    return absl::InvalidArgumentError(
        "Invalid type signature: invalid identifier");
  }

  return TypeSpec(AbstractType(name_str, std::move(params)));
}

}  // namespace

absl::StatusOr<ParsedFunctionOverload> ParseFunctionSignature(
    std::string_view signature) {
  std::string stripped_sig = StripUnescapedWhitespace(signature);
  if (stripped_sig.empty()) {
    return absl::InvalidArgumentError("Empty function signature");
  }

  CEL_ASSIGN_OR_RETURN(
      size_t paren_idx,
      SignatureScanner(stripped_sig, "Invalid function signature")
          .FindTopLevelChar('(', /*find_last=*/false));

  if (paren_idx == std::string_view::npos || stripped_sig.back() != ')') {
    return absl::InvalidArgumentError("Invalid function signature");
  }

  std::string_view prefix = std::string_view(stripped_sig).substr(0, paren_idx);
  std::string_view args_str =
      std::string_view(stripped_sig)
          .substr(paren_idx + 1, stripped_sig.size() - paren_idx - 2);

  std::vector<TypeSpec> arg_types;
  ParsedFunctionOverload out;

  CEL_ASSIGN_OR_RETURN(size_t dot_idx,
                       SignatureScanner(prefix, "Invalid function signature")
                           .FindTopLevelChar('.', /*find_last=*/true));

  if (dot_idx != std::string_view::npos) {
    out.is_member = true;
    std::string_view receiver_str = prefix.substr(0, dot_idx);
    std::string_view func_str = prefix.substr(dot_idx + 1);

    CEL_ASSIGN_OR_RETURN(auto receiver_param, ParseTypeSignature(receiver_str));
    arg_types.push_back(std::move(receiver_param));
    out.function_name = Unescape(func_str);
  } else {
    out.is_member = false;
    out.function_name = Unescape(prefix);
  }

  if (out.function_name.empty()) {
    return absl::InvalidArgumentError(
        "Invalid function signature: empty function name");
  }

  if (!args_str.empty()) {
    CEL_ASSIGN_OR_RETURN(auto arg_list, SplitTypeList(args_str));
    for (std::string_view arg_str : arg_list) {
      CEL_ASSIGN_OR_RETURN(auto arg_param, ParseTypeSignature(arg_str));
      arg_types.push_back(std::move(arg_param));
    }
  }

  auto result_type = std::make_unique<TypeSpec>(DynTypeSpec());
  out.signature_type =
      TypeSpec(FunctionTypeSpec(std::move(result_type), std::move(arg_types)));

  return out;
}

absl::StatusOr<TypeSpec> ParseTypeSpec(std::string_view signature) {
  std::string stripped_sig = StripUnescapedWhitespace(signature);
  return ParseTypeSignature(stripped_sig);
}

absl::StatusOr<Type> ParseType(std::string_view signature, google::protobuf::Arena* arena,
                               const google::protobuf::DescriptorPool& pool) {
  CEL_ASSIGN_OR_RETURN(auto type_spec, ParseTypeSpec(signature));
  return cel::ConvertTypeSpecToType(type_spec, pool, arena);
}

}  // namespace cel
