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

#include "parser/internal/pratt_parser.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "common/ast.h"
#include "common/constant.h"
#include "common/expr.h"
#include "common/expr_printer.h"
#include "common/source.h"
#include "internal/status_macros.h"
#include "internal/testing.h"
#include "parser/internal/lexer.h"
#include "parser/internal/pratt_parser_worker.h"
#include "parser/macro.h"
#include "parser/macro_expr_factory.h"
#include "parser/options.h"
#include "parser/parser_interface.h"

// Change to 0 to test with the ANTLR parser to check for differences.
#define USE_PRATT_PARSER 1

namespace cel::parser_internal {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::Eq;
using ::testing::NotNull;

template <typename T>
std::string TestName(const testing::TestParamInfo<T>& test_info) {
  std::string name = absl::StrCat(test_info.index, "-", test_info.param.source);
  absl::c_replace_if(name, [](char c) { return !absl::ascii_isalnum(c); }, '_');
  return name;
}

absl::StatusOr<std::unique_ptr<cel::Ast>> Parse(
    absl::string_view expression,
    const cel::ParserOptions& options = cel::ParserOptions(),
    std::vector<cel::ParseIssue>* issues = nullptr) {
#if USE_PRATT_PARSER
  std::unique_ptr<cel::ParserBuilder> builder = NewPrattParserBuilder(options);
#else
  std::unique_ptr<cel::ParserBuilder> builder = cel::NewParserBuilder(options);
#endif
  CEL_ASSIGN_OR_RETURN(std::unique_ptr<cel::Parser> parser, builder->Build());
  CEL_ASSIGN_OR_RETURN(std::unique_ptr<cel::Source> source,
                       cel::NewSource(expression));
  return parser->Parse(*source, issues);
}

struct TestCase {
  absl::string_view source;
  absl::string_view expected_ast;
  bool enable_optional_syntax = false;
  bool enable_variadic_logical_operators = false;
};

class PrattParserTest : public testing::TestWithParam<TestCase> {};

absl::string_view ConstantKind(const cel::Constant& c) {
  switch (c.kind_case()) {
    case ConstantKindCase::kBool:
      return "bool";
    case ConstantKindCase::kInt:
      return "int64";
    case ConstantKindCase::kUint:
      return "uint64";
    case ConstantKindCase::kDouble:
      return "double";
    case ConstantKindCase::kString:
      return "string";
    case ConstantKindCase::kBytes:
      return "bytes";
    case ConstantKindCase::kNull:
      return "NullValue";
    default:
      return "unspecified_constant";
  }
}

absl::string_view ExprKind(const cel::Expr& e) {
  switch (e.kind_case()) {
    case ExprKindCase::kConstant:
      // special cased, this doesn't appear.
      return "Expr.Constant";
    case ExprKindCase::kIdentExpr:
      return "Expr.Ident";
    case ExprKindCase::kSelectExpr:
      return "Expr.Select";
    case ExprKindCase::kCallExpr:
      return "Expr.Call";
    case ExprKindCase::kListExpr:
      return "Expr.CreateList";
    case ExprKindCase::kMapExpr:
      return "Expr.CreateMap";
    case ExprKindCase::kStructExpr:
      return "Expr.CreateStruct";
    case ExprKindCase::kComprehensionExpr:
      return "Expr.Comprehension";
    default:
      return "unspecified_expr";
  }
}

class KindAndIdAdorner : public cel::ExpressionAdorner {
 public:
  std::string Adorn(const cel::Expr& e) const override {
    if (e.has_const_expr()) {
      const cel::Constant& const_expr = e.const_expr();
      return absl::StrCat("^#", e.id(), ":", ConstantKind(const_expr), "#");
    } else {
      return absl::StrCat("^#", e.id(), ":", ExprKind(e), "#");
    }
  }

  std::string AdornStructField(const cel::StructExprField& e) const override {
    return absl::StrFormat("^#%d:Expr.CreateStruct.Entry#", e.id());
  }

  std::string AdornMapEntry(const cel::MapExprEntry& e) const override {
    return absl::StrFormat("^#%d:Expr.CreateStruct.Entry#", e.id());
  }
};

std::string Unindent(absl::string_view multiline) {
  std::vector<std::string> unindented_lines;
  int indent = -1;
  for (absl::string_view line : absl::StrSplit(multiline, '\n')) {
    std::size_t pos = line.find_first_not_of(" \t");
    if (pos == absl::string_view::npos) continue;
    if (indent == -1) indent = pos;
    unindented_lines.push_back(std::string(line.substr(indent)));
  }
  return absl::StrJoin(unindented_lines, "\n");
}

MATCHER_P(AstIs, expected_ast, "") {
  KindAndIdAdorner kind_and_id_adorner;
  cel::ExprPrinter printer(kind_and_id_adorner);
  std::string actual = Unindent(printer.Print(arg));
  std::string expected = Unindent(expected_ast);
  if (actual == expected) {
    return true;
  }
  *result_listener << "\n  Actual:   " << actual
                   << "\n  Expected: " << expected;
  return false;
}

TEST_P(PrattParserTest, Parse) {
  const TestCase& test_case = GetParam();
  cel::ParserOptions options;
  options.enable_optional_syntax = test_case.enable_optional_syntax;
  options.enable_variadic_logical_operators =
      test_case.enable_variadic_logical_operators;
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<cel::Ast> ast,
                       Parse(test_case.source, options));
  EXPECT_THAT(ast->root_expr(), AstIs(test_case.expected_ast));
}

