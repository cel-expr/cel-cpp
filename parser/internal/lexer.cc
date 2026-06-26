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

#include "parser/internal/lexer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "absl/base/attributes.h"
#include "absl/base/optimization.h"
#include "absl/functional/function_ref.h"
#include "absl/log/absl_check.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"

namespace cel_parser_internal {

namespace {

bool IsIdentTrailing(unsigned char c) {
  return absl::ascii_isdigit(c) || absl::ascii_isalpha(c) || c == '_';
}

bool IsBinaryDigit(unsigned char c) { return c == '0' || c == '1'; }

bool IsPlusOrMinus(unsigned char c) { return c == '+' || c == '-'; }

std::string_view TokenNumericLiteralName(TokenType type) {
  switch (type) {
    case TokenType::kInt:
      return "int";
    case TokenType::kUint:
      return "uint";
    case TokenType::kFloat:
      return "float";
    default:
      return "<unreachable>";
  }
}

}  // namespace

std::string_view TokenTypeToString(TokenType type) {
  switch (type) {
    case TokenType::kError:
      return "error";
    case TokenType::kEnd:
      return "end";
    case TokenType::kWhitespace:
      return "whitespace";
    case TokenType::kComment:
      return "comment";
    case TokenType::kNull:
      return "null";
    case TokenType::kFalse:
      return "false";
    case TokenType::kTrue:
      return "true";
    case TokenType::kIn:
      return "in";
    case TokenType::kInt:
      return "int";
    case TokenType::kUint:
      return "uint";
    case TokenType::kFloat:
      return "float";
    case TokenType::kString:
      return "string";
    case TokenType::kBytes:
      return "bytes";
    case TokenType::kIdent:
      return "ident";
    case TokenType::kLeftBracket:
      return "[";
    case TokenType::kRightBracket:
      return "]";
    case TokenType::kLeftBrace:
      return "{";
    case TokenType::kRightBrace:
      return "}";
    case TokenType::kLeftParen:
      return "(";
    case TokenType::kRightParen:
      return ")";
    case TokenType::kDot:
      return ".";
    case TokenType::kComma:
      return ",";
    case TokenType::kMinus:
      return "-";
    case TokenType::kPlus:
      return "+";
    case TokenType::kAsterisk:
      return "*";
    case TokenType::kSlash:
      return "/";
    case TokenType::kPercent:
      return "%";
    case TokenType::kQuestion:
      return "?";
    case TokenType::kColon:
      return ":";
    case TokenType::kExclamation:
      return "!";
    case TokenType::kEqual:
      return "=";
    case TokenType::kEqualEqual:
      return "==";
    case TokenType::kExclamationEqual:
      return "!=";
    case TokenType::kLess:
      return "<";
    case TokenType::kLessEqual:
      return "<=";
    case TokenType::kGreater:
      return ">";
    case TokenType::kGreaterEqual:
      return ">=";
    case TokenType::kLogicalAnd:
      return "&&";
    case TokenType::kLogicalOr:
      return "||";
    default:
      return "<unknown>";
  }
}

Token Lexer::Lex() {
  if (ABSL_PREDICT_FALSE(at_error_)) {
    return MakeToken(TokenType::kError, error_.start, error_.end);
  }
  int32_t start = GetPosition();
  if (ABSL_PREDICT_FALSE(text_ == text_end_)) {
    at_end_ = true;
    done_ = true;
    return MakeToken(TokenType::kEnd, start, start);
  }
  char c = *text_;
  switch (c) {
    case '\v':
      ABSL_FALLTHROUGH_INTENDED;
    case '\t':
      ABSL_FALLTHROUGH_INTENDED;
    case '\r':
      ABSL_FALLTHROUGH_INTENDED;
    case '\n':
      ABSL_FALLTHROUGH_INTENDED;
    case ' ': {
      static_cast<void>(ConsumeWhitespace());
      return MakeToken(TokenType::kWhitespace, start, GetPosition());
    }
    case '.': {
      if (text_ + 1 < text_end_ && absl::ascii_isdigit(text_[1])) {
        break;
      }
      Advance(1);
      return MakeToken(TokenType::kDot, start, GetPosition());
    }
    case ',': {
      Advance(1);
      return MakeToken(TokenType::kComma, start, GetPosition());
    }
    case '!': {
      Advance(1);
      if (Consume('=')) {
        return MakeToken(TokenType::kExclamationEqual, start, GetPosition());
      }
      return MakeToken(TokenType::kExclamation, start, GetPosition());
    }
    case '?': {
      Advance(1);
      return MakeToken(TokenType::kQuestion, start, GetPosition());
    }
    case '(': {
      Advance(1);
      return MakeToken(TokenType::kLeftParen, start, GetPosition());
    }
    case ')': {
      Advance(1);
      return MakeToken(TokenType::kRightParen, start, GetPosition());
    }
    case '{': {
      Advance(1);
      return MakeToken(TokenType::kLeftBrace, start, GetPosition());
    }
    case '}': {
      Advance(1);
      return MakeToken(TokenType::kRightBrace, start, GetPosition());
    }
    case '[': {
      Advance(1);
      return MakeToken(TokenType::kLeftBracket, start, GetPosition());
    }
    case ']': {
      Advance(1);
      return MakeToken(TokenType::kRightBracket, start, GetPosition());
    }
    case '=': {
      Advance(1);
      if (Consume('=')) {
        return MakeToken(TokenType::kEqualEqual, start, GetPosition());
      }
      return MakeToken(TokenType::kEqual, start, GetPosition());
    }
    case '<': {
      Advance(1);
      if (Consume('=')) {
        return MakeToken(TokenType::kLessEqual, start, GetPosition());
      }
      return MakeToken(TokenType::kLess, start, GetPosition());
    }
    case '>': {
      Advance(1);
      if (Consume('=')) {
        return MakeToken(TokenType::kGreaterEqual, start, GetPosition());
      }
      return MakeToken(TokenType::kGreater, start, GetPosition());
    }
    case ':': {
      Advance(1);
      return MakeToken(TokenType::kColon, start, GetPosition());
    }
    case '%': {
      Advance(1);
      return MakeToken(TokenType::kPercent, start, GetPosition());
    }
    case '+': {
      Advance(1);
      return MakeToken(TokenType::kPlus, start, GetPosition());
    }
    case '-': {
      Advance(1);
      return MakeToken(TokenType::kMinus, start, GetPosition());
    }
    case '*': {
      Advance(1);
      return MakeToken(TokenType::kAsterisk, start, GetPosition());
    }
    case '/': {
      Advance(1);
      if (Consume('/')) {
        ConsumeLine();
        return MakeToken(TokenType::kComment, start, GetPosition());
      }
      return MakeToken(TokenType::kSlash, start, GetPosition());
    }
    case '&': {
      Advance(1);
      if (Consume('&')) {
        return MakeToken(TokenType::kLogicalAnd, start, GetPosition());
      }
      return SetError(start, GetPosition(),
                      "unexpected single '&', expected '&&'");
    }
    case '|': {
      Advance(1);
      if (Consume('|')) {
        return MakeToken(TokenType::kLogicalOr, start, GetPosition());
      }
      return SetError(start, GetPosition(),
                      "unexpected single '|', expected '||'");
    }
    case '`': {
      Advance(1);
      if (!ConsumeUntilAfter('`')) {
        return SetError(start, GetPosition(), "unterminated quoted identifier");
      }
      return MakeToken(TokenType::kIdent, start, GetPosition());
    }
    case '\'': {
      Advance(1);
      if (ConsumeString("''")) {
        if (!ConsumeUntilAfterString("'''")) {
          return SetError(start, GetPosition(), "unterminated string literal");
        }
        return MakeToken(TokenType::kString, start, GetPosition());
      }
      if (!ConsumeUntilAfterUnescaped('\'')) {
        return SetError(start, GetPosition(), "unterminated string literal");
      }
      return MakeToken(TokenType::kString, start, GetPosition());
    }
    case '"': {
      Advance(1);
      if (ConsumeString("\"\"")) {
        if (!ConsumeUntilAfterString("\"\"\"")) {
          return SetError(start, GetPosition(), "unterminated string literal");
        }
        return MakeToken(TokenType::kString, start, GetPosition());
      }
      if (!ConsumeUntilAfterUnescaped('"')) {
        return SetError(start, GetPosition(), "unterminated string literal");
      }
      return MakeToken(TokenType::kString, start, GetPosition());
    }
    default:
      break;
  }
  if (c == 'r' || c == 'R' || c == 'b' || c == 'B') {
    bool is_bytes = (c == 'b' || c == 'B');
    size_t lookahead = 1;
    if (text_ + 1 < text_end_) {
      char c2 = text_[1];
      if ((is_bytes && (c2 == 'r' || c2 == 'R')) ||
          (!is_bytes && (c2 == 'b' || c2 == 'B'))) {
        is_bytes = true;
        lookahead = 2;
      }
    }
    if (text_ + lookahead < text_end_) {
      char quote = text_[lookahead];
      if (quote == '"' || quote == '\'') {
        Advance(lookahead + 1);
        std::string tripe_quote(3, quote);
        if (ConsumeString(std::string_view(tripe_quote.data(), 2))) {
          if (!ConsumeUntilAfterString(tripe_quote)) {
            return SetError(start, GetPosition(),
                            is_bytes ? "unterminated bytes literal"
                                     : "unterminated string literal");
          }
          return MakeToken(is_bytes ? TokenType::kBytes : TokenType::kString,
                           start, GetPosition());
        }
        if (!ConsumeUntilAfterUnescaped(quote)) {
          return SetError(start, GetPosition(),
                          is_bytes ? "unterminated bytes literal"
                                   : "unterminated string literal");
        }
        return MakeToken(is_bytes ? TokenType::kBytes : TokenType::kString,
                         start, GetPosition());
      }
    }
  }
  if (c == '.' || absl::ascii_isdigit(c)) {
    bool floating_point = false;
    if (c == '.') {
      floating_point = true;
      Advance(1);
      if (!ConsumeDigits()) {
        return SetError(
            start, GetPosition(),
            "floating point literal missing digits after decimal separator");
      }
    } else {
      Advance(1);
      if (c == '0') {
        if (ConsumeIgnoreCase('x')) {
          if (!ConsumeHexDigits()) {
            return SetError(
                start, GetPosition(),
                "integral literal missing digits after hexadecimal separator");
          }
          auto token_type = ConsumeIntegralSuffix();
          if (ConsumeIf(IsIdentTrailing)) {
            return SetError(
                start, GetPosition(),
                absl::StrCat(TokenNumericLiteralName(token_type),
                             " literal has unexpected trailing characters"));
          }
          return MakeToken(token_type, start, GetPosition());
        }
        if (ConsumeIgnoreCase('b')) {
          if (!ConsumeBinaryDigits()) {
            return SetError(
                start, GetPosition(),
                "integral literal missing digits after binary separator");
          }
          auto token_type = ConsumeIntegralSuffix();
          if (ConsumeIf(IsIdentTrailing)) {
            return SetError(
                start, GetPosition(),
                absl::StrCat(TokenNumericLiteralName(token_type),
                             " literal has unexpected trailing characters"));
          }
          return MakeToken(token_type, start, GetPosition());
        }
      }
      static_cast<void>(ConsumeDigits());
      if (text_ < text_end_ && *text_ == '.' && text_ + 1 < text_end_ &&
          absl::ascii_isdigit(text_[1])) {
        floating_point = true;
        Advance(1);
        ConsumeDigits();
      }
    }
    if (ConsumeIgnoreCase('e')) {
      floating_point = true;
      static_cast<void>(ConsumeIf(IsPlusOrMinus));
      if (!ConsumeDigits()) {
        return SetError(
            start, GetPosition(),
            "floating point literal missing digits after exponent separator");
      }
    }
    auto token_type =
        floating_point ? TokenType::kFloat : ConsumeIntegralSuffix();
    if (ConsumeIf(IsIdentTrailing)) {
      return SetError(
          start, GetPosition(),
          absl::StrCat(TokenNumericLiteralName(token_type),
                       " literal has unexpected trailing characters"));
    }
    return MakeToken(token_type, start, GetPosition());
  }
  if (c == '_' || absl::ascii_isalpha(c)) {
    const char* text = text_;
    Advance(1);
    ConsumeIdentTrailing();
    // Since we have so few keywords, it's faster to use a switch statement
    // on the first character rather than doing a map lookup.
    int32_t end = GetPosition();
    std::string_view word(text, static_cast<size_t>(end - start));
    switch (c) {
      case 'f':
        if (word == "false") {
          return MakeToken(TokenType::kFalse, start, end);
        }
        break;
      case 'i':
        if (word == "in") {
          return MakeToken(TokenType::kIn, start, end);
        }
        break;
      case 'n':
        if (word == "null") {
          return MakeToken(TokenType::kNull, start, end);
        }
        break;
      case 't':
        if (word == "true") {
          return MakeToken(TokenType::kTrue, start, end);
        }
        break;
      default:
        break;
    }
    return MakeToken(TokenType::kIdent, start, end);
  }
  Advance(1);
  return SetError(start, GetPosition(), "unexpected character");
}

bool Lexer::ConsumeUntilAfter(char c) {
  ABSL_DCHECK_NE(c, '\n');
  auto pos = GetRemainingText().find(c);
  if (pos == std::string_view::npos) {
    AdvanceProcessingNewLines(text_end_);
    return false;
  }
  AdvanceProcessingNewLines(pos + 1);
  return true;
}

bool Lexer::ConsumeUntilAfterString(std::string_view s) {
  ABSL_DCHECK(!absl::StrContains(s, '\n'));
  auto pos = GetRemainingText().find(s);
  if (pos == std::string_view::npos) {
    AdvanceProcessingNewLines(text_end_);
    return false;
  }
  AdvanceProcessingNewLines(pos + s.size());
  return true;
}

bool Lexer::ConsumeUntilAfterUnescaped(char c) {
  ABSL_DCHECK_NE(c, '\n');
  ABSL_DCHECK_NE(c, '\\');
  const char* text = text_;
  bool escaped = false;
  while (text != text_end_) {
    std::string_view chunk =
        std::string_view(text, static_cast<size_t>(text_end_ - text));
    for (size_t i = 0; i < chunk.size(); ++i) {
      char cc = chunk[i];
      if (cc == '\\') {
        escaped = !escaped;
      } else {
        if (cc == c && !escaped) {
          AdvanceProcessingNewLines(static_cast<size_t>(text - text_) + i + 1);
          return true;
        }
        escaped = false;
      }
    }
    text += chunk.size();
  }
  AdvanceProcessingNewLines(text_end_);
  return false;
}

void Lexer::ConsumeIdentTrailing() {
  while (text_ != text_end_) {
    std::string_view chunk = GetRemainingText();
    for (size_t i = 0; i < chunk.size(); ++i) {
      char c = chunk[i];
      if (!IsIdentTrailing(c)) {
        if (i > 0) {
          Advance(i);
        }
        return;
      }
    }
    Advance(chunk.size());
  }
}

bool Lexer::MatchString(std::string_view s) const {
  return absl::StartsWith(GetRemainingText(), s);
}

bool Lexer::MatchStringIgnoreCase(std::string_view s) const {
  return absl::StartsWithIgnoreCase(GetRemainingText(), s);
}

std::optional<char> Lexer::MatchIf(
    absl::FunctionRef<bool(unsigned char)> predicate) const {
  if (text_ != text_end_) {
    char c = *text_;
    if (predicate(c)) {
      return c;
    }
  }
  return std::nullopt;
}

void Lexer::ConsumeLine() {
  while (text_ != text_end_) {
    std::string_view chunk = GetRemainingText();
    auto pos = chunk.find('\n');
    if (pos != std::string_view::npos) {
      Advance(pos + 1);
      if (line_offsets_ != nullptr) {
        line_offsets_->push_back(GetPosition());
      }
      break;
    }
    Advance(chunk.size());
  }
}

bool Lexer::ConsumeWhitespace() {
  bool advanced = false;
  while (text_ != text_end_) {
    std::string_view chunk = GetRemainingText();
    size_t i = 0;
  next_char:
    while (i < chunk.size()) {
      char c = chunk[i];
      switch (c) {
        case '\n':
          if (line_offsets_ != nullptr) {
            line_offsets_->push_back(GetPosition() + static_cast<int32_t>(i) +
                                     1);
          }
          ABSL_FALLTHROUGH_INTENDED;
        case ' ':
          ABSL_FALLTHROUGH_INTENDED;
        case '\r':
          ABSL_FALLTHROUGH_INTENDED;
        case '\v':
          ABSL_FALLTHROUGH_INTENDED;
        case '\t':
          ++i;
          goto next_char;
        default:
          if (i != 0) {
            Advance(i);
            return true;
          }
          return advanced;
      }
    }
    Advance(chunk.size());
    advanced = true;
  }
  return advanced;
}

bool Lexer::Consume(char c) {
  ABSL_DCHECK_NE(c, '\n');
  if (Match(c)) {
    Advance(1);
    return true;
  }
  return false;
}

bool Lexer::ConsumeIgnoreCase(char c) {
  ABSL_DCHECK_NE(c, '\n');
  if (MatchIgnoreCase(c)) {
    Advance(1);
    return true;
  }
  return false;
}

bool Lexer::ConsumeString(std::string_view s) {
  ABSL_DCHECK(!absl::StrContains(s, '\n'));
  if (MatchString(s)) {
    Advance(s.size());
    return true;
  }
  return false;
}

bool Lexer::ConsumeStringIgnoreCase(std::string_view s) {
  ABSL_DCHECK(!absl::StrContains(s, '\n'));
  if (MatchStringIgnoreCase(s)) {
    Advance(s.size());
    return true;
  }
  return false;
}

std::optional<char> Lexer::ConsumeIf(
    absl::FunctionRef<bool(unsigned char)> predicate) {
  std::optional<char> match = MatchIf(predicate);
  if (match.has_value()) {
    ABSL_DCHECK_NE(*match, '\n');
    Advance(1);
  }
  return match;
}

bool Lexer::ConsumeDigits() {
  bool advanced = false;
  while (text_ != text_end_) {
    std::string_view chunk = GetRemainingText();
    for (size_t i = 0; i < chunk.size(); ++i) {
      if (!absl::ascii_isdigit(chunk[i])) {
        if (i != 0) {
          Advance(i);
          return true;
        }
        return advanced;
      }
    }
    Advance(chunk.size());
    advanced = true;
  }
  return advanced;
}

bool Lexer::ConsumeHexDigits() {
  bool advanced = false;
  while (text_ != text_end_) {
    std::string_view chunk = GetRemainingText();
    for (size_t i = 0; i < chunk.size(); ++i) {
      if (!absl::ascii_isxdigit(chunk[i])) {
        if (i != 0) {
          Advance(i);
          return true;
        }
        return advanced;
      }
    }
    Advance(chunk.size());
    advanced = true;
  }
  return advanced;
}

bool Lexer::ConsumeBinaryDigits() {
  bool advanced = false;
  while (text_ != text_end_) {
    std::string_view chunk = GetRemainingText();
    for (size_t i = 0; i < chunk.size(); ++i) {
      if (!IsBinaryDigit(chunk[i])) {
        if (i != 0) {
          Advance(i);
          return true;
        }
        return advanced;
      }
    }
    Advance(chunk.size());
    advanced = true;
  }
  return advanced;
}

TokenType Lexer::ConsumeIntegralSuffix() {
  if (ConsumeIgnoreCase('u')) {
    return TokenType::kUint;
  }
  return TokenType::kInt;
}

void Lexer::AdvanceProcessingNewLines(size_t n) {
  while (n > 0) {
    std::string_view chunk = GetRemainingText();
    chunk = chunk.substr(0, std::min(chunk.size(), n));
    std::string_view::size_type pos = 0;
    while (pos < chunk.size()) {
      std::string_view::size_type npos = chunk.find('\n', pos);
      if (npos == std::string_view::npos) {
        break;
      }
      ++npos;
      if (line_offsets_ != nullptr) {
        line_offsets_->push_back(GetPosition() + static_cast<int32_t>(npos));
      }
      pos = npos;
    }
    n -= chunk.size();
    Advance(chunk.size());
  }
}

void Lexer::AdvanceProcessingNewLines(const char* end) {
  ABSL_DCHECK_LE(end, text_end_);
  ABSL_DCHECK_GE(end, text_);
  AdvanceProcessingNewLines(static_cast<size_t>(end - text_));
}

}  // namespace cel_parser_internal
