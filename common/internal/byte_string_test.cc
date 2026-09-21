// Copyright 2024 Google LLC
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

#include "common/internal/byte_string.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <utility>

#include "absl/base/no_destructor.h"
#include "absl/hash/hash.h"
#include "absl/strings/cord.h"
#include "absl/strings/cord_test_helpers.h"
#include "absl/strings/string_view.h"
#include "internal/testing.h"
#include "google/protobuf/arena.h"

namespace cel::common_internal {

struct ByteStringTestFriend {
  static ByteStringKind GetKind(const ByteString& byte_string) {
    return byte_string.GetKind();
  }
};

namespace {

using ::testing::_;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::Optional;
using ::testing::SizeIs;
using ::testing::TestWithParam;

TEST(ByteStringKind, Ostream) {
  {
    std::ostringstream out;
    out << ByteStringKind::kSmall;
    EXPECT_EQ(out.str(), "SMALL");
  }
  {
    std::ostringstream out;
    out << ByteStringKind::kMedium;
    EXPECT_EQ(out.str(), "MEDIUM");
  }
  {
    std::ostringstream out;
    out << ByteStringKind::kLarge;
    EXPECT_EQ(out.str(), "LARGE");
  }
}

class ByteStringTest : public ByteStringTestFriend, public ::testing::Test {
 public:
  google::protobuf::Arena* GetArena() { return &arena_; }