std::vector<TestCase> GetParserTestCases() {
  return {
      TestCase{
          .source = "null",
          .expected_ast = R"(
              null^#1:NullValue#
            )",
      },
      TestCase{
          .source = "true",
          .expected_ast = R"(
              true^#1:bool#
            )",
      },
      TestCase{
          .source = "false",
          .expected_ast = R"(
              false^#1:bool#
            )",
      },
      TestCase{
          .source = "123",
          .expected_ast = R"(
              123^#1:int64#
            )",
      },
      TestCase{
          .source = "-123",
          .expected_ast = R"(
              -123^#1:int64#
            )",
      },
      TestCase{
          .source = "9223372036854775807",
          .expected_ast = R"(
              9223372036854775807^#1:int64#
            )",
      },
      TestCase{
          .source = "-9223372036854775808",
          .expected_ast = R"(
              -9223372036854775808^#1:int64#
            )",
      },
      TestCase{
          .source = "0xA",
          .expected_ast = R"(
              10^#1:int64#
            )",
      },
      TestCase{
          .source = "-0x1A",
          .expected_ast = R"(
              -26^#1:int64#
            )",
      },
      TestCase{
          .source = "-0X1a",
          .expected_ast = R"(
              -26^#1:int64#
            )",
      },
      TestCase{
          .source = "-0x8000000000000000",
          .expected_ast = R"(
              -9223372036854775808^#1:int64#
            )",
      },
      TestCase{
          .source = "-0X8000000000000000",
          .expected_ast = R"(
              -9223372036854775808^#1:int64#
            )",
      },
      TestCase{
          .source = "42u",
          .expected_ast = R"(
              42u^#1:uint64#
            )",
      },
      TestCase{
          .source = "0u",
          .expected_ast = R"(
              0u^#1:uint64#
            )",
      },
      TestCase{
          .source = "0xAu",
          .expected_ast = R"(
              10u^#1:uint64#
            )",
      },
      TestCase{
          .source = "3.14159",
          .expected_ast = R"(
              3.14159^#1:double#
            )",
      },
      TestCase{
          .source = "-3.14159",
          .expected_ast = R"(
              -3.14159^#1:double#
            )",
      },
      TestCase{
          .source = "-5.5e-3",
          .expected_ast = R"(
              -0.0055^#1:double#
            )",
      },
      TestCase{
          .source = "b'hello'",
          .expected_ast = R"(
              b"hello"^#1:bytes#
            )",
      },
      TestCase{
          .source = "b\"hello\"",
          .expected_ast = R"(
              b"hello"^#1:bytes#
            )",
      },
      TestCase{
          .source = "'hello world'",
          .expected_ast = R"(
              "hello world"^#1:string#
            )",
      },
      TestCase{
          .source = "\"hello world\"",
          .expected_ast = R"(
              "hello world"^#1:string#
            )",
      },
      TestCase{
          .source = "\"\u2764\"",
          .expected_ast = "\"\u2764\"^#1:string#",
      },
      TestCase{
          .source = "\"\\a\\b\\f\\n\\r\\t\\v'\\\"\\\\\\? Legal escapes\"",
          .expected_ast = R"(
              "\x07\x08\x0c\n\r\t\x0b'\"\\? Legal escapes"^#1:string#
            )",
      },
      TestCase{
          .source = "a",
          .expected_ast = R"(
              a^#1:Expr.Ident#
            )",
      },
      TestCase{
          .source = "(a)",
          .expected_ast = R"(
              a^#1:Expr.Ident#
            )",
      },
      TestCase{
          .source = "((a))",
          .expected_ast = R"(
              a^#1:Expr.Ident#
            )",
      },
      TestCase{
          .source = "a.b",
          .expected_ast = R"(
              a^#1:Expr.Ident#.b^#2:Expr.Select#
            )",
      },
      TestCase{
          .source = "a.?b",
          .expected_ast = R"(
              _?._(
                a^#1:Expr.Ident#,
                "b"^#3:string#
              )^#2:Expr.Call#
            )",
          .enable_optional_syntax = true,
      },
      TestCase{
          .source = "a.b.c",
          .expected_ast = R"(
              a^#1:Expr.Ident#.b^#2:Expr.Select#.c^#3:Expr.Select#
            )",
      },
      TestCase{
          .source = "a.`b`",
          .expected_ast = R"(
              a^#1:Expr.Ident#.b^#2:Expr.Select#
            )",
      },
      TestCase{
          .source = "a.`b-c`",
          .expected_ast = R"(
              a^#1:Expr.Ident#.b-c^#2:Expr.Select#
            )",
      },
      TestCase{
          .source = "a.?`b`",
          .expected_ast = R"(
              _?._(
                a^#1:Expr.Ident#,
                "b"^#3:string#
              )^#2:Expr.Call#
            )",
          .enable_optional_syntax = true,
      },
      TestCase{
          .source = "-x",
          .expected_ast = R"(
              -_(
                x^#2:Expr.Ident#
              )^#1:Expr.Call#
            )",
      },
      TestCase{
          .source = "- -1",
          .expected_ast = R"(
              -_(
                -1^#2:int64#
              )^#1:Expr.Call#
            )",
      },
      TestCase{
          .source = "-(1 + 2)",
          .expected_ast = R"(
              -_(
                _+_(
                  1^#2:int64#,
                  2^#4:int64#
                )^#3:Expr.Call#
              )^#1:Expr.Call#
            )",
      },
      TestCase{
          .source = "---a",
          .expected_ast = R"(
              -_(
                -_(
                  -_(
                    a^#4:Expr.Ident#
                  )^#3:Expr.Call#
                )^#2:Expr.Call#
              )^#1:Expr.Call#
            )",
      },
      TestCase{
          .source = "!false",
          .expected_ast = R"(
              !_(
                false^#2:bool#
              )^#1:Expr.Call#
            )",
      },
      TestCase{
          .source = "!a",
          .expected_ast = R"(
              !_(
                a^#2:Expr.Ident#
              )^#1:Expr.Call#
            )",
      },
      TestCase{
          .source = "-!true",
          .expected_ast = R"(
              -_(
                !_(
                  true^#3:bool#
                )^#2:Expr.Call#
              )^#1:Expr.Call#
            )",
      },
      TestCase{
          .source = "a + b",
          .expected_ast = R"(
              _+_(
                a^#1:Expr.Ident#,
                b^#3:Expr.Ident#
              )^#2:Expr.Call#
            )",
      },
      TestCase{
          .source = "a - b",
          .expected_ast = R"(
              _-_(
                a^#1:Expr.Ident#,
                b^#3:Expr.Ident#
              )^#2:Expr.Call#
            )",
      },
      TestCase{
          .source = "4--4",
          .expected_ast = R"(
              _-_(
                4^#1:int64#,
                -4^#3:int64#
              )^#2:Expr.Call#
            )",
      },
      TestCase{
          .source = "a * b",
          .expected_ast = R"(
              _*_(
                a^#1:Expr.Ident#,
                b^#3:Expr.Ident#
              )^#2:Expr.Call#
            )",
      },
      TestCase{
          .source = "a / b",
          .expected_ast = R"(
              _/_(
                a^#1:Expr.Ident#,
                b^#3:Expr.Ident#
              )^#2:Expr.Call#
            )",
      },
      TestCase{
          .source = "a % b",
          .expected_ast = R"(
              _%_(
                a^#1:Expr.Ident#,
                b^#3:Expr.Ident#
              )^#2:Expr.Call#
            )",
      },
      TestCase{
          .source = "a in b",
          .expected_ast = R"(
              @in(
                a^#1:Expr.Ident#,
                b^#3:Expr.Ident#
              )^#2:Expr.Call#
            )",
      },
      TestCase{
          .source = "a == b",
          .expected_ast = R"(
              _==_(
                a^#1:Expr.Ident#,
                b^#3:Expr.Ident#
              )^#2:Expr.Call#
            )",
      },
      TestCase{
          .source = "a != b",
          .expected_ast = R"(
              _!=_(
                a^#1:Expr.Ident#,
                b^#3:Expr.Ident#
              )^#2:Expr.Call#
            )",
      },
      TestCase{
          .source = "a > b",
          .expected_ast = R"(
              _>_(
                a^#1:Expr.Ident#,
                b^#3:Expr.Ident#
              )^#2:Expr.Call#
            )",
      },
      TestCase{
          .source = "a >= b",
          .expected_ast = R"(
              _>=_(
                a^#1:Expr.Ident#,
                b^#3:Expr.Ident#
              )^#2:Expr.Call#
            )",
      },
      TestCase{
          .source = "a < b",
          .expected_ast = R"(
              _<_(
                a^#1:Expr.Ident#,
                b^#3:Expr.Ident#
              )^#2:Expr.Call#
            )",
      },
      TestCase{
          .source = "a <= b",
          .expected_ast = R"(
              _<=_(
                a^#1:Expr.Ident#,
                b^#3:Expr.Ident#
              )^#2:Expr.Call#
            )",
      },
      TestCase{
          .source = "a && b",
          .expected_ast = R"(
              _&&_(
                a^#1:Expr.Ident#,
                b^#2:Expr.Ident#
              )^#3:Expr.Call#
            )",
      },
      TestCase{
          .source = "a && b && c",
          .expected_ast = R"(
              _&&_(
                _&&_(
                  a^#1:Expr.Ident#,
                  b^#2:Expr.Ident#
                )^#3:Expr.Call#,
                c^#4:Expr.Ident#
              )^#5:Expr.Call#
            )",
      },
      TestCase{
          .source = "a && b && c && d",
          .expected_ast = R"(
              _&&_(
                _&&_(
                  a^#1:Expr.Ident#,
                  b^#2:Expr.Ident#
                )^#3:Expr.Call#,
                _&&_(
                  c^#4:Expr.Ident#,
                  d^#6:Expr.Ident#
                )^#7:Expr.Call#
              )^#5:Expr.Call#
            )",
      },
      TestCase{
          .source = "a && b && c && d && e",
          .expected_ast = R"(
              _&&_(
                _&&_(
                  _&&_(
                    a^#1:Expr.Ident#,
                    b^#2:Expr.Ident#
                  )^#3:Expr.Call#,
                  c^#4:Expr.Ident#
                )^#5:Expr.Call#,
                _&&_(
                  d^#6:Expr.Ident#,
                  e^#8:Expr.Ident#
                )^#9:Expr.Call#
              )^#7:Expr.Call#
            )",
      },
      TestCase{
          .source = "a && b && c && d",
          .expected_ast = R"(
              _&&_(
                a^#1:Expr.Ident#,
                b^#2:Expr.Ident#,
                c^#4:Expr.Ident#,
                d^#6:Expr.Ident#
              )^#3:Expr.Call#
            )",
          .enable_variadic_logical_operators = true,
      },
      TestCase{
          .source = "a || b",
          .expected_ast = R"(
              _||_(
                a^#1:Expr.Ident#,
                b^#2:Expr.Ident#
              )^#3:Expr.Call#
            )",
      },
      TestCase{
          .source = "a || b || c",
          .expected_ast = R"(
              _||_(
                _||_(
                  a^#1:Expr.Ident#,
                  b^#2:Expr.Ident#
                )^#3:Expr.Call#,
                c^#4:Expr.Ident#
              )^#5:Expr.Call#
            )",
      },
      TestCase{
          .source = "a || b || c || d",
          .expected_ast = R"(
              _||_(
                _||_(
                  a^#1:Expr.Ident#,
                  b^#2:Expr.Ident#
                )^#3:Expr.Call#,
                _||_(
                  c^#4:Expr.Ident#,
                  d^#6:Expr.Ident#
                )^#7:Expr.Call#
              )^#5:Expr.Call#
            )",
      },
      TestCase{
          .source = "a || b || c || d || e",
          .expected_ast = R"(
              _||_(
                _||_(
                  _||_(
                    a^#1:Expr.Ident#,
                    b^#2:Expr.Ident#
                  )^#3:Expr.Call#,
                  c^#4:Expr.Ident#
                )^#5:Expr.Call#,
                _||_(
                  d^#6:Expr.Ident#,
                  e^#8:Expr.Ident#
                )^#9:Expr.Call#
              )^#7:Expr.Call#
            )",
      },
      TestCase{
          .source = "a || b || c || d",
          .expected_ast = R"(
              _||_(
                a^#1:Expr.Ident#,
                b^#2:Expr.Ident#,
                c^#4:Expr.Ident#,
                d^#6:Expr.Ident#
              )^#3:Expr.Call#
            )",
          .enable_variadic_logical_operators = true,
      },
      TestCase{
          .source = "10 - 3 - 2",
          .expected_ast = R"(
              _-_(
                _-_(
                  10^#1:int64#,
                  3^#3:int64#
                )^#2:Expr.Call#,
                2^#5:int64#
              )^#4:Expr.Call#
            )",
      },
      TestCase{
          .source = "(((10 - 3) - 2))",
          .expected_ast = R"(
              _-_(
                _-_(
                  10^#1:int64#,
                  3^#3:int64#
                )^#2:Expr.Call#,
                2^#5:int64#
              )^#4:Expr.Call#
            )",
      },
      TestCase{
          .source = "1 + 2 * 3 - 1 / 2 == 6 % 1",
          .expected_ast = R"(
              _==_(
                _-_(
                  _+_(
                    1^#1:int64#,
                    _*_(
                      2^#3:int64#,
                      3^#5:int64#
                    )^#4:Expr.Call#
                  )^#2:Expr.Call#,
                  _/_(
                    1^#7:int64#,
                    2^#9:int64#
                  )^#8:Expr.Call#
                )^#6:Expr.Call#,
                _%_(
                  6^#11:int64#,
                  1^#13:int64#
                )^#12:Expr.Call#
              )^#10:Expr.Call#
            )",
      },
      TestCase{
          .source = "(1 + (2 * 3) - (1 / 2)) == (6 % 1)",
          .expected_ast = R"(
              _==_(
                _-_(
                  _+_(
                    1^#1:int64#,
                    _*_(
                      2^#3:int64#,
                      3^#5:int64#
                    )^#4:Expr.Call#
                  )^#2:Expr.Call#,
                  _/_(
                    1^#7:int64#,
                    2^#9:int64#
                  )^#8:Expr.Call#
                )^#6:Expr.Call#,
                _%_(
                  6^#11:int64#,
                  1^#13:int64#
                )^#12:Expr.Call#
              )^#10:Expr.Call#
            )",
      },
      TestCase{
          .source = "1 + 2 * 3 == 7 && true || false",
          .expected_ast = R"(
              _||_(
                _&&_(
                  _==_(
                    _+_(
                      1^#1:int64#,
                      _*_(
                        2^#3:int64#,
                        3^#5:int64#
                      )^#4:Expr.Call#
                    )^#2:Expr.Call#,
                    7^#7:int64#
                  )^#6:Expr.Call#,
                  true^#8:bool#
                )^#9:Expr.Call#,
                false^#10:bool#
              )^#11:Expr.Call#
            )",
      },
      TestCase{
          .source = "x > 0 ? 'pos' : 'neg'",
          .expected_ast = R"(
              _?_:_(
                _>_(
                  x^#1:Expr.Ident#,
                  0^#3:int64#
                )^#2:Expr.Call#,
                "pos"^#5:string#,
                "neg"^#6:string#
              )^#4:Expr.Call#
            )",
      },
      TestCase{
          .source = "a?b:c",
          .expected_ast = R"(
              _?_:_(
                a^#1:Expr.Ident#,
                b^#3:Expr.Ident#,
                c^#4:Expr.Ident#
              )^#2:Expr.Call#
            )",
      },
      TestCase{
          .source = "a[b]",
          .expected_ast = R"(
              _[_](
                a^#1:Expr.Ident#,
                b^#3:Expr.Ident#
              )^#2:Expr.Call#
            )",
      },
      TestCase{
          .source = "a[?b]",
          .expected_ast = R"(
              _[?_](
                a^#1:Expr.Ident#,
                b^#3:Expr.Ident#
              )^#2:Expr.Call#
            )",
          .enable_optional_syntax = true,
      },
      TestCase{
          .source = "a[3]",
          .expected_ast = R"(
              _[_](
                a^#1:Expr.Ident#,
                3^#3:int64#
              )^#2:Expr.Call#
            )",
      },
      TestCase{
          .source = "[1,3,4][0]",
          .expected_ast = R"(
              _[_](
                [
                  1^#2:int64#,
                  3^#3:int64#,
                  4^#4:int64#
                ]^#1:Expr.CreateList#,
                0^#6:int64#
              )^#5:Expr.Call#
            )",
      },
      TestCase{
          .source = "a()",
          .expected_ast = R"(
              a()^#1:Expr.Call#
            )",
      },
      TestCase{
          .source = "a(b)",
          .expected_ast = R"(
              a(
                b^#2:Expr.Ident#
              )^#1:Expr.Call#
            )",
      },
      TestCase{
          .source = "a(b, c)",
          .expected_ast = R"(
              a(
                b^#2:Expr.Ident#,
                c^#3:Expr.Ident#
              )^#1:Expr.Call#
            )",
      },
      TestCase{
          .source = "a.b()",
          .expected_ast = R"(
              a^#1:Expr.Ident#.b()^#2:Expr.Call#
            )",
      },
      TestCase{
          .source = "a.b(1)",
          .expected_ast = R"(
              a^#1:Expr.Ident#.b(
                1^#3:int64#
              )^#2:Expr.Call#
            )",
      },
      TestCase{
          .source = "a.b(c)",
          .expected_ast = R"(
              a^#1:Expr.Ident#.b(
                c^#3:Expr.Ident#
              )^#2:Expr.Call#
            )",
      },
      TestCase{
          .source = "size(x) == x.size()",
          .expected_ast = R"(
              _==_(
                size(
                  x^#2:Expr.Ident#
                )^#1:Expr.Call#,
                x^#4:Expr.Ident#.size()^#5:Expr.Call#
              )^#3:Expr.Call#
            )",
      },
      TestCase{
          .source = "\"foo\".size()",
          .expected_ast = R"(
              "foo"^#1:string#.size()^#2:Expr.Call#
            )",
      },
      TestCase{
          .source = "[true, true].size() == 2",
          .expected_ast = R"(
              _==_(
                [
                  true^#2:bool#,
                  true^#3:bool#
                ]^#1:Expr.CreateList#.size()^#4:Expr.Call#,
                2^#6:int64#
              )^#5:Expr.Call#
            )",
      },
      TestCase{
          .source = "[]",
          .expected_ast = R"(
              []^#1:Expr.CreateList#
            )",
      },
      TestCase{
          .source = "[a]",
          .expected_ast = R"(
              [
                a^#2:Expr.Ident#
              ]^#1:Expr.CreateList#
            )",
      },
      TestCase{
          .source = "[1, 2, 3]",
          .expected_ast = R"(
              [
                1^#2:int64#,
                2^#3:int64#,
                3^#4:int64#
              ]^#1:Expr.CreateList#
            )",
      },
      TestCase{
          .source = "[1, 2, 3,]",
          .expected_ast = R"(
              [
                1^#2:int64#,
                2^#3:int64#,
                3^#4:int64#
              ]^#1:Expr.CreateList#
            )",
      },
      TestCase{
          .source = "[?1, 2]",
          .expected_ast = R"(
              [
                ?1^#2:int64#,
                2^#3:int64#
              ]^#1:Expr.CreateList#
            )",
          .enable_optional_syntax = true,
      },
      TestCase{
          .source = "{}",
          .expected_ast = R"(
              {}^#1:Expr.CreateMap#
            )",
      },
      TestCase{
          .source = "{'key': 'value', 'num': 42}",
          .expected_ast = R"(
              {
                "key"^#2:string#:"value"^#4:string#^#3:Expr.CreateStruct.Entry#,
                "num"^#5:string#:42^#7:int64#^#6:Expr.CreateStruct.Entry#
              }^#1:Expr.CreateMap#
            )",
      },
      TestCase{
          .source = "{?'key': 'value', 'num': 42}",
          .expected_ast = R"(
              {
                ?"key"^#2:string#:"value"^#4:string#^#3:Expr.CreateStruct.Entry#,
                "num"^#5:string#:42^#7:int64#^#6:Expr.CreateStruct.Entry#
              }^#1:Expr.CreateMap#
            )",
          .enable_optional_syntax = true,
      },
      TestCase{
          .source = "{foo: 5, bar: \"xyz\"}",
          .expected_ast = R"(
              {
                foo^#2:Expr.Ident#:5^#4:int64#^#3:Expr.CreateStruct.Entry#,
                bar^#5:Expr.Ident#:"xyz"^#7:string#^#6:Expr.CreateStruct.Entry#
              }^#1:Expr.CreateMap#
            )",
      },
      TestCase{
          .source = "google.protobuf.Empty{}",
          .expected_ast = R"(
              google.protobuf.Empty{}^#1:Expr.CreateStruct#
            )",
      },
      TestCase{
          .source = "foo{ }",
          .expected_ast = R"(
              foo{}^#1:Expr.CreateStruct#
            )",
      },
      TestCase{
          .source = "foo{ a:b }",
          .expected_ast = R"(
              foo{
                a:b^#3:Expr.Ident#^#2:Expr.CreateStruct.Entry#
              }^#1:Expr.CreateStruct#
            )",
      },
      TestCase{
          .source = "A{`b`: 1}",
          .expected_ast = R"(
              A{
                b:1^#3:int64#^#2:Expr.CreateStruct.Entry#
              }^#1:Expr.CreateStruct#
            )",
      },
      TestCase{
          .source = "A{`b-c`: 1}",
          .expected_ast = R"(
              A{
                b-c:1^#3:int64#^#2:Expr.CreateStruct.Entry#
              }^#1:Expr.CreateStruct#
            )",
      },
      TestCase{
          .source = "Msg{field: 10, other: 'val'}",
          .expected_ast = R"(
              Msg{
                field:10^#3:int64#^#2:Expr.CreateStruct.Entry#,
                other:"val"^#5:string#^#4:Expr.CreateStruct.Entry#
              }^#1:Expr.CreateStruct#
            )",
      },
      TestCase{
          .source = "Msg{?field: 10, other: 'val'}",
          .expected_ast = R"(
              Msg{
                ?field:10^#3:int64#^#2:Expr.CreateStruct.Entry#,
                other:"val"^#5:string#^#4:Expr.CreateStruct.Entry#
              }^#1:Expr.CreateStruct#
            )",
          .enable_optional_syntax = true,
      },
      TestCase{
          .source = "(((10 - 3) - 2))",
          .expected_ast = R"(
              _-_(
                _-_(
                  10^#1:int64#,
                  3^#3:int64#
                )^#2:Expr.Call#,
                2^#5:int64#
              )^#4:Expr.Call#
            )",
      },
  };
}

