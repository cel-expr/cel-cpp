// Copyright 2021 Google LLC
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

#include "parser/parser.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cel/expr/syntax.pb.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "common/ast.h"
#include "common/ast/expr_proto.h"
#include "common/ast/source_info_proto.h"
#include "common/source.h"
#include "internal/status_macros.h"
#ifndef EXCLUDE_CEL_ANTLR_PARSER
#include "parser/internal/antlr_parser.h"
#endif
#include "parser/internal/pratt_parser.h"
#include "parser/macro.h"
#include "parser/macro_registry.h"
#include "parser/options.h"
#include "parser/parser_interface.h"
#include "parser/source_factory.h"

namespace google::api::expr::parser {

using ::cel::Macro;
using ::cel::ParserOptions;
using ::cel::expr::ParsedExpr;
using ::google::api::expr::parser::EnrichedSourceInfo;
using ::google::api::expr::parser::VerboseParsedExpr;

absl::StatusOr<VerboseParsedExpr> EnrichedParse(
    const cel::Source& source, const cel::MacroRegistry& registry,
    const ParserOptions& options) {
  ParsedExpr parsed_expr;
  EnrichedSourceInfo enriched_source_info;
  std::unique_ptr<cel::Ast> ast;
#ifdef EXCLUDE_CEL_ANTLR_PARSER
  CEL_ASSIGN_OR_RETURN(
      ast, cel::parser_internal::PrattParseImpl(source, registry, options,
                                                /*parse_issues=*/nullptr,
                                                &enriched_source_info));
#else
  if (options.enable_pratt_parser) {
    CEL_ASSIGN_OR_RETURN(
        ast, cel::parser_internal::PrattParseImpl(source, registry, options,
                                                  /*parse_issues=*/nullptr,
                                                  &enriched_source_info));
  } else {
    CEL_ASSIGN_OR_RETURN(
        ast, cel::parser_internal::AntlrParseImpl(source, registry, options,
                                                  /*parse_issues=*/nullptr,
                                                  &enriched_source_info));
  }
#endif

  CEL_RETURN_IF_ERROR(cel::ast_internal::ExprToProto(
      ast->root_expr(), parsed_expr.mutable_expr()));
  CEL_RETURN_IF_ERROR(cel::ast_internal::SourceInfoToProto(
      ast->source_info(), parsed_expr.mutable_source_info()));
  return VerboseParsedExpr(std::move(parsed_expr),
                           std::move(enriched_source_info));
}

absl::StatusOr<VerboseParsedExpr> EnrichedParse(
    absl::string_view expression, const std::vector<Macro>& macros,
    absl::string_view description, const ParserOptions& options) {
  CEL_ASSIGN_OR_RETURN(auto source,
                       cel::NewSource(expression, std::string(description)));
  cel::MacroRegistry macro_registry;
  CEL_RETURN_IF_ERROR(macro_registry.RegisterMacros(macros));
  return EnrichedParse(*source, macro_registry, options);
}

absl::StatusOr<ParsedExpr> ParseWithMacros(absl::string_view expression,
                                           const std::vector<Macro>& macros,
                                           absl::string_view description,
                                           const ParserOptions& options) {
  CEL_ASSIGN_OR_RETURN(auto verbose_parsed_expr,
                       EnrichedParse(expression, macros, description, options));
  return verbose_parsed_expr.parsed_expr();
}

absl::StatusOr<ParsedExpr> Parse(absl::string_view expression,
                                 absl::string_view description,
                                 const ParserOptions& options) {
  std::vector<Macro> macros;
  if (!options.disable_standard_macros) {
    macros = Macro::AllMacros();
  }
  if (options.enable_optional_syntax) {
    macros.push_back(cel::OptMapMacro());
    macros.push_back(cel::OptFlatMapMacro());
  }
  return ParseWithMacros(expression, macros, description, options);
}

absl::StatusOr<::cel::expr::ParsedExpr> Parse(
    const cel::Source& source, const cel::MacroRegistry& registry,
    const ParserOptions& options) {
  CEL_ASSIGN_OR_RETURN(auto verbose_expr,
                       EnrichedParse(source, registry, options));
  return verbose_expr.parsed_expr();
}

}  // namespace google::api::expr::parser

namespace cel {

std::unique_ptr<ParserBuilder> NewParserBuilder(const ParserOptions& options) {
#ifdef EXCLUDE_CEL_ANTLR_PARSER
  return parser_internal::NewPrattParserBuilder(options);
#else
  if (options.enable_pratt_parser) {
    return parser_internal::NewPrattParserBuilder(options);
  }
  return parser_internal::NewAntlrParserBuilder(options);
#endif
}

}  // namespace cel
