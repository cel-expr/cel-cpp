// Copyright 2022 Google LLC
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

#include "base/attribute.h"

#include <cstdint>
#include <optional>

#include "absl/strings/string_view.h"
#include "internal/testing.h"

namespace cel {
namespace {

using ::testing::Optional;

TEST(AttributeQualifierView, Bool) {
  AttributeQualifierView qualifier = AttributeQualifierView::OfBool(true);
  EXPECT_TRUE(qualifier.IsBool());
  EXPECT_FALSE(qualifier.IsInt());
  EXPECT_FALSE(qualifier.IsUint());
  EXPECT_FALSE(qualifier.IsString());
  EXPECT_TRUE(qualifier.GetBool());
  EXPECT_THAT(qualifier.AsBool(), Optional(true));
  EXPECT_THAT(qualifier.AsInt(), std::nullopt);
  EXPECT_THAT(qualifier.AsUint(), std::nullopt);
  EXPECT_THAT(qualifier.AsString(), std::nullopt);
}

TEST(AttributeQualifierView, Int) {
  AttributeQualifierView qualifier = AttributeQualifierView::OfInt(1);
  EXPECT_FALSE(qualifier.IsBool());
  EXPECT_TRUE(qualifier.IsInt());
  EXPECT_FALSE(qualifier.IsUint());
  EXPECT_FALSE(qualifier.IsString());
  EXPECT_EQ(qualifier.GetInt(), 1);
  EXPECT_THAT(qualifier.AsBool(), std::nullopt);
  EXPECT_THAT(qualifier.AsInt(), Optional(1));
  EXPECT_THAT(qualifier.AsUint(), std::nullopt);
  EXPECT_THAT(qualifier.AsString(), std::nullopt);
}

TEST(AttributeQualifierView, Uint) {
  AttributeQualifierView qualifier = AttributeQualifierView::OfUint(1);
  EXPECT_FALSE(qualifier.IsBool());
  EXPECT_FALSE(qualifier.IsInt());
  EXPECT_TRUE(qualifier.IsUint());
  EXPECT_FALSE(qualifier.IsString());
  EXPECT_EQ(qualifier.GetUint(), 1);
  EXPECT_THAT(qualifier.AsBool(), std::nullopt);
  EXPECT_THAT(qualifier.AsInt(), std::nullopt);
  EXPECT_THAT(qualifier.AsUint(), Optional(1));
  EXPECT_THAT(qualifier.AsString(), std::nullopt);
}

TEST(AttributeQualifierView, String) {
  AttributeQualifierView qualifier = AttributeQualifierView::OfString("foo");
  EXPECT_FALSE(qualifier.IsBool());
  EXPECT_FALSE(qualifier.IsInt());
  EXPECT_FALSE(qualifier.IsUint());
  EXPECT_TRUE(qualifier.IsString());
  EXPECT_EQ(qualifier.GetString(), "foo");
  EXPECT_THAT(qualifier.AsBool(), std::nullopt);
  EXPECT_THAT(qualifier.AsInt(), std::nullopt);
  EXPECT_THAT(qualifier.AsUint(), std::nullopt);
  EXPECT_THAT(qualifier.AsString(), Optional(absl::string_view("foo")));
}

TEST(Attribute, ToString) {
  EXPECT_EQ(Attribute("foo", {AttributeQualifier::OfBool(true),
                              AttributeQualifier::OfInt(2),
                              AttributeQualifier::OfUint(3),
                              AttributeQualifier::OfString("bar"),
                              AttributeQualifier::OfString("baz qux")})
                .ToString(),
            "foo[true][2][3u].bar[\"baz qux\"]");
}

TEST(AttributePattern, ToString) {
  EXPECT_EQ(
      AttributePattern("foo", {AttributeQualifierPattern::OfBool(true),
                               AttributeQualifierPattern::OfInt(2),
                               AttributeQualifierPattern::OfUint(3),
                               AttributeQualifierPattern::OfString("bar"),
                               AttributeQualifierPattern::OfString("baz qux"),
                               AttributeQualifierPattern::Wildcard()})
          .ToString(),
      "foo[true][2][3u].bar[\"baz qux\"].*");
}

TEST(AttributeQualifierView, Order) {
  // int < uint < string < bool
  EXPECT_LT(AttributeQualifierView::OfInt(int64_t{0}),
            AttributeQualifierView::OfUint(uint64_t{0}));
  EXPECT_LT(AttributeQualifierView::OfInt(int64_t{0}),
            AttributeQualifierView::OfString(""));
  EXPECT_LT(AttributeQualifierView::OfInt(int64_t{0}),
            AttributeQualifierView::OfBool(false));
  EXPECT_LT(AttributeQualifierView::OfUint(uint64_t{0}),
            AttributeQualifierView::OfString(""));
  EXPECT_LT(AttributeQualifierView::OfUint(uint64_t{0}),
            AttributeQualifierView::OfBool(false));
}

TEST(AttributeQualifierView, TransparentOrder) {
  EXPECT_EQ(AttributeQualifierView::OfInt(int64_t{0}),
            AttributeQualifier::OfInt(0));
  EXPECT_EQ(AttributeQualifierView::OfUint(uint64_t{0}),
            AttributeQualifier::OfUint(0));
  EXPECT_EQ(AttributeQualifierView::OfString("0"),
            AttributeQualifier::OfString("0"));
  EXPECT_EQ(AttributeQualifierView::OfBool(false),
            AttributeQualifier::OfBool(false));
  EXPECT_EQ(AttributeQualifier::OfInt(0),
            AttributeQualifierView::OfInt(int64_t{0}));
  EXPECT_EQ(AttributeQualifier::OfUint(0),
            AttributeQualifierView::OfUint(uint64_t{0}));
  EXPECT_EQ(AttributeQualifier::OfString("0"),
            AttributeQualifierView::OfString("0"));
  EXPECT_EQ(AttributeQualifier::OfBool(false),
            AttributeQualifierView::OfBool(false));
  EXPECT_LT(AttributeQualifierView::OfInt(int64_t{0}),
            AttributeQualifier::OfInt(1));
  EXPECT_LT(AttributeQualifierView::OfUint(uint64_t{0}),
            AttributeQualifier::OfUint(1));
  EXPECT_LT(AttributeQualifierView::OfString("0"),
            AttributeQualifier::OfString("1"));
  EXPECT_LT(AttributeQualifierView::OfBool(false),
            AttributeQualifier::OfBool(true));
  EXPECT_LT(AttributeQualifier::OfInt(0),
            AttributeQualifierView::OfInt(int64_t{1}));
  EXPECT_LT(AttributeQualifier::OfUint(0),
            AttributeQualifierView::OfUint(uint64_t{1}));
  EXPECT_LT(AttributeQualifier::OfString("0"),
            AttributeQualifierView::OfString("1"));
  EXPECT_LT(AttributeQualifier::OfBool(false),
            AttributeQualifierView::OfBool(true));
}

template <typename From, typename To>
void TestAttributeQualifierConversion() {
  EXPECT_EQ(To(From::OfInt(1)), To::OfInt(1));
  EXPECT_EQ(To(From::OfUint(1)), To::OfUint(1));
  EXPECT_EQ(To(From::OfString("foo")), To::OfString("foo"));
  EXPECT_EQ(To(From::OfBool(true)), To::OfBool(true));
}

TEST(AttributeQualifier, Conversion) {
  TestAttributeQualifierConversion<AttributeQualifier,
                                   AttributeQualifierView>();
  TestAttributeQualifierConversion<AttributeQualifierView,
                                   AttributeQualifier>();
  TestAttributeQualifierConversion<AttributeQualifier,
                                   AttributeQualifierPattern>();
}

}  // namespace
}  // namespace cel
