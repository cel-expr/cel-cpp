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

#ifndef THIRD_PARTY_CEL_CPP_PARSER_OPTIONS_H_
#define THIRD_PARTY_CEL_CPP_PARSER_OPTIONS_H_

#include "absl/base/attributes.h"
#include "parser/internal/options.h"

namespace cel {

// Options for configuring the limits and features of the parser.
struct ParserOptions final {
  // Limit of the number of error recovery attempts made by the ANTLR parser
  // when processing an input. This limit, when reached, will halt further
  // parsing of the expression.
  int error_recovery_limit = ::cel::parser_internal::kDefaultErrorRecoveryLimit;

  // Limit on the amount of recursive parse instructions permitted when building
  // the abstract syntax tree for the expression. This prevents pathological
  // inputs from causing stack overflows.
  int max_recursion_depth = ::cel::parser_internal::kDefaultMaxRecursionDepth;

  // Limit on the number of codepoints in the input string which the parser will
  // attempt to parse.
  int expression_size_codepoint_limit =
      ::cel::parser_internal::kExpressionSizeCodepointLimit;

  // Limit on the number of lookahead tokens to consume when attempting to
  // recover from an error.
  int error_recovery_token_lookahead_limit =
      ::cel::parser_internal::kDefaultErrorRecoveryTokenLookaheadLimit;

  // Limit on the number of expression nodes in the abstract syntax tree for the
  // expression. This prevents cases where macro expansion results in an AST
  // that is larger than expected from the source expression. Once exceeded,
  // the parser will record an error and stop expanding macros but continue
  // parsing to report other errors.
  int expression_node_limit =
      ::cel::parser_internal::kDefaultExpressionNodeLimit;

  // Add macro calls to macro_calls list in source_info.
  bool add_macro_calls = ::cel::parser_internal::kDefaultAddMacroCalls;

  // Enable support for optional syntax.
  bool enable_optional_syntax = false;

  // Disable standard macros (has, all, exists, exists_one, filter, map).
  bool disable_standard_macros = false;

  // Deprecated: The builtin and extension macros now always use the new
  // accumulator variable name.
  // This option has no effect.
  bool enable_hidden_accumulator_var = true;

  // Enables support for identifier quoting syntax:
  // "message.`skewer-case-field`"
  //
  // Limited to field specifiers in select and message creation,
  // enabled by default
  bool enable_quoted_identifiers = true;

  // Enables parsing logical AND & OR operators as a single flat variadic call
  // instead of a balanced/nested binary AST structure.
  bool enable_variadic_logical_operators = false;

  // Enable the manual (Pratt) parser instead of the ANTLR parser.
  //
  // CAUTION: Pratt Parser is work in progress. Don't use unless you are aware
  // of the risks. There is currently no support whatsoever available for
  // clients of this option.
  //
  // This option is temporary and should not be used by new code outside of the
  // early testing of the Pratt parser.
  // TODO(b/527638023): Remove this option once the ANTLR parser is removed.
  bool enable_pratt_parser = true;
};

}  // namespace cel

namespace google::api::expr::parser {

using ParserOptions = ::cel::ParserOptions;

ABSL_DEPRECATED("Use ParserOptions().error_recovery_limit instead.")
inline constexpr int kDefaultErrorRecoveryLimit =
    ::cel::parser_internal::kDefaultErrorRecoveryLimit;
ABSL_DEPRECATED("Use ParserOptions().max_recursion_depth instead.")
inline constexpr int kDefaultMaxRecursionDepth =
    ::cel::parser_internal::kDefaultMaxRecursionDepth;
ABSL_DEPRECATED("Use ParserOptions().expression_size_codepoint_limit instead.")
inline constexpr int kExpressionSizeCodepointLimit =
    ::cel::parser_internal::kExpressionSizeCodepointLimit;
ABSL_DEPRECATED(
    "Use ParserOptions().error_recovery_token_lookahead_limit instead.")
inline constexpr int kDefaultErrorRecoveryTokenLookaheadLimit =
    ::cel::parser_internal::kDefaultErrorRecoveryTokenLookaheadLimit;
ABSL_DEPRECATED("Use ParserOptions().add_macro_calls instead.")
inline constexpr bool kDefaultAddMacroCalls =
    ::cel::parser_internal::kDefaultAddMacroCalls;

}  // namespace google::api::expr::parser

#endif  // THIRD_PARTY_CEL_CPP_PARSER_OPTIONS_H_
