// Copyright 2023 Google LLC
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

#include "common/source.h"

#include <cstdint>
#include <optional>

#include "absl/status/status.h"
#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"
#include "internal/testing.h"

namespace cel {
namespace {

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Ne;
using ::testing::Optional;

TEST(SourceRange, Default) {
  SourceRange range;
  EXPECT_EQ(range.begin, -1);
  EXPECT_EQ(range.end, -1);
}

TEST(SourceRange, Equality) {
  EXPECT_THAT((SourceRange{}), (Eq(SourceRange{})));
  EXPECT_THAT((SourceRange{0, 1}), (Ne(SourceRange{0, 0})));
}

TEST(SourceLocation, Default) {
  SourceLocation location;
  EXPECT_EQ(location.line, -1);
  EXPECT_EQ(location.column, -1);
}

TEST(SourceLocation, Equality) {
  EXPECT_THAT((SourceLocation{}), (Eq(SourceLocation{})));
  EXPECT_THAT((SourceLocation{1, 1}), (Ne(SourceLocation{1, 0})));
}

TEST(StringSource, Description) {
  ASSERT_OK_AND_ASSIGN(
      auto source,
      NewSource("c.d &&\n\t b.c.arg(10) &&\n\t test(10)", "offset-test"));

  EXPECT_THAT(source->description(), Eq("offset-test"));
}

TEST(StringSource, Content) {
  ASSERT_OK_AND_ASSIGN(
      auto source,
      NewSource("c.d &&\n\t b.c.arg(10) &&\n\t test(10)", "offset-test"));

  EXPECT_THAT(source->content().ToString(),
              Eq("c.d &&\n\t b.c.arg(10) &&\n\t test(10)"));
}

TEST(StringSource, PositionAndLocation) {
  ASSERT_OK_AND_ASSIGN(
      auto source,
      NewSource("c.d &&\n\t b.c.arg(10) &&\n\t test(10)", "offset-test"));

  EXPECT_THAT(source->line_offsets(), ElementsAre(7, 24, 35));

  auto start = source->GetPosition(SourceLocation{int32_t{1}, int32_t{2}});
  auto end = source->GetPosition(SourceLocation{int32_t{3}, int32_t{2}});
  ASSERT_TRUE(start.has_value());
  ASSERT_TRUE(end.has_value());

  EXPECT_THAT(source->GetLocation(*start),
              Optional(Eq(SourceLocation{int32_t{1}, int32_t{2}})));
  EXPECT_THAT(source->GetLocation(*end),
              Optional(Eq(SourceLocation{int32_t{3}, int32_t{2}})));
  EXPECT_THAT(source->GetLocation(-1), Eq(std::nullopt));

  EXPECT_THAT(source->content().ToString(*start, *end),
              Eq("d &&\n\t b.c.arg(10) &&\n\t "));

  EXPECT_THAT(source->GetPosition(SourceLocation{int32_t{0}, int32_t{0}}),
              Eq(std::nullopt));
  EXPECT_THAT(source->GetPosition(SourceLocation{int32_t{1}, int32_t{-1}}),
              Eq(std::nullopt));
  EXPECT_THAT(source->GetPosition(SourceLocation{int32_t{4}, int32_t{0}}),
              Eq(std::nullopt));
}

TEST(StringSource, SnippetSingle) {
  ASSERT_OK_AND_ASSIGN(auto source, NewSource("hello, world", "one-line-test"));

  EXPECT_THAT(source->Snippet(1), Optional(Eq("hello, world")));
  EXPECT_THAT(source->Snippet(2), Eq(std::nullopt));
}

TEST(StringSource, SnippetMulti) {
  ASSERT_OK_AND_ASSIGN(auto source,
                       NewSource("hello\nworld\nmy\nbub\n", "four-line-test"));

  EXPECT_THAT(source->Snippet(0), Eq(std::nullopt));
  EXPECT_THAT(source->Snippet(1), Optional(Eq("hello")));
  EXPECT_THAT(source->Snippet(2), Optional(Eq("world")));
  EXPECT_THAT(source->Snippet(3), Optional(Eq("my")));
  EXPECT_THAT(source->Snippet(4), Optional(Eq("bub")));
  EXPECT_THAT(source->Snippet(5), Optional(Eq("")));
  EXPECT_THAT(source->Snippet(6), Eq(std::nullopt));
}

TEST(CordSource, Description) {
  ASSERT_OK_AND_ASSIGN(
      auto source,
      NewSource(absl::Cord("c.d &&\n\t b.c.arg(10) &&\n\t test(10)"),
                "offset-test"));

  EXPECT_THAT(source->description(), Eq("offset-test"));
}

TEST(CordSource, Content) {
  ASSERT_OK_AND_ASSIGN(
      auto source,
      NewSource(absl::Cord("c.d &&\n\t b.c.arg(10) &&\n\t test(10)"),
                "offset-test"));

  EXPECT_THAT(source->content().ToString(),
              Eq("c.d &&\n\t b.c.arg(10) &&\n\t test(10)"));
}

TEST(CordSource, PositionAndLocation) {
  ASSERT_OK_AND_ASSIGN(
      auto source,
      NewSource(absl::Cord("c.d &&\n\t b.c.arg(10) &&\n\t test(10)"),
                "offset-test"));

  EXPECT_THAT(source->line_offsets(), ElementsAre(7, 24, 35));

  auto start = source->GetPosition(SourceLocation{int32_t{1}, int32_t{2}});
  auto end = source->GetPosition(SourceLocation{int32_t{3}, int32_t{2}});
  ASSERT_TRUE(start.has_value());
  ASSERT_TRUE(end.has_value());

  EXPECT_THAT(source->GetLocation(*start),
              Optional(Eq(SourceLocation{int32_t{1}, int32_t{2}})));
  EXPECT_THAT(source->GetLocation(*end),
              Optional(Eq(SourceLocation{int32_t{3}, int32_t{2}})));
  EXPECT_THAT(source->GetLocation(-1), Eq(std::nullopt));

  EXPECT_THAT(source->content().ToString(*start, *end),
              Eq("d &&\n\t b.c.arg(10) &&\n\t "));

  EXPECT_THAT(source->GetPosition(SourceLocation{int32_t{0}, int32_t{0}}),
              Eq(std::nullopt));
  EXPECT_THAT(source->GetPosition(SourceLocation{int32_t{1}, int32_t{-1}}),
              Eq(std::nullopt));
  EXPECT_THAT(source->GetPosition(SourceLocation{int32_t{4}, int32_t{0}}),
              Eq(std::nullopt));
}

TEST(CordSource, SnippetSingle) {
  ASSERT_OK_AND_ASSIGN(auto source,
                       NewSource(absl::Cord("hello, world"), "one-line-test"));

  EXPECT_THAT(source->Snippet(1), Optional(Eq("hello, world")));
  EXPECT_THAT(source->Snippet(2), Eq(std::nullopt));
}

TEST(CordSource, SnippetMulti) {
  ASSERT_OK_AND_ASSIGN(
      auto source,
      NewSource(absl::Cord("hello\nworld\nmy\nbub\n"), "four-line-test"));

  EXPECT_THAT(source->Snippet(0), Eq(std::nullopt));
  EXPECT_THAT(source->Snippet(1), Optional(Eq("hello")));
  EXPECT_THAT(source->Snippet(2), Optional(Eq("world")));
  EXPECT_THAT(source->Snippet(3), Optional(Eq("my")));
  EXPECT_THAT(source->Snippet(4), Optional(Eq("bub")));
  EXPECT_THAT(source->Snippet(5), Optional(Eq("")));
  EXPECT_THAT(source->Snippet(6), Eq(std::nullopt));
}

TEST(Source, DisplayErrorLocationBasic) {
  ASSERT_OK_AND_ASSIGN(auto source, NewSource("'Hello' +\n  'world'"));

  SourceLocation location{/*line=*/2, /*column=*/3};

  EXPECT_EQ(source->DisplayErrorLocation(location),
            "\n |   'world'"
            "\n | ...^");
}

TEST(Source, DisplayErrorLocationOutOfRange) {
  ASSERT_OK_AND_ASSIGN(auto source, NewSource("'Hello world!'"));

  SourceLocation location{/*line=*/3, /*column=*/3};

  EXPECT_EQ(source->DisplayErrorLocation(location), "");
}

TEST(Source, DisplayErrorLocationTabsShortened) {
  ASSERT_OK_AND_ASSIGN(auto source, NewSource("'Hello' +\n\t\t'world!'"));

  SourceLocation location{/*line=*/2, /*column=*/4};

  EXPECT_EQ(source->DisplayErrorLocation(location),
            "\n |   'world!'"
            "\n | ....^");
}

TEST(Source, DisplayErrorLocationFullWidth) {
  ASSERT_OK_AND_ASSIGN(auto source, NewSource("'Ｈｅｌｌｏ'"));

  SourceLocation location{/*line=*/1, /*column=*/2};

  EXPECT_EQ(source->DisplayErrorLocation(location),
            "\n | 'Ｈｅｌｌｏ'"
            "\n | .．＾");
}

TEST(SourceSubrange, Description) {
  ASSERT_OK_AND_ASSIGN(auto source, NewSource("hello world", "subrange-test"));
  SourceSubrange subrange(*source, SourceRange{0, 5});
  EXPECT_THAT(subrange.description(), Eq("subrange-test"));
}

TEST(SourceSubrange, Content) {
  ASSERT_OK_AND_ASSIGN(auto source, NewSource("hello world", "subrange-test"));
  SourceSubrange subrange(*source, SourceRange{6, 11});
  EXPECT_THAT(subrange.content().ToString(), Eq("world"));
}

TEST(SourceSubrange, ContentEmpty) {
  ASSERT_OK_AND_ASSIGN(auto source, NewSource("hello world", "subrange-test"));
  SourceSubrange subrange(*source, SourceRange{5, 5});
  EXPECT_THAT(subrange.content().ToString(), Eq(""));
}

TEST(SourceSubrange, LineOffsetsNoNewlines) {
  ASSERT_OK_AND_ASSIGN(auto source,
                       NewSource("hello\nworld\n", "subrange-test"));
  SourceSubrange subrange(*source, SourceRange{0, 5});
  EXPECT_THAT(subrange.line_offsets(), ElementsAre(6));
}

TEST(SourceSubrange, LineOffsetsWithNewlines) {
  ASSERT_OK_AND_ASSIGN(auto source,
                       NewSource("hello\nworld\ncel", "subrange-test"));
  SourceSubrange subrange(*source, SourceRange{0, 11});
  EXPECT_THAT(subrange.line_offsets(), ElementsAre(6, 12));
}

TEST(SourceSubrange, LineOffsetsMiddleSubrange) {
  ASSERT_OK_AND_ASSIGN(auto source,
                       NewSource("hello\nworld\ncel\ncpp", "subrange-test"));
  SourceSubrange subrange(*source, SourceRange{6, 15});
  EXPECT_THAT(subrange.line_offsets(), ElementsAre(6, 10));
}

TEST(StringSource, CodepointLimitExceeded) {
  SourceOptions options;
  options.max_codepoint_size = 5;

  EXPECT_THAT(
      NewSource("123456", "test", options),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInvalidArgument,
          ::testing::HasSubstr("expression is larger than codepoint limit 5")));

