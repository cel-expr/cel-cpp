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

#include "common/internal/unknowns.h"

#include <algorithm>

#include "absl/status/status_matchers.h"
#include "base/attribute.h"
#include "base/attribute_matcher.h"
#include "internal/testing.h"
#include "google/protobuf/arena.h"

namespace cel::common_internal {
namespace {

using ::absl_testing::IsOk;
using ::testing::IsNull;
using ::testing::NotNull;

TEST(UnknownTree, MatchToString) {
  AttributeMatcher matcher;
  EXPECT_THAT(matcher.AddUnknownAttribute(
                  Attribute("foo", {AttributeQualifier::OfString("bar"),
                                    AttributeQualifier::OfString("baz")})),
              IsOk());
  google::protobuf::Arena arena;
  UnknownTree tree(&arena);
  EXPECT_EQ(tree.ToString(
                matcher.MatchVariable("foo")
                    .MatchQualifier(AttributeQualifierView::OfString("bar"))
                    .MatchQualifier(AttributeQualifierView::OfString("baz")),
                AttributeQualifierView::OfString("baz")),
            "foo[\"bar\"][\"baz\"]");
  EXPECT_EQ(tree.ToString(matcher.MatchVariable("foo").MatchQualifier(
                              AttributeQualifierView::OfString("bar")),
                          AttributeQualifierView::OfString("bar")),
            "foo[\"bar\"]");
  EXPECT_EQ(tree.ToString(matcher.MatchVariable("foo"),
                          AttributeQualifierView::OfString("foo")),
            "foo");
}

TEST(UnknownTree, UnknownToString) {
  google::protobuf::Arena arena;
  UnknownTree tree(&arena);
  UnknownAttributeNode* node_foo =
      tree.Root()->Attributes()->Step(&tree, UnknownAttributeKey("foo"));
  UnknownAttributeNode* node_bar =
      node_foo->Step(&tree, UnknownAttributeKey("bar"));
  UnknownAttributeNode* node_baz =
      node_bar->Step(&tree, UnknownAttributeKey("baz"));
  EXPECT_EQ(tree.ToString(node_baz), "foo[\"bar\"][\"baz\"]");
  EXPECT_EQ(tree.ToString(node_bar), "foo[\"bar\"]");
  EXPECT_EQ(tree.ToString(node_foo), "foo");
}

TEST(UnknownTree, UnknownAttributeConvert) {
  AttributeMatcher matcher;
  EXPECT_THAT(matcher.AddUnknownAttribute(
                  Attribute("foo", {AttributeQualifier::OfString("bar"),
                                    AttributeQualifier::OfString("baz")})),
              IsOk());
  google::protobuf::Arena arena;
  UnknownTree tree(&arena);
  UnknownAttributeNode* node_baz =
      tree.Convert(matcher.MatchVariable("foo")
                       .MatchQualifier(AttributeQualifierView::OfString("bar"))
                       .MatchQualifier(AttributeQualifierView::OfString("baz")),
                   AttributeQualifierView::OfString("baz"));
  UnknownAttributeNode* node_bar =
      tree.Convert(matcher.MatchVariable("foo").MatchQualifier(
                       AttributeQualifierView::OfString("bar")),
                   AttributeQualifierView::OfString("bar"));
  UnknownAttributeNode* node_foo = tree.Convert(
      matcher.MatchVariable("foo"), AttributeQualifierView::OfString("foo"));
  EXPECT_EQ(node_baz->parent(), node_bar);
  EXPECT_EQ(node_bar->parent(), node_foo);
  EXPECT_EQ(tree.ToString(node_baz), "foo[\"bar\"][\"baz\"]");
  EXPECT_EQ(tree.ToString(node_bar), "foo[\"bar\"]");
  EXPECT_EQ(tree.ToString(node_foo), "foo");
  EXPECT_EQ(
      tree.Root()->Attributes()->Find(AttributeQualifierView::OfString("foo")),
      node_foo);
}

TEST(UnknownTree, UnknownFunction) {
  google::protobuf::Arena arena;
  UnknownTree tree(&arena);

  EXPECT_THAT(tree.Root()->FindFunction("foo"), IsNull());
  UnknownFunctionNode* node_foo = tree.Root()->SetFunction(&tree, "foo");
  EXPECT_THAT(node_foo, NotNull());
  EXPECT_EQ(tree.Root()->FindFunction("foo"), node_foo);

  EXPECT_THAT(tree.Root()->FindFunction("bar"), IsNull());
  UnknownFunctionNode* node_bar = tree.Root()->SetFunction(&tree, "bar");
  EXPECT_THAT(node_bar, NotNull());
  EXPECT_NE(node_bar, node_foo);
  EXPECT_EQ(tree.Root()->FindFunction("bar"), node_bar);

  EXPECT_THAT(tree.Root()->FindFunction("baz"), IsNull());
  UnknownFunctionNode* node_baz = tree.Root()->SetFunction(&tree, "baz");
  EXPECT_THAT(node_baz, NotNull());
  EXPECT_NE(node_baz, node_foo);
  EXPECT_EQ(tree.Root()->FindFunction("baz"), node_baz);
}

void TestUnknownAttributeSets(const UnknownAttributeSet* lhs,
                              const UnknownAttributeSet* rhs) {
  EXPECT_EQ(lhs->size, rhs->size);
  EXPECT_EQ(lhs->max_depth, rhs->max_depth);
  EXPECT_TRUE(std::is_sorted(lhs->nodes, lhs->nodes + lhs->size));
  EXPECT_TRUE(std::is_sorted(rhs->nodes, rhs->nodes + rhs->size));
  EXPECT_TRUE(std::equal(lhs->nodes, lhs->nodes + lhs->size, rhs->nodes,
                         rhs->nodes + rhs->size));
}

TEST(UnknownTree, UnknownAttributeSet) {
  google::protobuf::Arena arena;
  UnknownTree tree(&arena);
  UnknownAttributeNode* node_foo =
      tree.Root()->Attributes()->Step(&tree, UnknownAttributeKey("foo"));
  UnknownAttributeNode* node_bar =
      tree.Root()->Attributes()->Step(&tree, UnknownAttributeKey("bar"));
  UnknownAttributeNode* node_baz =
      tree.Root()->Attributes()->Step(&tree, UnknownAttributeKey("baz"));

  const UnknownAttributeSet* set_foo =
      CreateUnknownAttributeSet(node_foo, tree.GetArena());
  EXPECT_EQ(set_foo->size, 1);
  EXPECT_EQ(set_foo->max_depth, 1);
  EXPECT_EQ(set_foo->nodes[0], node_foo);
  TestUnknownAttributeSets(CreateUnknownAttributeSet(node_foo, tree.GetArena()),
                           set_foo);

  const UnknownAttributeSet* set_bar =
      CreateUnknownAttributeSet(node_bar, tree.GetArena());
  EXPECT_EQ(set_bar->size, 1);
  EXPECT_EQ(set_bar->max_depth, 1);
  EXPECT_EQ(set_bar->nodes[0], node_bar);
  TestUnknownAttributeSets(CreateUnknownAttributeSet(node_bar, tree.GetArena()),
                           set_bar);

  const UnknownAttributeSet* set_baz =
      CreateUnknownAttributeSet(node_baz, tree.GetArena());
  EXPECT_EQ(set_baz->size, 1);
  EXPECT_EQ(set_baz->max_depth, 1);
  EXPECT_EQ(set_baz->nodes[0], node_baz);
  TestUnknownAttributeSets(CreateUnknownAttributeSet(node_baz, tree.GetArena()),
                           set_baz);

  const UnknownAttributeSet* set_foo_bar =
      MergeUnknownAttributeSets(set_foo, set_bar, &arena);
  EXPECT_EQ(set_foo_bar->size, 2);
  EXPECT_EQ(set_foo_bar->max_depth, 1);
  EXPECT_TRUE(std::is_sorted(set_foo_bar->nodes,
                             set_foo_bar->nodes + set_foo_bar->size));
  TestUnknownAttributeSets(
      MergeUnknownAttributeSets(set_bar, set_foo, tree.GetArena()),
      set_foo_bar);

  const UnknownAttributeSet* set_foo_bar_baz =
      MergeUnknownAttributeSets(set_foo_bar, set_baz, &arena);
  EXPECT_EQ(set_foo_bar_baz->size, 3);
  EXPECT_EQ(set_foo_bar_baz->max_depth, 1);
  EXPECT_TRUE(std::is_sorted(set_foo_bar_baz->nodes,
                             set_foo_bar_baz->nodes + set_foo_bar_baz->size));
  TestUnknownAttributeSets(
      MergeUnknownAttributeSets(set_baz, set_foo_bar, tree.GetArena()),
      set_foo_bar_baz);

  EXPECT_EQ(MergeUnknownAttributeSets(set_foo_bar_baz, set_foo, &arena),
            set_foo_bar_baz);
  EXPECT_EQ(MergeUnknownAttributeSets(set_foo_bar_baz, set_bar, &arena),
            set_foo_bar_baz);
  EXPECT_EQ(MergeUnknownAttributeSets(set_foo_bar_baz, set_baz, &arena),
            set_foo_bar_baz);
  EXPECT_EQ(MergeUnknownAttributeSets(set_foo_bar_baz, set_foo_bar, &arena),
            set_foo_bar_baz);
  EXPECT_EQ(MergeUnknownAttributeSets(set_foo, set_foo_bar_baz, &arena),
            set_foo_bar_baz);
  EXPECT_EQ(MergeUnknownAttributeSets(set_bar, set_foo_bar_baz, &arena),
            set_foo_bar_baz);
  EXPECT_EQ(MergeUnknownAttributeSets(set_baz, set_foo_bar_baz, &arena),
            set_foo_bar_baz);
  EXPECT_EQ(MergeUnknownAttributeSets(set_foo_bar, set_foo_bar_baz, &arena),
            set_foo_bar_baz);
}

void TestUnknownFunctionSets(const UnknownFunctionSet* lhs,
                             const UnknownFunctionSet* rhs) {
  EXPECT_EQ(lhs->size, rhs->size);
  EXPECT_TRUE(std::is_sorted(lhs->nodes, lhs->nodes + lhs->size));
  EXPECT_TRUE(std::is_sorted(rhs->nodes, rhs->nodes + rhs->size));
  EXPECT_TRUE(std::equal(lhs->nodes, lhs->nodes + lhs->size, rhs->nodes,
                         rhs->nodes + rhs->size));
}

TEST(UnknownTree, UnknownFunctionSet) {
  google::protobuf::Arena arena;
  UnknownTree tree(&arena);
  UnknownFunctionNode* node_foo = tree.Root()->SetFunction(&tree, "foo");
  UnknownFunctionNode* node_bar = tree.Root()->SetFunction(&tree, "bar");
  UnknownFunctionNode* node_baz = tree.Root()->SetFunction(&tree, "baz");

  const UnknownFunctionSet* set_foo =
      CreateUnknownFunctionSet(node_foo, tree.GetArena());
  EXPECT_EQ(set_foo->size, 1);
  EXPECT_EQ(set_foo->nodes[0], node_foo);
  TestUnknownFunctionSets(CreateUnknownFunctionSet(node_foo, tree.GetArena()),
                          set_foo);

  const UnknownFunctionSet* set_bar =
      CreateUnknownFunctionSet(node_bar, tree.GetArena());
  EXPECT_EQ(set_bar->size, 1);
  EXPECT_EQ(set_bar->nodes[0], node_bar);
  TestUnknownFunctionSets(CreateUnknownFunctionSet(node_bar, tree.GetArena()),
                          set_bar);

  const UnknownFunctionSet* set_baz =
      CreateUnknownFunctionSet(node_baz, tree.GetArena());
  EXPECT_EQ(set_baz->size, 1);
  EXPECT_EQ(set_baz->nodes[0], node_baz);
  TestUnknownFunctionSets(CreateUnknownFunctionSet(node_baz, tree.GetArena()),
                          set_baz);

  const UnknownFunctionSet* set_foo_bar =
      MergeUnknownFunctionSets(set_foo, set_bar, &arena);
  EXPECT_EQ(set_foo_bar->size, 2);
  EXPECT_TRUE(std::is_sorted(set_foo_bar->nodes,
                             set_foo_bar->nodes + set_foo_bar->size));
  TestUnknownFunctionSets(MergeUnknownFunctionSets(set_bar, set_foo, &arena),
                          set_foo_bar);

  const UnknownFunctionSet* set_foo_bar_baz =
      MergeUnknownFunctionSets(set_foo_bar, set_baz, &arena);
  EXPECT_EQ(set_foo_bar_baz->size, 3);
  EXPECT_TRUE(std::is_sorted(set_foo_bar_baz->nodes,
                             set_foo_bar_baz->nodes + set_foo_bar_baz->size));
  TestUnknownFunctionSets(
      MergeUnknownFunctionSets(set_baz, set_foo_bar, &arena), set_foo_bar_baz);

  EXPECT_EQ(MergeUnknownFunctionSets(set_foo_bar_baz, set_foo, &arena),
            set_foo_bar_baz);
  EXPECT_EQ(MergeUnknownFunctionSets(set_foo_bar_baz, set_bar, &arena),
            set_foo_bar_baz);
  EXPECT_EQ(MergeUnknownFunctionSets(set_foo_bar_baz, set_baz, &arena),
            set_foo_bar_baz);
  EXPECT_EQ(MergeUnknownFunctionSets(set_foo_bar_baz, set_foo_bar, &arena),
            set_foo_bar_baz);
  EXPECT_EQ(MergeUnknownFunctionSets(set_foo, set_foo_bar_baz, &arena),
            set_foo_bar_baz);
  EXPECT_EQ(MergeUnknownFunctionSets(set_bar, set_foo_bar_baz, &arena),
            set_foo_bar_baz);
  EXPECT_EQ(MergeUnknownFunctionSets(set_baz, set_foo_bar_baz, &arena),
            set_foo_bar_baz);
  EXPECT_EQ(MergeUnknownFunctionSets(set_foo_bar, set_foo_bar_baz, &arena),
            set_foo_bar_baz);
}

}  // namespace
}  // namespace cel::common_internal
