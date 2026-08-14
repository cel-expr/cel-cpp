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

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "common/source.h"
#include "internal/testing.h"

namespace cel::parser_internal {
namespace {

MATCHER_P3(IsToken, source, expected_type, expected_text, "") {
  if (arg.type != expected_type) {
    *result_listener << "type is " << TokenTypeToString(arg.type)
                     << " (expected " << TokenTypeToString(expected_type)
                     << ")";
    return false;
  }
  std::string actual_text = source->content().ToString(arg.start, arg.end);
  if (actual_text != expected_text) {
    *result_listener << "text is '" << actual_text << "' (expected '"
                     << expected_text << "')";
    return false;
  }
  return true;
}

struct LexerTestCase {
  std::string_view name;
  std::string_view source;
  std::vector<std::pair<TokenType, std::string_view>> expected_tokens;
};

using LexerTest = testing::TestWithParam<LexerTestCase>;

TEST_P(LexerTest, LexesSuccessTokens) {
  const LexerTestCase& test_case = GetParam();
  ASSERT_OK_AND_ASSIGN(auto source, cel::NewSource(test_case.source));
  Lexer lexer(*source);

  for (const auto& [type, text] : test_case.expected_tokens) {
    EXPECT_THAT(lexer.Lex(), IsToken(source.get(), type, text));
  }
  EXPECT_THAT(lexer.Lex(), IsToken(source.get(), TokenType::kEnd, ""));
}

INSTANTIATE_TEST_SUITE_P(
    LexerTest, LexerTest,
    testing::ValuesIn<LexerTestCase>({
        {"NullSource", std::string_view(nullptr, 0), {}},
        {"Empty", "", {}},
        {"Whitespace", " \n  ", {{TokenType::kWhitespace, " \n  "}}},
        {"KeywordsAndIdents",
         "null false true in as return foo_bar _foo_bar _ `quoted.ident`",
         {{TokenType::kNull, "null"},
          {TokenType::kWhitespace, " "},
          {TokenType::kFalse, "false"},
          {TokenType::kWhitespace, " "},
          {TokenType::kTrue, "true"},
          {TokenType::kWhitespace, " "},
          {TokenType::kIn, "in"},
          {TokenType::kWhitespace, " "},
          {TokenType::kReservedWord, "as"},
          {TokenType::kWhitespace, " "},
          {TokenType::kReservedWord, "return"},
          {TokenType::kWhitespace, " "},
          {TokenType::kIdent, "foo_bar"},
          {TokenType::kWhitespace, " "},
          {TokenType::kIdent, "_foo_bar"},
          {TokenType::kWhitespace, " "},
          {TokenType::kIdent, "_"},
          {TokenType::kWhitespace, " "},
          {TokenType::kIdent, "`quoted.ident`"}}},
        {"Numbers",
         "123 45u 0x1A 3.14 .5 1e6 2.5e-3 45U 0x1Au 0x1AU",
         {{TokenType::kInt, "123"},
          {TokenType::kWhitespace, " "},
          {TokenType::kUint, "45u"},
          {TokenType::kWhitespace, " "},
          {TokenType::kInt, "0x1A"},
          {TokenType::kWhitespace, " "},
          {TokenType::kFloat, "3.14"},
          {TokenType::kWhitespace, " "},
          {TokenType::kFloat, ".5"},
          {TokenType::kWhitespace, " "},
          {TokenType::kFloat, "1e6"},
          {TokenType::kWhitespace, " "},
          {TokenType::kFloat, "2.5e-3"},
          {TokenType::kWhitespace, " "},
          {TokenType::kUint, "45U"},
          {TokenType::kWhitespace, " "},
          {TokenType::kUint, "0x1Au"},
          {TokenType::kWhitespace, " "},
          {TokenType::kUint, "0x1AU"}}},
        {"IntEOF", "123456", {{TokenType::kInt, "123456"}}},
        {"HexIntEOF", "0x1A2B", {{TokenType::kInt, "0x1A2B"}}},
        {"FloatPositiveExponentEOF", "1e+6", {{TokenType::kFloat, "1e+6"}}},
        {"FloatEOF", ".12345", {{TokenType::kFloat, ".12345"}}},
        {"IntDotIdent",
         "1.foo",
         {{TokenType::kInt, "1"},
          {TokenType::kDot, "."},
          {TokenType::kIdent, "foo"}}},
        {"IntDotWhitespace",
         "1. ",
         {{TokenType::kInt, "1"},
          {TokenType::kDot, "."},
          {TokenType::kWhitespace, " "}}},
        {"IntDotEOF", "1.", {{TokenType::kInt, "1"}, {TokenType::kDot, "."}}},
        {"DotAtEOFBeforeDigit",
         std::string_view(".6", /*length=*/1),
         {{TokenType::kDot, "."}}},
        {"DotAtEOFBeforeIdent",
         std::string_view(".a", /*length=*/1),
         {{TokenType::kDot, "."}}},
        {"ZeroNumbers",
         "0 0u 0x0",
         {{TokenType::kInt, "0"},
          {TokenType::kWhitespace, " "},
          {TokenType::kUint, "0u"},
          {TokenType::kWhitespace, " "},
          {TokenType::kInt, "0x0"}}},
        {"StringsAndBytes",
         R"("hello" 'world' """ "allowed!" ""also allowed"" \"""also allowed""\" """ r"raw" b"bytes" rb'\x00' '''multi
single''' R"raw_upper" B"bytes_upper" b'''multi
bytes''' br"raw_bytes" `a.b-c/d e`
"\a\b\f\n\r\t\v\"\'\\\?\` \x1A \u00A0 \U0001F600 \012")",
         {{TokenType::kString, "\"hello\""},
          {TokenType::kWhitespace, " "},
          {TokenType::kString, "'world'"},
          {TokenType::kWhitespace, " "},
          {TokenType::kString,
           "\"\"\" \"allowed!\" \"\"also allowed\"\" \\\"\"\"also "
           "allowed\"\"\\\" \"\"\""},
          {TokenType::kWhitespace, " "},
          {TokenType::kString, "r\"raw\""},
          {TokenType::kWhitespace, " "},
          {TokenType::kBytes, "b\"bytes\""},
          {TokenType::kWhitespace, " "},
          {TokenType::kBytes, "rb'\\x00'"},
          {TokenType::kWhitespace, " "},
          {TokenType::kString, "'''multi\nsingle'''"},
          {TokenType::kWhitespace, " "},
          {TokenType::kString, "R\"raw_upper\""},
          {TokenType::kWhitespace, " "},
          {TokenType::kBytes, "B\"bytes_upper\""},
          {TokenType::kWhitespace, " "},
          {TokenType::kBytes, "b'''multi\nbytes'''"},
          {TokenType::kWhitespace, " "},
          {TokenType::kBytes, "br\"raw_bytes\""},
          {TokenType::kWhitespace, " "},
          {TokenType::kIdent, "`a.b-c/d e`"},
          {TokenType::kWhitespace, "\n"},
          {TokenType::kString,
           "\"\\a\\b\\f\\n\\r\\t\\v\\\"\\'\\\\\\?\\` \\x1A \\u00A0 \\U0001F600 "
           "\\012\""}}},
        {"EmptyStrings",
         "\"\" '' \"\"\"\"\"\" '''''' r\"\" r'' r\"\"\"\"\"\" r'''''' b\"\" "
         "b'' b\"\"\"\"\"\" b''''''",
         {{TokenType::kString, "\"\""},
          {TokenType::kWhitespace, " "},
          {TokenType::kString, "''"},
          {TokenType::kWhitespace, " "},
          {TokenType::kString, "\"\"\"\"\"\""},
          {TokenType::kWhitespace, " "},
          {TokenType::kString, "''''''"},
          {TokenType::kWhitespace, " "},
          {TokenType::kString, "r\"\""},
          {TokenType::kWhitespace, " "},
          {TokenType::kString, "r''"},
          {TokenType::kWhitespace, " "},
          {TokenType::kString, "r\"\"\"\"\"\""},
          {TokenType::kWhitespace, " "},
          {TokenType::kString, "r''''''"},
          {TokenType::kWhitespace, " "},
          {TokenType::kBytes, "b\"\""},
          {TokenType::kWhitespace, " "},
          {TokenType::kBytes, "b''"},
          {TokenType::kWhitespace, " "},
          {TokenType::kBytes, "b\"\"\"\"\"\""},
          {TokenType::kWhitespace, " "},
          {TokenType::kBytes, "b''''''"}}},
        {"OperatorsAndDelimiters",
         ". , + - * / % == != < <= > >= && || ! ? : [] { } ( )",
         {{TokenType::kDot, "."},
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
          {TokenType::kRightParen, ")"}}},
        {"Comments",
         "a\n// comment\nb",
         {{TokenType::kIdent, "a"},
          {TokenType::kWhitespace, "\n"},
          {TokenType::kComment, "// comment\n"},
          {TokenType::kIdent, "b"}}},
        {"CommentWithoutTrailingNewlineEOF",
         "// comment without trailing newline",
         {{TokenType::kComment, "// comment without trailing newline"}}},
        {"CommentAfterTokenWithoutTrailingNewlineEOF",
         "a // comment without trailing newline",
         {{TokenType::kIdent, "a"},
          {TokenType::kWhitespace, " "},
          {TokenType::kComment, "// comment without trailing newline"}}},
        {"NestedDelimiters",
         "(((1 + 2) * [3, {4: 5}])))",
         {{TokenType::kLeftParen, "("},    {TokenType::kLeftParen, "("},
          {TokenType::kLeftParen, "("},    {TokenType::kInt, "1"},
          {TokenType::kWhitespace, " "},   {TokenType::kPlus, "+"},
          {TokenType::kWhitespace, " "},   {TokenType::kInt, "2"},
          {TokenType::kRightParen, ")"},   {TokenType::kWhitespace, " "},
          {TokenType::kAsterisk, "*"},     {TokenType::kWhitespace, " "},
          {TokenType::kLeftBracket, "["},  {TokenType::kInt, "3"},
          {TokenType::kComma, ","},        {TokenType::kWhitespace, " "},
          {TokenType::kLeftBrace, "{"},    {TokenType::kInt, "4"},
          {TokenType::kColon, ":"},        {TokenType::kWhitespace, " "},
          {TokenType::kInt, "5"},          {TokenType::kRightBrace, "}"},
          {TokenType::kRightBracket, "]"}, {TokenType::kRightParen, ")"},
          {TokenType::kRightParen, ")"},   {TokenType::kRightParen, ")"}}},
        {"NestedCommentsInDelimiters",
         "(\n  // leading comment\n  [\n    1, // inline comment\n    {2: 3}\n "
         " ]\n)",
         {{TokenType::kLeftParen, "("},
          {TokenType::kWhitespace, "\n  "},
          {TokenType::kComment, "// leading comment\n"},
          {TokenType::kWhitespace, "  "},
          {TokenType::kLeftBracket, "["},
          {TokenType::kWhitespace, "\n    "},
          {TokenType::kInt, "1"},
          {TokenType::kComma, ","},
          {TokenType::kWhitespace, " "},
          {TokenType::kComment, "// inline comment\n"},
          {TokenType::kWhitespace, "    "},
          {TokenType::kLeftBrace, "{"},
          {TokenType::kInt, "2"},
          {TokenType::kColon, ":"},
          {TokenType::kWhitespace, " "},
          {TokenType::kInt, "3"},
          {TokenType::kRightBrace, "}"},
          {TokenType::kWhitespace, "\n  "},
          {TokenType::kRightBracket, "]"},
          {TokenType::kWhitespace, "\n"},
          {TokenType::kRightParen, ")"}}},
        {"ComplexNestedLiteralsAndDelimiters",
         "`foo.bar`([\"nested\\\"quote\", r'''raw 'quotes' inside'''], "
         "b\"bytes\")",
         {{TokenType::kIdent, "`foo.bar`"},
          {TokenType::kLeftParen, "("},
          {TokenType::kLeftBracket, "["},
          {TokenType::kString, "\"nested\\\"quote\""},
          {TokenType::kComma, ","},
          {TokenType::kWhitespace, " "},
          {TokenType::kString, "r'''raw 'quotes' inside'''"},
          {TokenType::kRightBracket, "]"},
          {TokenType::kComma, ","},
          {TokenType::kWhitespace, " "},
          {TokenType::kBytes, "b\"bytes\""},
          {TokenType::kRightParen, ")"}}},
    }),
    [](const testing::TestParamInfo<LexerTestCase>& info) {
      return std::string(info.param.name);
    });

TEST(LexerTest, LineOffsets) {
  std::string_view source_text = "a\n// comment\nb";
  ASSERT_OK_AND_ASSIGN(auto source, cel::NewSource(source_text));
  Lexer lexer(*source);

  EXPECT_THAT(lexer.Lex(), IsToken(source.get(), TokenType::kIdent, "a"));
  EXPECT_THAT(lexer.Lex(), IsToken(source.get(), TokenType::kWhitespace, "\n"));
  EXPECT_THAT(lexer.Lex(),
              IsToken(source.get(), TokenType::kComment, "// comment\n"));
  EXPECT_THAT(lexer.Lex(), IsToken(source.get(), TokenType::kIdent, "b"));

  auto line_offsets = source->line_offsets();
  ASSERT_GE(line_offsets.size(), 2);
  EXPECT_EQ(line_offsets[0], 2);
  EXPECT_EQ(line_offsets[1], 13);
}

TEST(LexerTest, LineOffsetsInStringsAndIdentifiers) {
  std::string_view source_text =
      "'''multi\nline'''\n\"another\nline\"\n`ident\nhere`";
  ASSERT_OK_AND_ASSIGN(auto source, cel::NewSource(source_text));
  Lexer lexer(*source);

  EXPECT_THAT(lexer.Lex(),
              IsToken(source.get(), TokenType::kString, "'''multi\nline'''"));
  EXPECT_THAT(lexer.Lex(), IsToken(source.get(), TokenType::kWhitespace, "\n"));
  EXPECT_THAT(lexer.Lex(),
              IsToken(source.get(), TokenType::kString, "\"another\nline\""));
  EXPECT_THAT(lexer.Lex(), IsToken(source.get(), TokenType::kWhitespace, "\n"));
  EXPECT_THAT(lexer.Lex(),
              IsToken(source.get(), TokenType::kIdent, "`ident\nhere`"));
  EXPECT_THAT(lexer.Lex(), IsToken(source.get(), TokenType::kEnd, ""));

  auto line_offsets = source->line_offsets();
  ASSERT_GE(line_offsets.size(), 5);
  EXPECT_EQ(line_offsets[0], 9);
  EXPECT_EQ(line_offsets[1], 17);
  EXPECT_EQ(line_offsets[2], 26);
  EXPECT_EQ(line_offsets[3], 32);
  EXPECT_EQ(line_offsets[4], 39);
}

struct LexerErrorTestCase {
  std::string_view source;
  std::string_view expected_error_message;
  std::string_view expected_error_location;
};

using LexerErrorTest = testing::TestWithParam<LexerErrorTestCase>;

TEST_P(LexerErrorTest, LexesErrorTokenAndStoresError) {
  const LexerErrorTestCase& test_case = GetParam();
  ASSERT_OK_AND_ASSIGN(auto source, cel::NewSource(test_case.source));
  Lexer lexer(*source);
  Token token = lexer.Lex();
  while (token.type != TokenType::kError && token.type != TokenType::kEnd) {
    token = lexer.Lex();
  }
  EXPECT_EQ(token.type, TokenType::kError);
  EXPECT_EQ(lexer.GetError().message, test_case.expected_error_message);
  auto location = source->GetLocation(lexer.GetPosition());
  ASSERT_TRUE(location.has_value());
  EXPECT_EQ(source->DisplayErrorLocation(*location),
            test_case.expected_error_location);
}

INSTANTIATE_TEST_SUITE_P(
    ErrorCases, LexerErrorTest,
    testing::Values(
        LexerErrorTestCase{
            .source = "\"unterminated",
            .expected_error_message = "unterminated string literal",
            .expected_error_location = "\n | \"unterminated"
                                       "\n | .............^",
        },
        LexerErrorTestCase{
            .source = "0x",
            .expected_error_message =
                "integral literal missing digits after hexadecimal separator",
            .expected_error_location = "\n | 0x"
                                       "\n | ..^",
        },
        LexerErrorTestCase{
            .source = "@",
            .expected_error_message = "unexpected character",
            .expected_error_location = "\n | @"
                                       "\n | .^",
        },
        LexerErrorTestCase{
            .source = "0x1A_invalid",
            .expected_error_message =
                "int literal has unexpected trailing characters",
            .expected_error_location = "\n | 0x1A_invalid"
                                       "\n | .....^",
        },
        LexerErrorTestCase{
            .source = "123_invalid",
            .expected_error_message =
                "int literal has unexpected trailing characters",
            .expected_error_location = "\n | 123_invalid"
                                       "\n | ....^",
        },
        LexerErrorTestCase{
            .source = "1x0",
            .expected_error_message =
                "int literal has unexpected trailing characters",
            .expected_error_location = "\n | 1x0"
                                       "\n | ..^",
        },
        LexerErrorTestCase{
            .source = "2x",
            .expected_error_message =
                "int literal has unexpected trailing characters",
            .expected_error_location = "\n | 2x"
                                       "\n | ..^",
        },
        LexerErrorTestCase{
            .source = "`unterminated quoted",
            .expected_error_message = "unterminated quoted identifier",
            .expected_error_location = "\n | `unterminated quoted"
                                       "\n | ....................^",
        },
        LexerErrorTestCase{
            .source = "'''unterminated multi",
            .expected_error_message = "unterminated string literal",
            .expected_error_location = "\n | '''unterminated multi"
                                       "\n | .....................^",
        },
        LexerErrorTestCase{
            .source = "r'unterminated raw",
            .expected_error_message = "unterminated string literal",
            .expected_error_location = "\n | r'unterminated raw"
                                       "\n | ..................^",
        },
        LexerErrorTestCase{
            .source = "b'unterminated bytes",
            .expected_error_message = "unterminated bytes literal",
            .expected_error_location = "\n | b'unterminated bytes"
                                       "\n | ....................^",
        },
        LexerErrorTestCase{
            .source = "1e",
            .expected_error_message =
                "floating point literal missing digits after exponent "
                "separator",
            .expected_error_location = "\n | 1e"
                                       "\n | ..^",
        },
        LexerErrorTestCase{
            .source = "\"😀😀😀😀😀\" ~error",
            .expected_error_message = "unexpected character",
            .expected_error_location = "\n | \"😀😀😀😀😀\" ~error"
                                       "\n | .．．．．．...^",
        }));

TEST(LexerErrorRecoveryTest, ResumesAfterError) {
  ASSERT_OK_AND_ASSIGN(auto source, cel::NewSource("1e, {2 3}"));
  Lexer lexer(*source);
  Token token = lexer.Lex();
  EXPECT_EQ(token.type, TokenType::kError);
  EXPECT_EQ(lexer.GetError().message,
            "floating point literal missing digits after exponent separator");

  token = lexer.Lex();
  EXPECT_EQ(token.type, TokenType::kComma);

  token = lexer.Lex();
  EXPECT_EQ(token.type, TokenType::kWhitespace);

  token = lexer.Lex();
  EXPECT_EQ(token.type, TokenType::kLeftBrace);

  token = lexer.Lex();
  EXPECT_EQ(token.type, TokenType::kInt);
  EXPECT_EQ(token.start, 5);
  EXPECT_EQ(token.end, 6);
}

TEST(LexerPositionTest, SaveAndRestorePosition) {
  ASSERT_OK_AND_ASSIGN(auto source, cel::NewSource("foo + bar * 42"));
  Lexer lexer(*source);

  Token tok1 = lexer.Lex();
  EXPECT_EQ(tok1.type, TokenType::kIdent);

  Token tok2 = lexer.Lex();
  EXPECT_EQ(tok2.type, TokenType::kWhitespace);

  // Save position before '+'
  Lexer::Position saved = lexer.SavePosition();

  Token tok3 = lexer.Lex();
  EXPECT_EQ(tok3.type, TokenType::kPlus);

  Token tok4 = lexer.Lex();
  EXPECT_EQ(tok4.type, TokenType::kWhitespace);

  Token tok5 = lexer.Lex();
  EXPECT_EQ(tok5.type, TokenType::kIdent);

  // Restore position to before '+'
  lexer.RestorePosition(saved);

  Token tok3_restored = lexer.Lex();
  EXPECT_EQ(tok3_restored.type, TokenType::kPlus);
  EXPECT_EQ(tok3_restored.start, tok3.start);
  EXPECT_EQ(tok3_restored.end, tok3.end);

  Token tok4_restored = lexer.Lex();
  EXPECT_EQ(tok4_restored.type, TokenType::kWhitespace);

  Token tok5_restored = lexer.Lex();
  EXPECT_EQ(tok5_restored.type, TokenType::kIdent);
}

}  // namespace
}  // namespace cel::parser_internal