INSTANTIATE_TEST_SUITE_P(PrattParserTest, PrattParserTest,
                         testing::ValuesIn(GetParserTestCases()),
                         TestName<TestCase>);

struct ErrorTestCase {
  absl::string_view source;
  absl::string_view expected_error;
  bool enable_optional_syntax = false;
  bool enable_quoted_identifiers = false;
};

class PrattParserErrorTest : public testing::TestWithParam<ErrorTestCase> {};

std::string FormatIssues(const cel::Source& source,
                         const std::vector<cel::ParseIssue>& issues) {
  return absl::StrJoin(
      issues, "\n", [&source](std::string* out, const cel::ParseIssue& issue) {
        absl::StrAppend(
            out,
            absl::StrFormat("ERROR: %s:%d:%d: %s", source.description(),
                            issue.location().line, issue.location().column + 1,
                            issue.message()),
            source.DisplayErrorLocation(issue.location()));
      });
}

TEST_P(PrattParserErrorTest, ParseSyntaxError) {
  const ErrorTestCase& test_case = GetParam();
  cel::ParserOptions options;
  options.enable_optional_syntax = test_case.enable_optional_syntax;
  options.enable_quoted_identifiers = test_case.enable_quoted_identifiers;
  std::vector<cel::ParseIssue> issues;
  absl::StatusOr<std::unique_ptr<cel::Ast>> result =
      Parse(test_case.source, options, &issues);
  EXPECT_THAT(result, StatusIs(absl::StatusCode::kInvalidArgument,
                               Eq(test_case.expected_error)));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<cel::Source> source,
                       cel::NewSource(test_case.source));
  EXPECT_EQ(FormatIssues(*source, issues), test_case.expected_error);
}

