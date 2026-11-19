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

#include "conformance/env/normalize_yaml.h"

#include "internal/testing.h"

namespace cel {
namespace {

TEST(NormalizeYamlTest, NormalizesLineEndingsWhitespaceAndBlankLines) {
  EXPECT_EQ(NormalizeYaml(
                "variables:  \r\n\r\n  - name: x\t \r\n\n    type: bool  \n"),
            "variables:\n  - name: x\n    type: bool");
}

TEST(NormalizeYamlTest, UnquotesDoubleQuotedMappingScalars) {
  EXPECT_EQ(NormalizeYaml("variables:\n"
                          "  - name: \"x\"\n"
                          "    type: \"map<int, string>\"\n"),
            "variables:\n"
            "  - name: x\n"
            "    type: map<int, string>");
}

TEST(NormalizeYamlTest, DecodesDoubleQuotedEscapeSequences) {
  EXPECT_EQ(NormalizeYaml(R"(a: "hello\"world\\path\n\r\t\x")"),
            "a: hello\"world\\path\n\r\t\\x");
}

TEST(NormalizeYamlTest, UnquotesSingleQuotedMappingScalars) {
  EXPECT_EQ(NormalizeYaml("a: 'int'\nb: 'it''s'\nc: '~T\\.U'\nd: 'it's'\n"),
            "a: int\nb: it's\nc: ~T\\.U\nd: it's");
}

TEST(NormalizeYamlTest, DoubleAndSingleQuotedEscapesMatch) {
  EXPECT_EQ(NormalizeYaml(R"(type: "~T\\.U")"),
            NormalizeYaml(R"(type: '~T\.U')"));
}

TEST(NormalizeYamlTest, CollapsesExtraSpacesAfterColon) {
  EXPECT_EQ(NormalizeYaml("type:    \"int\"\n"), "type: int");
}

TEST(NormalizeYamlTest, UnquotesSequenceItemScalars) {
  EXPECT_EQ(NormalizeYaml("params:\n"
                          "  - \"int\"\n"
                          "  - 'bool'\n"
                          "  -   \"string\"\n"
                          "  - double\n"),
            "params:\n"
            "  - int\n"
            "  - bool\n"
            "  - string\n"
            "  - double");
}

TEST(NormalizeYamlTest, HandlesEmptyAndUnclosedQuotes) {
  EXPECT_EQ(NormalizeYaml("a: \"\"\nb: ''\nc: \"\nd: '\ne: \"unclosed\n"),
            "a: \nb: \nc: \"\nd: '\ne: \"unclosed");
}

TEST(NormalizeYamlTest, PreservesNonSequenceStructuralLines) {
  EXPECT_EQ(NormalizeYaml("x\nab   \"c\"\n"), "x\nab   \"c\"");
}

}  // namespace
}  // namespace cel
