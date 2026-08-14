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

#include "parser/internal/pratt_parser_worker.h"

#include <cstdint>
#include <string>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "common/operators.h"
#include "common/source.h"
#include "parser/internal/lexer.h"
#include "parser/options.h"
#include "parser/parser_interface.h"

namespace cel::parser_internal {
namespace {

using CelOperator = ::google::api::expr::common::CelOperator;

const BinaryOpInfo kLogicalOr = {1, CelOperator::LOGICAL_OR, true,
                                 TokenType::kLogicalOr};
const BinaryOpInfo kLogicalAnd = {2, CelOperator::LOGICAL_AND, true,
                                  TokenType::kLogicalAnd};
const BinaryOpInfo kLess = {3, CelOperator::LESS, false, TokenType::kLess};
const BinaryOpInfo kLessEqual = {3, CelOperator::LESS_EQUALS, false,
                                 TokenType::kLessEqual};
const BinaryOpInfo kGreater = {3, CelOperator::GREATER, false,
                               TokenType::kGreater};
const BinaryOpInfo kGreaterEqual = {3, CelOperator::GREATER_EQUALS, false,
                                    TokenType::kGreaterEqual};
const BinaryOpInfo kEqualEqual = {3, CelOperator::EQUALS, false,
                                  TokenType::kEqualEqual};
const BinaryOpInfo kExclamationEqual = {3, CelOperator::NOT_EQUALS, false,
                                        TokenType::kExclamationEqual};
#pragma push_macro("IN")
#undef IN
const BinaryOpInfo kIn = {3, CelOperator::IN, false, TokenType::kIn};
#pragma pop_macro("IN")
const BinaryOpInfo kPlus = {4, CelOperator::ADD, false, TokenType::kPlus};
const BinaryOpInfo kMinus = {4, CelOperator::SUBTRACT, false,
                             TokenType::kMinus};
const BinaryOpInfo kAsterisk = {5, CelOperator::MULTIPLY, false,
                                TokenType::kAsterisk};
const BinaryOpInfo kSlash = {5, CelOperator::DIVIDE, false, TokenType::kSlash};
const BinaryOpInfo kPercent = {5, CelOperator::MODULO, false,
                               TokenType::kPercent};
const BinaryOpInfo kDefaultOpInfo = {0, "", false, TokenType::kError};

}  // namespace

const BinaryOpInfo& GetBinaryOpInfo(TokenType type) {
  switch (type) {
    case TokenType::kLogicalOr:
      return kLogicalOr;
    case TokenType::kLogicalAnd:
      return kLogicalAnd;
    case TokenType::kLess:
      return kLess;
    case TokenType::kLessEqual:
      return kLessEqual;
    case TokenType::kGreater:
      return kGreater;
    case TokenType::kGreaterEqual:
      return kGreaterEqual;
    case TokenType::kEqualEqual:
      return kEqualEqual;
    case TokenType::kExclamationEqual:
      return kExclamationEqual;
    case TokenType::kIn:
      return kIn;
    case TokenType::kPlus:
      return kPlus;
    case TokenType::kMinus:
      return kMinus;
    case TokenType::kAsterisk:
      return kAsterisk;
    case TokenType::kSlash:
      return kSlash;
    case TokenType::kPercent:
      return kPercent;
    default:
      return kDefaultOpInfo;
  }
}

ParserWorker::ParserWorker(
    const cel::Source& source, const cel::ParserOptions& options,
    std::vector<cel::ParseIssue>* absl_nullable parse_issues,
    bool track_node_ranges)
    : source_(source),
      options_(options),
      lexer_(source_),
      parse_issues_(parse_issues),
      track_node_ranges_(track_node_ranges) {}

void ParserWorker::InitTokenStream() {
  current_token_ = Token{.type = TokenType::kError, .start = 0, .end = 0};
  peek_token_ = NextSignificantToken();
}

std::string ParserWorker::GetTokenText(const Token& tok) const {
  if (tok.start >= 0 && tok.end >= tok.start &&
      tok.end <= static_cast<int32_t>(source_.content().size())) {
    return source_.content().ToString(tok.start, tok.end);
  }
  return "";
}

Token ParserWorker::NextSignificantToken(bool report_error) {
  if (is_recovery_limit_exceeded()) {
    return Token{.type = TokenType::kEnd, .start = 0, .end = 0};
  }
  while (true) {
    Token tok = lexer_.Lex();
    if (tok.type == TokenType::kWhitespace || tok.type == TokenType::kComment) {
      continue;
    }
    if (tok.type == TokenType::kError && report_error) {
      ReportError(tok, lexer_.GetError().message);
      if (is_recovery_limit_exceeded()) {
        return Token{.type = TokenType::kEnd, .start = 0, .end = 0};
      }
    }
    return tok;
  }
}

Token ParserWorker::NextToken() {
  current_token_ = peek_token_;
  if (is_recovery_limit_exceeded()) {
    peek_token_ = Token{.type = TokenType::kEnd, .start = 0, .end = 0};
    return current_token_;
  }
  if (peek_token_.type != TokenType::kEnd) {
    peek_token_ = NextSignificantToken();
  }
  return current_token_;
}

bool ParserWorker::Expect(TokenType type, absl::string_view msg) {
  if (peek_token_.type == type) {
    NextToken();
    return true;
  }
  if (is_recovery_limit_exceeded()) {
    return false;
  }
  if (peek_token_.type != TokenType::kError) {
    std::string err_msg;
    if (msg.empty()) {
      std::string tok_text = GetTokenText(peek_token_);
      std::string formatted_tok;
      if (peek_token_.type == TokenType::kEnd) {
        formatted_tok = "<EOF>";
      } else {
        formatted_tok = absl::StrCat("'", tok_text, "'");
      }
      err_msg = absl::StrCat("mismatched input ", formatted_tok, " expecting '",
                             TokenTypeToString(type), "'");
    } else {
      err_msg = std::string(msg);
    }
    ReportError(peek_token_, err_msg);
  }
  SynchronizeOnDelimiter();
  return false;
}

void ParserWorker::SynchronizeOnDelimiter() {
  if (is_recovery_limit_exceeded()) {
    peek_token_ = Token{.type = TokenType::kEnd, .start = 0, .end = 0};
    return;
  }
  while (peek_token_.type != TokenType::kEnd) {
    if (peek_token_.type == TokenType::kComma ||
        peek_token_.type == TokenType::kRightParen ||
        peek_token_.type == TokenType::kRightBracket ||
        peek_token_.type == TokenType::kRightBrace) {
      break;
    }
    NextToken();
  }
}

int64_t ParserWorker::NextId(int32_t position) {
  int64_t id = next_id_++;
  if (id > options_.expression_node_limit && !node_limit_exceeded_) {
    ReportError(position, "expression node limit exceeded");
    node_limit_exceeded_ = true;
  }
  if (position >= 0) {
    positions_.insert({id, position});
    if (ABSL_PREDICT_FALSE(track_node_ranges_)) {
      node_ranges_.insert({id, {position, position}});
    }
  }
  return id;
}

int64_t ParserWorker::NextId() { return NextId(-1); }

int64_t ParserWorker::CopyId(int64_t id) {
  if (id == 0) {
    return 0;
  }
  int32_t pos = 0;
  if (auto it = positions_.find(id); it != positions_.end()) {
    pos = it->second;
  }
  int64_t new_id = NextId(pos);
  if (ABSL_PREDICT_FALSE(track_node_ranges_)) {
    if (auto it = node_ranges_.find(id); it != node_ranges_.end()) {
      node_ranges_[new_id] = it->second;
    }
  }
  return new_id;
}

void ParserWorker::EraseId(int64_t id) {
  positions_.erase(id);
  if (ABSL_PREDICT_FALSE(track_node_ranges_)) {
    node_ranges_.erase(id);
  }
  if (next_id_ == id + 1) {
    --next_id_;
  }
}

void ParserWorker::ReportError(int32_t position, absl::string_view msg) {
  cel::SourceLocation loc;
  if (auto found = source_.GetLocation(position); found.has_value()) {
    loc = *found;
  }
  ReportError(loc, msg);
}

void ParserWorker::ReportError(const SourceLocation& loc,
                               absl::string_view msg) {
  if (error_count_ > options_.error_recovery_limit) {
    return;
  }
  error_count_++;
  if (error_count_ == options_.error_recovery_limit + 1) {
    if (parse_issues_ != nullptr) {
      parse_issues_->push_back(
          cel::ParseIssue(absl::StrFormat("Error recovery limit (%d) exceeded",
                                          options_.error_recovery_limit)));
    }
    peek_token_ = Token{.type = TokenType::kEnd, .start = 0, .end = 0};
  }
  if (parse_issues_ != nullptr &&
      error_count_ <= options_.error_recovery_limit) {
    parse_issues_->push_back(cel::ParseIssue(loc, std::string(msg)));
  }
}

}  // namespace cel::parser_internal