std::vector<ErrorTestCase> GetErrorTestCases() {
  return {
      ErrorTestCase{
          .source = "1 + 2 * 3 4",
          .expected_error =
              "ERROR: <input>:1:11: unexpected token after expression\n"
              " | 1 + 2 * 3 4\n"
              " | ..........^",
      },
      ErrorTestCase{
          .source = "1{}",
          .expected_error =
              "ERROR: <input>:1:2: unexpected token after expression\n"
              " | 1{}\n"
              " | .^",
      },
      ErrorTestCase{
          .source = "true ? 1",
          .expected_error =
              "ERROR: <input>:1:9: expected ':' in conditional expression\n"
              " | true ? 1\n"
              " | ........^",
      },
      ErrorTestCase{
          .source = "a.?b",
          .expected_error = "ERROR: <input>:1:2: unsupported syntax '.?'\n"
                            " | a.?b\n"
                            " | .^",
      },
      ErrorTestCase{
          .source = "a.",
          .expected_error =
              "ERROR: <input>:1:3: expected identifier after '.'\n"
              " | a.\n"
              " | ..^",
      },
      ErrorTestCase{
          .source = "a[?0]",
          .expected_error = "ERROR: <input>:1:2: unsupported syntax '?'\n"
                            " | a[?0]\n"
                            " | .^",
      },
      ErrorTestCase{
          .source = ". *",
          .expected_error = "ERROR: <input>:1:3: expected identifier\n"
                            " | . *\n"
                            " | ..^",
      },
      ErrorTestCase{
          .source = ".as",
          .expected_error = "ERROR: <input>:1:2: reserved identifier: as\n"
                            " | .as\n"
                            " | .^",
      },
      ErrorTestCase{
          .source = "* 2",
          .expected_error =
              "ERROR: <input>:1:1: unexpected token\n"
              " | * 2\n"
              " | ^\n"
              "ERROR: <input>:1:3: unexpected token after expression\n"
              " | * 2\n"
              " | ..^",
      },
      ErrorTestCase{
          .source = "(1 + 2",
          .expected_error =
              "ERROR: <input>:1:7: mismatched input <EOF> expecting ')'\n"
              " | (1 + 2\n"
              " | ......^",
      },
      ErrorTestCase{
          .source = "[?1]",
          .expected_error = "ERROR: <input>:1:2: unsupported syntax '?'\n"
                            " | [?1]\n"
                            " | .^",
      },
      ErrorTestCase{
          .source = "[1, 2",
          .expected_error = "ERROR: <input>:1:6: expected ']'\n"
                            " | [1, 2\n"
                            " | .....^",
      },
      ErrorTestCase{
          .source = "{?'k': 'v'}",
          .expected_error = "ERROR: <input>:1:2: unsupported syntax '?'\n"
                            " | {?'k': 'v'}\n"
                            " | .^",
      },
      ErrorTestCase{
          .source = "{'k' 'v'}",
          .expected_error = "ERROR: <input>:1:6: expected ':' in map entry\n"
                            " | {'k' 'v'}\n"
                            " | .....^",
      },
      ErrorTestCase{
          .source = "{'k': 'v'",
          .expected_error = "ERROR: <input>:1:10: expected '}'\n"
                            " | {'k': 'v'\n"
                            " | .........^",
      },
      ErrorTestCase{
          .source = "Msg{?f: 1}",
          .expected_error = "ERROR: <input>:1:5: unsupported syntax '?'\n"
                            " | Msg{?f: 1}\n"
                            " | ....^",
      },
      ErrorTestCase{
          .source = "Msg{1: 2}",
          .expected_error = "ERROR: <input>:1:5: expected struct field name\n"
                            " | Msg{1: 2}\n"
                            " | ....^",
      },
      ErrorTestCase{
          .source = "Msg{f 10}",
          .expected_error = "ERROR: <input>:1:7: expected ':' in struct field\n"
                            " | Msg{f 10}\n"
                            " | ......^",
      },
      ErrorTestCase{
          .source = "Msg{f: 10",
          .expected_error = "ERROR: <input>:1:10: expected '}'\n"
                            " | Msg{f: 10\n"
                            " | .........^",
      },
      ErrorTestCase{
          .source = "f(1, 2",
          .expected_error =
              "ERROR: <input>:1:7: mismatched input <EOF> expecting ')'\n"
              " | f(1, 2\n"
              " | ......^",
      },
      ErrorTestCase{
          .source = "999999999999999999999999999999999999999",
          .expected_error = "ERROR: <input>:1:1: invalid int literal\n"
                            " | 999999999999999999999999999999999999999\n"
                            " | ^",
      },
      ErrorTestCase{
          .source = "999999999999999999999999999999999999999u",
          .expected_error = "ERROR: <input>:1:1: invalid uint literal\n"
                            " | 999999999999999999999999999999999999999u\n"
                            " | ^",
      },
      ErrorTestCase{
          .source = "1e",
          .expected_error =
              "ERROR: <input>:1:1: floating point literal missing digits after "
              "exponent separator\n"
              " | 1e\n"
              " | ^",
      },
      ErrorTestCase{
          .source = "\"unterminated",
          .expected_error = "ERROR: <input>:1:1: unterminated string literal\n"
                            " | \"unterminated\n"
                            " | ^",
      },
      ErrorTestCase{
          .source = "b\"unterminated",
          .expected_error = "ERROR: <input>:1:1: unterminated bytes literal\n"
                            " | b\"unterminated\n"
                            " | ^",
      },
      ErrorTestCase{
          .source = "a.?`foo`",
          .expected_error = "ERROR: <input>:1:4: unsupported syntax '`'\n"
                            " | a.?`foo`\n"
                            " | ...^",
          .enable_optional_syntax = true,
          .enable_quoted_identifiers = false,
      },
      ErrorTestCase{
          .source = "a.`foo`()",
          .expected_error = "ERROR: <input>:1:3: unexpected quoted identifier\n"
                            " | a.`foo`()\n"
                            " | ..^",
          .enable_quoted_identifiers = true,
      },
      ErrorTestCase{
          .source = "`foo`",
          .expected_error = "ERROR: <input>:1:1: unexpected quoted identifier\n"
                            " | `foo`\n"
                            " | ^",
          .enable_quoted_identifiers = true,
      },
      ErrorTestCase{
          .source = "`foo`()",
          .expected_error = "ERROR: <input>:1:1: unexpected quoted identifier\n"
                            " | `foo`()\n"
                            " | ^",
          .enable_quoted_identifiers = true,
      },
      ErrorTestCase{
          .source = "a.`b@c`",
          .expected_error = "ERROR: <input>:1:3: unexpected quoted identifier\n"
                            " | a.`b@c`\n"
                            " | ..^",
          .enable_quoted_identifiers = true,
      },
      ErrorTestCase{
          .source = "a.``",
          .expected_error = "ERROR: <input>:1:3: unexpected quoted identifier\n"
                            " | a.``\n"
                            " | ..^",
          .enable_quoted_identifiers = true,
      },
      ErrorTestCase{
          .source = "a.`foo`",
          .expected_error = "ERROR: <input>:1:3: unsupported syntax '`'\n"
                            " | a.`foo`\n"
                            " | ..^",
          .enable_quoted_identifiers = false,
      },
      ErrorTestCase{
          .source = "`foo",
          .expected_error =
              "ERROR: <input>:1:1: unterminated quoted identifier\n"
              " | `foo\n"
              " | ^",
          .enable_quoted_identifiers = true,
      },
      ErrorTestCase{
          .source = "f(*, 1e, {2 3})",
          .expected_error =
              "ERROR: <input>:1:3: unexpected token\n"
              " | f(*, 1e, {2 3})\n"
              " | ..^\n"
              "ERROR: <input>:1:6: floating point literal missing digits after "
              "exponent separator\n"
              " | f(*, 1e, {2 3})\n"
              " | .....^\n"
              "ERROR: <input>:1:13: expected ':' in map entry\n"
              " | f(*, 1e, {2 3})\n"
              " | ............^",
      },
      ErrorTestCase{
          .source = "(1 + *) + 2",
          .expected_error = "ERROR: <input>:1:6: unexpected token\n"
                            " | (1 + *) + 2\n"
                            " | .....^",
      },
      ErrorTestCase{
          .source = "f(1 + *, 2)",
          .expected_error = "ERROR: <input>:1:7: unexpected token\n"
                            " | f(1 + *, 2)\n"
                            " | ......^",
      },
      ErrorTestCase{
          .source = "(a. + 1)",
          .expected_error =
              "ERROR: <input>:1:5: expected identifier after '.'\n"
              " | (a. + 1)\n"
              " | ....^",
      },
      ErrorTestCase{
          .source = "f(a., 1)",
          .expected_error =
              "ERROR: <input>:1:5: expected identifier after '.'\n"
              " | f(a., 1)\n"
              " | ....^",
      },
      ErrorTestCase{
          .source = "[a., 1]",
          .expected_error =
              "ERROR: <input>:1:4: expected identifier after '.'\n"
              " | [a., 1]\n"
              " | ...^",
      },
      ErrorTestCase{
          .source = "-0x8000000000000001",
          .expected_error = "ERROR: <input>:1:2: invalid int literal\n"
                            " | -0x8000000000000001\n"
                            " | .^",
      },
      ErrorTestCase{
          .source = "-0x10000000000000000",
          .expected_error = "ERROR: <input>:1:2: invalid int literal\n"
                            " | -0x10000000000000000\n"
                            " | .^",
      },
      ErrorTestCase{
          .source = "-9223372036854775809",
          .expected_error = "ERROR: <input>:1:2: invalid int literal\n"
                            " | -9223372036854775809\n"
                            " | .^",
      },
      ErrorTestCase{
          .source = "-999999999999999999999999999999999999999",
          .expected_error = "ERROR: <input>:1:2: invalid int literal\n"
                            " | -999999999999999999999999999999999999999\n"
                            " | .^",
      },
      ErrorTestCase{
          .source = "-",
          .expected_error =
              "ERROR: <input>:1:2: Syntax error: mismatched input '<EOF>' "
              "expecting expression\n"
              " | -\n"
              " | .^",
      },
      ErrorTestCase{
          .source = "- *",
          .expected_error = "ERROR: <input>:1:3: unexpected token\n"
                            " | - *\n"
                            " | ..^",
      },
      ErrorTestCase{
          .source = "\"😀😀😀😀😀\" ~error",
          .expected_error = "ERROR: <input>:1:9: unexpected character\n"
                            " | \"😀😀😀😀😀\" ~error\n"
                            " | .．．．．．..^",
      },
  };
}