  ASSERT_OK_AND_ASSIGN(auto source, NewSource("12345", "test", options));
  EXPECT_THAT(source->content().ToString(), ::testing::Eq("12345"));
}

TEST(StringSource, CodepointLimitMultibyteUtf8) {
  SourceOptions options;
  options.max_codepoint_size = 5;

  // "Ｈｅｌｌｏ" consists of 5 full-width Unicode characters (15 bytes in
  // UTF-8).
  ASSERT_OK_AND_ASSIGN(auto source, NewSource("Ｈｅｌｌｏ", "test", options));
  EXPECT_THAT(source->content().ToString(), ::testing::Eq("Ｈｅｌｌｏ"));

  options.max_codepoint_size = 4;
  EXPECT_THAT(
      NewSource("Ｈｅｌｌｏ", "test", options),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInvalidArgument,
          ::testing::HasSubstr("expression is larger than codepoint limit 4")));
}

TEST(CordSource, CodepointLimitExceeded) {
  SourceOptions options;
  options.max_codepoint_size = 5;

  EXPECT_THAT(
      NewSource(absl::Cord("123456"), "test", options),
      ::absl_testing::StatusIs(
          absl::StatusCode::kInvalidArgument,
          ::testing::HasSubstr("expression is larger than codepoint limit 5")));

  ASSERT_OK_AND_ASSIGN(SourcePtr source,
                       NewSource(absl::Cord("12345"), "test", options));
  EXPECT_THAT(source->content().ToString(), ::testing::Eq("12345"));
}

