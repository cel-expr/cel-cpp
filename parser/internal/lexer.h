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

#ifndef THIRD_PARTY_CEL_CPP_PARSER_INTERNAL_LEXER_H_
#define THIRD_PARTY_CEL_CPP_PARSER_INTERNAL_LEXER_H_

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/optimization.h"
#include "absl/functional/function_ref.h"
#include "absl/log/absl_check.h"
#include "absl/strings/ascii.h"
#include "common/source.h"

namespace cel::parser_internal {

enum class TokenType {
  kError = 0,
  kEnd,
  kWhitespace,
  kComment,

  // Keywords
  kNull,
  kFalse,
  kTrue,
  kIn,
  kReservedWord,

  // Literals
  kInt,
  kUint,
  kFloat,
  kString,
  kBytes,

  // Identifiers (standard bare identifiers and backtick-quoted identifiers).
  // Note: The lexer does not validate whether a quoted/escaped identifier is
  // source-legal or permitted in its syntactic context. Because
  // non-source-legal identifiers are used internally in macros and functions,
  // the parser must strictly validate the characters inside quoted identifiers
  // and verify that they only appear where permitted (e.g., field selections
  // and struct field specifiers).
  kIdent,

  // Delimiters
  kLeftBracket,   // [
  kRightBracket,  // ]
  kLeftBrace,     // {
  kRightBrace,    // }
  kLeftParen,     // (
  kRightParen,    // )

  // Operators
  kDot,               // .
  kComma,             // ,
  kMinus,             // -
  kPlus,              // +
  kAsterisk,          // *
  kSlash,             // /
  kPercent,           // %
  kQuestion,          // ?
  kColon,             // :
  kExclamation,       // !
  kEqual,             // =
  kEqualEqual,        // ==
  kExclamationEqual,  // !=
  kLess,              // <
  kLessEqual,         // <=
  kGreater,           // >
  kGreaterEqual,      // >=
  kLogicalAnd,        // &&
  kLogicalOr,         // ||
};

ABSL_ATTRIBUTE_PURE_FUNCTION std::string_view TokenTypeToString(TokenType type);

struct Token final {
  TokenType type = TokenType::kError;
  int32_t start = 0;
  int32_t end = 0;
};

struct LexerError final {
  int32_t start = 0;
  int32_t end = 0;
  std::string message;
};

// Lexer performs fast tokenization of CEL expression source code.
//
// Responsibilities & Parser Expectations:
// This lexer is designed for speed and does not perform comprehensive semantic
// or syntax validation:
//
// 1. String and Bytes Literal Escape Sequences:
//    - For standard single- and double-quoted literals ("..." and '...'), the
//      lexer recognizes backslash ('\') only to determine whether the closing
//      delimiter ('"' or '\'') is escaped (e.g., \" and \' do not terminate the
//      literal, whereas \\" terminates it because the backslash is escaped).
//    - For triple-quoted literals ("""...""" and '''...''') and raw literals
//      (r"...", r'''...'''), backslashes and escape sequences are not processed
//      when locating the closing delimiter.
//    - The lexer does NOT validate, decode, or check the syntax of any escape
//      sequences (e.g., \n, \r, \t, \xHH, \uHHHH, \U00HHHHHH, \0, or invalid
//      escapes like \q). All characters and backslashes within the literal
//      boundaries are preserved verbatim in the token's text span.
//    - The parser/caller is strictly responsible for validating and unescaping
//      all escape sequences and reporting syntax errors for invalid escape
//      sequences when converting string and bytes tokens during AST
//      construction.
//
// 2. Numeric Literals:
//    - Performs only general bounds and format matching for integers and
//      floating-point numeric literals. The lexer expects the parser to perform
//      final validation and numeric conversion when building the AST.
class Lexer final {
 public:
  explicit Lexer(const cel::Source& source)
      : content_(source.content()), position_(0) {
    ABSL_DCHECK_LE(content_.size(), static_cast<SourcePosition>(
                                        std::numeric_limits<int32_t>::max()));
  }

  struct Position final {
    int32_t position = 0;
    bool at_end = false;
    bool done = false;
    LexerError error;
  };

  Lexer(const Lexer&) = delete;
  Lexer(Lexer&&) = delete;
  Lexer& operator=(const Lexer&) = delete;
  Lexer& operator=(Lexer&&) = delete;

  // Scans and returns the next token from the source.
  [[nodiscard]] ABSL_ATTRIBUTE_NOINLINE Token Lex();

  // Inspect the error from the last call to `Lex()` that returned an error
  // token. The reference is not guaranteed to be valid after further calls to
  // `Lex`.
  [[nodiscard]] const LexerError& GetError() const
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return error_;
  }

  [[nodiscard]] int32_t GetPosition() const { return position_; }

  [[nodiscard]] Position SavePosition() const {
    return Position{position_, at_end_, done_, error_};
  }

  void RestorePosition(const Position& position) {
    position_ = position.position;
    at_end_ = position.at_end;
    done_ = position.done;
    error_ = position.error;
  }

 private:
  [[nodiscard]] bool Match(char32_t c) const {
    return position_ < content_.size() && content_.at(position_) == c;
  }

