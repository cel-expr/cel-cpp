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

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "absl/base/attributes.h"
#include "absl/base/no_destructor.h"
#include "absl/base/optimization.h"
#include "absl/container/flat_hash_map.h"
#include "absl/functional/function_ref.h"
#include "absl/log/absl_check.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace cel::parser_internal {

namespace {

[[nodiscard]] bool IsIdentTrailing(char32_t c) {
  return c <= 0x7f && (absl::ascii_isdigit(static_cast<char>(c)) ||
                       absl::ascii_isalpha(static_cast<char>(c)) || c == '_');
}

[[nodiscard]] bool IsPlusOrMinus(char32_t c) { return c == '+' || c == '-'; }

[[nodiscard]] const absl::flat_hash_map<std::string_view, TokenType>&
Keywords() {
  static const absl::NoDestructor<
      absl::flat_hash_map<std::string_view, TokenType>>
      kKeywords({
          {"false", TokenType::kFalse},
          {"true", TokenType::kTrue},
          {"null", TokenType::kNull},
          {"in", TokenType::kIn},
          {"as", TokenType::kReservedWord},
          {"break", TokenType::kReservedWord},
          {"const", TokenType::kReservedWord},
          {"continue", TokenType::kReservedWord},
          {"else", TokenType::kReservedWord},
          {"for", TokenType::kReservedWord},
          {"function", TokenType::kReservedWord},
          {"if", TokenType::kReservedWord},
          {"import", TokenType::kReservedWord},
          {"let", TokenType::kReservedWord},
          {"loop", TokenType::kReservedWord},
          {"package", TokenType::kReservedWord},
          {"namespace", TokenType::kReservedWord},
          {"return", TokenType::kReservedWord},
          {"var", TokenType::kReservedWord},
          {"void", TokenType::kReservedWord},
          {"while", TokenType::kReservedWord},
      });
  return *kKeywords;
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
    case TokenType::kReservedWord:
      return "reserved_word";
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
  int32_t start = GetPosition();
  if (ABSL_PREDICT_FALSE(position_ >= content_.size())) {
    return MakeToken(TokenType::kEnd, start, start);
  }
  char32_t c = content_.at(position_);
  switch (c) {
    case '\f':
      ABSL_FALLTHROUGH_INTENDED;
    case '\v':
      ABSL_FALLTHROUGH_INTENDED;
    case '\t':
      ABSL_FALLTHROUGH_INTENDED;
    case '\r':
      ABSL_FALLTHROUGH_INTENDED;
    case '\n':
      ABSL_FALLTHROUGH_INTENDED;
    case ' ': {
      ConsumeWhitespace();
      return MakeToken(TokenType::kWhitespace, start, GetPosition());
    }
    case '.': {
      if (position_ + 1 < content_.size() &&
          content_.at(position_ + 1) <= 0x7f &&
          absl::ascii_isdigit(static_cast<char>(content_.at(position_ + 1)))) {
        return ConsumeNumericLiteral();
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
    case '_': {
      return ConsumeIdent();
    }
    case '`': {
      return ConsumeQuotedIdent();
    }
    case '\'': {
      return ConsumeStringLiteral(start, '\'');
    }
    case '"': {
      return ConsumeStringLiteral(start, '"');
    }
    case 'r':
      ABSL_FALLTHROUGH_INTENDED;
    case 'R':
      ABSL_FALLTHROUGH_INTENDED;
    case 'b':
      ABSL_FALLTHROUGH_INTENDED;
    case 'B': {
      if (auto token = ConsumePrefixedStringLiteral(); token.has_value()) {
        return *token;
      }
      break;
    }
    default:
      break;
  }
  if (c <= 0x7f && absl::ascii_isdigit(static_cast<char>(c))) {
    return ConsumeNumericLiteral();
  }
  if (c <= 0x7f && absl::ascii_isalpha(static_cast<char>(c))) {
    // Root identifiers (the ones starting with a period) are returned as
    // a sequence of kDot and kIdent tokens.
    return ConsumeIdent();
  }
  Advance(1);
  return SetError(start, GetPosition(), "unexpected character");
}

// Consumes characters up to and including the first occurrence of character `c`
// without interpreting backslashes as escapes.
// Returns true if `c` was found and consumed; false if end of input was
// reached.
bool Lexer::ConsumeUntilAfter(char32_t c) {
  ABSL_DCHECK_NE(c, '\n');
  for (int32_t pos = position_; pos < content_.size(); ++pos) {
    if (content_.at(pos) == c) {
      AdvanceProcessingNewLines(pos + 1);
      return true;
    }
  }
  AdvanceProcessingNewLines(content_.size());
  return false;
}

// Consumes characters up to and including the first occurrence of substring `s`
// without interpreting backslashes as escapes (`s` must not contain newlines).
// Returns true if `s` was found and consumed; false if end of input was
// reached.
bool Lexer::ConsumeUntilAfterString(std::u32string_view s) {
  ABSL_DCHECK(s.find(U'\n') == std::u32string_view::npos);
  int32_t pos = position_;
  while (pos + static_cast<int32_t>(s.size()) <= content_.size()) {
    bool match = true;
    for (size_t i = 0; i < s.size(); ++i) {
      if (content_.at(pos + static_cast<int32_t>(i)) != s[i]) {
        match = false;
        break;
      }
    }
    if (match) {
      AdvanceProcessingNewLines(pos + static_cast<int32_t>(s.size()));
      return true;
    }
    ++pos;
  }
  AdvanceProcessingNewLines(content_.size());
  return false;
}

// Consumes characters up to and including the first occurrence of `c` that is
// not preceded by an odd number of backslash ('\') escape characters. Returns
// true if an unescaped `c` was found and consumed; false if reached EOF.
bool Lexer::ConsumeUntilAfterUnescaped(char32_t c) {
  ABSL_DCHECK_NE(c, '\n');
  ABSL_DCHECK_NE(c, '\\');
  int32_t pos = position_;
  bool escaped = false;
  while (pos < content_.size()) {
    char32_t cc = content_.at(pos);
    if (cc == '\\') {
      escaped = !escaped;
    } else {
      if (cc == c && !escaped) {
        AdvanceProcessingNewLines(pos + 1);
        return true;
      }
      escaped = false;
    }
    ++pos;
  }
  AdvanceProcessingNewLines(content_.size());
  return false;
}

// Consumes characters up to and including the first occurrence of substring `s`
// where the first character of `s` is not preceded by an odd number of
// backslashes. Returns true if an unescaped `s` was found and consumed; false
// if reached EOF.
bool Lexer::ConsumeUntilAfterUnescapedString(std::u32string_view s) {
  ABSL_DCHECK(s.find(U'\n') == std::u32string_view::npos);
  int32_t pos = position_;
  bool escaped = false;
  while (pos < content_.size()) {
    char32_t cc = content_.at(pos);
    if (cc == '\\') {
      escaped = !escaped;
    } else {
      if (!escaped && pos + static_cast<int32_t>(s.size()) <= content_.size()) {
        bool match = true;
        for (size_t j = 0; j < s.size(); ++j) {
          if (content_.at(pos + static_cast<int32_t>(j)) != s[j]) {
            match = false;
            break;
          }
        }
        if (match) {
          AdvanceProcessingNewLines(pos + static_cast<int32_t>(s.size()));
          return true;
        }
      }
      escaped = false;
    }
    ++pos;
  }
  AdvanceProcessingNewLines(content_.size());
  return false;
}

bool Lexer::MatchString(std::u32string_view s) const {
  if (position_ + static_cast<int32_t>(s.size()) > content_.size()) {
    return false;
  }
  for (size_t i = 0; i < s.size(); ++i) {
    if (content_.at(position_ + static_cast<int32_t>(i)) != s[i]) {
      return false;
    }
  }
  return true;
}

std::optional<char32_t> Lexer::MatchIf(
    absl::FunctionRef<bool(char32_t)> predicate) const {
  if (position_ < content_.size()) {
    char32_t cp = content_.at(position_);
    if (predicate(cp)) {
      return cp;
    }
  }
  return std::nullopt;
}

void Lexer::ConsumeLine() {
  while (position_ < content_.size()) {
    if (content_.at(position_) == '\n') {
      Advance(1);
      return;
    }
    Advance(1);
  }
}

void Lexer::ConsumeWhitespace() {
  while (position_ < content_.size()) {
    char32_t c = content_.at(position_);
    switch (c) {
      case '\f':
        ABSL_FALLTHROUGH_INTENDED;
      case '\n':
        ABSL_FALLTHROUGH_INTENDED;
      case ' ':
        ABSL_FALLTHROUGH_INTENDED;
      case '\r':
        ABSL_FALLTHROUGH_INTENDED;
      case '\v':
        ABSL_FALLTHROUGH_INTENDED;
      case '\t':
        Advance(1);
        break;
      default:
        return;
    }
  }
}

bool Lexer::Consume(char32_t c) {
  ABSL_DCHECK_NE(c, '\n');
  if (Match(c)) {
    Advance(1);
    return true;
  }
  return false;
}

bool Lexer::ConsumeIgnoreCase(char32_t c) {
  ABSL_DCHECK_NE(c, '\n');
  if (MatchIgnoreCase(c)) {
    Advance(1);
    return true;
  }
  return false;
}

bool Lexer::ConsumeString(std::u32string_view s) {
  ABSL_DCHECK(s.find(U'\n') == std::u32string_view::npos);
  if (MatchString(s)) {
    Advance(s.size());
    return true;
  }
  return false;
}

std::optional<char32_t> Lexer::ConsumeIf(
    absl::FunctionRef<bool(char32_t)> predicate) {
  std::optional<char32_t> match = MatchIf(predicate);
  if (match.has_value()) {
    ABSL_DCHECK_NE(*match, '\n');
    Advance(1);
  }
  return match;
}

bool Lexer::ConsumeDigits() {
  bool advanced = false;
  while (position_ < content_.size()) {
    char32_t c = content_.at(position_);
    if (c > 0x7f || !absl::ascii_isdigit(static_cast<char>(c))) {
      break;
    }
    Advance(1);
    advanced = true;
  }
  return advanced;
}

bool Lexer::ConsumeHexDigits() {
  bool advanced = false;
  while (position_ < content_.size()) {
    char32_t c = content_.at(position_);
    if (c > 0x7f || !absl::ascii_isxdigit(static_cast<char>(c))) {
      break;
    }
    Advance(1);
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

Token Lexer::ConsumeQuotedIdent() {
  int32_t start = GetPosition();
  Advance(1);
  if (!ConsumeUntilAfter('`')) {
    return SetError(start, GetPosition(), "unterminated quoted identifier");
  }
  return MakeToken(TokenType::kIdent, start, GetPosition());
}

Token Lexer::ConsumeStringLiteral(int32_t start, char32_t quote, bool is_bytes,
                                  bool is_raw) {
  Advance(1);
  std::u32string triple_quote(3, quote);
  if (ConsumeString(std::u32string_view(triple_quote.data(), 2))) {
    if (is_raw ? !ConsumeUntilAfterString(triple_quote)
               : !ConsumeUntilAfterUnescapedString(triple_quote)) {
      return SetError(start, GetPosition(),
                      is_bytes ? "unterminated bytes literal"
                               : "unterminated string literal");
    }
    return MakeToken(is_bytes ? TokenType::kBytes : TokenType::kString, start,
                     GetPosition());
  }
  if (is_raw ? !ConsumeUntilAfter(quote) : !ConsumeUntilAfterUnescaped(quote)) {
    return SetError(start, GetPosition(),
                    is_bytes ? "unterminated bytes literal"
                             : "unterminated string literal");
  }
  return MakeToken(is_bytes ? TokenType::kBytes : TokenType::kString, start,
                   GetPosition());
}

// Consumes prefixed string and bytes literals.
// Handles the following prefix sequences (case-insensitive for 'r' and 'b'):
// - Raw strings: r"...", r'...', r"""...""", r'''...'''
// - Bytes: b"...", b'...', b"""...""", b'''...'''
// - Raw bytes: br"...", br'...', br"""...""", br'''...''', rb"...", rb'...',
//              rb"""...""", rb'''...'''
std::optional<Token> Lexer::ConsumePrefixedStringLiteral() {
  int32_t start = GetPosition();
  if (position_ >= content_.size()) return std::nullopt;
  char32_t c = content_.at(position_);
  bool is_bytes = (c == 'b' || c == 'B');
  bool is_raw = (c == 'r' || c == 'R');
  size_t lookahead = 1;
  if (position_ + 1 < content_.size()) {
    char32_t c2 = content_.at(position_ + 1);
    if ((is_bytes && (c2 == 'r' || c2 == 'R')) ||
        (!is_bytes && (c2 == 'b' || c2 == 'B'))) {
      is_bytes = true;
      is_raw = true;
      lookahead = 2;
    }
  }
  if (position_ + static_cast<int32_t>(lookahead) < content_.size()) {
    char32_t quote = content_.at(position_ + static_cast<int32_t>(lookahead));
    if (quote == '"' || quote == '\'') {
      Advance(lookahead);
      return ConsumeStringLiteral(start, quote, is_bytes, is_raw);
    }
  }
  return std::nullopt;
}

// Consumes a numeric literal token and returns its TokenType (kInt, kUint, or
// kFloat). Recognizes the following literal formats:
// - Hexadecimal integers (kInt / kUint): 0x1A, 0XFFu, 0x0U
// - Decimal integers (kInt / kUint): 0, 45U, 123456
// - Floating-point numbers (kFloat): .12345, 1.23, 1e6, 1.5e+10, .5e-3
Token Lexer::ConsumeNumericLiteral() {
  int32_t start = GetPosition();
  char32_t c = content_.at(position_);
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
              absl::StrCat(TokenTypeToString(token_type),
                           " literal has unexpected trailing characters"));
        }
        return MakeToken(token_type, start, GetPosition());
      }
    }
    static_cast<void>(ConsumeDigits());
    if (position_ < content_.size() && content_.at(position_) == '.' &&
        position_ + 1 < content_.size() && content_.at(position_ + 1) <= 0x7f &&
        absl::ascii_isdigit(static_cast<char>(content_.at(position_ + 1)))) {
      floating_point = true;
      Advance(1);
      static_cast<void>(ConsumeDigits());
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
        absl::StrCat(TokenTypeToString(token_type),
                     " literal has unexpected trailing characters"));
  }
  return MakeToken(token_type, start, GetPosition());
}

Token Lexer::ConsumeIdent() {
  int32_t start = GetPosition();
  while (position_ < content_.size()) {
    char32_t c = content_.at(position_);
    if (!IsIdentTrailing(c)) {
      break;
    }
    Advance(1);
  }
  int32_t end = GetPosition();
  std::string word = content_.ToString(start, end);
  const auto& keywords = Keywords();
  if (auto it = keywords.find(word); it != keywords.end()) {
    return MakeToken(it->second, start, end);
  }
  return MakeToken(TokenType::kIdent, start, end);
}

}  // namespace cel::parser_internal
