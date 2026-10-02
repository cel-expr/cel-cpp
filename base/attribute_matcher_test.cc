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

#include "base/attribute_matcher.h"

#include <algorithm>
#include <cstddef>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "base/attribute.h"
#include "internal/testing.h"

namespace cel {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::UnorderedElementsAre;
using ::testing::UnorderedElementsAreArray;

using MatchType = AttributePattern::MatchType;

MATCHER_P2(AttributeMatchIs, missing_type, unknown_type, "") {
  return ::testing::ExplainMatchResult(::testing::Eq(missing_type),
                                       arg.GetMissingType(), result_listener) &&
         ::testing::ExplainMatchResult(::testing::Eq(unknown_type),
                                       arg.GetUnknownType(), result_listener);
}

using ::testing::IsEmpty;

TEST(AttributeMatcher, Empty) {
  AttributeMatcher matcher;
  EXPECT_FALSE(matcher.HasMissingAttributes());
  EXPECT_FALSE(matcher.HasUnknownAttributes());
  EXPECT_THAT(matcher.GetMissingAttributes(), IsEmpty());
  EXPECT_THAT(matcher.GetUnknownAttributes(), IsEmpty());
  EXPECT_THAT(matcher.MatchVariable("foo"),
              AttributeMatchIs(MatchType::NONE, MatchType::NONE));
}

TEST(AttributeMatcher, Unknown_Attribute_OneVariable_ZeroQualifier_Subset) {
  AttributeMatcher matcher;
  EXPECT_THAT(matcher.AddUnknownAttribute(Attribute("foo")), IsOk());
  EXPECT_THAT(matcher.AddUnknownAttribute(Attribute("foo")),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_THAT(matcher.AddUnknownAttribute(
                  Attribute("foo", {AttributeQualifier::OfString("bar")})),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_THAT(matcher.AddUnknownAttribute(AttributePattern("foo")),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_THAT(matcher.AddUnknownAttribute(AttributePattern(
                  "foo", {AttributeQualifierPattern::OfString("bar")})),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_THAT(matcher.AddUnknownAttribute(AttributePattern(
                  "foo", {AttributeQualifierPattern::Wildcard()})),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_FALSE(matcher.HasMissingAttributes());
  EXPECT_TRUE(matcher.HasUnknownAttributes());
  EXPECT_THAT(matcher.GetMissingAttributes(), IsEmpty());
  EXPECT_THAT(matcher.GetUnknownAttributes(),
              UnorderedElementsAre(AttributePattern("foo")));
}

TEST(AttributeMatcher,
     Unknown_AttributePattern_OneVariable_ZeroQualifier_Subset) {
  AttributeMatcher matcher;
  EXPECT_THAT(matcher.AddUnknownAttribute(AttributePattern("foo")), IsOk());
  EXPECT_THAT(matcher.AddUnknownAttribute(Attribute("foo")),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_THAT(matcher.AddUnknownAttribute(
                  Attribute("foo", {AttributeQualifier::OfString("bar")})),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_THAT(matcher.AddUnknownAttribute(AttributePattern("foo")),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_THAT(matcher.AddUnknownAttribute(AttributePattern(
                  "foo", {AttributeQualifierPattern::OfString("bar")})),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_THAT(matcher.AddUnknownAttribute(AttributePattern(
                  "foo", {AttributeQualifierPattern::Wildcard()})),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_FALSE(matcher.HasMissingAttributes());
  EXPECT_TRUE(matcher.HasUnknownAttributes());
  EXPECT_THAT(matcher.GetMissingAttributes(), IsEmpty());
  EXPECT_THAT(matcher.GetUnknownAttributes(),
              UnorderedElementsAre(AttributePattern("foo")));
}

TEST(AttributeMatcher, Unknown_Attribute_OneVariable_TwoQualifier_Superset) {
  AttributeMatcher matcher;
  EXPECT_THAT(matcher.AddUnknownAttribute(
                  Attribute("foo", {AttributeQualifier::OfString("bar"),
                                    AttributeQualifier::OfString("baz")})),
              IsOk());
  EXPECT_THAT(matcher.AddUnknownAttribute(Attribute("foo")),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_THAT(matcher.AddUnknownAttribute(
                  Attribute("foo", {AttributeQualifier::OfString("bar")})),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_THAT(matcher.AddUnknownAttribute(AttributePattern("foo")),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_THAT(matcher.AddUnknownAttribute(AttributePattern(
                  "foo", {AttributeQualifierPattern::OfString("bar")})),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_FALSE(matcher.HasMissingAttributes());
  EXPECT_TRUE(matcher.HasUnknownAttributes());
  EXPECT_THAT(matcher.GetMissingAttributes(), IsEmpty());
  EXPECT_THAT(matcher.GetUnknownAttributes(),
              UnorderedElementsAre(AttributePattern(
                  "foo", {AttributeQualifierPattern::OfString("bar"),
                          AttributeQualifierPattern::OfString("baz")})));
}

TEST(AttributeMatcher,
     Unknown_AttributePattern_OneVariable_TwoQualifier_Superset) {
  AttributeMatcher matcher;
  EXPECT_THAT(matcher.AddUnknownAttribute(AttributePattern(
                  "foo", {AttributeQualifierPattern::OfString("bar"),
                          AttributeQualifierPattern::OfString("baz")})),
              IsOk());
  EXPECT_THAT(matcher.AddUnknownAttribute(Attribute("foo")),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_THAT(matcher.AddUnknownAttribute(
                  Attribute("foo", {AttributeQualifier::OfString("bar")})),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_THAT(matcher.AddUnknownAttribute(AttributePattern("foo")),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_THAT(matcher.AddUnknownAttribute(AttributePattern(
                  "foo", {AttributeQualifierPattern::OfString("bar")})),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_FALSE(matcher.HasMissingAttributes());
  EXPECT_TRUE(matcher.HasUnknownAttributes());
  EXPECT_THAT(matcher.GetMissingAttributes(), IsEmpty());
  EXPECT_THAT(matcher.GetUnknownAttributes(),
              UnorderedElementsAre(AttributePattern(
                  "foo", {AttributeQualifierPattern::OfString("bar"),
                          AttributeQualifierPattern::OfString("baz")})));
}

TEST(AttributeMatcher, Unknown_Attribute_OneVariable_ZeroQualifier) {
  AttributeMatcher matcher;
  EXPECT_THAT(matcher.AddUnknownAttribute(Attribute("foo")), IsOk());
  EXPECT_FALSE(matcher.HasMissingAttributes());
  EXPECT_TRUE(matcher.HasUnknownAttributes());
  EXPECT_THAT(matcher.GetMissingAttributes(), IsEmpty());
  EXPECT_THAT(matcher.GetUnknownAttributes(),
              UnorderedElementsAre(AttributePattern("foo")));
  EXPECT_THAT(matcher.MatchVariable("foo"),
              AttributeMatchIs(MatchType::NONE, MatchType::FULL));
}

TEST(AttributeMatcher, Unknown_Attribute_OneVariable_OneQualifier) {
  AttributeMatcher matcher;
  EXPECT_THAT(matcher.AddUnknownAttribute(
                  Attribute("foo", {AttributeQualifier::OfString("bar")})),
              IsOk());
  EXPECT_FALSE(matcher.HasMissingAttributes());
  EXPECT_TRUE(matcher.HasUnknownAttributes());
  EXPECT_THAT(matcher.GetMissingAttributes(), IsEmpty());
  EXPECT_THAT(matcher.GetUnknownAttributes(),
              UnorderedElementsAre(AttributePattern(
                  "foo", {AttributeQualifierPattern::OfString("bar")})));
  EXPECT_THAT(matcher.MatchVariable("foo"),
              AttributeMatchIs(MatchType::NONE, MatchType::PARTIAL));
  EXPECT_THAT(matcher.MatchVariable("foo").MatchQualifier(
                  AttributeQualifierView::OfString("bar")),
              AttributeMatchIs(MatchType::NONE, MatchType::FULL));
}

TEST(AttributeMatcher, Unknown_Attribute_OneVariable_TwoQualifier) {
  AttributeMatcher matcher;
  EXPECT_THAT(matcher.AddUnknownAttribute(
                  Attribute("foo", {AttributeQualifier::OfString("bar")})),
              IsOk());
  EXPECT_THAT(matcher.AddUnknownAttribute(
                  Attribute("foo", {AttributeQualifier::OfString("baz")})),
              IsOk());
  EXPECT_FALSE(matcher.HasMissingAttributes());
  EXPECT_TRUE(matcher.HasUnknownAttributes());
  EXPECT_THAT(matcher.GetMissingAttributes(), IsEmpty());
  EXPECT_THAT(
      matcher.GetUnknownAttributes(),
      UnorderedElementsAre(
          AttributePattern("foo", {AttributeQualifierPattern::OfString("bar")}),
          AttributePattern("foo",
                           {AttributeQualifierPattern::OfString("baz")})));
  EXPECT_THAT(matcher.MatchVariable("foo"),
              AttributeMatchIs(MatchType::NONE, MatchType::PARTIAL));
  EXPECT_THAT(matcher.MatchVariable("foo").MatchQualifier(
                  AttributeQualifierView::OfString("bar")),
              AttributeMatchIs(MatchType::NONE, MatchType::FULL));
  EXPECT_THAT(matcher.MatchVariable("foo").MatchQualifier(
                  AttributeQualifierView::OfString("baz")),
              AttributeMatchIs(MatchType::NONE, MatchType::FULL));
}

TEST(AttributeMatcher, Unknown_AttributePattern_OneVariable_ZeroQualifier) {
  AttributeMatcher matcher;
  EXPECT_THAT(matcher.AddUnknownAttribute(AttributePattern("foo")), IsOk());
  EXPECT_FALSE(matcher.HasMissingAttributes());
  EXPECT_TRUE(matcher.HasUnknownAttributes());
  EXPECT_THAT(matcher.GetMissingAttributes(), IsEmpty());
  EXPECT_THAT(matcher.GetUnknownAttributes(),
              UnorderedElementsAre(AttributePattern("foo")));
  EXPECT_THAT(matcher.MatchVariable("foo"),
              AttributeMatchIs(MatchType::NONE, MatchType::FULL));
}

TEST(AttributeMatcher, Unknown_AttributePattern_OneVariable_OneQualifier) {
  AttributeMatcher matcher;
  EXPECT_THAT(matcher.AddUnknownAttribute(AttributePattern(
                  "foo", {AttributeQualifierPattern::OfString("bar")})),
              IsOk());
  EXPECT_FALSE(matcher.HasMissingAttributes());
  EXPECT_TRUE(matcher.HasUnknownAttributes());
  EXPECT_THAT(matcher.GetMissingAttributes(), IsEmpty());
  EXPECT_THAT(matcher.GetUnknownAttributes(),
              UnorderedElementsAre(AttributePattern(
                  "foo", {AttributeQualifierPattern::OfString("bar")})));
  EXPECT_THAT(matcher.MatchVariable("foo"),
              AttributeMatchIs(MatchType::NONE, MatchType::PARTIAL));
  EXPECT_THAT(matcher.MatchVariable("foo").MatchQualifier(
                  AttributeQualifierView::OfString("bar")),
              AttributeMatchIs(MatchType::NONE, MatchType::FULL));
}

TEST(AttributeMatcher, Unknown_AttributePattern_OneVariable_TwoQualifier) {
  AttributeMatcher matcher;
  EXPECT_THAT(matcher.AddUnknownAttribute(AttributePattern(
                  "foo", {AttributeQualifierPattern::OfString("bar")})),
              IsOk());
  EXPECT_THAT(matcher.AddUnknownAttribute(AttributePattern(
                  "foo", {AttributeQualifierPattern::OfString("baz")})),
              IsOk());
  EXPECT_FALSE(matcher.HasMissingAttributes());
  EXPECT_TRUE(matcher.HasUnknownAttributes());
  EXPECT_THAT(matcher.GetMissingAttributes(), IsEmpty());
  EXPECT_THAT(
      matcher.GetUnknownAttributes(),
      UnorderedElementsAre(
          AttributePattern("foo", {AttributeQualifierPattern::OfString("bar")}),
          AttributePattern("foo",
                           {AttributeQualifierPattern::OfString("baz")})));
  EXPECT_THAT(matcher.MatchVariable("foo"),
              AttributeMatchIs(MatchType::NONE, MatchType::PARTIAL));
  EXPECT_THAT(matcher.MatchVariable("foo").MatchQualifier(
                  AttributeQualifierView::OfString("bar")),
              AttributeMatchIs(MatchType::NONE, MatchType::FULL));
  EXPECT_THAT(matcher.MatchVariable("foo").MatchQualifier(
                  AttributeQualifierView::OfString("baz")),
              AttributeMatchIs(MatchType::NONE, MatchType::FULL));
}

TEST(AttributeMatcher, ClearMissingAttributes) {
  AttributeMatcher matcher;
  EXPECT_THAT(matcher.AddMissingAttribute(
                  Attribute("foo", {AttributeQualifier::OfString("bar")})),
              IsOk());
  EXPECT_THAT(matcher.AddUnknownAttribute(
                  Attribute("foo", {AttributeQualifier::OfString("bar")})),
              IsOk());
  EXPECT_THAT(matcher.AddMissingAttribute(
                  Attribute("bar", {AttributeQualifier::OfString("foo")})),
              IsOk());
  EXPECT_THAT(matcher.AddUnknownAttribute(Attribute("bar")), IsOk());
  EXPECT_THAT(matcher.AddMissingAttribute(
                  Attribute("baz", {AttributeQualifier::OfString("foo")})),
              IsOk());
  EXPECT_THAT(matcher.AddUnknownAttribute(
                  Attribute("baz", {AttributeQualifier::OfString("foo"),
                                    AttributeQualifier::OfString("bar")})),
              IsOk());
  matcher.ClearMissingAttributes();
  EXPECT_FALSE(matcher.HasMissingAttributes());
  EXPECT_THAT(matcher.GetMissingAttributes(), IsEmpty());
  EXPECT_TRUE(matcher.HasUnknownAttributes());
  EXPECT_THAT(
      matcher.GetUnknownAttributes(),
      UnorderedElementsAre(
          AttributePattern("foo", {AttributeQualifierPattern::OfString("bar")}),
          AttributePattern("bar"),
          AttributePattern("baz",
                           {AttributeQualifierPattern::OfString("foo"),
                            AttributeQualifierPattern::OfString("bar")})));
}

TEST(AttributeMatcher, ClearUnknownAttributes) {
  AttributeMatcher matcher;
  EXPECT_THAT(matcher.AddUnknownAttribute(
                  Attribute("foo", {AttributeQualifier::OfString("bar")})),
              IsOk());
  EXPECT_THAT(matcher.AddMissingAttribute(
                  Attribute("foo", {AttributeQualifier::OfString("bar")})),
              IsOk());
  EXPECT_THAT(matcher.AddUnknownAttribute(
                  Attribute("bar", {AttributeQualifier::OfString("foo")})),
              IsOk());
  EXPECT_THAT(matcher.AddMissingAttribute(Attribute("bar")), IsOk());
  EXPECT_THAT(matcher.AddUnknownAttribute(
                  Attribute("baz", {AttributeQualifier::OfString("foo")})),
              IsOk());
  EXPECT_THAT(matcher.AddMissingAttribute(
                  Attribute("baz", {AttributeQualifier::OfString("foo"),
                                    AttributeQualifier::OfString("bar")})),
              IsOk());
  matcher.ClearUnknownAttributes();
  EXPECT_FALSE(matcher.HasUnknownAttributes());
  EXPECT_THAT(matcher.GetUnknownAttributes(), IsEmpty());
  EXPECT_TRUE(matcher.HasMissingAttributes());
  EXPECT_THAT(
      matcher.GetMissingAttributes(),
      UnorderedElementsAre(
          AttributePattern("foo", {AttributeQualifierPattern::OfString("bar")}),
          AttributePattern("bar"),
          AttributePattern("baz",
                           {AttributeQualifierPattern::OfString("foo"),
                            AttributeQualifierPattern::OfString("bar")})));
}

TEST(AttributeMatcher, RemoveMissingAttribute) {
  const std::vector<AttributePattern> unknown_attributes(
      {AttributePattern("foo", {AttributeQualifierPattern::OfString("bar")}),
       AttributePattern("bar", {AttributeQualifierPattern::OfString("foo")}),
       AttributePattern("baz", {AttributeQualifierPattern::OfString("foo")})});
  const std::vector<AttributePattern> missing_attributes(
      {AttributePattern("foo", {AttributeQualifierPattern::OfString("bar")}),
       AttributePattern("bar"),
       AttributePattern("baz", {AttributeQualifierPattern::OfString("foo"),
                                AttributeQualifierPattern::OfString("bar")})});
  AttributeMatcher matcher;
  for (const auto& attribute : unknown_attributes) {
    EXPECT_THAT(matcher.AddUnknownAttribute(attribute), IsOk());
  }
  std::vector<size_t> removal_order({0, 1, 2});
  do {
    for (const auto& attribute : missing_attributes) {
      EXPECT_THAT(matcher.AddMissingAttribute(attribute), IsOk());
    }
    // 0
    EXPECT_THAT(
        matcher.RemoveMissingAttribute(missing_attributes[removal_order[0]]),
        IsOk());
    EXPECT_TRUE(matcher.HasMissingAttributes());
    EXPECT_THAT(matcher.GetMissingAttributes(),
                UnorderedElementsAre(missing_attributes[removal_order[1]],
                                     missing_attributes[removal_order[2]]));
    EXPECT_TRUE(matcher.HasUnknownAttributes());
    EXPECT_THAT(matcher.GetUnknownAttributes(),
                UnorderedElementsAreArray(unknown_attributes));
    // 1
    EXPECT_THAT(
        matcher.RemoveMissingAttribute(missing_attributes[removal_order[1]]),
        IsOk());
    EXPECT_TRUE(matcher.HasMissingAttributes());
    EXPECT_THAT(matcher.GetMissingAttributes(),
                UnorderedElementsAre(missing_attributes[removal_order[2]]));
    EXPECT_TRUE(matcher.HasUnknownAttributes());
    EXPECT_THAT(matcher.GetUnknownAttributes(),
                UnorderedElementsAreArray(unknown_attributes));
    // 2
    EXPECT_THAT(
        matcher.RemoveMissingAttribute(missing_attributes[removal_order[2]]),
        IsOk());
    EXPECT_FALSE(matcher.HasMissingAttributes());
    EXPECT_THAT(matcher.GetMissingAttributes(), IsEmpty());
    EXPECT_TRUE(matcher.HasUnknownAttributes());
    EXPECT_THAT(matcher.GetUnknownAttributes(),
                UnorderedElementsAreArray(unknown_attributes));
  } while (std::next_permutation(removal_order.begin(), removal_order.end()));
}

TEST(AttributeMatcher, RemoveUnknownAttribute) {
  const std::vector<AttributePattern> missing_attributes(
      {AttributePattern("foo", {AttributeQualifierPattern::OfString("bar")}),
       AttributePattern("bar", {AttributeQualifierPattern::OfString("foo")}),
       AttributePattern("baz", {AttributeQualifierPattern::OfString("foo")})});
  const std::vector<AttributePattern> unknown_attributes(
      {AttributePattern("foo", {AttributeQualifierPattern::OfString("bar")}),
       AttributePattern("bar"),
       AttributePattern("baz", {AttributeQualifierPattern::OfString("foo"),
                                AttributeQualifierPattern::OfString("bar")})});
  AttributeMatcher matcher;
  for (const auto& attribute : missing_attributes) {
    EXPECT_THAT(matcher.AddMissingAttribute(attribute), IsOk());
  }
  std::vector<size_t> removal_order({0, 1, 2});
  do {
    for (const auto& attribute : unknown_attributes) {
      EXPECT_THAT(matcher.AddUnknownAttribute(attribute), IsOk());
    }
    // 0
    EXPECT_THAT(
        matcher.RemoveUnknownAttribute(unknown_attributes[removal_order[0]]),
        IsOk());
    EXPECT_TRUE(matcher.HasUnknownAttributes());
    EXPECT_THAT(matcher.GetUnknownAttributes(),
                UnorderedElementsAre(unknown_attributes[removal_order[1]],
                                     unknown_attributes[removal_order[2]]));
    EXPECT_TRUE(matcher.HasMissingAttributes());
    EXPECT_THAT(matcher.GetMissingAttributes(),
                UnorderedElementsAreArray(missing_attributes));
    // 1
    EXPECT_THAT(
        matcher.RemoveUnknownAttribute(unknown_attributes[removal_order[1]]),
        IsOk());
    EXPECT_TRUE(matcher.HasUnknownAttributes());
    EXPECT_THAT(matcher.GetUnknownAttributes(),
                UnorderedElementsAre(unknown_attributes[removal_order[2]]));
    EXPECT_TRUE(matcher.HasMissingAttributes());
    EXPECT_THAT(matcher.GetMissingAttributes(),
                UnorderedElementsAreArray(missing_attributes));
    // 2
    EXPECT_THAT(
        matcher.RemoveUnknownAttribute(unknown_attributes[removal_order[2]]),
        IsOk());
    EXPECT_FALSE(matcher.HasUnknownAttributes());
    EXPECT_THAT(matcher.GetUnknownAttributes(), IsEmpty());
    EXPECT_TRUE(matcher.HasMissingAttributes());
    EXPECT_THAT(matcher.GetMissingAttributes(),
                UnorderedElementsAreArray(missing_attributes));
  } while (std::next_permutation(removal_order.begin(), removal_order.end()));
}

TEST(AttributeMatcher, Wildcards) {
  AttributeMatcher matcher;
  EXPECT_THAT(matcher.AddMissingAttribute(AttributePattern(
                  "foo", {AttributeQualifierPattern::Wildcard(),
                          AttributeQualifierPattern::Wildcard()})),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(matcher.AddUnknownAttribute(AttributePattern(
                  "foo", {AttributeQualifierPattern::Wildcard(),
                          AttributeQualifierPattern::Wildcard()})),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(matcher.AddMissingAttribute(AttributePattern(
                  "foo", {AttributeQualifierPattern::Wildcard()})),
              IsOk());
  EXPECT_THAT(matcher.AddMissingAttribute(
                  Attribute("foo", {AttributeQualifier::OfString("bar")})),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_THAT(matcher.AddUnknownAttribute(AttributePattern(
                  "foo", {AttributeQualifierPattern::Wildcard()})),
              IsOk());
  EXPECT_THAT(matcher.AddUnknownAttribute(
                  Attribute("foo", {AttributeQualifier::OfString("bar")})),
              StatusIs(absl::StatusCode::kAlreadyExists));
}

}  // namespace
}  // namespace cel
