// Copyright 2022 Google LLC
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

#include "common/ast_proto.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cel/expr/checked.pb.h"
#include "cel/expr/syntax.pb.h"
#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/ast.h"
#include "common/ast/constant_proto.h"
#include "common/ast/expr_proto.h"
#include "common/ast/source_info_proto.h"
#include "common/ast/type_proto.h"
#include "common/constant.h"
#include "common/expr.h"
#include "internal/status_macros.h"

namespace cel {
namespace {

using ::cel::ast_internal::ConstantFromProto;
using ::cel::ast_internal::ConstantToProto;
using ::cel::ast_internal::ExprFromProto;
using ::cel::ast_internal::ExprToProto;
using ::cel::ast_internal::TypeSpecFromProto;
using ::cel::ast_internal::TypeSpecToProto;

using ExprPb = cel::expr::Expr;
using ParsedExprPb = cel::expr::ParsedExpr;
using CheckedExprPb = cel::expr::CheckedExpr;
using SourceInfoPb = cel::expr::SourceInfo;
using ExtensionPb = cel::expr::SourceInfo::Extension;
using ReferencePb = cel::expr::Reference;
using TypePb = cel::expr::Type;

absl::StatusOr<Expr> ExprValueFromProto(const ExprPb& expr) {
  Expr result;
  CEL_RETURN_IF_ERROR(ExprFromProto(expr, result));
  return result;
}

absl::StatusOr<SourceInfo> ConvertProtoSourceInfoToNative(
    const cel::expr::SourceInfo& source_info) {
  absl::flat_hash_map<int64_t, Expr> macro_calls;
  for (const auto& pair : source_info.macro_calls()) {
    auto native_expr = ExprValueFromProto(pair.second);
    if (!native_expr.ok()) {
      return native_expr.status();
    }
    macro_calls.emplace(pair.first, *(std::move(native_expr)));
  }
  std::vector<ExtensionSpec> extensions;
  extensions.reserve(source_info.extensions_size());
  for (const auto& extension : source_info.extensions()) {
    std::vector<ExtensionSpec::Component> components;
    components.reserve(extension.affected_components().size());
    for (const auto& component : extension.affected_components()) {
      switch (component) {
        case ExtensionPb::COMPONENT_PARSER:
          components.push_back(ExtensionSpec::Component::kParser);
          break;
        case ExtensionPb::COMPONENT_TYPE_CHECKER:
          components.push_back(ExtensionSpec::Component::kTypeChecker);
          break;
        case ExtensionPb::COMPONENT_RUNTIME:
          components.push_back(ExtensionSpec::Component::kRuntime);
          break;
        default:
          components.push_back(ExtensionSpec::Component::kUnspecified);
          break;
      }
    }
    extensions.push_back(ExtensionSpec(
        extension.id(),
        std::make_unique<ExtensionSpec::Version>(extension.version().major(),
                                                 extension.version().minor()),
        std::move(components)));
  }
  return SourceInfo(
      source_info.syntax_version(), source_info.location(),
      std::vector<int32_t>(source_info.line_offsets().begin(),
                           source_info.line_offsets().end()),
      absl::flat_hash_map<int64_t, int32_t>(source_info.positions().begin(),
                                            source_info.positions().end()),
      std::move(macro_calls), std::move(extensions));
}

absl::StatusOr<Reference> ConvertProtoReferenceToNative(
    const cel::expr::Reference& reference) {
  Reference ret_val;
  ret_val.set_name(reference.name());
  ret_val.mutable_overload_id().reserve(reference.overload_id_size());
  for (const auto& elem : reference.overload_id()) {
    ret_val.mutable_overload_id().emplace_back(elem);
  }
  if (reference.has_value()) {
    CEL_RETURN_IF_ERROR(
        ConstantFromProto(reference.value(), ret_val.mutable_value()));
  }
  return ret_val;
}

absl::StatusOr<ReferencePb> ReferenceToProto(const Reference& reference) {
  ReferencePb result;

  result.set_name(reference.name());

  for (const auto& overload_id : reference.overload_id()) {
    result.add_overload_id(overload_id);
  }

  if (reference.has_value()) {
    CEL_RETURN_IF_ERROR(
        ConstantToProto(reference.value(), result.mutable_value()));
  }

  return result;
}

}  // namespace

absl::StatusOr<std::unique_ptr<Ast>> CreateAstFromParsedExpr(
    const cel::expr::Expr& expr,
    const cel::expr::SourceInfo* source_info) {
  CEL_ASSIGN_OR_RETURN(auto runtime_expr, ExprValueFromProto(expr));
  SourceInfo runtime_source_info;
  if (source_info != nullptr) {
    CEL_ASSIGN_OR_RETURN(runtime_source_info,
                         ConvertProtoSourceInfoToNative(*source_info));
  }
  return std::make_unique<Ast>(std::move(runtime_expr),
                               std::move(runtime_source_info));
}

absl::StatusOr<std::unique_ptr<Ast>> CreateAstFromParsedExpr(
    const ParsedExprPb& parsed_expr) {
  return CreateAstFromParsedExpr(parsed_expr.expr(),
                                 &parsed_expr.source_info());
}

absl::Status AstToParsedExpr(const Ast& ast,
                             cel::expr::ParsedExpr* absl_nonnull out) {
  ParsedExprPb& parsed_expr = *out;
  CEL_RETURN_IF_ERROR(ExprToProto(ast.root_expr(), parsed_expr.mutable_expr()));
  CEL_RETURN_IF_ERROR(ast_internal::SourceInfoToProto(
      ast.source_info(), parsed_expr.mutable_source_info()));

  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<Ast>> CreateAstFromCheckedExpr(
    const CheckedExprPb& checked_expr) {
  CEL_ASSIGN_OR_RETURN(Expr expr, ExprValueFromProto(checked_expr.expr()));
  CEL_ASSIGN_OR_RETURN(SourceInfo source_info, ConvertProtoSourceInfoToNative(
                                                   checked_expr.source_info()));

  Ast::ReferenceMap reference_map;
  for (const auto& pair : checked_expr.reference_map()) {
    auto native_reference = ConvertProtoReferenceToNative(pair.second);
    if (!native_reference.ok()) {
      return native_reference.status();
    }
    reference_map.emplace(pair.first, *(std::move(native_reference)));
  }
  Ast::TypeMap type_map;
  for (const auto& pair : checked_expr.type_map()) {
    auto native_type = TypeSpecFromProto(pair.second);
    if (!native_type.ok()) {
      return native_type.status();
    }
    type_map.emplace(pair.first, *(std::move(native_type)));
  }

  return std::make_unique<Ast>(std::move(expr), std::move(source_info),
                               std::move(reference_map), std::move(type_map),
                               checked_expr.expr_version());
}

absl::Status AstToCheckedExpr(
    const Ast& ast, cel::expr::CheckedExpr* absl_nonnull out) {
  if (!ast.is_checked()) {
    return absl::InvalidArgumentError("AST is not type-checked");
  }
  CheckedExprPb& checked_expr = *out;
  checked_expr.set_expr_version(ast.expr_version());
  CEL_RETURN_IF_ERROR(
      ExprToProto(ast.root_expr(), checked_expr.mutable_expr()));
  CEL_RETURN_IF_ERROR(ast_internal::SourceInfoToProto(
      ast.source_info(), checked_expr.mutable_source_info()));
  for (auto it = ast.reference_map().begin(); it != ast.reference_map().end();
       ++it) {
    ReferencePb& dest_reference =
        (*checked_expr.mutable_reference_map())[it->first];
    CEL_ASSIGN_OR_RETURN(dest_reference, ReferenceToProto(it->second));
  }

  for (auto it = ast.type_map().begin(); it != ast.type_map().end(); ++it) {
    TypePb& dest_type = (*checked_expr.mutable_type_map())[it->first];
    CEL_RETURN_IF_ERROR(TypeSpecToProto(it->second, &dest_type));
  }

  return absl::OkStatus();
}

}  // namespace cel
