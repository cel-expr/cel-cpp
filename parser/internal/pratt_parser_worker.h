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

#ifndef THIRD_PARTY_CEL_CPP_PARSER_INTERNAL_PRATT_PARSER_WORKER_H_
#define THIRD_PARTY_CEL_CPP_PARSER_INTERNAL_PRATT_PARSER_WORKER_H_

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "common/operators.h"
#include "common/source.h"
#include "internal/lexis.h"
#include "internal/strings.h"
#include "parser/internal/ast_factory_interface.h"
#include "parser/internal/lexer.h"
#include "parser/options.h"
#include "parser/parser_interface.h"

namespace cel::parser_internal {

class ParserWorker {
 public:
  ParserWorker(const cel::Source& source, const cel::ParserOptions& options,
               std::vector<cel::ParseIssue>* absl_nullable parse_issues,
               bool track_node_ranges = false);

  const absl::flat_hash_map<int64_t, int32_t>& GetNodePositions() const {
    return positions_;
  }
  const absl::flat_hash_map<int64_t, std::pair<int32_t, int32_t>>&
  GetNodeRanges() const {
    return node_ranges_;
  }
  absl::Span<const int32_t> GetLineOffsets() const {
    return source_.line_offsets();
  }
  bool has_errors() const { return error_count_ > 0; }
  bool is_recursion_limit_exceeded() const { return recursion_limit_exceeded_; }

 protected:
  const cel::Source& source() const { return source_; }
  const cel::ParserOptions& options() const { return options_; }
  // Token stream management
  void InitTokenStream();
  Token NextSignificantToken(bool report_error = true);
  Token NextToken();
  bool Expect(TokenType type, absl::string_view msg = "");
  std::string GetTokenText(const Token& tok) const;
  void SynchronizeOnDelimiter();

  // ID and Position tracking
  int64_t NextId(int32_t position);
  int64_t NextId(const Token& token) {
    int64_t id = NextId(token.start);
    if (ABSL_PREDICT_FALSE(track_node_ranges_)) {
      if (token.start >= 0 && token.end > token.start) {
        node_ranges_[id] = {token.start, token.end - 1};
      }
    }
    return id;
  }
  int64_t NextId();
  int64_t CopyId(int64_t id);
  void EraseId(int64_t id);
  void SetNodeRange(int64_t id, int32_t begin, int32_t end) {
    if (ABSL_PREDICT_FALSE(track_node_ranges_)) {
      if (id != 0 && begin >= 0 && end >= begin) {
        node_ranges_[id] = {begin, end};
      }
    }
  }

  // Error reporting and recovery
  bool is_recovery_limit_exceeded() const {
    return error_count_ > options_.error_recovery_limit;
  }
  void ReportError(int32_t position, absl::string_view msg);
  void ReportError(const SourceLocation& loc, absl::string_view msg);
  void ReportError(const Token& token, absl::string_view msg) {
    ReportError(token.start, msg);
  }

  const cel::Source& source_;
  cel::ParserOptions options_;
  Lexer lexer_;
  Token current_token_;
  Token peek_token_;
  int recursion_depth_ = 0;
  int64_t next_id_ = 1;
  bool node_limit_exceeded_ = false;
  absl::flat_hash_map<int64_t, int32_t> positions_;
  absl::flat_hash_map<int64_t, std::pair<int32_t, int32_t>> node_ranges_;
  std::vector<cel::ParseIssue>* absl_nullable parse_issues_;
  int error_count_ = 0;
  bool lexer_error_reported_ = false;
  bool recursion_limit_exceeded_ = false;
  bool track_node_ranges_ = false;
};

struct BinaryOpInfo {
  int precedence = 0;
  absl::string_view name;
  bool is_logical = false;
  TokenType type = TokenType::kError;
};

// Generic Pratt parser implementation parameterized by the AST node type
// (`ExprNode`).
//
// This class implements the core recursive-descent and operator-precedence
// parsing logic for CEL without depending on a concrete expression node data
// structure. All inspection and construction of AST nodes is performed through
// `AstFactoryInterface<ExprNode>`.
//
// See `AstFactoryInterface` documentation in `ast_factory_interface.h` for
// details on how to use this generic parser with alternative AST structures.
template <typename ExprNode>
class PrattParserWorker : public ParserWorker {
 public:
  using ParserWorker::NextId;

  explicit PrattParserWorker(
      const cel::Source& source, const cel::ParserOptions& options,
      std::vector<cel::ParseIssue>* absl_nullable parse_issues,
      AstFactoryInterface<ExprNode>& ast_factory,
      bool track_node_ranges = false)
      : ParserWorker(source, options, parse_issues, track_node_ranges),
        ast_factory_(ast_factory) {
    this->InitTokenStream();
  }

  ExprNode Parse();

  absl::flat_hash_map<int64_t, ExprNode> ReleaseMacroCalls() {
    return std::move(macro_calls_);
  }

 private:
  class MacroExpanderSupport : public MacroExprExpanderSupport<ExprNode> {
   public:
    MacroExpanderSupport(PrattParserWorker& worker, int32_t macro_position)
        : worker_(worker), macro_position_(macro_position) {}

    int64_t NextId() {
      return macro_position_ >= 0 ? worker_.NextId(macro_position_)
                                  : worker_.NextId();
    }

    int64_t CopyId(int64_t id) { return worker_.CopyId(id); }

    ExprNode ReportError(absl::string_view message) {
      if (macro_position_ >= 0) {
        worker_.ReportError(macro_position_, message);
      } else {
        worker_.ReportError(worker_.current_token_.start, message);
      }
      return ExprNode();
    }

    ExprNode ReportErrorAt(const ExprNode& expr, absl::string_view message) {
      int32_t pos = 0;
      auto it =
          worker_.GetNodePositions().find(worker_.ast_factory_.GetId(expr));
      if (it != worker_.GetNodePositions().end()) {
        pos = it->second;
      }
      worker_.ReportError(pos, message);
      return ExprNode();
    }

   private:
    PrattParserWorker& worker_;
    int32_t macro_position_;
  };

