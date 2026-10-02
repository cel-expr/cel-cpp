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
                              AttributeQualifier::OfString("bar")})
                .ToString(),
            "foo[true][2][3u][\"bar\"]");
}

TEST(AttributePattern, ToString) {
  EXPECT_EQ(AttributePattern("foo", {AttributeQualifierPattern::OfBool(true),
                                     AttributeQualifierPattern::OfInt(2),
                                     AttributeQualifierPattern::OfUint(3),
                                     AttributeQualifierPattern::OfString("bar"),
                                     AttributeQualifierPattern::Wildcard()})
                .ToString(),
            "foo[true][2][3u][\"bar\"][*]");
}

using UnknownAttributeKey = common_internal::UnknownAttributeKey;

TEST(UnknownAttributeKey, Bool) {
  UnknownAttributeKey key(true);
  EXPECT_TRUE(key.IsBool());
  EXPECT_FALSE(key.IsInt());
  EXPECT_FALSE(key.IsUint());
  EXPECT_FALSE(key.IsString());
  EXPECT_EQ(key.GetBool(), true);
  EXPECT_EQ(UnknownAttributeKey(true), key);
  EXPECT_NE(UnknownAttributeKey(false), key);
  EXPECT_LT(UnknownAttributeKey(false), key);
}

TEST(UnknownAttributeKey, Int) {
  UnknownAttributeKey key(int64_t{1});
  EXPECT_FALSE(key.IsBool());
  EXPECT_TRUE(key.IsInt());
  EXPECT_FALSE(key.IsUint());
  EXPECT_FALSE(key.IsString());
  EXPECT_EQ(key.GetInt(), int64_t{1});
  EXPECT_EQ(UnknownAttributeKey(int64_t{1}), key);
  EXPECT_NE(UnknownAttributeKey(int64_t{0}), key);
  EXPECT_LT(UnknownAttributeKey(int64_t{0}), key);
}

TEST(UnknownAttributeKey, Uint) {
  UnknownAttributeKey key(uint64_t{1});
  EXPECT_FALSE(key.IsBool());
  EXPECT_FALSE(key.IsInt());
  EXPECT_TRUE(key.IsUint());
  EXPECT_FALSE(key.IsString());
  EXPECT_EQ(key.GetUint(), uint64_t{1});
  EXPECT_EQ(UnknownAttributeKey(uint64_t{1}), key);
  EXPECT_NE(UnknownAttributeKey(uint64_t{0}), key);
  EXPECT_LT(UnknownAttributeKey(uint64_t{0}), key);
}

TEST(UnknownAttributeKey, String) {
  UnknownAttributeKey key("1");
  EXPECT_FALSE(key.IsBool());
  EXPECT_FALSE(key.IsInt());
  EXPECT_FALSE(key.IsUint());
  EXPECT_TRUE(key.IsString());
  EXPECT_EQ(key.GetString(), "1");
  EXPECT_EQ(UnknownAttributeKey("1"), key);
  EXPECT_NE(UnknownAttributeKey("0"), key);
  EXPECT_LT(UnknownAttributeKey("0"), key);
}

TEST(UnknownAttributeKey, Order) {
  // int < uint < string < bool
  EXPECT_LT(UnknownAttributeKey(int64_t{0}), UnknownAttributeKey(uint64_t{0}));
  EXPECT_LT(UnknownAttributeKey(int64_t{0}), UnknownAttributeKey(""));
  EXPECT_LT(UnknownAttributeKey(int64_t{0}), UnknownAttributeKey(false));
  EXPECT_LT(UnknownAttributeKey(uint64_t{0}), UnknownAttributeKey(""));
  EXPECT_LT(UnknownAttributeKey(uint64_t{0}), UnknownAttributeKey(false));
}

TEST(UnknownAttributeKey, TransparentOrder) {
  EXPECT_EQ(UnknownAttributeKey(int64_t{0}), AttributeQualifierView::OfInt(0));
  EXPECT_EQ(UnknownAttributeKey(uint64_t{0}),
            AttributeQualifierView::OfUint(0));
  EXPECT_EQ(UnknownAttributeKey("0"), AttributeQualifierView::OfString("0"));
  EXPECT_EQ(UnknownAttributeKey(false), AttributeQualifierView::OfBool(false));
  EXPECT_EQ(AttributeQualifierView::OfInt(0), UnknownAttributeKey(int64_t{0}));
  EXPECT_EQ(AttributeQualifierView::OfUint(0),
            UnknownAttributeKey(uint64_t{0}));
  EXPECT_EQ(AttributeQualifierView::OfString("0"), UnknownAttributeKey("0"));
  EXPECT_EQ(AttributeQualifierView::OfBool(false), UnknownAttributeKey(false));
  EXPECT_LT(UnknownAttributeKey(int64_t{0}), AttributeQualifierView::OfInt(1));
  EXPECT_LT(UnknownAttributeKey(uint64_t{0}),
            AttributeQualifierView::OfUint(1));
  EXPECT_LT(UnknownAttributeKey("0"), AttributeQualifierView::OfString("1"));
  EXPECT_LT(UnknownAttributeKey(false), AttributeQualifierView::OfBool(true));
  EXPECT_LT(AttributeQualifierView::OfInt(0), UnknownAttributeKey(int64_t{1}));
  EXPECT_LT(AttributeQualifierView::OfUint(0),
            UnknownAttributeKey(uint64_t{1}));
  EXPECT_LT(AttributeQualifierView::OfString("0"), UnknownAttributeKey("1"));
  EXPECT_LT(AttributeQualifierView::OfBool(false), UnknownAttributeKey(true));
}

}  // namespace
}  // namespace cel