  [[nodiscard]] bool MatchIgnoreCase(char32_t c) const {
    if (position_ >= content_.size()) return false;
    char32_t cp = content_.at(position_);
    return cp <= 0x7f && c <= 0x7f &&
           absl::ascii_tolower(static_cast<char>(cp)) ==
               absl::ascii_tolower(static_cast<char>(c));
  }

  void Advance(size_t n) {
    ABSL_DCHECK_LE(n, static_cast<size_t>(content_.size() - position_));
    position_ += static_cast<int32_t>(n);
  }

  void AdvanceProcessingNewLines(int32_t end_position) {
    ABSL_DCHECK_LE(end_position, content_.size());
    ABSL_DCHECK_GE(end_position, position_);
    Advance(static_cast<size_t>(end_position - position_));
  }

  [[nodiscard]] Token MakeToken(TokenType type, int32_t start, int32_t end) {
    if (ABSL_PREDICT_FALSE(at_end_)) {
      AtEndTokenCreated();
    }
    return Token{.type = type, .start = start, .end = end};
  }

  [[nodiscard]] Token SetError(int32_t start, int32_t end,
                               std::string message) {
    error_ =
        LexerError{.start = start, .end = end, .message = std::move(message)};
    return Token{.type = TokenType::kError, .start = start, .end = end};
  }

  void AtEndTokenCreated() { done_ = true; }

  // Consumes characters up to and including the first occurrence of character
  // `c` without interpreting backslashes as escapes. Returns true if `c` was
  // found and consumed; false if end of input was reached.
  [[nodiscard]] bool ConsumeUntilAfter(char32_t c);

  // Consumes characters up to and including the first occurrence of substring
  // `s` without interpreting backslashes as escapes (`s` must not contain
  // newlines). Returns true if `s` was found and consumed; false if end of
  // input was reached.
  [[nodiscard]] bool ConsumeUntilAfterString(std::u32string_view s);

  // Consumes characters up to and including the first occurrence of `c` that is
  // not preceded by an odd number of backslash ('\') escape characters. Returns
  // true if an unescaped `c` was found and consumed; false if reached EOF.
  [[nodiscard]] bool ConsumeUntilAfterUnescaped(char32_t c);

  // Consumes characters up to and including the first occurrence of substring
  // `s` where the first character of `s` is not preceded by an odd number of
  // backslashes. Returns true if an unescaped `s` was found and consumed; false
  // if reached EOF.
  [[nodiscard]] bool ConsumeUntilAfterUnescapedString(std::u32string_view s);

  [[nodiscard]] bool MatchString(std::u32string_view s) const;

  [[nodiscard]] std::optional<char32_t> MatchIf(
      absl::FunctionRef<bool(char32_t)> predicate) const;

  void ConsumeLine();

  void ConsumeWhitespace();

  [[nodiscard]] bool Consume(char32_t c);

  [[nodiscard]] bool ConsumeIgnoreCase(char32_t c);

  [[nodiscard]] bool ConsumeString(std::u32string_view s);

  [[nodiscard]] std::optional<char32_t> ConsumeIf(
      absl::FunctionRef<bool(char32_t)> predicate);

  [[nodiscard]] bool ConsumeDigits();

  [[nodiscard]] bool ConsumeHexDigits();

  [[nodiscard]] TokenType ConsumeIntegralSuffix();

  // Consumes a backtick-quoted identifier (`...`) and returns
  // TokenType::kIdent. The token text preserves the surrounding backticks so
  // the parser can detect quoted identifiers and enforce restrictions on their
  // characters and allowed syntactic locations (such as field selections and
  // struct field specifiers).
  [[nodiscard]] Token ConsumeQuotedIdent();

  [[nodiscard]] Token ConsumeStringLiteral(int32_t start, char32_t quote,
                                           bool is_bytes = false,
                                           bool is_raw = false);

  // Consumes prefixed string and bytes literals.
  // Handles the following prefix sequences (case-insensitive for 'r' and 'b'):
  // - Raw strings: r"...", r'...', r"""...""", r'''...'''
  // - Bytes: b"...", b'...', b"""...""", b'''...'''
  // - Raw bytes: br"...", br'...', br"""...""", br'''...''', rb"...", rb'...',
  // rb"""...""", rb'''...'''
  [[nodiscard]] std::optional<Token> ConsumePrefixedStringLiteral();

  // Consumes a numeric literal token and returns its TokenType (kInt, kUint, or
  // kFloat). Recognizes the following literal formats:
  // - Decimal integers (kInt / kUint): 0, 45U, 123456
  // - Hexadecimal integers (kInt / kUint): 0x1A, 0XFFu, 0x0U
  // - Floating-point numbers (kFloat): .12345, 1.23, 1e6, 1.5e+10, .5e-3
  [[nodiscard]] Token ConsumeNumericLiteral();

  // Consumes an identifier token and checks if it matches any reserved
  // keywords.
  [[nodiscard]] Token ConsumeIdent();

  cel::SourceContentView content_;
  int32_t position_ = 0;
  bool at_end_ = false;
  bool done_ = false;
  LexerError error_;
};

}  // namespace cel::parser_internal

#endif  // THIRD_PARTY_CEL_CPP_PARSER_INTERNAL_LEXER_H_