INSTANTIATE_TEST_SUITE_P(PrattParserErrorTest, PrattParserErrorTest,
                         testing::ValuesIn(GetErrorTestCases()),
                         TestName<ErrorTestCase>);

TEST(PrattParserTest, SourceInfoPositionsPopulated) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<cel::Ast> ast, Parse("a + b"));
  const cel::SourceInfo& source_info = ast->source_info();
  EXPECT_FALSE(source_info.positions().empty());

  const cel::Expr& root = ast->root_expr();
  EXPECT_EQ(source_info.positions().at(root.id()), 2);
  ASSERT_TRUE(root.has_call_expr());
  ASSERT_EQ(root.call_expr().args().size(), 2);
  EXPECT_EQ(source_info.positions().at(root.call_expr().args()[0].id()), 0);
  EXPECT_EQ(source_info.positions().at(root.call_expr().args()[1].id()), 4);
}

TEST(PrattParserRecursionDepthTest, ParseRecursionDepth) {
  cel::ParserOptions options;
  options.max_recursion_depth = 5;
  EXPECT_THAT(Parse("((((1))))", options), IsOkAndHolds(NotNull()));
  EXPECT_THAT(Parse("[[[[[[1]]]]]]", options),
              StatusIs(absl::StatusCode::kCancelled));
}