  using CelOperator = ::google::api::expr::common::CelOperator;

  ExprNode ParseExpr();
  // Parses binary operator expressions and ternary conditional expressions
  // (`? :`) using operator-precedence (Pratt) parsing. Consumes operators from
  // the token stream whose binding precedence is greater than or equal to
  // `min_prec`.
  //
  // Example (`min_prec = 0` on `a + b * c`): Parses prefix/postfix expression
  // `a`, encounters `+` (precedence 1 >= 0), recurses with `ParseBinary(2)` to
  // parse `b * c` at higher precedence, and builds the `+` call node.
  //
  // Example (`min_prec = 0` on `a ? b : c`): Parses condition `a`, encounters
  // `?`, consumes `?`, and recurses with `ParseBinary(1)` for true branch `b`
  // and `ParseBinary(0)` for false branch `c`.
  ExprNode ParseBinaryAndTernary(int min_prec);

  // Parses ternary conditional expressions (`condition ? true_expr :
  // false_expr`).
  void ParseTernary(ExprNode& lhs);

  // Helper method for parsing a contiguous chain of same-precedence logical
  // operators (`&&` or `||`) iteratively into a list of terms and operator IDs.
  // Constructs either a balanced binary AST (`(a && b) && c`) or a single
  // variadic call (`_&&_(a, b, c)`) via `BalanceLogical`. This avoids deep C++
  // recursion when processing expressions with dozens of chained logical terms.
  //
  // Example (`a && b && c && d`): Iteratively collects terms `[a, b, c, d]` and
  // builds `((a && b) && c) && d` without ascending/descending C++ stack
  // frames for each term.
  void ParseBalancedLogicalChain(ExprNode& lhs, const BinaryOpInfo& op_info);

  // Parses prefix unary operators (`!`, `-`) and trailing postfix
  // member/indexing operations (`.field`, `[index]`, `.method(args)`). First
  // calls `ParseUnary()` to obtain the base operand, then consumes trailing
  // selector
  // (`.`) and bracket (`[` or `{`) operations in an iterative loop.
  //
  // Example (`!a.b[0].c(x)`): `ParseUnary()` consumes `!` and calls
  // `ParseSelectorChain()` for `a.b[0].c(x)`, which parses primary `a` and
  // then loops iteratively through `.b`, `[0]`, and `.c(x)`.
  ExprNode ParseSelectorChain();

  // Handler for postfix member, index, receiver method, and
  // struct initializer operations (`.field`, `[index]`, `.method(args)`,
  // `Type{field: val}`).
  //
  // Processes continuous postfix operation chains iteratively.
  void ParseSelectorChainTail(ExprNode& lhs);

  // Parses prefix unary operators (logical NOT `!` and negation `-`). If a
  // numeric literal immediately follows `-`, folds it directly into a negative
  // constant node (`-42`, `-3.14`). Otherwise, wraps the operand in a `_!_` or
  // `_-_` function call node. Delegates to `ParseSelectorChain()` for the
  // operand to naturally support chained prefix operations (`!!x`, `-!y`), or
  // falls back to `ParsePrimary()` if no prefix operator is encountered.
  //
  // Example (`-42`): Folds directly into a negative integer constant `-42`.
  // Example (`!has(x.y)`): Consumes `!` and creates a `LOGICAL_NOT` call node
  // wrapping `has(x.y)`.
  ExprNode ParseUnary();

  // Parses unary operators (`!`, `-`).
  ExprNode ParseUnaryOps();

  // Parses unary operator chains (`!`, `-`).
  ExprNode ParseUnaryOpsChain(Token first_op);

  // Parses primary leaf expressions (`nud` atomic atoms), including
  // parenthesized expressions (`(expr)`), literal constants (`null`, `true`,
  // `false`, numbers, strings, bytes), collection initializers (`[list]`,
  // `{map}`), identifiers
  // (`foo`), and global or namespace function/macro calls (`foo(args)`,
  // `.pkg.func(args)`).
  //
  // For parenthesized expressions and collection literals, delegates back to
  // `ParseExpr()` or collection subroutines (`ParseList()`, `ParseMap()`).
  // For global function calls (`foo(args)`), attempts macro expansion before
  // falling back to standard call node construction.
  //
  // Example (`(a + b)`): Consumes `(`, recurses to `ParseExpr()`, and expects
  // `)`. Example (`has(x.y)`): Consumes `has`, parses arguments `(x.y)`, and
  // expands the `has` macro.
  ExprNode ParsePrimary();

  ExprNode ParseList();
  ExprNode ParseMap();
  ExprNode ParseStruct(int64_t obj_id, absl::string_view struct_name);
  std::vector<ExprNode> ParseArguments(TokenType close_token);
  ExprNode ParseIntLiteral();
  ExprNode ParseNegativeIntLiteral(int64_t node_id);
  ExprNode ParseUintLiteral();
  ExprNode ParseDoubleLiteral();
  ExprNode ParseNegativeDoubleLiteral(int64_t node_id);
  ExprNode ParseStringLiteral();
  ExprNode ParseBytesLiteral();
  void BuildBinaryCall(int64_t op_id, absl::string_view op_name, ExprNode& lhs,
                       ExprNode rhs);
  ExprNode ParseIdentOrCall();
  std::string NormalizeIdent(const Token& tok, bool allow_quoted);
  std::optional<std::string> ExtractStructName(const ExprNode& expr);
  int32_t GetLeftmostPosition(const ExprNode& expr);
  ExprNode BalancedTree(absl::string_view op, std::vector<ExprNode>& terms,
                        const std::vector<int64_t>& ops, int lo, int hi);
  ExprNode BalanceLogical(absl::string_view op, std::vector<ExprNode> terms,
                          std::vector<int64_t> ops, bool enable_variadic);

  std::optional<ExprNode> TryExpandMacro(int64_t expr_id,
                                         absl::string_view function,
                                         ExprNode* target,
                                         std::vector<ExprNode>& args);

  // Save the original structure of the expression before macro expansion.
  void RecordMacroCall(int64_t macro_id, absl::string_view function,
                       std::optional<ExprNode> target,
                       std::vector<ExprNode> arguments);