TEST(Source, GetPositionFromByteOffsetAscii) {
  ASSERT_OK_AND_ASSIGN(SourcePtr empty_source, NewSource(""));
  EXPECT_THAT(empty_source->GetPositionFromByteOffset(-1), Eq(std::nullopt));
  EXPECT_THAT(empty_source->GetPositionFromByteOffset(0), Optional(Eq(0)));
  EXPECT_THAT(empty_source->GetPositionFromByteOffset(1), Eq(std::nullopt));

  ASSERT_OK_AND_ASSIGN(SourcePtr source, NewSource("hello"));
  EXPECT_THAT(source->GetPositionFromByteOffset(-1), Eq(std::nullopt));
  for (SourcePosition i = 0; i <= 5; ++i) {
    EXPECT_THAT(source->GetPositionFromByteOffset(i), Optional(Eq(i)));
  }
  EXPECT_THAT(source->GetPositionFromByteOffset(6), Eq(std::nullopt));
}

TEST(Source, GetPositionFromByteOffsetLatin1) {
  // "a£b": 'a' (1 byte: 0), '£' (2 bytes: 1..2), 'b' (1 byte: 3). Total 4
  // bytes, 3 codepoints.
  ASSERT_OK_AND_ASSIGN(SourcePtr source, NewSource("a£b"));
  EXPECT_THAT(source->GetPositionFromByteOffset(-1), Eq(std::nullopt));
  EXPECT_THAT(source->GetPositionFromByteOffset(0), Optional(Eq(0)));
  EXPECT_THAT(source->GetPositionFromByteOffset(1), Optional(Eq(1)));
  EXPECT_THAT(source->GetPositionFromByteOffset(2), Optional(Eq(1)));
  EXPECT_THAT(source->GetPositionFromByteOffset(3), Optional(Eq(2)));
  EXPECT_THAT(source->GetPositionFromByteOffset(4), Optional(Eq(3)));
  EXPECT_THAT(source->GetPositionFromByteOffset(5), Eq(std::nullopt));
}

