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

#include "absl/types/optional.h"
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
  EXPECT_THAT(qualifier.AsInt(), absl::nullopt);
  EXPECT_THAT(qualifier.AsUint(), absl::nullopt);
  EXPECT_THAT(qualifier.AsString(), absl::nullopt);
}

TEST(AttributeQualifierView, Int) {
  AttributeQualifierView qualifier = AttributeQualifierView::OfInt(1);
  EXPECT_FALSE(qualifier.IsBool());
  EXPECT_TRUE(qualifier.IsInt());
  EXPECT_FALSE(qualifier.IsUint());
  EXPECT_FALSE(qualifier.IsString());
  EXPECT_EQ(qualifier.GetInt(), 1);
  EXPECT_THAT(qualifier.AsBool(), absl::nullopt);
  EXPECT_THAT(qualifier.AsInt(), Optional(1));
  EXPECT_THAT(qualifier.AsUint(), absl::nullopt);
  EXPECT_THAT(qualifier.AsString(), absl::nullopt);
}

TEST(AttributeQualifierView, Uint) {
  AttributeQualifierView qualifier = AttributeQualifierView::OfUint(1);
  EXPECT_FALSE(qualifier.IsBool());
  EXPECT_FALSE(qualifier.IsInt());
  EXPECT_TRUE(qualifier.IsUint());
  EXPECT_FALSE(qualifier.IsString());
  EXPECT_EQ(qualifier.GetUint(), 1);
  EXPECT_THAT(qualifier.AsBool(), absl::nullopt);
  EXPECT_THAT(qualifier.AsInt(), absl::nullopt);
  EXPECT_THAT(qualifier.AsUint(), Optional(1));
  EXPECT_THAT(qualifier.AsString(), absl::nullopt);
}

TEST(AttributeQualifierView, String) {
  AttributeQualifierView qualifier = AttributeQualifierView::OfString("foo");
  EXPECT_FALSE(qualifier.IsBool());
  EXPECT_FALSE(qualifier.IsInt());
  EXPECT_FALSE(qualifier.IsUint());
  EXPECT_TRUE(qualifier.IsString());
  EXPECT_EQ(qualifier.GetString(), "foo");
  EXPECT_THAT(qualifier.AsBool(), absl::nullopt);
  EXPECT_THAT(qualifier.AsInt(), absl::nullopt);
  EXPECT_THAT(qualifier.AsUint(), absl::nullopt);
  EXPECT_THAT(qualifier.AsString(), Optional("foo"));
}

}  // namespace
}  // namespace cel
