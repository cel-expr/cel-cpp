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

#include "internal/cstring_view.h"

#include <cstddef>

#include "absl/hash/hash.h"
#include "absl/hash/hash_testing.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "internal/testing.h"

namespace cel::internal {
namespace {

TEST(CStringView, Empty) {
  cstring_view string;
  EXPECT_TRUE(string.empty());
  EXPECT_EQ(string.size(), size_t{0});
  EXPECT_EQ(string[0], '\0');
  // NOLINTBEGIN(readability/check)
  // NOLINTBEGIN(readability-container-size-empty)
  EXPECT_TRUE(string == "");
  EXPECT_TRUE("" == string);
  EXPECT_FALSE(string != "");
  EXPECT_FALSE("" != string);
  EXPECT_FALSE(string < "");
  EXPECT_FALSE("" < string);
  EXPECT_TRUE(string == absl::string_view(""));
  EXPECT_TRUE(absl::string_view("") == string);
  EXPECT_FALSE(string != absl::string_view(""));
  EXPECT_FALSE(absl::string_view("") != string);
  EXPECT_FALSE(string < absl::string_view(""));
  EXPECT_FALSE(absl::string_view("") < string);
  // NOLINTEND(readability-container-size-empty)
  // NOLINTEND(readability/check)
}

TEST(CStringView, Basic) {
  // NOLINTBEGIN(readability/check)
  // NOLINTBEGIN(readability-container-size-empty)
  EXPECT_TRUE(cstring_view("foo") == cstring_view("foo"));
  EXPECT_TRUE(cstring_view("foo") == absl::string_view("foo\0", 4));
  EXPECT_TRUE(absl::string_view("foo\0", 4) == cstring_view("foo"));
  EXPECT_FALSE(cstring_view("foo") < absl::string_view("foo\0", 4));
  EXPECT_FALSE(absl::string_view("foo\0", 4) < cstring_view("foo"));
  EXPECT_FALSE(cstring_view("fooo") == absl::string_view("foo\0", 4));
  EXPECT_FALSE(absl::string_view("foo\0", 4) == cstring_view("fooo"));
  EXPECT_FALSE(cstring_view("fooo") < absl::string_view("foo\0", 4));
  EXPECT_TRUE(absl::string_view("foo\0", 4) < cstring_view("fooo"));
  // NOLINTEND(readability-container-size-empty)
  // NOLINTEND(readability/check)
}

TEST(CStringView, AbslStringify) {
  EXPECT_EQ(absl::StrCat(cstring_view()), absl::StrCat(absl::string_view("")));
  EXPECT_EQ(absl::StrCat(cstring_view("foo")),
            absl::StrCat(absl::string_view("foo")));
}

TEST(CStringView, AbslHashValue) {
  EXPECT_EQ(absl::HashOf(cstring_view()), absl::HashOf(absl::string_view("")));
  EXPECT_EQ(absl::HashOf(cstring_view("foo")),
            absl::HashOf(absl::string_view("foo")));
  EXPECT_EQ(absl::HashOf(cstring_view("bar")),
            absl::HashOf(absl::string_view("bar")));
  EXPECT_EQ(absl::HashOf(cstring_view("Hello, World!")),
            absl::HashOf(absl::string_view("Hello, World!")));
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(
      {cstring_view("foo"), cstring_view("bar"), cstring_view("Hello, World!"),
       cstring_view()}));
}

}  // namespace
}  // namespace cel::internal