TEST(Source, GetPositionFromByteOffsetBasicPlane) {
  // "a€b": 'a' (1 byte: 0), '€' (3 bytes: 1..3), 'b' (1 byte: 4). Total 5
  // bytes, 3 codepoints.
  ASSERT_OK_AND_ASSIGN(SourcePtr source, NewSource("a€b"));
  EXPECT_THAT(source->GetPositionFromByteOffset(-1), Eq(std::nullopt));
  EXPECT_THAT(source->GetPositionFromByteOffset(0), Optional(Eq(0)));
  EXPECT_THAT(source->GetPositionFromByteOffset(1), Optional(Eq(1)));
  EXPECT_THAT(source->GetPositionFromByteOffset(2), Optional(Eq(1)));
  EXPECT_THAT(source->GetPositionFromByteOffset(3), Optional(Eq(1)));
  EXPECT_THAT(source->GetPositionFromByteOffset(4), Optional(Eq(2)));
  EXPECT_THAT(source->GetPositionFromByteOffset(5), Optional(Eq(3)));
  EXPECT_THAT(source->GetPositionFromByteOffset(6), Eq(std::nullopt));
}

TEST(Source, GetPositionFromByteOffsetSupplementalPlaneAndMixed) {
  // "a£€𐍈b":
  // cp 0: 'a' (byte 0, 1 byte)
  // cp 1: '£' (bytes 1..2, 2 bytes)
  // cp 2: '€' (bytes 3..5, 3 bytes)
  // cp 3: '𐍈' (bytes 6..9, 4 bytes)
  // cp 4: 'b' (byte 10, 1 byte)
  // Total: 11 bytes, 5 codepoints.
  constexpr absl::string_view kText = "a£€𐍈b";
  ASSERT_OK_AND_ASSIGN(SourcePtr source, NewSource(kText));
  ASSERT_OK_AND_ASSIGN(SourcePtr cord_source, NewSource(absl::Cord(kText)));

  for (const Source* s : {source.get(), cord_source.get()}) {
    EXPECT_THAT(s->GetPositionFromByteOffset(-1), Eq(std::nullopt));
    EXPECT_THAT(s->GetPositionFromByteOffset(0), Optional(Eq(0)));
    EXPECT_THAT(s->GetPositionFromByteOffset(1), Optional(Eq(1)));
    EXPECT_THAT(s->GetPositionFromByteOffset(2), Optional(Eq(1)));
    EXPECT_THAT(s->GetPositionFromByteOffset(3), Optional(Eq(2)));
    EXPECT_THAT(s->GetPositionFromByteOffset(4), Optional(Eq(2)));
    EXPECT_THAT(s->GetPositionFromByteOffset(5), Optional(Eq(2)));
    EXPECT_THAT(s->GetPositionFromByteOffset(6), Optional(Eq(3)));
    EXPECT_THAT(s->GetPositionFromByteOffset(7), Optional(Eq(3)));
    EXPECT_THAT(s->GetPositionFromByteOffset(8), Optional(Eq(3)));
    EXPECT_THAT(s->GetPositionFromByteOffset(9), Optional(Eq(3)));
    EXPECT_THAT(s->GetPositionFromByteOffset(10), Optional(Eq(4)));
    EXPECT_THAT(s->GetPositionFromByteOffset(11), Optional(Eq(5)));
    EXPECT_THAT(s->GetPositionFromByteOffset(12), Eq(std::nullopt));
  }
}