TEST(PrattParserRecursionDepthTest, SequentialScopesDoNotAccumulateDepth) {
  cel::ParserOptions options;
  options.max_recursion_depth = 2;
  EXPECT_THAT(Parse("[1] + [2] + [3]", options), IsOkAndHolds(NotNull()));
}

class TestParserWorker : public ParserWorker {
  // Expose the protected constructor and methods for testing.
 public:
  using ParserWorker::GetTokenText;
  using ParserWorker::ParserWorker;
};

TEST(ParserWorkerTest, GetTokenTextBoundsChecking) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<cel::Source> source,
                       cel::NewSource("hello world"));
  cel::ParserOptions options;
  TestParserWorker worker(*source, options, nullptr);
  EXPECT_EQ(worker.GetTokenText(
                Token{.type = TokenType::kIdent, .start = 0, .end = 5}),
            "hello");
  EXPECT_EQ(worker.GetTokenText(
                Token{.type = TokenType::kIdent, .start = -1, .end = 5}),
            "");
  EXPECT_EQ(worker.GetTokenText(
                Token{.type = TokenType::kIdent, .start = 5, .end = 2}),
            "");
  EXPECT_EQ(worker.GetTokenText(
                Token{.type = TokenType::kIdent, .start = 0, .end = 100}),
            "");
}