 private:
  google::protobuf::Arena arena_;
};

absl::string_view GetSmallStringView() {
  static constexpr absl::string_view small = "A small string!";
  return small.substr(0, std::min(kSmallByteStringCapacity, small.size()));
}

std::string GetSmallString() { return std::string(GetSmallStringView()); }

absl::Cord GetSmallCord() {
  static const absl::NoDestructor<absl::Cord> small(GetSmallStringView());
  return *small;
}

absl::string_view GetMediumStringView() {
  static constexpr absl::string_view medium =
      "A string that is too large for the small string optimization!";
  return medium;
}

std::string GetMediumString() { return std::string(GetMediumStringView()); }

const absl::Cord& GetMediumOrLargeCord() {
  static const absl::NoDestructor<absl::Cord> medium_or_large(
      GetMediumStringView());
  return *medium_or_large;
}

const absl::Cord& GetMediumOrLargeFragmentedCord() {
  static const absl::NoDestructor<absl::Cord> medium_or_large(
      absl::MakeFragmentedCord(
          {GetMediumStringView().substr(0, kSmallByteStringCapacity),
           GetMediumStringView().substr(kSmallByteStringCapacity)}));
  return *medium_or_large;
}

TEST_F(ByteStringTest, Default) {
  ByteString byte_string = ByteString();
  EXPECT_THAT(byte_string, SizeIs(0));
  EXPECT_THAT(byte_string, IsEmpty());
  EXPECT_EQ(GetKind(byte_string), ByteStringKind::kSmall);
}

TEST_F(ByteStringTest, ConstructNullDataStringView) {
  ByteString byte_string = ByteString::From(absl::string_view(), GetArena());
  EXPECT_THAT(byte_string, IsEmpty());
  EXPECT_EQ(byte_string.GetArena(), GetArena());
}

TEST_F(ByteStringTest, ConstructSmallCString) {
  ByteString byte_string =
      ByteString::From(GetSmallString().c_str(), GetArena());
  EXPECT_THAT(byte_string, SizeIs(GetSmallStringView().size()));
  EXPECT_THAT(byte_string, Not(IsEmpty()));
  EXPECT_EQ(byte_string, GetSmallStringView());
  EXPECT_EQ(GetKind(byte_string), ByteStringKind::kSmall);
  EXPECT_EQ(byte_string.GetArena(), GetArena());
}

TEST_F(ByteStringTest, ConstructMediumCString) {
  ByteString byte_string =
      ByteString::From(GetMediumString().c_str(), GetArena());
  EXPECT_THAT(byte_string, SizeIs(GetMediumStringView().size()));
  EXPECT_THAT(byte_string, Not(IsEmpty()));
  EXPECT_EQ(byte_string, GetMediumStringView());
  EXPECT_EQ(GetKind(byte_string), ByteStringKind::kMedium);
  EXPECT_EQ(byte_string.GetArena(), GetArena());
}

TEST_F(ByteStringTest, ConstructSmallRValueString) {
  ByteString byte_string = ByteString::From(GetSmallString(), GetArena());
  EXPECT_THAT(byte_string, SizeIs(GetSmallStringView().size()));
  EXPECT_THAT(byte_string, Not(IsEmpty()));
  EXPECT_EQ(byte_string, GetSmallStringView());
  EXPECT_EQ(GetKind(byte_string), ByteStringKind::kSmall);
  EXPECT_EQ(byte_string.GetArena(), GetArena());
}

TEST_F(ByteStringTest, ConstructSmallLValueString) {
  ByteString byte_string = ByteString::From(
      static_cast<const std::string&>(GetSmallString()), GetArena());
  EXPECT_THAT(byte_string, SizeIs(GetSmallStringView().size()));
  EXPECT_THAT(byte_string, Not(IsEmpty()));
  EXPECT_EQ(byte_string, GetSmallStringView());
  EXPECT_EQ(GetKind(byte_string), ByteStringKind::kSmall);
  EXPECT_EQ(byte_string.GetArena(), GetArena());
}

TEST_F(ByteStringTest, ConstructMediumRValueString) {
  ByteString byte_string = ByteString::From(GetMediumString(), GetArena());
  EXPECT_THAT(byte_string, SizeIs(GetMediumStringView().size()));
  EXPECT_THAT(byte_string, Not(IsEmpty()));
  EXPECT_EQ(byte_string, GetMediumStringView());
  EXPECT_EQ(GetKind(byte_string), ByteStringKind::kMedium);
  EXPECT_EQ(byte_string.GetArena(), GetArena());
}

TEST_F(ByteStringTest, ConstructMediumLValueString) {
  ByteString byte_string = ByteString::From(
      static_cast<const std::string&>(GetMediumString()), GetArena());
  EXPECT_THAT(byte_string, SizeIs(GetMediumStringView().size()));
  EXPECT_THAT(byte_string, Not(IsEmpty()));
  EXPECT_EQ(byte_string, GetMediumStringView());
  EXPECT_EQ(GetKind(byte_string), ByteStringKind::kMedium);
  EXPECT_EQ(byte_string.GetArena(), GetArena());
}

TEST_F(ByteStringTest, ConstructSmallCord) {
  ByteString byte_string = ByteString::From(GetSmallCord(), GetArena());
  EXPECT_THAT(byte_string, SizeIs(GetSmallStringView().size()));
  EXPECT_THAT(byte_string, Not(IsEmpty()));
  EXPECT_EQ(byte_string, GetSmallStringView());
  EXPECT_EQ(GetKind(byte_string), ByteStringKind::kSmall);
  EXPECT_EQ(byte_string.GetArena(), GetArena());
}

TEST_F(ByteStringTest, ConstructMediumOrLargeCord) {
  ByteString byte_string = ByteString::From(GetMediumOrLargeCord(), GetArena());
  EXPECT_THAT(byte_string, SizeIs(GetMediumStringView().size()));
  EXPECT_THAT(byte_string, Not(IsEmpty()));
  EXPECT_EQ(byte_string, GetMediumStringView());
  EXPECT_EQ(GetKind(byte_string), ByteStringKind::kLarge);
  EXPECT_EQ(byte_string.GetArena(), GetArena());
}

TEST_F(ByteStringTest, BorrowedArenaSmallString) {
  ByteString byte_string = ByteString::Wrap(GetSmallStringView(), GetArena());
  EXPECT_EQ(ByteStringTestFriend::GetKind(byte_string),
            ByteStringKind::kMedium);
  EXPECT_EQ(byte_string.GetArena(), GetArena());
  EXPECT_EQ(byte_string, GetSmallStringView());
}

TEST_F(ByteStringTest, BorrowedArenaMediumString) {
  ByteString byte_string = ByteString::Wrap(GetMediumStringView(), GetArena());
  EXPECT_EQ(ByteStringTestFriend::GetKind(byte_string),
            ByteStringKind::kMedium);
  EXPECT_EQ(byte_string.GetArena(), GetArena());
  EXPECT_EQ(byte_string, GetMediumStringView());
}

TEST_F(ByteStringTest, BorrowedArenaCord) {
  ByteString byte_string =
      ByteString::Wrap(&GetMediumOrLargeCord(), GetArena());
  EXPECT_EQ(ByteStringTestFriend::GetKind(byte_string), ByteStringKind::kLarge);
  EXPECT_EQ(byte_string.GetArena(), GetArena());
  EXPECT_EQ(byte_string, GetMediumOrLargeCord());
}

TEST_F(ByteStringTest, TryFlatSmall) {
  ByteString byte_string = ByteString::From(GetSmallStringView(), GetArena());
  EXPECT_EQ(GetKind(byte_string), ByteStringKind::kSmall);
  EXPECT_THAT(byte_string.TryFlat(), Optional(GetSmallStringView()));
  EXPECT_EQ(GetKind(byte_string), ByteStringKind::kSmall);
}

TEST_F(ByteStringTest, TryFlatMedium) {
  ByteString byte_string = ByteString::From(GetMediumStringView(), GetArena());
  EXPECT_EQ(GetKind(byte_string), ByteStringKind::kMedium);
  EXPECT_THAT(byte_string.TryFlat(), Optional(GetMediumStringView()));
  EXPECT_EQ(GetKind(byte_string), ByteStringKind::kMedium);
}

TEST_F(ByteStringTest, TryFlatLarge) {
  if (GetArena() != nullptr) {
    GTEST_SKIP();
  }
  ByteString byte_string =
      ByteString::From(GetMediumOrLargeFragmentedCord(), GetArena());
  EXPECT_EQ(GetKind(byte_string), ByteStringKind::kLarge);
  EXPECT_THAT(byte_string.TryFlat(), Eq(std::nullopt));
  EXPECT_EQ(GetKind(byte_string), ByteStringKind::kLarge);
}

TEST_F(ByteStringTest, Equals) {
  ByteString byte_string = ByteString::From(GetMediumOrLargeCord(), GetArena());
  EXPECT_TRUE(byte_string.Equals(GetMediumStringView()));
}

TEST_F(ByteStringTest, Compare) {
  ByteString byte_string = ByteString::From(GetMediumOrLargeCord(), GetArena());
  EXPECT_EQ(byte_string.Compare(GetMediumStringView()), 0);
  EXPECT_EQ(byte_string.Compare(GetMediumOrLargeCord()), 0);
}

TEST_F(ByteStringTest, StartsWith) {
  ByteString byte_string = ByteString::From(GetMediumOrLargeCord(), GetArena());
  EXPECT_TRUE(byte_string.StartsWith(
      GetMediumStringView().substr(0, kSmallByteStringCapacity)));
  EXPECT_TRUE(byte_string.StartsWith(
      GetMediumOrLargeCord().Subcord(0, kSmallByteStringCapacity)));
}

TEST_F(ByteStringTest, EndsWith) {
  ByteString byte_string = ByteString::From(GetMediumOrLargeCord(), GetArena());
  EXPECT_TRUE(byte_string.EndsWith(
      GetMediumStringView().substr(kSmallByteStringCapacity)));
  EXPECT_TRUE(byte_string.EndsWith(GetMediumOrLargeCord().Subcord(
      kSmallByteStringCapacity,
      GetMediumOrLargeCord().size() - kSmallByteStringCapacity)));
}

TEST_F(ByteStringTest, Find) {
  ByteString byte_string = ByteString::From(GetMediumStringView(), GetArena());

  // Find string_view
  EXPECT_THAT(byte_string.Find("A string"), Optional(0));
  EXPECT_THAT(
      byte_string.Find("small string optimization!"),
      Optional(GetMediumStringView().find("small string optimization!")));
  EXPECT_THAT(byte_string.Find("not found"), Eq(std::nullopt));
  EXPECT_THAT(byte_string.Find(""), Optional(0));
  EXPECT_THAT(byte_string.Find("", 3), Optional(3));
  EXPECT_THAT(byte_string.Find("A string", 1), Eq(std::nullopt));

  // Find cord
  EXPECT_THAT(byte_string.Find(absl::Cord("A string")), Optional(0));
  EXPECT_THAT(
      byte_string.Find(absl::Cord("small string optimization!")),
      Optional(GetMediumStringView().find("small string optimization!")));
  EXPECT_THAT(
      byte_string.Find(absl::MakeFragmentedCord(
          {"A string", " that is too large for the small string optimization!",
           " extra"})),
      Eq(std::nullopt));
  EXPECT_THAT(byte_string.Find(GetMediumOrLargeFragmentedCord()), Optional(0));
  EXPECT_THAT(byte_string.Find(absl::Cord("not found")), Eq(std::nullopt));
  EXPECT_THAT(byte_string.Find(absl::Cord("")), Optional(0));
  EXPECT_THAT(byte_string.Find(absl::Cord(""), 3), Optional(3));
}

TEST_F(ByteStringTest, FindEdgeCases) {
  ByteString empty_byte_string;
  EXPECT_THAT(empty_byte_string.Find("a"), Eq(std::nullopt));
  EXPECT_THAT(empty_byte_string.Find(""), Optional(0));
  ByteString cord_byte_string =
      ByteString::From(GetMediumOrLargeCord(), GetArena());
  EXPECT_THAT(cord_byte_string.Find("not found"), Eq(std::nullopt));
  ByteString byte_string = ByteString::From(GetMediumStringView(), GetArena());

  // Needle longer than haystack.
  EXPECT_THAT(byte_string.Find(std::string(byte_string.size() + 1, 'a')),
              Eq(std::nullopt));

  // Needle at the end.
  absl::string_view suffix = "optimization!";
  EXPECT_THAT(byte_string.Find(suffix),
              Optional(byte_string.size() - suffix.size()));

  // pos at the end.
  EXPECT_THAT(byte_string.Find("a", byte_string.size()), Eq(std::nullopt));
  EXPECT_THAT(byte_string.Find("", byte_string.size()),
              Optional(byte_string.size()));

  // Search in a cord-backed ByteString with pos > 0.
  EXPECT_THAT(cord_byte_string.Find("string", 1),
              Optional(GetMediumStringView().find("string", 1)));

  // Needle at the end of a cord-backed ByteString.
  absl::string_view suffix_sv = "optimization!";
  EXPECT_THAT(cord_byte_string.Find(suffix_sv),
              Optional(cord_byte_string.size() - suffix_sv.size()));
  EXPECT_THAT(cord_byte_string.Find(absl::Cord(suffix_sv)),
              Optional(cord_byte_string.size() - suffix_sv.size()));

  // Fragmented needle with empty first chunk.
  absl::Cord fragmented_with_empty_chunk;
  fragmented_with_empty_chunk.Append("");
  fragmented_with_empty_chunk.Append("A string");
  EXPECT_THAT(byte_string.Find(fragmented_with_empty_chunk), Optional(0));

  // Search with fragmented cord needle on string_view backed ByteString with
  // partial match.
  ByteString partial_match_haystack = ByteString::WrapUnsafe("abababac");
  absl::Cord partial_match_needle = absl::MakeFragmentedCord({"aba", "c"});
  EXPECT_THAT(partial_match_haystack.Find(partial_match_needle), Optional(4));

  // Search with fragmented cord needle where first chunk is found but not
  // enough space for the rest.
  ByteString short_haystack = ByteString::WrapUnsafe("abcdefg");
  absl::Cord needle_too_long = absl::MakeFragmentedCord({"ef", "gh"});
  EXPECT_THAT(short_haystack.Find(needle_too_long), Eq(std::nullopt));

  // Search with a fragmented empty cord.
  absl::Cord fragmented_empty_cord = absl::MakeFragmentedCord({"", ""});
  EXPECT_THAT(byte_string.Find(fragmented_empty_cord), Optional(0));
  EXPECT_THAT(byte_string.Find(fragmented_empty_cord, 3), Optional(3));

  // Search for suffix in a fragmented cord.
  ByteString fragmented_cord_byte_string =
      ByteString::From(GetMediumOrLargeFragmentedCord(), GetArena());
  EXPECT_THAT(fragmented_cord_byte_string.Find(suffix_sv),
              Optional(fragmented_cord_byte_string.size() - suffix_sv.size()));
  EXPECT_THAT(fragmented_cord_byte_string.Find(absl::Cord(suffix_sv)),
              Optional(fragmented_cord_byte_string.size() - suffix_sv.size()));
}

#ifndef NDEBUG
TEST_F(ByteStringTest, FindOutOfBounds) {
  ByteString byte_string = ByteString::WrapUnsafe("test");
  EXPECT_DEATH(byte_string.Find("t", 5), _);
}
#endif

TEST_F(ByteStringTest, Substring) {
  // small byte_string substring
  ByteString small_byte_string = ByteString::WrapUnsafe(GetSmallStringView());
  EXPECT_EQ(small_byte_string.Substring(1, 5),
            GetSmallStringView().substr(1, 4));
  EXPECT_EQ(small_byte_string.Substring(0, small_byte_string.size()),
            GetSmallStringView());
  EXPECT_EQ(small_byte_string.Substring(1, 1), "");
  // medium byte_string substring
  ByteString medium_byte_string = ByteString::WrapUnsafe(GetMediumStringView());
  EXPECT_EQ(medium_byte_string.Substring(2, 12),
            GetMediumStringView().substr(2, 10));
  EXPECT_EQ(medium_byte_string.Substring(0, medium_byte_string.size()),
            GetMediumStringView());
  // large byte_string substring
  ByteString large_byte_string =
      ByteString::WrapUnsafe(&GetMediumOrLargeCord());
  EXPECT_EQ(large_byte_string.Substring(3, 15),
            GetMediumOrLargeCord().Subcord(3, 12));
  EXPECT_EQ(large_byte_string.Substring(0, large_byte_string.size()),
            GetMediumOrLargeCord());
  // substring with one parameter
  ByteString tacocat_byte_string = ByteString::WrapUnsafe("tacocat");
  EXPECT_EQ(tacocat_byte_string.Substring(4), "cat");
}

TEST_F(ByteStringTest, SubstringEdgeCases) {
  ByteString byte_string = ByteString::WrapUnsafe(GetSmallStringView());
  EXPECT_EQ(byte_string.Substring(byte_string.size(), byte_string.size()), "");
  EXPECT_EQ(byte_string.Substring(0, 0), "");
}

#ifndef NDEBUG
TEST_F(ByteStringTest, SubstringOutOfBounds) {
  ByteString byte_string = ByteString::WrapUnsafe("test");
  EXPECT_DEATH(static_cast<void>(byte_string.Substring(5, 5)), _);
  EXPECT_DEATH(static_cast<void>(byte_string.Substring(0, 5)), _);
  EXPECT_DEATH(static_cast<void>(byte_string.Substring(3, 2)), _);
}
#endif

TEST_F(ByteStringTest, RemovePrefixSmall) {
  ByteString byte_string = ByteString::WrapUnsafe(GetSmallStringView());
  byte_string.RemovePrefix(1);
  EXPECT_EQ(byte_string, GetSmallStringView().substr(1));
}

TEST_F(ByteStringTest, RemovePrefixMedium) {
  ByteString byte_string = ByteString::WrapUnsafe(GetMediumStringView());
  EXPECT_EQ(GetKind(byte_string), ByteStringKind::kMedium);
  byte_string.RemovePrefix(byte_string.size() - kSmallByteStringCapacity);
  EXPECT_EQ(GetKind(byte_string), ByteStringKind::kMedium);
  EXPECT_EQ(byte_string,
            GetMediumStringView().substr(GetMediumStringView().size() -
                                         kSmallByteStringCapacity));
}

TEST_F(ByteStringTest, RemovePrefixMediumOrLarge) {
  ByteString byte_string = ByteString::WrapUnsafe(&GetMediumOrLargeCord());
  byte_string.RemovePrefix(byte_string.size() - kSmallByteStringCapacity);
  EXPECT_EQ(GetKind(byte_string), ByteStringKind::kLarge);
  EXPECT_EQ(byte_string,
            GetMediumStringView().substr(GetMediumStringView().size() -
                                         kSmallByteStringCapacity));
}

TEST_F(ByteStringTest, RemoveSuffixSmall) {
  ByteString byte_string = ByteString::WrapUnsafe(GetSmallStringView());
  byte_string.RemoveSuffix(1);
  EXPECT_EQ(byte_string,
            GetSmallStringView().substr(0, GetSmallStringView().size() - 1));
}

TEST_F(ByteStringTest, RemoveSuffixMedium) {
  ByteString byte_string = ByteString::WrapUnsafe(GetMediumStringView());
  EXPECT_EQ(GetKind(byte_string), ByteStringKind::kMedium);
  byte_string.RemoveSuffix(byte_string.size() - kSmallByteStringCapacity);
  EXPECT_EQ(GetKind(byte_string), ByteStringKind::kMedium);
  EXPECT_EQ(byte_string,
            GetMediumStringView().substr(0, kSmallByteStringCapacity));
}

TEST_F(ByteStringTest, RemoveSuffixMediumOrLarge) {
  ByteString byte_string = ByteString::WrapUnsafe(&GetMediumOrLargeCord());
  byte_string.RemoveSuffix(byte_string.size() - kSmallByteStringCapacity);
  EXPECT_EQ(GetKind(byte_string), ByteStringKind::kLarge);
  EXPECT_EQ(byte_string,
            GetMediumStringView().substr(0, kSmallByteStringCapacity));
}

TEST_F(ByteStringTest, ToStringSmall) {
  ByteString byte_string = ByteString::WrapUnsafe(GetSmallStringView());
  EXPECT_EQ(byte_string.ToString(), byte_string);
}

TEST_F(ByteStringTest, ToStringMedium) {
  ByteString byte_string = ByteString::WrapUnsafe(GetMediumStringView());
  EXPECT_EQ(byte_string.ToString(), byte_string);
}

TEST_F(ByteStringTest, ToStringLarge) {
  ByteString byte_string = ByteString::WrapUnsafe(&GetMediumOrLargeCord());
  EXPECT_EQ(byte_string.ToString(), byte_string);
}

TEST_F(ByteStringTest, ToStringViewSmall) {
  std::string scratch;
  ByteString byte_string = ByteString::WrapUnsafe(GetSmallStringView());
  EXPECT_EQ(byte_string.ToStringView(&scratch), GetSmallStringView());
}

TEST_F(ByteStringTest, ToStringViewMedium) {
  std::string scratch;
  ByteString byte_string = ByteString::WrapUnsafe(GetMediumStringView());
  EXPECT_EQ(byte_string.ToStringView(&scratch), GetMediumStringView());
}

TEST_F(ByteStringTest, ToStringViewLarge) {
  std::string scratch;
  ByteString byte_string = ByteString::WrapUnsafe(&GetMediumOrLargeCord());
  EXPECT_EQ(byte_string.ToStringView(&scratch), GetMediumOrLargeCord());
}

TEST_F(ByteStringTest, AsStringViewSmall) {
  ByteString byte_string = ByteString::WrapUnsafe(GetSmallStringView());
  EXPECT_EQ(byte_string.AsStringView(), GetSmallStringView());
}

TEST_F(ByteStringTest, AsStringViewMedium) {
  ByteString byte_string = ByteString::WrapUnsafe(GetMediumStringView());
  EXPECT_EQ(byte_string.AsStringView(), GetMediumStringView());
}

TEST_F(ByteStringTest, AsStringViewLarge) {
  ByteString byte_string = ByteString::WrapUnsafe(&GetMediumOrLargeCord());
  EXPECT_DEATH(byte_string.AsStringView(), _);
}

TEST_F(ByteStringTest, CopyToStringSmall) {
  std::string out;

  ByteString::WrapUnsafe(GetSmallStringView()).CopyToString(&out);
  EXPECT_EQ(out, GetSmallStringView());
}

TEST_F(ByteStringTest, CopyToStringMedium) {
  std::string out;

  ByteString::WrapUnsafe(GetMediumStringView()).CopyToString(&out);
  EXPECT_EQ(out, GetMediumStringView());
}

TEST_F(ByteStringTest, CopyToStringLarge) {
  std::string out;

  ByteString::WrapUnsafe(&GetMediumOrLargeCord()).CopyToString(&out);
  EXPECT_EQ(out, GetMediumOrLargeCord());
}

TEST_F(ByteStringTest, AppendToStringSmall) {
  std::string out;

  ByteString::WrapUnsafe(GetSmallStringView()).AppendToString(&out);
  EXPECT_EQ(out, GetSmallStringView());
}

TEST_F(ByteStringTest, AppendToStringMedium) {
  std::string out;

  ByteString::WrapUnsafe(GetMediumStringView()).AppendToString(&out);
  EXPECT_EQ(out, GetMediumStringView());
}

TEST_F(ByteStringTest, AppendToStringLarge) {
  std::string out;

  ByteString::WrapUnsafe(&GetMediumOrLargeCord()).AppendToString(&out);
  EXPECT_EQ(out, GetMediumOrLargeCord());
}

TEST_F(ByteStringTest, ToCordSmall) {
  ByteString byte_string = ByteString::WrapUnsafe(GetSmallStringView());
  EXPECT_EQ(byte_string.ToCord(), byte_string);
  EXPECT_EQ(std::move(byte_string).ToCord(), GetSmallStringView());
}

TEST_F(ByteStringTest, ToCordMedium) {
  ByteString byte_string = ByteString::WrapUnsafe(GetMediumStringView());
  EXPECT_EQ(byte_string.ToCord(), byte_string);
  EXPECT_EQ(std::move(byte_string).ToCord(), GetMediumStringView());
}

TEST_F(ByteStringTest, ToCordLarge) {
  ByteString byte_string = ByteString::WrapUnsafe(&GetMediumOrLargeCord());
  EXPECT_EQ(byte_string.ToCord(), byte_string);
  EXPECT_EQ(std::move(byte_string).ToCord(), GetMediumOrLargeCord());
}

TEST_F(ByteStringTest, CopyToCordSmall) {
  absl::Cord out;

  ByteString::WrapUnsafe(GetSmallStringView()).CopyToCord(&out);
  EXPECT_EQ(out, GetSmallStringView());
}

TEST_F(ByteStringTest, CopyToCordMedium) {
  absl::Cord out;

  ByteString::WrapUnsafe(GetMediumStringView()).CopyToCord(&out);
  EXPECT_EQ(out, GetMediumStringView());
}

TEST_F(ByteStringTest, CopyToCordLarge) {
  absl::Cord out;

  ByteString::WrapUnsafe(&GetMediumOrLargeCord()).CopyToCord(&out);
  EXPECT_EQ(out, GetMediumOrLargeCord());
}

TEST_F(ByteStringTest, AppendToCordSmall) {
  absl::Cord out;

  ByteString::WrapUnsafe(GetSmallStringView()).AppendToCord(&out);
  EXPECT_EQ(out, GetSmallStringView());
}

TEST_F(ByteStringTest, AppendToCordMedium) {
  absl::Cord out;

  ByteString::WrapUnsafe(GetMediumStringView()).AppendToCord(&out);
  EXPECT_EQ(out, GetMediumStringView());
}

TEST_F(ByteStringTest, AppendToCordLarge) {
  absl::Cord out;

  ByteString::WrapUnsafe(&GetMediumOrLargeCord()).AppendToCord(&out);
  EXPECT_EQ(out, GetMediumOrLargeCord());
}

TEST_F(ByteStringTest, CloneSmall) {
  google::protobuf::Arena arena;
  ByteString byte_string = ByteString::WrapUnsafe(GetSmallStringView());
  EXPECT_EQ(byte_string.Clone(&arena), byte_string);
}

TEST_F(ByteStringTest, CloneMedium) {
  google::protobuf::Arena arena;
  ByteString byte_string = ByteString::WrapUnsafe(GetMediumStringView());
  EXPECT_EQ(byte_string.Clone(&arena), byte_string);
}

TEST_F(ByteStringTest, CloneLarge) {
  google::protobuf::Arena arena;
  ByteString byte_string = ByteString::WrapUnsafe(&GetMediumOrLargeCord());
  EXPECT_EQ(byte_string.Clone(&arena), byte_string);
}

TEST_F(ByteStringTest, LegacyByteStringSmall) {
  google::protobuf::Arena arena;
  ByteString byte_string = ByteString::WrapUnsafe(GetSmallStringView());
  EXPECT_EQ(LegacyByteString(byte_string, /*stable=*/false, &arena),
            GetSmallStringView());
  EXPECT_EQ(LegacyByteString(byte_string, /*stable=*/true, &arena),
            GetSmallStringView());
}

TEST_F(ByteStringTest, LegacyByteStringMedium) {
  google::protobuf::Arena arena;
  ByteString byte_string = ByteString::WrapUnsafe(GetMediumStringView());
  EXPECT_EQ(LegacyByteString(byte_string, /*stable=*/false, &arena),
            GetMediumStringView());
  EXPECT_EQ(LegacyByteString(byte_string, /*stable=*/true, &arena),
            GetMediumStringView());
}

TEST_F(ByteStringTest, LegacyByteStringLarge) {
  google::protobuf::Arena arena;
  ByteString byte_string = ByteString::WrapUnsafe(&GetMediumOrLargeCord());
  EXPECT_EQ(LegacyByteString(byte_string, /*stable=*/false, &arena),
            GetMediumOrLargeCord());
  EXPECT_EQ(LegacyByteString(byte_string, /*stable=*/true, &arena),
            GetMediumOrLargeCord());
}

TEST_F(ByteStringTest, HashValue) {
  EXPECT_EQ(absl::HashOf(ByteString::WrapUnsafe(GetSmallStringView())),
            absl::HashOf(GetSmallStringView()));
  EXPECT_EQ(absl::HashOf(ByteString::WrapUnsafe(GetMediumStringView())),
            absl::HashOf(GetMediumStringView()));
  EXPECT_EQ(absl::HashOf(ByteString::WrapUnsafe(&GetMediumOrLargeCord())),
            absl::HashOf(GetMediumOrLargeCord()));
}

}  // namespace
}  // namespace cel::common_internal