TEST(Source, GetPositionFromByteOffsetStartsAndEndsWithMultibyte) {
  // "£a€b𐍈":
  // cp 0: '£' (bytes 0..1, 2 bytes)
  // cp 1: 'a' (byte 2, 1 byte)
  // cp 2: '€' (bytes 3..5, 3 bytes)
  // cp 3: 'b' (byte 6, 1 byte)
  // cp 4: '𐍈' (bytes 7..10, 4 bytes)
  // Total: 11 bytes, 5 codepoints.
  constexpr absl::string_view kText = "£a€b𐍈";
  ASSERT_OK_AND_ASSIGN(SourcePtr source, NewSource(kText));
  ASSERT_OK_AND_ASSIGN(SourcePtr cord_source, NewSource(absl::Cord(kText)));

  for (const Source* s : {source.get(), cord_source.get()}) {
    EXPECT_THAT(s->GetPositionFromByteOffset(-1), Eq(std::nullopt));
    EXPECT_THAT(s->GetPositionFromByteOffset(0), Optional(Eq(0)));
    EXPECT_THAT(s->GetPositionFromByteOffset(1), Optional(Eq(0)));
    EXPECT_THAT(s->GetPositionFromByteOffset(2), Optional(Eq(1)));
    EXPECT_THAT(s->GetPositionFromByteOffset(3), Optional(Eq(2)));
    EXPECT_THAT(s->GetPositionFromByteOffset(4), Optional(Eq(2)));
    EXPECT_THAT(s->GetPositionFromByteOffset(5), Optional(Eq(2)));
    EXPECT_THAT(s->GetPositionFromByteOffset(6), Optional(Eq(3)));
    EXPECT_THAT(s->GetPositionFromByteOffset(7), Optional(Eq(4)));
    EXPECT_THAT(s->GetPositionFromByteOffset(8), Optional(Eq(4)));
    EXPECT_THAT(s->GetPositionFromByteOffset(9), Optional(Eq(4)));
    EXPECT_THAT(s->GetPositionFromByteOffset(10), Optional(Eq(4)));
    EXPECT_THAT(s->GetPositionFromByteOffset(11), Optional(Eq(5)));
    EXPECT_THAT(s->GetPositionFromByteOffset(12), Eq(std::nullopt));
  }
}

TEST(SourceSubrange, GetPositionFromByteOffsetReturnsNullopt) {
  ASSERT_OK_AND_ASSIGN(SourcePtr source, NewSource("hello world"));
  SourceSubrange subrange(*source, SourceRange{0, 5});
  EXPECT_THAT(subrange.GetPositionFromByteOffset(0), Eq(std::nullopt));
}

}  // namespace
}  // namespace cel
