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
using ::testing::_;
using ::testing::UnorderedElementsAre;

using MatchType = AttributePattern::MatchType;

MATCHER_P(AttributeMatchIs, type, "") {
  return ::testing::ExplainMatchResult(::testing::Eq(type), arg.GetType(),
                                       result_listener);
}

using ::testing::IsEmpty;

TEST(AttributeMatcher, Empty) {
  AttributeMatcher matcher;
  EXPECT_FALSE(matcher.HasAttributes());
  EXPECT_THAT(matcher.GetAttributes(), IsEmpty());
  EXPECT_THAT(matcher.MatchVariable("foo"), AttributeMatchIs(MatchType::NONE));
}

TEST(AttributeMatcher, BadAttribute) {
  AttributeMatcher matcher;
  EXPECT_THAT(matcher.InsertAttribute(Attribute("")),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(matcher.InsertAttribute(AttributePattern("")),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(matcher.InsertAttribute(Attribute("foo", {AttributeQualifier()})),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(matcher.InsertAttribute(
                  AttributePattern("foo", {AttributeQualifierPattern()})),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(matcher.UpsertAttribute(Attribute("")),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(matcher.UpsertAttribute(AttributePattern("")),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(matcher.UpsertAttribute(Attribute("foo", {AttributeQualifier()})),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(matcher.UpsertAttribute(
                  AttributePattern("foo", {AttributeQualifierPattern()})),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(matcher.RemoveAttribute(Attribute("")),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(matcher.RemoveAttribute(AttributePattern("")),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(matcher.RemoveAttribute(Attribute("foo", {AttributeQualifier()})),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(matcher.RemoveAttribute(
                  AttributePattern("foo", {AttributeQualifierPattern()})),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(AttributeMatcher, Attribute_OneVariable_ZeroQualifier_Subset) {
  AttributeMatcher matcher;
  EXPECT_THAT(matcher.InsertAttribute(Attribute("foo")), IsOk());
  EXPECT_THAT(matcher.InsertAttribute(Attribute("foo")),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_THAT(matcher.InsertAttribute(
                  Attribute("foo", {AttributeQualifier::OfString("bar")})),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_THAT(matcher.InsertAttribute(AttributePattern("foo")),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_THAT(matcher.InsertAttribute(AttributePattern(
                  "foo", {AttributeQualifierPattern::OfString("bar")})),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_THAT(matcher.InsertAttribute(AttributePattern(
                  "foo", {AttributeQualifierPattern::Wildcard()})),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_TRUE(matcher.HasAttributes());
  EXPECT_THAT(matcher.GetAttributes(),
              UnorderedElementsAre(AttributePattern("foo")));
}

TEST(AttributeMatcher,
     Unknown_AttributePattern_OneVariable_ZeroQualifier_Subset) {
  AttributeMatcher matcher;
  EXPECT_THAT(matcher.InsertAttribute(AttributePattern("foo")), IsOk());
  EXPECT_THAT(matcher.InsertAttribute(Attribute("foo")),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_THAT(matcher.InsertAttribute(
                  Attribute("foo", {AttributeQualifier::OfString("bar")})),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_THAT(matcher.InsertAttribute(AttributePattern("foo")),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_THAT(matcher.InsertAttribute(AttributePattern(
                  "foo", {AttributeQualifierPattern::OfString("bar")})),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_THAT(matcher.InsertAttribute(AttributePattern(
                  "foo", {AttributeQualifierPattern::Wildcard()})),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_TRUE(matcher.HasAttributes());
  EXPECT_THAT(matcher.GetAttributes(),
              UnorderedElementsAre(AttributePattern("foo")));
}

TEST(AttributeMatcher, Unknown_Attribute_OneVariable_TwoQualifier_Superset) {
  AttributeMatcher matcher;
  EXPECT_THAT(matcher.InsertAttribute(
                  Attribute("foo", {AttributeQualifier::OfString("bar"),
                                    AttributeQualifier::OfString("baz")})),
              IsOk());
  EXPECT_THAT(matcher.InsertAttribute(Attribute("foo")),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_THAT(matcher.InsertAttribute(
                  Attribute("foo", {AttributeQualifier::OfString("bar")})),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_THAT(matcher.InsertAttribute(AttributePattern("foo")),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_THAT(matcher.InsertAttribute(AttributePattern(
                  "foo", {AttributeQualifierPattern::OfString("bar")})),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_TRUE(matcher.HasAttributes());
  EXPECT_THAT(matcher.GetAttributes(),
              UnorderedElementsAre(AttributePattern(
                  "foo", {AttributeQualifierPattern::OfString("bar"),
                          AttributeQualifierPattern::OfString("baz")})));
}

TEST(AttributeMatcher,
     Unknown_AttributePattern_OneVariable_TwoQualifier_Superset) {
  AttributeMatcher matcher;
  EXPECT_THAT(matcher.InsertAttribute(AttributePattern(
                  "foo", {AttributeQualifierPattern::OfString("bar"),
                          AttributeQualifierPattern::OfString("baz")})),
              IsOk());
  EXPECT_THAT(matcher.InsertAttribute(Attribute("foo")),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_THAT(matcher.InsertAttribute(
                  Attribute("foo", {AttributeQualifier::OfString("bar")})),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_THAT(matcher.InsertAttribute(AttributePattern("foo")),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_THAT(matcher.InsertAttribute(AttributePattern(
                  "foo", {AttributeQualifierPattern::OfString("bar")})),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_TRUE(matcher.HasAttributes());
  EXPECT_THAT(matcher.GetAttributes(),
              UnorderedElementsAre(AttributePattern(
                  "foo", {AttributeQualifierPattern::OfString("bar"),
                          AttributeQualifierPattern::OfString("baz")})));
}

TEST(AttributeMatcher, Unknown_Attribute_OneVariable_ZeroQualifier) {
  AttributeMatcher matcher;
  EXPECT_THAT(matcher.InsertAttribute(Attribute("foo")), IsOk());
  EXPECT_TRUE(matcher.HasAttributes());
  EXPECT_THAT(matcher.GetAttributes(),
              UnorderedElementsAre(AttributePattern("foo")));
  EXPECT_THAT(matcher.MatchVariable("foo"), AttributeMatchIs(MatchType::FULL));
}

TEST(AttributeMatcher, Unknown_Attribute_OneVariable_OneQualifier) {
  AttributeMatcher matcher;
  EXPECT_THAT(matcher.InsertAttribute(
                  Attribute("foo", {AttributeQualifier::OfString("bar")})),
              IsOk());
  EXPECT_TRUE(matcher.HasAttributes());
  EXPECT_THAT(matcher.GetAttributes(),
              UnorderedElementsAre(AttributePattern(
                  "foo", {AttributeQualifierPattern::OfString("bar")})));
  EXPECT_THAT(matcher.MatchVariable("foo"),
              AttributeMatchIs(MatchType::PARTIAL));
  EXPECT_THAT(matcher.MatchVariable("foo").MatchQualifier(
                  AttributeQualifierView::OfString("bar")),
              AttributeMatchIs(MatchType::FULL));
}

TEST(AttributeMatcher, Unknown_Attribute_OneVariable_TwoQualifier) {
  AttributeMatcher matcher;
  EXPECT_THAT(matcher.InsertAttribute(
                  Attribute("foo", {AttributeQualifier::OfString("bar")})),
              IsOk());
  EXPECT_THAT(matcher.InsertAttribute(
                  Attribute("foo", {AttributeQualifier::OfString("baz")})),
              IsOk());
  EXPECT_TRUE(matcher.HasAttributes());
  EXPECT_THAT(
      matcher.GetAttributes(),
      UnorderedElementsAre(
          AttributePattern("foo", {AttributeQualifierPattern::OfString("bar")}),
          AttributePattern("foo",
                           {AttributeQualifierPattern::OfString("baz")})));
  EXPECT_THAT(matcher.MatchVariable("foo"),
              AttributeMatchIs(MatchType::PARTIAL));
  EXPECT_THAT(matcher.MatchVariable("foo").MatchQualifier(
                  AttributeQualifierView::OfString("bar")),
              AttributeMatchIs(MatchType::FULL));
  EXPECT_THAT(matcher.MatchVariable("foo").MatchQualifier(
                  AttributeQualifierView::OfString("baz")),
              AttributeMatchIs(MatchType::FULL));
}

TEST(AttributeMatcher, Unknown_AttributePattern_OneVariable_ZeroQualifier) {
  AttributeMatcher matcher;
  EXPECT_THAT(matcher.InsertAttribute(AttributePattern("foo")), IsOk());
  EXPECT_TRUE(matcher.HasAttributes());
  EXPECT_THAT(matcher.GetAttributes(),
              UnorderedElementsAre(AttributePattern("foo")));
  EXPECT_THAT(matcher.MatchVariable("foo"), AttributeMatchIs(MatchType::FULL));
}

TEST(AttributeMatcher, Unknown_AttributePattern_OneVariable_OneQualifier) {
  AttributeMatcher matcher;
  EXPECT_THAT(matcher.InsertAttribute(AttributePattern(
                  "foo", {AttributeQualifierPattern::OfString("bar")})),
              IsOk());
  EXPECT_TRUE(matcher.HasAttributes());
  EXPECT_THAT(matcher.GetAttributes(),
              UnorderedElementsAre(AttributePattern(
                  "foo", {AttributeQualifierPattern::OfString("bar")})));
  EXPECT_THAT(matcher.MatchVariable("foo"),
              AttributeMatchIs(MatchType::PARTIAL));
  EXPECT_THAT(matcher.MatchVariable("foo").MatchQualifier(
                  AttributeQualifierView::OfString("bar")),
              AttributeMatchIs(MatchType::FULL));
}

TEST(AttributeMatcher, Unknown_AttributePattern_OneVariable_TwoQualifier) {
  AttributeMatcher matcher;
  EXPECT_THAT(matcher.InsertAttribute(AttributePattern(
                  "foo", {AttributeQualifierPattern::OfString("bar")})),
              IsOk());
  EXPECT_THAT(matcher.InsertAttribute(AttributePattern(
                  "foo", {AttributeQualifierPattern::OfString("baz")})),
              IsOk());
  EXPECT_TRUE(matcher.HasAttributes());
  EXPECT_THAT(
      matcher.GetAttributes(),
      UnorderedElementsAre(
          AttributePattern("foo", {AttributeQualifierPattern::OfString("bar")}),
          AttributePattern("foo",
                           {AttributeQualifierPattern::OfString("baz")})));
  EXPECT_THAT(matcher.MatchVariable("foo"),
              AttributeMatchIs(MatchType::PARTIAL));
  EXPECT_THAT(matcher.MatchVariable("foo").MatchQualifier(
                  AttributeQualifierView::OfString("bar")),
              AttributeMatchIs(MatchType::FULL));
  EXPECT_THAT(matcher.MatchVariable("foo").MatchQualifier(
                  AttributeQualifierView::OfString("baz")),
              AttributeMatchIs(MatchType::FULL));
}

TEST(AttributeMatcher, RemoveAttribute) {
  const std::vector<AttributePattern> unknown_attributes(
      {AttributePattern("foo", {AttributeQualifierPattern::OfString("bar")}),
       AttributePattern("bar", {AttributeQualifierPattern::OfString("foo")}),
       AttributePattern("baz", {AttributeQualifierPattern::OfString("foo")})});
  AttributeMatcher matcher;
  std::vector<size_t> removal_order({0, 1, 2});
  do {
    for (const auto& attribute : unknown_attributes) {
      EXPECT_THAT(matcher.InsertAttribute(attribute), IsOk());
    }
    // 0
    EXPECT_THAT(matcher.RemoveAttribute(unknown_attributes[removal_order[0]]),
                IsOk());
    EXPECT_TRUE(matcher.HasAttributes());
    EXPECT_THAT(matcher.GetAttributes(),
                UnorderedElementsAre(unknown_attributes[removal_order[1]],
                                     unknown_attributes[removal_order[2]]));
    // 1
    EXPECT_THAT(matcher.RemoveAttribute(unknown_attributes[removal_order[1]]),
                IsOk());
    EXPECT_TRUE(matcher.HasAttributes());
    EXPECT_THAT(matcher.GetAttributes(),
                UnorderedElementsAre(unknown_attributes[removal_order[2]]));
    // 2
    EXPECT_THAT(matcher.RemoveAttribute(unknown_attributes[removal_order[2]]),
                IsOk());
    EXPECT_FALSE(matcher.HasAttributes());
    EXPECT_THAT(matcher.GetAttributes(), IsEmpty());
  } while (std::next_permutation(removal_order.begin(), removal_order.end()));
}

TEST(AttributeMatcher, Wildcards) {
  AttributeMatcher matcher;
  EXPECT_THAT(matcher.InsertAttribute(AttributePattern(
                  "foo", {AttributeQualifierPattern::Wildcard(),
                          AttributeQualifierPattern::Wildcard()})),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(matcher.InsertAttribute(AttributePattern(
                  "foo", {AttributeQualifierPattern::Wildcard()})),
              IsOk());
  EXPECT_THAT(matcher.InsertAttribute(AttributePattern(
                  "bar", {AttributeQualifierPattern::OfString("baz"),
                          AttributeQualifierPattern::Wildcard()})),
              IsOk());
  EXPECT_THAT(matcher.InsertAttribute(
                  Attribute("foo", {AttributeQualifier::OfString("bar")})),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_THAT(matcher.InsertAttribute(
                  Attribute("bar", {AttributeQualifier::OfString("baz")})),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_THAT(matcher.MatchVariable("foo").MatchQualifier(
                  AttributeQualifierView::OfString("bar")),
              AttributeMatchIs(MatchType::FULL));
  EXPECT_THAT(matcher.MatchVariable("foo").MatchQualifier(
                  AttributeQualifierView::OfInt(1)),
              AttributeMatchIs(MatchType::FULL));
  EXPECT_THAT(matcher.MatchVariable("foo").MatchQualifier(
                  AttributeQualifierView::OfUint(1)),
              AttributeMatchIs(MatchType::FULL));
  EXPECT_THAT(matcher.MatchVariable("foo").MatchQualifier(
                  AttributeQualifierView::OfBool(true)),
              AttributeMatchIs(MatchType::FULL));
  EXPECT_THAT(
      matcher.GetAttributes(),
      UnorderedElementsAre(
          AttributePattern("foo", {AttributeQualifierPattern::Wildcard()}),
          AttributePattern("bar", {AttributeQualifierPattern::OfString("baz"),
                                   AttributeQualifierPattern::Wildcard()})));
  EXPECT_THAT(matcher.RemoveAttribute(Attribute("foo")),
              StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(matcher.RemoveAttribute(
                  Attribute("foo", {AttributeQualifier::OfString("bar")})),
              StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(matcher.RemoveAttribute(
                  Attribute("foo", {AttributeQualifier::OfString("bar"),
                                    AttributeQualifier::OfString("baz")})),
              StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(matcher.RemoveAttribute(Attribute("bar")),
              StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(matcher.RemoveAttribute(
                  Attribute("bar", {AttributeQualifier::OfString("baz")})),
              StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(matcher.RemoveAttribute(
                  Attribute("bar", {AttributeQualifier::OfString("baz"),
                                    AttributeQualifier::OfString("foo")})),
              StatusIs(absl::StatusCode::kNotFound));
}

TEST(AttributeMatcher, Upsert) {
  AttributeMatcher matcher;
  EXPECT_THAT(matcher.InsertAttribute(
                  Attribute("foo", {AttributeQualifier::OfString("bar"),
                                    AttributeQualifier::OfString("baz")})),
              IsOk());
  EXPECT_THAT(matcher.GetAttributes(),
              UnorderedElementsAre(AttributePattern(
                  "foo", {AttributeQualifierPattern::OfString("bar"),
                          AttributeQualifierPattern::OfString("baz")})));

  // Upserting the same attribute does nothing.
  EXPECT_THAT(matcher.UpsertAttribute(
                  Attribute("foo", {AttributeQualifier::OfString("bar"),
                                    AttributeQualifier::OfString("baz")})),
              IsOk());
  EXPECT_THAT(matcher.GetAttributes(),
              UnorderedElementsAre(AttributePattern(
                  "foo", {AttributeQualifierPattern::OfString("bar"),
                          AttributeQualifierPattern::OfString("baz")})));

  // Upserting a more specific attribute does nothing.
  EXPECT_THAT(matcher.UpsertAttribute(
                  Attribute("foo", {AttributeQualifier::OfString("bar"),
                                    AttributeQualifier::OfString("baz"),
                                    AttributeQualifier::OfString("qux")})),
              IsOk());
  EXPECT_THAT(matcher.GetAttributes(),
              UnorderedElementsAre(AttributePattern(
                  "foo", {AttributeQualifierPattern::OfString("bar"),
                          AttributeQualifierPattern::OfString("baz")})));

  // Upserting a less specific attribute replaces others.
  EXPECT_THAT(matcher.UpsertAttribute(
                  Attribute("foo", {AttributeQualifier::OfString("bar"),
                                    AttributeQualifier::OfString("qux")})),
              IsOk());
  EXPECT_THAT(
      matcher.GetAttributes(),
      UnorderedElementsAre(
          AttributePattern("foo", {AttributeQualifierPattern::OfString("bar"),
                                   AttributeQualifierPattern::OfString("baz")}),
          AttributePattern("foo",
                           {AttributeQualifierPattern::OfString("bar"),
                            AttributeQualifierPattern::OfString("qux")})));
  EXPECT_THAT(matcher.UpsertAttribute(
                  Attribute("foo", {AttributeQualifier::OfString("bar")})),
              IsOk());
  EXPECT_THAT(matcher.GetAttributes(),
              UnorderedElementsAre(AttributePattern(
                  "foo", {AttributeQualifierPattern::OfString("bar")})));
}

TEST(AttributeMatcher, PastFullMatch) {
  AttributeMatcher matcher;
  EXPECT_THAT(matcher.InsertAttribute(
                  Attribute("foo", {AttributeQualifier::OfString("bar")})),
              IsOk());
  EXPECT_DEBUG_DEATH(
      static_cast<void>(
          matcher.MatchVariable("foo")
              .MatchQualifier(AttributeQualifierView::OfString("bar"))
              .MatchQualifier(AttributeQualifierView::OfString("baz"))),
      _);
}

}  // namespace
}  // namespace cel
