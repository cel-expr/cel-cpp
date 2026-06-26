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

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "internal/testing.h"

namespace cel_parser_internal {
namespace {

MATCHER_P3(IsToken, source, expected_type, expected_text, "") {
  if (arg.type != expected_type) {
    *result_listener << "type is " << TokenTypeToString(arg.type)
                     << " (expected " << TokenTypeToString(expected_type)
                     << ")";
    return false;
  }
  std::string_view actual_text = source.substr(arg.start, arg.end - arg.start);
  if (actual_text != expected_text) {
    *result_listener << "text is '" << actual_text << "' (expected '"
                     << expected_text << "')";
    return false;
  }
  return true;
}

TEST(LexerTest, KeywordsAndIdents) {
  std::string_view source = "null false true in foo_bar `quoted.ident`";
  Lexer lexer(source);

  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kNull, "null"));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kWhitespace, " "));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kFalse, "false"));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kWhitespace, " "));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kTrue, "true"));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kWhitespace, " "));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kIn, "in"));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kWhitespace, " "));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kIdent, "foo_bar"));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kWhitespace, " "));
  EXPECT_THAT(lexer.Lex(),
              IsToken(source, TokenType::kIdent, "`quoted.ident`"));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kEnd, ""));
}

TEST(LexerTest, Numbers) {
  std::string_view source =
      "123 45u 0x1A 0b101 3.14 .5 1e6 2.5e-3 45U 0x1Au 0x1AU";
  Lexer lexer(source);

  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kInt, "123"));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kWhitespace, " "));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kUint, "45u"));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kWhitespace, " "));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kInt, "0x1A"));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kWhitespace, " "));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kInt, "0b101"));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kWhitespace, " "));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kFloat, "3.14"));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kWhitespace, " "));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kFloat, ".5"));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kWhitespace, " "));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kFloat, "1e6"));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kWhitespace, " "));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kFloat, "2.5e-3"));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kWhitespace, " "));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kUint, "45U"));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kWhitespace, " "));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kUint, "0x1Au"));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kWhitespace, " "));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kUint, "0x1AU"));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kEnd, ""));
}

TEST(LexerTest, ZeroNumbers) {
  std::string_view source = "0 0u 0x0 0b0";
  Lexer lexer(source);

  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kInt, "0"));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kWhitespace, " "));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kUint, "0u"));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kWhitespace, " "));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kInt, "0x0"));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kWhitespace, " "));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kInt, "0b0"));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kEnd, ""));
}