struct MacroTestCase {
  absl::string_view source;
  absl::string_view expected_ast;
};

class PrattParserMacroTest : public testing::TestWithParam<MacroTestCase> {};

TEST_P(PrattParserMacroTest, MacroExprExpander) {
  const MacroTestCase& test_case = GetParam();
  auto builder = NewPrattParserBuilder();
  ASSERT_OK_AND_ASSIGN(
      auto global_macro,
      Macro::Global("foo", 1,
                    [](MacroExprFactory& macro_factory,
                       absl::Span<Expr> args) -> std::optional<Expr> {
                      return macro_factory.NewCall("my_macro", std::move(args));
                    }));

  ASSERT_OK_AND_ASSIGN(
      auto receiver_macro,
      Macro::Receiver("bar", 2,
                      [](MacroExprFactory& macro_factory, Expr& target,
                         absl::Span<Expr> args) -> std::optional<Expr> {
                        return macro_factory.NewMemberCall(
                            "my_bar", std::move(target), std::move(args));
                      }));

  ASSERT_THAT(builder->AddMacro(global_macro), IsOk());
  ASSERT_THAT(builder->AddMacro(receiver_macro), IsOk());
  ASSERT_OK_AND_ASSIGN(auto parser, builder->Build());

  ASSERT_OK_AND_ASSIGN(auto source, cel::NewSource(test_case.source));
  ASSERT_OK_AND_ASSIGN(auto ast, parser->Parse(*source));

  EXPECT_THAT(ast->root_expr(), AstIs(test_case.expected_ast));
}

std::vector<MacroTestCase> GetMacroTestCases() {
  return {
      MacroTestCase{
          .source = "foo(x)",
          .expected_ast = R"(
              my_macro(
                x^#2:Expr.Ident#
              )^#3:Expr.Call#
            )",
      },
      MacroTestCase{
          .source = "x.bar(y, z)",
          .expected_ast = R"(
              x^#1:Expr.Ident#.my_bar(
                y^#3:Expr.Ident#,
                z^#4:Expr.Ident#
              )^#5:Expr.Call#
            )",
      },
      // Number of args doesn't match the macro definition
      MacroTestCase{
          .source = "foo(x, y)",
          .expected_ast = R"(
              foo(
                x^#2:Expr.Ident#,
                y^#3:Expr.Ident#
              )^#1:Expr.Call#
            )",
      },
      // Number of args doesn't match the macro definition
      MacroTestCase{
          .source = "x.bar(y)",
          .expected_ast = R"(
              x^#1:Expr.Ident#.bar(
                y^#3:Expr.Ident#
              )^#2:Expr.Call#
            )",
      },
      // No target provided for receiver macro
      MacroTestCase{
          .source = "bar(x, y)",
          .expected_ast = R"(
              bar(
                x^#2:Expr.Ident#,
                y^#3:Expr.Ident#
              )^#1:Expr.Call#
            )",
      },
      // Target provided for global macro
      MacroTestCase{
          .source = "x.foo(y)",
          .expected_ast = R"(
              x^#1:Expr.Ident#.foo(
                y^#3:Expr.Ident#
              )^#2:Expr.Call#
            )",
      },
      // Global macro not registered
      MacroTestCase{
          .source = "baz(x)",
          .expected_ast = R"(
              baz(
                x^#2:Expr.Ident#
              )^#1:Expr.Call#
            )",
      },
      // Receiver macro not registered
      MacroTestCase{
          .source = "x.baz(x)",
          .expected_ast = R"(
              x^#1:Expr.Ident#.baz(
                x^#3:Expr.Ident#
              )^#2:Expr.Call#
            )",
      },
      // has() macro
      MacroTestCase{
          .source = "has(message.field)",
          .expected_ast = R"(
              message^#2:Expr.Ident#.field~test-only~^#4:Expr.Select#
            )",
      },
  };
}

INSTANTIATE_TEST_SUITE_P(PrattParserMacroTest, PrattParserMacroTest,
                         testing::ValuesIn(GetMacroTestCases()),
                         TestName<MacroTestCase>);

TEST(PrattParserMacroCallsTest, MacroCallsDisabledByDefault) {
  cel::ParserOptions options;
  options.add_macro_calls = false;
  ASSERT_OK_AND_ASSIGN(auto ast, Parse("has(a.b)", options));
  EXPECT_TRUE(ast->source_info().macro_calls().empty());
}