  int CountGroupingParentheses();

  AstFactoryInterface<ExprNode>& ast_factory_;
  absl::flat_hash_map<int64_t, ExprNode> macro_calls_;
};

template <typename ExprNode>
ExprNode PrattParserWorker<ExprNode>::Parse() {
  ExprNode expr = ParseExpr();
  if (is_recursion_limit_exceeded() || is_recovery_limit_exceeded()) {
    return expr;
  }
  if (peek_token_.type != TokenType::kEnd &&
      peek_token_.type != TokenType::kError) {
    ReportError(peek_token_, "unexpected token after expression");
  }
  return expr;
}

template <typename ExprNode>
ExprNode PrattParserWorker<ExprNode>::ParseExpr() {
  if (recursion_limit_exceeded_ || is_recovery_limit_exceeded()) {
    return ExprNode();
  }
  if (recursion_depth_ > options_.max_recursion_depth) {
    recursion_limit_exceeded_ = true;
    return ExprNode();
  }
  recursion_depth_++;
  ExprNode expr = ParseBinaryAndTernary(0);
  recursion_depth_--;
  return expr;
}

template <typename ExprNode>
void PrattParserWorker<ExprNode>::ParseTernary(ExprNode& lhs) {
  NextToken();
  int64_t op_id = NextId();
  ExprNode true_expr = ParseBinaryAndTernary(1);
  if (!Expect(TokenType::kColon, "expected ':' in conditional expression")) {
    return;
  }
  ExprNode false_expr = ParseBinaryAndTernary(0);
  std::vector<ExprNode> args;
  args.reserve(3);
  args.push_back(std::move(lhs));
  args.push_back(std::move(true_expr));
  args.push_back(std::move(false_expr));
  lhs = ast_factory_.NewCall(op_id, CelOperator::CONDITIONAL, std::move(args));
}

const BinaryOpInfo& GetBinaryOpInfo(TokenType type);

template <typename ExprNode>
void PrattParserWorker<ExprNode>::BuildBinaryCall(int64_t op_id,
                                                  absl::string_view op_name,
                                                  ExprNode& lhs, ExprNode rhs) {
  std::vector<ExprNode> args;
  args.reserve(2);
  args.push_back(std::move(lhs));
  args.push_back(std::move(rhs));
  lhs = ast_factory_.NewCall(op_id, std::string(op_name), std::move(args));
}

// Parses binary operator expressions and ternary conditional expressions
// (`? :`) using Pratt operator-precedence parsing (e.g., `a + b * c`).
template <typename ExprNode>
ExprNode PrattParserWorker<ExprNode>::ParseBinaryAndTernary(int min_prec) {
  ExprNode lhs = ParseSelectorChain();
  while (true) {
    TokenType tok = peek_token_.type;
    if (tok == TokenType::kQuestion && min_prec <= 0) {
      ParseTernary(lhs);
      continue;
    }

    const BinaryOpInfo& op_info = GetBinaryOpInfo(tok);
    if (op_info.precedence < min_prec || op_info.precedence == 0) break;

    if (op_info.is_logical) {
      ParseBalancedLogicalChain(lhs, op_info);
      continue;
    }

    Token op_tok = NextToken();
    int64_t op_id = NextId(op_tok);
    BuildBinaryCall(op_id, op_info.name, lhs,
                    ParseBinaryAndTernary(op_info.precedence + 1));
  }
  return lhs;
}

// Parses continuous chains of logical operators (`&&`, `||`) iteratively
// (e.g., `a && b && c`) and constructs a balanced or variadic AST.
template <typename ExprNode>
void PrattParserWorker<ExprNode>::ParseBalancedLogicalChain(
    ExprNode& lhs, const BinaryOpInfo& op_info) {
  std::vector<ExprNode> terms;
  std::vector<int64_t> ops;
  terms.push_back(std::move(lhs));
  while (peek_token_.type == op_info.type) {
    Token op_tok = NextToken();
    ExprNode rhs = ParseBinaryAndTernary(op_info.precedence + 1);
    ops.push_back(NextId(op_tok));
    terms.push_back(std::move(rhs));
  }
  lhs = BalanceLogical(op_info.name, std::move(terms), std::move(ops),
                       options_.enable_variadic_logical_operators);
}

template <typename ExprNode>
ExprNode PrattParserWorker<ExprNode>::ParseSelectorChain() {
  ExprNode lhs = ParseUnary();
  TokenType tok = peek_token_.type;
  if (tok == TokenType::kDot || tok == TokenType::kLeftBracket ||
      tok == TokenType::kLeftBrace) {
    ParseSelectorChainTail(lhs);
  }
  return lhs;
}

// Parses prefix and postfix member/indexing operations iteratively
// (e.g., `!a.b[0].c(x)`).
template <typename ExprNode>
void PrattParserWorker<ExprNode>::ParseSelectorChainTail(ExprNode& lhs) {
  while (true) {
    TokenType tok = peek_token_.type;
    if (tok == TokenType::kDot) {
      Token dot_tok = NextToken();
      bool optional = false;
      if (peek_token_.type == TokenType::kQuestion) {
        NextToken();
        optional = true;
        if (!options_.enable_optional_syntax) {
          ReportError(dot_tok, "unsupported syntax '.?'");
        }
      }
      Token id_tok = NextToken();
      if (id_tok.type != TokenType::kIdent &&
          id_tok.type != TokenType::kReservedWord) {
        if (id_tok.type != TokenType::kError) {
          ReportError(id_tok, "expected identifier after '.'");
        }
        SynchronizeOnDelimiter();
        return;
      }
      bool is_member_call = peek_token_.type == TokenType::kLeftParen;
      std::string id_text =
          NormalizeIdent(id_tok, /*allow_quoted=*/!is_member_call);
      if (optional) {
        int64_t op_id = NextId(dot_tok);
        std::vector<ExprNode> args;
        args.reserve(2);
        args.push_back(std::move(lhs));
        args.push_back(
            ast_factory_.NewStringConst(NextId(id_tok), std::move(id_text)));
        lhs = ast_factory_.NewCall(op_id, CelOperator::OPT_SELECT,
                                   std::move(args));
      } else if (peek_token_.type == TokenType::kLeftParen) {
        Token lparen = NextToken();
        int64_t call_id = NextId(lparen);
        std::vector<ExprNode> args = ParseArguments(TokenType::kRightParen);
        if (std::optional<ExprNode> expanded =
                TryExpandMacro(call_id, id_text, &lhs, args);
            expanded.has_value()) {
          lhs = std::move(*expanded);
        } else {
          lhs = ast_factory_.NewMemberCall(call_id, id_text, std::move(lhs),
                                           std::move(args));
        }
      } else {
        lhs = ast_factory_.NewSelect(NextId(dot_tok), std::move(lhs), id_text);
      }
    } else if (tok == TokenType::kLeftBracket) {
      Token bracket_tok = NextToken();
      int64_t op_id = NextId(bracket_tok);
      bool optional = false;
      if (peek_token_.type == TokenType::kQuestion) {
        NextToken();
        optional = true;
        if (!options_.enable_optional_syntax) {
          ReportError(bracket_tok, "unsupported syntax '?'");
        }
      }
      ExprNode index = ParseExpr();
      Expect(TokenType::kRightBracket, "expected ']'");
      std::vector<ExprNode> args;
      args.reserve(2);
      args.push_back(std::move(lhs));
      args.push_back(std::move(index));
      lhs = ast_factory_.NewCall(
          op_id, optional ? CelOperator::OPT_INDEX : CelOperator::INDEX,
          std::move(args));
    } else if (tok == TokenType::kLeftBrace) {
      int32_t struct_pos = GetLeftmostPosition(lhs);
      if (auto struct_name = ExtractStructName(lhs); struct_name.has_value()) {
        lhs = ParseStruct(NextId(struct_pos), *struct_name);
      } else {
        break;
      }
    } else {
      break;
    }
  }
}

template <typename ExprNode>
ExprNode PrattParserWorker<ExprNode>::ParseUnaryOpsChain(Token first_op) {
  struct UnaryOpInfo {
    TokenType type;
    int64_t id;
  };
  std::vector<UnaryOpInfo> ops;
  ops.push_back({first_op.type, NextId(first_op)});

  while (peek_token_.type == TokenType::kExclamation ||
         peek_token_.type == TokenType::kMinus) {
    Token op = NextToken();
    ops.push_back({op.type, NextId(op)});
  }

  ExprNode operand;
  if (!ops.empty() && ops.back().type == TokenType::kMinus) {
    if (peek_token_.type == TokenType::kInt) {
      int64_t op_id = ops.back().id;
      ops.pop_back();
      operand = ParseNegativeIntLiteral(op_id);
    } else if (peek_token_.type == TokenType::kFloat) {
      int64_t op_id = ops.back().id;
      ops.pop_back();
      operand = ParseNegativeDoubleLiteral(op_id);
    } else {
      operand = ParseSelectorChain();
    }
  } else {
    operand = ParseSelectorChain();
  }

  for (int i = static_cast<int>(ops.size()) - 1; i >= 0; --i) {
    std::vector<ExprNode> args;
    args.push_back(std::move(operand));
    absl::string_view op_name = (ops[i].type == TokenType::kExclamation)
                                    ? CelOperator::LOGICAL_NOT
                                    : CelOperator::NEGATE;
    operand =
        ast_factory_.NewCall(ops[i].id, std::string(op_name), std::move(args));
  }

  return operand;
}

// Parses prefix unary operators (`!`, `-`) iteratively and folds negative
// numeric literals (e.g., `-42`, `!has(x.y)`).
template <typename ExprNode>
ExprNode PrattParserWorker<ExprNode>::ParseUnary() {
  TokenType tok = peek_token_.type;
  if (tok == TokenType::kExclamation || tok == TokenType::kMinus) {
    return ParseUnaryOps();
  }
  return ParsePrimary();
}

template <typename ExprNode>
ExprNode PrattParserWorker<ExprNode>::ParseUnaryOps() {
  Token op = NextToken();
  TokenType op_type = op.type;
  if (peek_token_.type == TokenType::kExclamation ||
      peek_token_.type == TokenType::kMinus) {
    return ParseUnaryOpsChain(op);
  }

  if (op_type == TokenType::kMinus) {
    if (peek_token_.type == TokenType::kInt) {
      return ParseNegativeIntLiteral(NextId(op));
    }
    if (peek_token_.type == TokenType::kFloat) {
      return ParseNegativeDoubleLiteral(NextId(op));
    }
  }

  int64_t op_id = NextId(op);
  ExprNode operand = ParseSelectorChain();
  std::vector<ExprNode> args;
  args.push_back(std::move(operand));
  absl::string_view op_name = (op_type == TokenType::kExclamation)
                                  ? CelOperator::LOGICAL_NOT
                                  : CelOperator::NEGATE;
  return ast_factory_.NewCall(op_id, std::string(op_name), std::move(args));
}

// Parses identifiers (e.g., `foo`, `.foo`) and global function or macro calls
// (e.g., `foo(args)`, `has(x.y)`).
template <typename ExprNode>
ExprNode PrattParserWorker<ExprNode>::ParseIdentOrCall() {
  TokenType tok_type = peek_token_.type;
  bool leading_dot = false;
  Token first_tok = peek_token_;
  if (tok_type == TokenType::kDot) {
    NextToken();
    leading_dot = true;
  }
  Token id_tok = NextToken();
  if (id_tok.type != TokenType::kIdent &&
      id_tok.type != TokenType::kReservedWord) {
    if (id_tok.type != TokenType::kError) {
      ReportError(id_tok, "expected identifier");
    }
    return ast_factory_.NewUnspecified(NextId(id_tok));
  }
  std::string id_text = NormalizeIdent(id_tok, /*allow_quoted=*/false);
  if (ABSL_PREDICT_FALSE(id_tok.type == TokenType::kReservedWord)) {
    if (cel::internal::LexisIsReserved(id_text)) {
      ReportError(id_tok, absl::StrFormat("reserved identifier: %s", id_text));
    }
  }
  std::string name =
      leading_dot ? absl::StrCat(".", id_text) : std::string(id_text);
  int64_t id = NextId(leading_dot ? first_tok : id_tok);
  if (peek_token_.type == TokenType::kLeftParen) {
    NextToken();
    std::vector<ExprNode> args = ParseArguments(TokenType::kRightParen);
    if (auto expanded = TryExpandMacro(id, name, nullptr, args);
        expanded.has_value()) {
      return std::move(*expanded);
    }
    return ast_factory_.NewCall(id, name, std::move(args));
  }
  return ast_factory_.NewIdent(id, std::move(name));
}

// Parses primary leaf expressions, including parenthesized expressions
// (`(expr)`), literals (`null`, `true`, numbers, strings, bytes), collection
// initializers
// (`[...]`, `{...}`), and identifiers/global function calls (`foo`,
// `has(x.y)`).
template <typename ExprNode>
ExprNode PrattParserWorker<ExprNode>::ParsePrimary() {
  switch (peek_token_.type) {
    case TokenType::kLeftParen: {
      int grouping_paren_count = CountGroupingParentheses();
      for (int i = 0; i < grouping_paren_count; ++i) {
        NextToken();
      }
      ExprNode expr = ParseExpr();
      for (int i = 0; i < grouping_paren_count; ++i) {
        Expect(TokenType::kRightParen);
      }
      return expr;
    }
    case TokenType::kNull:
      return ast_factory_.NewNullConst(NextId(NextToken()));
    case TokenType::kTrue:
    case TokenType::kFalse: {
      Token tok = NextToken();
      return ast_factory_.NewBoolConst(NextId(tok),
                                       tok.type == TokenType::kTrue);
    }
    case TokenType::kInt:
      return ParseIntLiteral();
    case TokenType::kUint:
      return ParseUintLiteral();
    case TokenType::kFloat:
      return ParseDoubleLiteral();
    case TokenType::kString:
      return ParseStringLiteral();
    case TokenType::kBytes:
      return ParseBytesLiteral();
    case TokenType::kLeftBracket:
      return ParseList();
    case TokenType::kLeftBrace:
      return ParseMap();
    case TokenType::kDot:
    case TokenType::kIdent:
    case TokenType::kReservedWord:
      return ParseIdentOrCall();
    default: {
      Token bad_tok = NextToken();
      if (bad_tok.type != TokenType::kError) {
        if (bad_tok.type == TokenType::kEnd) {
          ReportError(
              bad_tok,
              "Syntax error: mismatched input '<EOF>' expecting expression");
        } else {
          ReportError(bad_tok, "unexpected token");
        }
      }
      return ast_factory_.NewUnspecified(NextId(bad_tok));
    }
  }
}

// Parses list creation literals (e.g., `[1, 2, ?3]`).
template <typename ExprNode>
ExprNode PrattParserWorker<ExprNode>::ParseList() {
  Token open_tok = NextToken();
  int64_t list_id = NextId(open_tok);
  ListNodeBuilder<ExprNode> builder = ast_factory_.NewListBuilder(list_id);
  while (peek_token_.type != TokenType::kRightBracket &&
         peek_token_.type != TokenType::kEnd) {
    bool optional = false;
    if (peek_token_.type == TokenType::kQuestion) {
      Token q = NextToken();
      optional = true;
      if (!options_.enable_optional_syntax) {
        ReportError(q, "unsupported syntax '?'");
      }
    }
    builder.Add(ParseExpr(), optional);
    if (peek_token_.type == TokenType::kComma) {
      NextToken();
    } else {
      break;
    }
  }
  if (Expect(TokenType::kRightBracket, "expected ']'")) {
    SetNodeRange(list_id, open_tok.start, current_token_.end - 1);
  }
  return builder.Build();
}

// Parses map creation literals (e.g., `{"key": "value", ?"opt_key": 42}`).
template <typename ExprNode>
ExprNode PrattParserWorker<ExprNode>::ParseMap() {
  Token open_tok = NextToken();
  int64_t map_id = NextId(open_tok);
  MapNodeBuilder<ExprNode> builder = ast_factory_.NewMapBuilder(map_id);
  while (peek_token_.type != TokenType::kRightBrace &&
         peek_token_.type != TokenType::kEnd) {
    bool optional = false;
    Token key_start = peek_token_;
    if (key_start.type == TokenType::kQuestion) {
      Token q = NextToken();
      optional = true;
      if (!options_.enable_optional_syntax) {
        ReportError(q, "unsupported syntax '?'");
      }
      key_start = peek_token_;
    }
    ExprNode key = ParseExpr();
    Token colon = peek_token_;
    if (!Expect(TokenType::kColon, "expected ':' in map entry")) {
      break;
    }
    int64_t entry_id = NextId(colon);
    builder.Add(entry_id, std::move(key), ParseExpr(), optional);
    if (peek_token_.type == TokenType::kComma) {
      NextToken();
    } else {
      break;
    }
  }
  if (Expect(TokenType::kRightBrace, "expected '}'")) {
    SetNodeRange(map_id, open_tok.start, current_token_.end - 1);
  }
  return builder.Build();
}

// Parses struct literas for a type name (e.g., `Msg{field: 10, ?opt: "val"}`).
template <typename ExprNode>
ExprNode PrattParserWorker<ExprNode>::ParseStruct(
    int64_t obj_id, absl::string_view struct_name) {
  Token open_tok = NextToken();
  StructNodeBuilder<ExprNode> builder =
      ast_factory_.NewStructBuilder(obj_id, std::string(struct_name));
  while (peek_token_.type != TokenType::kRightBrace &&
         peek_token_.type != TokenType::kEnd) {
    bool optional = false;
    if (peek_token_.type == TokenType::kQuestion) {
      Token q = NextToken();
      optional = true;
      if (!options_.enable_optional_syntax) {
        ReportError(q, "unsupported syntax '?'");
      }
    }
    Token field_tok = NextToken();
    if (field_tok.type != TokenType::kIdent &&
        field_tok.type != TokenType::kReservedWord) {
      ReportError(field_tok, "expected struct field name");
      SynchronizeOnDelimiter();
      break;
    }
    std::string field_name = NormalizeIdent(field_tok, /*allow_quoted=*/true);
    Token colon = peek_token_;
    if (!Expect(TokenType::kColon, "expected ':' in struct field")) {
      break;
    }
    int64_t field_id = NextId(colon);
    builder.Add(field_id, std::move(field_name), ParseExpr(), optional);
    if (peek_token_.type == TokenType::kComma) {
      NextToken();
    } else {
      break;
    }
  }
  if (Expect(TokenType::kRightBrace, "expected '}'")) {
    int32_t start_pos = open_tok.start;
    auto it = positions_.find(obj_id);
    if (it != positions_.end()) {
      start_pos = it->second;
    }
    SetNodeRange(obj_id, start_pos, current_token_.end - 1);
  }
  return builder.Build();
}

// Parses comma-separated arguments (e.g., `(arg1, arg2)` in call).
template <typename ExprNode>
std::vector<ExprNode> PrattParserWorker<ExprNode>::ParseArguments(
    TokenType close_token) {
  std::vector<ExprNode> args;
  if (peek_token_.type != close_token && peek_token_.type != TokenType::kEnd) {
    while (true) {
      args.push_back(ParseExpr());
      if (peek_token_.type == TokenType::kComma) {
        NextToken();
        if (peek_token_.type == close_token) {
          break;
        }
        continue;
      }
      break;
    }
  }
  Expect(close_token);
  return args;
}

// Parses decimal & hexadecimal ints (e.g., `42`, `0x1A`).
template <typename ExprNode>
ExprNode PrattParserWorker<ExprNode>::ParseIntLiteral() {
  Token tok = NextToken();
  std::string value(GetTokenText(tok));
  int64_t int_val = 0;
  if (absl::StartsWith(value, "0x") || absl::StartsWith(value, "0X")) {
    if (absl::SimpleHexAtoi(value, &int_val)) {
      return ast_factory_.NewIntConst(NextId(tok), int_val);
    }
  } else if (absl::SimpleAtoi(value, &int_val)) {
    return ast_factory_.NewIntConst(NextId(tok), int_val);
  }
  ReportError(tok, "invalid int literal");
  return ast_factory_.NewUnspecified(NextId(tok));
}

template <typename ExprNode>
ExprNode PrattParserWorker<ExprNode>::ParseNegativeIntLiteral(int64_t node_id) {
  Token lit_tok = NextToken();
  std::string text = GetTokenText(lit_tok);
  int64_t int_val = 0;
  bool success = false;
  if (absl::StartsWith(text, "0x") || absl::StartsWith(text, "0X")) {
    uint64_t uint_val = 0;
    if (absl::SimpleHexAtoi(text, &uint_val)) {
      if (uint_val <= uint64_t{0x8000000000000000}) {
        if (uint_val == uint64_t{0x8000000000000000}) {
          int_val = std::numeric_limits<int64_t>::min();
        } else {
          int_val = -static_cast<int64_t>(uint_val);
        }
        success = true;
      }
    }
  } else if (absl::SimpleAtoi(text, &int_val)) {
    int_val = -int_val;
    success = true;
  } else {
    // Separately handle -2^63, which is not representable as -(2^63)
    std::string val = absl::StrCat("-", text);
    success = absl::SimpleAtoi(val, &int_val);
  }

  if (success) {
    return ast_factory_.NewIntConst(node_id, int_val);
  }
  ReportError(lit_tok, "invalid int literal");
  return ast_factory_.NewUnspecified(NextId(lit_tok));
}

// Parses unsigned ints (e.g., `42u`, `0x1Au`).
template <typename ExprNode>
ExprNode PrattParserWorker<ExprNode>::ParseUintLiteral() {
  Token tok = NextToken();
  std::string value(GetTokenText(tok));
  if (!value.empty() && (value.back() == 'u' || value.back() == 'U')) {
    value.pop_back();
  }
  uint64_t uint_val = 0;
  if (absl::StartsWith(value, "0x") || absl::StartsWith(value, "0X")) {
    if (absl::SimpleHexAtoi(value, &uint_val)) {
      return ast_factory_.NewUintConst(NextId(tok), uint_val);
    }
  } else if (absl::SimpleAtoi(value, &uint_val)) {
    return ast_factory_.NewUintConst(NextId(tok), uint_val);
  }
  ReportError(tok, "invalid uint literal");
  return ast_factory_.NewUnspecified(NextId(tok));
}

// Parses floating-point numbers (e.g., `3.14159`, `1e-10`).
template <typename ExprNode>
ExprNode PrattParserWorker<ExprNode>::ParseDoubleLiteral() {
  Token tok = NextToken();
  std::string value(GetTokenText(tok));
  double double_val = 0.0;
  if (absl::SimpleAtod(value, &double_val)) {
    return ast_factory_.NewDoubleConst(NextId(tok), double_val);
  }
  ReportError(tok, "invalid double literal");
  return ast_factory_.NewUnspecified(NextId(tok));
}

template <typename ExprNode>
ExprNode PrattParserWorker<ExprNode>::ParseNegativeDoubleLiteral(
    int64_t node_id) {
  Token lit_tok = NextToken();
  double double_val = 0.0;
  if (absl::SimpleAtod(GetTokenText(lit_tok), &double_val)) {
    return ast_factory_.NewDoubleConst(node_id, -double_val);
  }
  ReportError(lit_tok, "invalid double literal");
  return ast_factory_.NewUnspecified(NextId(lit_tok));
}

// Parses string literals (e.g., `"hello"`, `'world'`, `"""multi"""`).
template <typename ExprNode>
ExprNode PrattParserWorker<ExprNode>::ParseStringLiteral() {
  Token tok = NextToken();
  absl::StatusOr<std::string> status_or_val =
      cel::internal::ParseStringLiteral(GetTokenText(tok));
  if (!status_or_val.ok()) {
    ReportError(tok, status_or_val.status().message());
    return ast_factory_.NewUnspecified(NextId(tok));
  }
  return ast_factory_.NewStringConst(NextId(tok), std::move(*status_or_val));
}

// Parses byte sequence literals (e.g., `b"hello"`, `b'world'`).
template <typename ExprNode>
ExprNode PrattParserWorker<ExprNode>::ParseBytesLiteral() {
  Token tok = NextToken();
  absl::StatusOr<std::string> status_or_val =
      cel::internal::ParseBytesLiteral(GetTokenText(tok));
  if (!status_or_val.ok()) {
    ReportError(tok, status_or_val.status().message());
    return ast_factory_.NewUnspecified(NextId(tok));
  }
  return ast_factory_.NewBytesConst(NextId(tok), std::move(*status_or_val));
}

// Normalizes regular & quoted identifiers (e.g., `foo`, `` `quoted.ident` ``).
template <typename ExprNode>
std::string PrattParserWorker<ExprNode>::NormalizeIdent(const Token& tok,
                                                        bool allow_quoted) {
  std::string text = GetTokenText(tok);
  if (text.empty()) return "";
  if (text.front() == '`') {
    if (!allow_quoted) {
      ReportError(tok, "unexpected quoted identifier");
      return "";
    }
    if (!options_.enable_quoted_identifiers) {
      ReportError(tok, "unsupported syntax '`'");
    }
    if (text.size() < 2 || text.back() != '`') {
      ReportError(tok, "unterminated quoted identifier");
      return "";
    }
    // Validate the quoted identifier syntax:
    // ESC_IDENTIFIER : '`' (LETTER | DIGIT | '_' | '.' | '-' | '/' | ' ')+ '`';
    std::string_view inner(text.data() + 1, text.size() - 2);
    if (inner.empty()) {
      ReportError(tok, "unexpected quoted identifier");
      return "";
    }
    for (char c : inner) {
      if (!absl::ascii_isalnum(static_cast<unsigned char>(c)) && c != '_' &&
          c != '.' && c != '-' && c != '/' && c != ' ') {
        ReportError(tok, "unexpected quoted identifier");
        return "";
      }
    }
    return std::string(inner);
  }
  return std::string(text);
}

// Extracts qualified type names (e.g., `a.b.Msg` from `a.b.Msg{}`).
template <typename ExprNode>
std::optional<std::string> PrattParserWorker<ExprNode>::ExtractStructName(
    const ExprNode& expr) {
  if (ast_factory_.IsConst(expr)) {
    return std::nullopt;
  }
  if (ast_factory_.IsIdent(expr)) {
    std::string name(ast_factory_.GetIdentName(expr));
    EraseId(ast_factory_.GetId(expr));
    return name;
  }
  if (ast_factory_.IsSelect(expr)) {
    if (ast_factory_.IsPresenceTest(expr)) return std::nullopt;
    const ExprNode* operand = ast_factory_.GetSelectOperand(expr);
    if (operand == nullptr) return std::nullopt;
    EraseId(ast_factory_.GetId(expr));
    absl::optional<std::string> prefix = ExtractStructName(*operand);
    if (!prefix) return std::nullopt;
    std::string name =
        absl::StrCat(*prefix, ".", ast_factory_.GetSelectField(expr));
    return name;
  }
  return std::nullopt;
}

template <typename ExprNode>
int32_t PrattParserWorker<ExprNode>::GetLeftmostPosition(const ExprNode& expr) {
  if (ast_factory_.IsIdent(expr)) {
    auto it = positions_.find(ast_factory_.GetId(expr));
    return it != positions_.end() ? it->second : 0;
  }
  if (ast_factory_.IsSelect(expr)) {
    return GetLeftmostPosition(*ast_factory_.GetSelectOperand(expr));
  }
  auto it = positions_.find(ast_factory_.GetId(expr));
  return it != positions_.end() ? it->second : 0;
}

// Recursively constructs a balanced binary AST tree for chained associative
// operators (e.g., chains of `+` or `*`). To prevent deep recursion and
// stack overflow during evaluation of expressions like `a + b + c + d`, this
// method splits the operand terms in half at midpoint `(lo + hi + 1) / 2` and
// combines the left and right subtrees with binary call nodes.
template <typename ExprNode>
ExprNode PrattParserWorker<ExprNode>::BalancedTree(
    absl::string_view op, std::vector<ExprNode>& terms,
    const std::vector<int64_t>& ops, int lo, int hi) {
  int mid = (lo + hi + 1) / 2;
  std::vector<ExprNode> arguments;
  arguments.reserve(2);
  if (mid == lo) {
    arguments.push_back(std::move(terms[mid]));
  } else {
    arguments.push_back(BalancedTree(op, terms, ops, lo, mid - 1));
  }
  if (mid == hi) {
    arguments.push_back(std::move(terms[mid + 1]));
  } else {
    arguments.push_back(BalancedTree(op, terms, ops, mid + 1, hi));
  }
  return ast_factory_.NewCall(ops[mid], std::string(op), std::move(arguments));
}

// Constructs the AST representation for chained logical operators (e.g.,
// `a && b && c` or `a || b || c`). When variadic logical ops are enabled
// (`enable_variadic = true`), it creates a single N-ary function call node
// containing all terms as arguments (e.g., `_&&_(a, b, c)`). Otherwise, it
// delegates to `BalancedTree` to produce a balanced binary tree of logical
// operations `(a && b) && c`.
template <typename ExprNode>
ExprNode PrattParserWorker<ExprNode>::BalanceLogical(
    absl::string_view op, std::vector<ExprNode> terms, std::vector<int64_t> ops,
    bool enable_variadic) {
  if (terms.size() == 1) {
    return std::move(terms[0]);
  }
  if (enable_variadic) {
    return ast_factory_.NewCall(ops[0], std::string(op), std::move(terms));
  }
  return BalancedTree(op, terms, ops, 0, ops.size() - 1);
}

template <typename ExprNode>
std::optional<ExprNode> PrattParserWorker<ExprNode>::TryExpandMacro(
    int64_t expr_id, absl::string_view function, ExprNode* target,
    std::vector<ExprNode>& args) {
  bool is_receiver = target != nullptr;
  auto expander =
      ast_factory_.NewMacroExprExpander(function, args.size(), is_receiver);
  if (!expander) {
    return std::nullopt;
  }
  if (node_limit_exceeded_) {
    ReportError(expr_id,
                "could not expand macro: expression node limit exceeded");
    return std::nullopt;
  }

  std::vector<ExprNode> macro_args;
  ExprNode macro_target;
  bool add_macro_calls = options_.add_macro_calls;
  // We must build the copies of the macro arguments before calling Expand,
  // because Expand is allowed to mutate the arguments in-place upon success.
  // However, we only record the macro call if Expand actually succeeds.
  if (add_macro_calls) {
    auto build_macro_call_arg = [&](const ExprNode& expr) -> ExprNode {
      absl::StatusOr<ExprNode> copy_or = ast_factory_.CopyAndReplace(
          expr,
          [this](const ExprNode& e) -> std::optional<ExprNode> {
            if (auto it = macro_calls_.find(ast_factory_.GetId(e));
                it != macro_calls_.end()) {
              return ast_factory_.NewUnspecified(ast_factory_.GetId(e));
            }
            return std::nullopt;
          },
          options_.max_recursion_depth - recursion_depth_);
      if (!copy_or.ok()) {
        int32_t macro_position = 0;
        if (auto it = positions_.find(expr_id); it != positions_.end()) {
          macro_position = it->second;
        }
        ReportError(macro_position, copy_or.status().message());
        return ast_factory_.NewUnspecified(0);
      }
      return *std::move(copy_or);
    };
    macro_args.reserve(args.size());
    for (const auto& arg : args) {
      macro_args.push_back(build_macro_call_arg(arg));
    }
    if (target != nullptr) {
      macro_target = build_macro_call_arg(*target);
    }
  }

  absl::optional<std::reference_wrapper<ExprNode>> target_ref;
  if (target != nullptr) {
    target_ref = *target;
  }

  int32_t macro_position = 0;
  if (auto it = positions_.find(expr_id); it != positions_.end()) {
    macro_position = it->second;
  }
  MacroExpanderSupport support(*this, macro_position);
  std::optional<ExprNode> expanded_expr =
      expander->Expand(target_ref, absl::MakeSpan(args), support);

  if (expanded_expr) {
    if (add_macro_calls) {
      RecordMacroCall(ast_factory_.GetId(*expanded_expr), function,
                      target != nullptr
                          ? std::make_optional(std::move(macro_target))
                          : std::nullopt,
                      std::move(macro_args));
    }
    EraseId(expr_id);
    return expanded_expr;
  }

  return std::nullopt;
}

template <typename ExprNode>
void PrattParserWorker<ExprNode>::RecordMacroCall(
    int64_t macro_id, absl::string_view function,
    std::optional<ExprNode> target, std::vector<ExprNode> arguments) {
  ExprNode call_expr;
  if (target.has_value()) {
    call_expr = ast_factory_.NewMemberCall(
        0, std::string(function), std::move(*target), std::move(arguments));
  } else {
    call_expr =
        ast_factory_.NewCall(0, std::string(function), std::move(arguments));
  }
  macro_calls_.insert({macro_id, std::move(call_expr)});
}

// Scans ahead in the token stream to detect contiguous grouping
// parentheses (e.g., `((((expr))))`). By determining the number of outermost
// parentheses that enclose the exact same expression and close contiguously,
// the parser unnests them in a single C++ stack frame, avoiding deep recursive
// descent.
template <typename ExprNode>
int PrattParserWorker<ExprNode>::CountGroupingParentheses() {
  if (peek_token_.type != TokenType::kLeftParen) {
    return 0;
  }

  // Save lexer position to restore after scanning ahead.
  const Lexer::Position saved_pos = lexer_.SavePosition();
  auto restore_lexer = absl::MakeCleanup(
      [this, saved_pos] { lexer_.RestorePosition(saved_pos); });

  int leading_open_parens = 1;
  Token tok = this->NextSignificantToken(/*report_error=*/false);
  while (tok.type == TokenType::kLeftParen) {
    leading_open_parens++;
    tok = this->NextSignificantToken(/*report_error=*/false);
  }
  if (leading_open_parens == 1) {
    return 1;
  }

  int open_parens = leading_open_parens;
  int consecutive_leading_closed = 0;

  while (open_parens > 0) {
    if (tok.type == TokenType::kEnd || tok.type == TokenType::kError) {
      // Return 1 to ensure the parser consumes '(' and standard error handling
      // catches incomplete expressions like `(ident`.
      return 1;
    }

    if (tok.type == TokenType::kLeftParen) {
      // An inner parenthesis opens within the expression
      // (e.g. `(x` in `((1 + (x) ))`).
      open_parens++;
      consecutive_leading_closed = 0;
    } else if (tok.type == TokenType::kRightParen) {
      if (leading_open_parens == open_parens) {
        // All inner parentheses are balanced, so this ')' closes one of the
        // initial leading '(' parentheses (e.g. trailing ')' in `(((expr)))`).
        leading_open_parens--;
        consecutive_leading_closed++;
      } else {
        // This ')' closes an inner nested parenthesis (e.g. `(1 + 2)` in
        // `((1 + 2) * 3)`), not one of the outermost leading parentheses.
        consecutive_leading_closed = 0;
      }
      open_parens--;
    } else {
      // Non-parenthesis token (identifier, operator, literal, etc.). Any
      // preceding ')' did not close the entire expression, so reset the
      // contiguous outer closing count.
      consecutive_leading_closed = 0;
    }

    if (open_parens > 0) {
      tok = this->NextSignificantToken(/*report_error=*/false);
    }
  }

  // Return at least 1 to make sure we catch unclosed expressions like `(ident`.
  return std::max(1, consecutive_leading_closed);
}

}  // namespace cel::parser_internal

#endif  // THIRD_PARTY_CEL_CPP_PARSER_INTERNAL_PRATT_PARSER_WORKER_H_