TEST(LexerTest, StringsAndBytes) {
  std::string_view source = R"("hello" 'world' """multi
line""" r"raw" b"bytes" rb'\x00' '''multi
single''' R"raw_upper" B"bytes_upper" b'''multi
bytes''' br"raw_bytes" `a.b-c/d e`
"\a\b\f\n\r\t\v\"\'\\\?\` \x1A \u00A0 \U0001F600 \012")";
  Lexer lexer(source);

  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kString, "\"hello\""));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kWhitespace, " "));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kString, "'world'"));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kWhitespace, " "));
  EXPECT_THAT(lexer.Lex(),
              IsToken(source, TokenType::kString, "\"\"\"multi\nline\"\"\""));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kWhitespace, " "));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kString, "r\"raw\""));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kWhitespace, " "));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kBytes, "b\"bytes\""));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kWhitespace, " "));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kBytes, "rb'\\x00'"));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kWhitespace, " "));
  EXPECT_THAT(lexer.Lex(),
              IsToken(source, TokenType::kString, "'''multi\nsingle'''"));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kWhitespace, " "));
  EXPECT_THAT(lexer.Lex(),
              IsToken(source, TokenType::kString, "R\"raw_upper\""));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kWhitespace, " "));
  EXPECT_THAT(lexer.Lex(),
              IsToken(source, TokenType::kBytes, "B\"bytes_upper\""));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kWhitespace, " "));
  EXPECT_THAT(lexer.Lex(),
              IsToken(source, TokenType::kBytes, "b'''multi\nbytes'''"));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kWhitespace, " "));
  EXPECT_THAT(lexer.Lex(),
              IsToken(source, TokenType::kBytes, "br\"raw_bytes\""));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kWhitespace, " "));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kIdent, "`a.b-c/d e`"));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kWhitespace, "\n"));
  EXPECT_THAT(lexer.Lex(),
              IsToken(source, TokenType::kString,
                      "\"\\a\\b\\f\\n\\r\\t\\v\\\"\\'\\\\\\?\\` \\x1A \\u00A0 "
                      "\\U0001F600 \\012\""));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kEnd, ""));
}

TEST(LexerTest, OperatorsAndDelimiters) {
  std::string_view source =
      ". , + - * / % == != < <= > >= && || ! ? : [] { } ( )";
  Lexer lexer(source);

  std::pair<TokenType, std::string_view> expected[] = {
      {TokenType::kDot, "."},
      {TokenType::kWhitespace, " "},
      {TokenType::kComma, ","},
      {TokenType::kWhitespace, " "},
      {TokenType::kPlus, "+"},
      {TokenType::kWhitespace, " "},
      {TokenType::kMinus, "-"},
      {TokenType::kWhitespace, " "},
      {TokenType::kAsterisk, "*"},
      {TokenType::kWhitespace, " "},
      {TokenType::kSlash, "/"},
      {TokenType::kWhitespace, " "},
      {TokenType::kPercent, "%"},
      {TokenType::kWhitespace, " "},
      {TokenType::kEqualEqual, "=="},
      {TokenType::kWhitespace, " "},
      {TokenType::kExclamationEqual, "!="},
      {TokenType::kWhitespace, " "},
      {TokenType::kLess, "<"},
      {TokenType::kWhitespace, " "},
      {TokenType::kLessEqual, "<="},
      {TokenType::kWhitespace, " "},
      {TokenType::kGreater, ">"},
      {TokenType::kWhitespace, " "},
      {TokenType::kGreaterEqual, ">="},
      {TokenType::kWhitespace, " "},
      {TokenType::kLogicalAnd, "&&"},
      {TokenType::kWhitespace, " "},
      {TokenType::kLogicalOr, "||"},
      {TokenType::kWhitespace, " "},
      {TokenType::kExclamation, "!"},
      {TokenType::kWhitespace, " "},
      {TokenType::kQuestion, "?"},
      {TokenType::kWhitespace, " "},
      {TokenType::kColon, ":"},
      {TokenType::kWhitespace, " "},
      {TokenType::kLeftBracket, "["},
      {TokenType::kRightBracket, "]"},
      {TokenType::kWhitespace, " "},
      {TokenType::kLeftBrace, "{"},
      {TokenType::kWhitespace, " "},
      {TokenType::kRightBrace, "}"},
      {TokenType::kWhitespace, " "},
      {TokenType::kLeftParen, "("},
      {TokenType::kWhitespace, " "},
      {TokenType::kRightParen, ")"},
  };

  for (const auto& [t, text] : expected) {
    EXPECT_THAT(lexer.Lex(), IsToken(source, t, text));
  }
}

TEST(LexerTest, CommentsAndLineOffsets) {
  std::string_view source = "a\n// comment\nb";
  std::vector<int32_t> line_offsets;
  Lexer lexer(source, &line_offsets);

  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kIdent, "a"));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kWhitespace, "\n"));
  EXPECT_THAT(lexer.Lex(),
              IsToken(source, TokenType::kComment, "// comment\n"));
  EXPECT_THAT(lexer.Lex(), IsToken(source, TokenType::kIdent, "b"));

  ASSERT_EQ(line_offsets.size(), 2);
  EXPECT_EQ(line_offsets[0], 2);
  EXPECT_EQ(line_offsets[1], 13);
}

struct LexerErrorTestCase {
  std::string_view source;
  std::string_view expected_error_message;
  int32_t expected_position;
};

using LexerErrorTest = testing::TestWithParam<LexerErrorTestCase>;

TEST_P(LexerErrorTest, LexesErrorTokenAndStoresError) {
  const LexerErrorTestCase& test_case = GetParam();
  Lexer lexer(test_case.source);
  Token token = lexer.Lex();
  EXPECT_EQ(token.type, TokenType::kError);
  EXPECT_EQ(lexer.GetError().message, test_case.expected_error_message);
  EXPECT_EQ(lexer.GetPosition(), test_case.expected_position);
}

INSTANTIATE_TEST_SUITE_P(
    ErrorCases, LexerErrorTest,
    testing::Values(
        LexerErrorTestCase{
            .source = "\"unterminated",
            .expected_error_message = "unterminated string literal",
            .expected_position = 13,
        },
        LexerErrorTestCase{
            .source = "0x",
            .expected_error_message =
                "integral literal missing digits after hexadecimal separator",
            .expected_position = 2,
        },
        LexerErrorTestCase{
            .source = "@",
            .expected_error_message = "unexpected character",
            .expected_position = 1,
        },
        LexerErrorTestCase{
            .source = "0x1A_invalid",
            .expected_error_message =
                "int literal has unexpected trailing characters",
            .expected_position = 5,
        },
        LexerErrorTestCase{
            .source = "0b101_invalid",
            .expected_error_message =
                "int literal has unexpected trailing characters",
            .expected_position = 6,
        },
        LexerErrorTestCase{
            .source = "123_invalid",
            .expected_error_message =
                "int literal has unexpected trailing characters",
            .expected_position = 4,
        }));

}  // namespace
}  // namespace cel_parser_internal