TEST(PrattParserMacroCallsTest, GlobalMacroCallRecorded) {
  cel::ParserOptions options;
  options.add_macro_calls = true;
  ASSERT_OK_AND_ASSIGN(auto ast, Parse("has(a.b)", options));

  const auto& macro_calls = ast->source_info().macro_calls();
  EXPECT_FALSE(macro_calls.empty());
  EXPECT_TRUE(macro_calls.contains(ast->root_expr().id()));

  const auto& macro_call = macro_calls.at(ast->root_expr().id());
  EXPECT_THAT(macro_call, AstIs(R"(
      has(
        a^#2:Expr.Ident#.b^#3:Expr.Select#
      )^#0:Expr.Call#
    )"));
}

TEST(PrattParserMacroCallsTest, ReceiverMacroCallRecorded) {
  cel::ParserOptions options;
  options.add_macro_calls = true;
  ASSERT_OK_AND_ASSIGN(auto ast, Parse("[1, 2].exists(x, x > 0)", options));

  const auto& macro_calls = ast->source_info().macro_calls();
  EXPECT_FALSE(macro_calls.empty());
  EXPECT_TRUE(macro_calls.contains(ast->root_expr().id()));

  const auto& exists_macro_call = macro_calls.at(ast->root_expr().id());
  EXPECT_THAT(exists_macro_call, AstIs(R"(
      [
        1^#2:int64#,
        2^#3:int64#
      ]^#1:Expr.CreateList#.exists(
        x^#5:Expr.Ident#,
        _>_(
          x^#6:Expr.Ident#,
          0^#8:int64#
        )^#7:Expr.Call#
      )^#0:Expr.Call#
    )"));
}

TEST(PrattParserMacroCallsTest, NestedMacroCallsUseCopyAndReplaceReplacer) {
  cel::ParserOptions options;
  options.add_macro_calls = true;
  ASSERT_OK_AND_ASSIGN(auto ast, Parse("[1, 2].all(x, has(x.b))", options));

  const auto& root_expr = ast->root_expr();
  EXPECT_THAT(root_expr, AstIs(R"(
      __comprehension__(
        // Variable
        x,
        // Target
        [
          1^#2:int64#,
          2^#3:int64#
        ]^#1:Expr.CreateList#,
        // Accumulator
        @result,
        // Init
        true^#10:bool#,
        // LoopCondition
        @not_strictly_false(
          @result^#11:Expr.Ident#
        )^#12:Expr.Call#,
        // LoopStep
        _&&_(
          @result^#13:Expr.Ident#,
          x^#7:Expr.Ident#.b~test-only~^#9:Expr.Select#
        )^#14:Expr.Call#,
        // Result
        @result^#15:Expr.Ident#)^#16:Expr.Comprehension#
    )"));

  const auto& macro_calls = ast->source_info().macro_calls();
  // There should be 2 recorded macro calls: one for 'all', one for 'has'.
  EXPECT_EQ(macro_calls.size(), 2);

  int64_t all_macro_id = root_expr.id();
  EXPECT_TRUE(macro_calls.contains(all_macro_id));

  const auto& all_macro_call = macro_calls.at(all_macro_id);
  // The second argument of 'all' is the inner 'has(x.b)' call.
  // Because 'has(x.b)' was already expanded and recorded in macro_calls
  // it is represented as an UnspecifiedExpr holding the inner macro's ID.
  EXPECT_THAT(all_macro_call, AstIs(R"(
      [
        1^#2:int64#,
        2^#3:int64#
      ]^#1:Expr.CreateList#.all(
        x^#5:Expr.Ident#,
        ^#9:unspecified_expr#
      )^#0:Expr.Call#
    )"));

  const auto& has_call = all_macro_call.call_expr().args()[1];
  int64_t has_macro_id = has_call.id();
  EXPECT_EQ(has_macro_id, 9);  // ^#9:unspecified_expr#
  EXPECT_TRUE(macro_calls.contains(has_macro_id));

  const auto& has_macro_call = macro_calls.at(has_macro_id);
  EXPECT_THAT(has_macro_call, AstIs(R"(
      has(
        x^#7:Expr.Ident#.b^#8:Expr.Select#
      )^#0:Expr.Call#
    )"));
}

TEST(PrattParserMacroErrorTest, ReportError) {
  auto builder = NewPrattParserBuilder();
  ASSERT_OK_AND_ASSIGN(
      auto error_macro,
      Macro::Global("bad_macro", 1,
                    [](MacroExprFactory& macro_factory,
                       absl::Span<Expr> args) -> std::optional<Expr> {
                      return macro_factory.ReportError("custom macro error");
                    }));

  ASSERT_THAT(builder->AddMacro(error_macro), IsOk());
  ASSERT_OK_AND_ASSIGN(auto parser, builder->Build());

  ASSERT_OK_AND_ASSIGN(auto source, cel::NewSource("42 + bad_macro(x)"));
  std::vector<cel::ParseIssue> issues;
  auto ast = parser->Parse(*source, &issues);
  EXPECT_THAT(ast, StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_EQ(FormatIssues(*source, issues),
            "ERROR: <input>:1:6: custom macro error\n"
            " | 42 + bad_macro(x)\n"
            " | .....^");
}

TEST(PrattParserMacroErrorTest, ReportErrorAt) {
  auto builder = NewPrattParserBuilder();
  ASSERT_OK_AND_ASSIGN(
      auto error_at_macro,
      Macro::Global("bad_macro_at", 1,
                    [](MacroExprFactory& macro_factory,
                       absl::Span<Expr> args) -> std::optional<Expr> {
                      return macro_factory.ReportErrorAt(args[0],
                                                         "custom error at arg");
                    }));

  ASSERT_THAT(builder->AddMacro(error_at_macro), IsOk());
  ASSERT_OK_AND_ASSIGN(auto parser, builder->Build());

  ASSERT_OK_AND_ASSIGN(auto source, cel::NewSource("bad_macro_at(x)"));
  std::vector<cel::ParseIssue> issues;
  auto ast = parser->Parse(*source, &issues);
  EXPECT_THAT(ast, StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_EQ(FormatIssues(*source, issues),
            "ERROR: <input>:1:14: custom error at arg\n"
            " | bad_macro_at(x)\n"
            " | .............^");
}

TEST(PrattParserErrorRecoveryTest, ErrorRecoveryLimitZero) {
  cel::ParserOptions options;
  options.error_recovery_limit = 0;
  std::vector<cel::ParseIssue> issues;
  auto result = Parse("......", options, &issues);
  EXPECT_THAT(result, StatusIs(absl::StatusCode::kInvalidArgument));
  ASSERT_OK_AND_ASSIGN(auto source, cel::NewSource("......"));
  EXPECT_EQ(FormatIssues(*source, issues),
            "ERROR: <input>:-1:0: Error recovery limit (0) exceeded");
}

TEST(PrattParserErrorRecoveryTest, ErrorRecoveryLimitOne) {
  cel::ParserOptions options;
  options.error_recovery_limit = 1;
  std::vector<cel::ParseIssue> issues;
  auto result = Parse("......", options, &issues);
  EXPECT_THAT(result, StatusIs(absl::StatusCode::kInvalidArgument));
  ASSERT_OK_AND_ASSIGN(auto source, cel::NewSource("......"));
  EXPECT_EQ(FormatIssues(*source, issues),
            "ERROR: <input>:-1:0: Error recovery limit (1) exceeded\n"
            "ERROR: <input>:1:2: expected identifier\n"
            " | ......\n"
            " | .^");
}

}  // namespace
}  // namespace cel::parser_internal
