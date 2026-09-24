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

#include "internal/arena_tree.h"

#include <memory>

#include "internal/testing.h"

namespace cel::internal {
namespace {

using ::testing::IsNull;

using TestNode = ArenaTreeNode<int>;

struct TestNodeCompare {
  int operator()(int lhs, int rhs) const {
    if (lhs < rhs) {
      return -1;
    }
    if (lhs > rhs) {
      return 1;
    }
    return 0;
  }
};

struct TestNodeCrtp : ArenaTreeNodeCrtp<TestNodeCrtp> {
  explicit TestNodeCrtp(int value) : ArenaTreeNodeCrtp(), value(value) {}

  int value;
};

struct TestNodeCrtpCompare {
  int operator()(int lhs, int rhs) const {
    if (lhs < rhs) {
      return -1;
    }
    if (lhs > rhs) {
      return 1;
    }
    return 0;
  }

  int operator()(int lhs, const TestNodeCrtp& rhs) const {
    return (*this)(lhs, rhs.value);
  }

  int operator()(const TestNodeCrtp& lhs, int rhs) const {
    return (*this)(lhs.value, rhs);
  }

  int operator()(const TestNodeCrtp& lhs, const TestNodeCrtp& rhs) const {
    return (*this)(lhs.value, rhs.value);
  }
};

TEST(ArenaTree, Empty) {
  TestNode* head = nullptr;
  EXPECT_THAT(ArenaTreePrev(head), IsNull());
  EXPECT_THAT(ArenaTreeNext(head), IsNull());
  EXPECT_THAT(ArenaTreeMin(head), IsNull());
  EXPECT_THAT(ArenaTreeMax(head), IsNull());
}

TEST(ArenaTree, Single) {
  TestNode* head = nullptr;
  std::unique_ptr<TestNode> node1 = std::make_unique<TestNode>(1);
  EXPECT_EQ(ArenaTreeInsert(&head, node1.get(), TestNodeCompare{}),
            node1.get());
  EXPECT_EQ(ArenaTreePrev(head), nullptr);
  EXPECT_EQ(ArenaTreeNext(head), nullptr);
  EXPECT_EQ(ArenaTreeMin(head), node1.get());
  EXPECT_EQ(ArenaTreeMax(head), node1.get());
  ArenaTreeRemove(&head, node1.get());
  EXPECT_THAT(ArenaTreePrev(head), IsNull());
  EXPECT_THAT(ArenaTreeNext(head), IsNull());
  EXPECT_THAT(ArenaTreeMin(head), IsNull());
  EXPECT_THAT(ArenaTreeMax(head), IsNull());
}

TEST(ArenaTree, Couple) {
  TestNode* head = nullptr;
  std::unique_ptr<TestNode> node1 = std::make_unique<TestNode>(1);
  std::unique_ptr<TestNode> node2 = std::make_unique<TestNode>(2);

  EXPECT_EQ(ArenaTreeInsert(&head, node1.get(), TestNodeCompare{}),
            node1.get());
  EXPECT_EQ(ArenaTreeInsert(&head, node2.get(), TestNodeCompare{}),
            node2.get());
  EXPECT_EQ(ArenaTreePrev(node1.get()), nullptr);
  EXPECT_EQ(ArenaTreePrev(node2.get()), node1.get());
  EXPECT_EQ(ArenaTreeNext(node1.get()), node2.get());
  EXPECT_EQ(ArenaTreeNext(node2.get()), nullptr);
  EXPECT_EQ(ArenaTreeMin(head), node1.get());
  EXPECT_EQ(ArenaTreeMax(head), node2.get());

  ArenaTreeRemove(&head, node1.get());
  EXPECT_THAT(ArenaTreePrev(head), IsNull());
  EXPECT_THAT(ArenaTreeNext(head), IsNull());
  EXPECT_EQ(ArenaTreeMin(head), node2.get());
  EXPECT_EQ(ArenaTreeMax(head), node2.get());

  ArenaTreeRemove(&head, node2.get());
  EXPECT_THAT(ArenaTreePrev(head), IsNull());
  EXPECT_THAT(ArenaTreeNext(head), IsNull());
  EXPECT_THAT(ArenaTreeMin(head), IsNull());
  EXPECT_THAT(ArenaTreeMax(head), IsNull());

  EXPECT_EQ(ArenaTreeInsert(&head, node2.get(), TestNodeCompare{}),
            node2.get());
  EXPECT_EQ(ArenaTreeInsert(&head, node1.get(), TestNodeCompare{}),
            node1.get());
  EXPECT_EQ(ArenaTreePrev(node1.get()), nullptr);
  EXPECT_EQ(ArenaTreePrev(node2.get()), node1.get());
  EXPECT_EQ(ArenaTreeNext(node1.get()), node2.get());
  EXPECT_EQ(ArenaTreeNext(node2.get()), nullptr);
  EXPECT_EQ(ArenaTreeMin(head), node1.get());
  EXPECT_EQ(ArenaTreeMax(head), node2.get());

  ArenaTreeRemove(&head, node2.get());
  EXPECT_THAT(ArenaTreePrev(head), IsNull());
  EXPECT_THAT(ArenaTreeNext(head), IsNull());
  EXPECT_EQ(ArenaTreeMin(head), node1.get());
  EXPECT_EQ(ArenaTreeMax(head), node1.get());

  ArenaTreeRemove(&head, node1.get());
  EXPECT_THAT(ArenaTreePrev(head), IsNull());
  EXPECT_THAT(ArenaTreeNext(head), IsNull());
  EXPECT_THAT(ArenaTreeMin(head), IsNull());
  EXPECT_THAT(ArenaTreeMax(head), IsNull());
}

TEST(ArenaTree, CrtpEmpty) {
  TestNodeCrtp* head = nullptr;
  EXPECT_THAT(ArenaTreePrev(head), IsNull());
  EXPECT_THAT(ArenaTreeNext(head), IsNull());
  EXPECT_THAT(ArenaTreeMin(head), IsNull());
  EXPECT_THAT(ArenaTreeMax(head), IsNull());
}

TEST(ArenaTree, CrtpSingle) {
  TestNodeCrtp* head = nullptr;
  std::unique_ptr<TestNodeCrtp> node1 = std::make_unique<TestNodeCrtp>(1);
  EXPECT_EQ(ArenaTreeInsert(&head, node1.get(), TestNodeCrtpCompare{}),
            node1.get());
  EXPECT_EQ(ArenaTreePrev(head), nullptr);
  EXPECT_EQ(ArenaTreeNext(head), nullptr);
  EXPECT_EQ(ArenaTreeMin(head), node1.get());
  EXPECT_EQ(ArenaTreeMax(head), node1.get());
  ArenaTreeRemove(&head, node1.get());
  EXPECT_THAT(ArenaTreePrev(head), IsNull());
  EXPECT_THAT(ArenaTreeNext(head), IsNull());
  EXPECT_THAT(ArenaTreeMin(head), IsNull());
  EXPECT_THAT(ArenaTreeMax(head), IsNull());
}

TEST(ArenaTree, CrtpCouple) {
  TestNodeCrtp* head = nullptr;
  std::unique_ptr<TestNodeCrtp> node1 = std::make_unique<TestNodeCrtp>(1);
  std::unique_ptr<TestNodeCrtp> node2 = std::make_unique<TestNodeCrtp>(2);

  EXPECT_EQ(ArenaTreeInsert(&head, node1.get(), TestNodeCrtpCompare{}),
            node1.get());
  EXPECT_EQ(ArenaTreeInsert(&head, node2.get(), TestNodeCrtpCompare{}),
            node2.get());
  EXPECT_EQ(ArenaTreePrev(node1.get()), nullptr);
  EXPECT_EQ(ArenaTreePrev(node2.get()), node1.get());
  EXPECT_EQ(ArenaTreeNext(node1.get()), node2.get());
  EXPECT_EQ(ArenaTreeNext(node2.get()), nullptr);
  EXPECT_EQ(ArenaTreeMin(head), node1.get());
  EXPECT_EQ(ArenaTreeMax(head), node2.get());

  ArenaTreeRemove(&head, node1.get());
  EXPECT_THAT(ArenaTreePrev(head), IsNull());
  EXPECT_THAT(ArenaTreeNext(head), IsNull());
  EXPECT_EQ(ArenaTreeMin(head), node2.get());
  EXPECT_EQ(ArenaTreeMax(head), node2.get());

  ArenaTreeRemove(&head, node2.get());
  EXPECT_THAT(ArenaTreePrev(head), IsNull());
  EXPECT_THAT(ArenaTreeNext(head), IsNull());
  EXPECT_THAT(ArenaTreeMin(head), IsNull());
  EXPECT_THAT(ArenaTreeMax(head), IsNull());

  EXPECT_EQ(ArenaTreeInsert(&head, node2.get(), TestNodeCrtpCompare{}),
            node2.get());
  EXPECT_EQ(ArenaTreeInsert(&head, node1.get(), TestNodeCrtpCompare{}),
            node1.get());
  EXPECT_EQ(ArenaTreePrev(node1.get()), nullptr);
  EXPECT_EQ(ArenaTreePrev(node2.get()), node1.get());
  EXPECT_EQ(ArenaTreeNext(node1.get()), node2.get());
  EXPECT_EQ(ArenaTreeNext(node2.get()), nullptr);
  EXPECT_EQ(ArenaTreeMin(head), node1.get());
  EXPECT_EQ(ArenaTreeMax(head), node2.get());

  ArenaTreeRemove(&head, node2.get());
  EXPECT_THAT(ArenaTreePrev(head), IsNull());
  EXPECT_THAT(ArenaTreeNext(head), IsNull());
  EXPECT_EQ(ArenaTreeMin(head), node1.get());
  EXPECT_EQ(ArenaTreeMax(head), node1.get());

  ArenaTreeRemove(&head, node1.get());
  EXPECT_THAT(ArenaTreePrev(head), IsNull());
  EXPECT_THAT(ArenaTreeNext(head), IsNull());
  EXPECT_THAT(ArenaTreeMin(head), IsNull());
  EXPECT_THAT(ArenaTreeMax(head), IsNull());
}

}  // namespace
}  // namespace cel::internal
