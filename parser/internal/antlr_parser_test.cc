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

#include "parser/internal/antlr_parser.h"

#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "common/ast.h"
#include "common/source.h"
#include "internal/status_macros.h"
#include "internal/testing.h"
#include "parser/options.h"
#include "parser/parser_interface.h"

namespace cel::parser_internal {
namespace {

using ::absl_testing::IsOk;
using ::testing::HasSubstr;
using ::testing::Not;

absl::StatusOr<std::unique_ptr<cel::Ast>> Parse(
    absl::string_view expression, absl::string_view description = "<input>",
    const cel::ParserOptions& options = cel::ParserOptions(),
    std::vector<cel::ParseIssue>* issues = nullptr) {
  std::unique_ptr<cel::ParserBuilder> builder = NewAntlrParserBuilder(options);
  CEL_ASSIGN_OR_RETURN(std::unique_ptr<cel::Parser> parser, builder->Build());
  CEL_ASSIGN_OR_RETURN(std::unique_ptr<cel::Source> source,
                       cel::NewSource(expression, std::string(description)));
  return parser->Parse(*source, issues);
}

TEST(AntlrParserTest, ErrorRecoveryLimits) {
  ParserOptions options;
  options.error_recovery_limit = 1;
  auto result = Parse("......", "", options);
  EXPECT_THAT(result, Not(IsOk()));
  EXPECT_EQ(result.status().message(),
            "ERROR: :1:1: Syntax error: More than 1 parse errors.\n | ......\n "
            "| ^\nERROR: :1:2: Syntax error: no viable alternative at input "
            "'..'\n | ......\n | .^");
}

TEST(AntlrParserTest, RecursionDepthExceeded) {
  ParserOptions options;
  // AST visitor will recurse a variable amount depending on the terms used in
  // the expression. This check occurs in the business logic converting the raw
  // Antlr parse tree into an Expr. There is a separate check (via a custom
  // listener) for AST depth while running the antlr generated parser.
  options.max_recursion_depth = 6;
  auto result = Parse("1 + 2 + 3 + 4 + 5 + 6 + 7", "", options);

  EXPECT_THAT(result, Not(IsOk()));
  EXPECT_THAT(result.status().message(),
              HasSubstr("Exceeded max recursion depth of 6 when parsing."));
}

}  // namespace
}  // namespace cel::parser_internal
