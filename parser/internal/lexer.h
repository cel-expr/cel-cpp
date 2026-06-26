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
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/functional/function_ref.h"
#include "absl/log/absl_check.h"
#include "absl/strings/ascii.h"

namespace cel_parser_internal {

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

  // Literals
  kInt,
  kUint,
  kFloat,
  kString,
  kBytes,

  // Identifiers
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

class Lexer final {
 public:
  explicit Lexer(std::string_view source,
                 std::vector<int32_t>* absl_nullable line_offsets = nullptr)
      : line_offsets_(line_offsets),
        text_begin_(source.data()),
        text_end_(text_begin_ + source.size()),
        text_(text_begin_) {
    ABSL_DCHECK_LT(source.size(),
                   static_cast<size_t>(std::numeric_limits<int32_t>::max()));
  }

  Lexer(const Lexer&) = delete;
  Lexer(Lexer&&) = delete;
  Lexer& operator=(const Lexer&) = delete;
  Lexer& operator=(Lexer&&) = delete;

  ABSL_ATTRIBUTE_NOINLINE
  Token Lex();

  const LexerError& GetError() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    ABSL_DCHECK(at_error_);
    return error_;
  }

  int32_t GetPosition() const {
    return static_cast<int32_t>(text_ - text_begin_);
  }

 private:
  int32_t Find(char c) const;

  bool Match(char c) const { return text_ != text_end_ && *text_ == c; }

  bool MatchIgnoreCase(char c) const {
    return text_ != text_end_ &&
           absl::ascii_tolower(*text_) == absl::ascii_tolower(c);
  }

  void Advance(size_t n) {
    ABSL_DCHECK_LE(n, static_cast<size_t>(text_end_ - text_));
    text_ += n;
  }

  void AdvanceProcessingNewLines(size_t n);
  void AdvanceProcessingNewLines(const char* end);

  std::string_view GetRemainingText() const {
    return std::string_view(text_, static_cast<size_t>(text_end_ - text_));
  }

  Token MakeToken(TokenType type, int32_t start, int32_t end) {
    if (ABSL_PREDICT_FALSE(at_end_)) {
      AtEndTokenCreated();
    }
    return Token{.type = type, .start = start, .end = end};
  }

  Token SetError(int32_t start, int32_t end, std::string message) {
    ABSL_DCHECK(!at_error_);
    at_error_ = true;
    error_ =
        LexerError{.start = start, .end = end, .message = std::move(message)};
    return Token{.type = TokenType::kError, .start = start, .end = end};
  }

  void AtEndTokenCreated() { done_ = true; }

  bool ConsumeUntilAfter(char c);

  bool ConsumeUntilAfterString(std::string_view s);

  bool ConsumeUntilAfterUnescaped(char c);

  void ConsumeIdentTrailing();

  bool MatchString(std::string_view s) const;

  bool MatchStringIgnoreCase(std::string_view s) const;

  std::optional<char> MatchIf(
      absl::FunctionRef<bool(unsigned char)> predicate) const;

  void ConsumeLine();

  bool ConsumeWhitespace();

  bool Consume(char c);

  bool ConsumeIgnoreCase(char c);

  bool ConsumeString(std::string_view s);

  bool ConsumeStringIgnoreCase(std::string_view s);

  std::optional<char> ConsumeIf(
      absl::FunctionRef<bool(unsigned char)> predicate);

  bool ConsumeDigits();

  bool ConsumeHexDigits();

  bool ConsumeBinaryDigits();

  TokenType ConsumeIntegralSuffix();

  std::vector<int32_t>* absl_nullable line_offsets_;
  const char* absl_nonnull text_begin_;
  const char* absl_nonnull text_end_;
  const char* absl_nonnull text_;
  bool at_end_ = false;
  bool at_error_ = false;
  bool done_ = false;
  LexerError error_;
};

}  // namespace cel_parser_internal

#endif  // THIRD_PARTY_CEL_CPP_PARSER_INTERNAL_LEXER_H_
