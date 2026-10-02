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

#ifndef THIRD_PARTY_CEL_CPP_COMMON_INTERNAL_UNKNOWNS_H_
#define THIRD_PARTY_CEL_CPP_COMMON_INTERNAL_UNKNOWNS_H_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <variant>

#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/inlined_vector.h"
#include "absl/functional/overload.h"
#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "base/attribute.h"
#include "base/attribute_matcher.h"
#include "internal/arena_tree.h"
#include "internal/cstring_view.h"
#include "runtime/internal/errors.h"
#include "google/protobuf/arena.h"

namespace cel::common_internal {

class UnknownAttributeNode;
class UnknownTree;
class UnknownRoot;
struct UnknownFunctionSet;
struct UnknownAttributeSet;

// Node in an arena-based red black tree. Leaf nodes are matches to unknown
// attributes. The root node is owned by UnknownRoot. Nodes which are direct
// children of the root node are variables. All nodes below them are qualifiers.
class UnknownAttributeNode
    : public internal::ArenaTreeNodeCrtp<UnknownAttributeNode> {
 private:
  struct Compare {
    using is_transparent = void;

    int operator()(const UnknownAttributeKey& lhs,
                   const UnknownAttributeKey& rhs) const {
      if (lhs < rhs) {
        return -1;
      }
      if (lhs == rhs) {
        return 0;
      }
      return 1;
    }

    int operator()(const UnknownAttributeKey& lhs,
                   const AttributeQualifierView& rhs) const {
      if (lhs < rhs) {
        return -1;
      }
      if (lhs == rhs) {
        return 0;
      }
      return 1;
    }

    int operator()(const AttributeQualifierView& lhs,
                   const UnknownAttributeKey& rhs) const {
      if (lhs < rhs) {
        return -1;
      }
      if (lhs == rhs) {
        return 0;
      }
      return 1;
    }

    int operator()(const UnknownAttributeKey& lhs,
                   const UnknownAttributeNode& rhs) const {
      return (*this)(lhs, rhs.key());
    }

    int operator()(const AttributeQualifierView& lhs,
                   const UnknownAttributeNode& rhs) const {
      return (*this)(lhs, rhs.key());
    }

    int operator()(const UnknownAttributeNode& lhs,
                   const UnknownAttributeKey& rhs) const {
      return (*this)(lhs.key(), rhs);
    }

    int operator()(const UnknownAttributeNode& lhs,
                   const AttributeQualifierView& rhs) const {
      return (*this)(lhs.key(), rhs);
    }

    int operator()(const UnknownAttributeNode& lhs,
                   const UnknownAttributeNode& rhs) const {
      return (*this)(lhs.key(), rhs.key());
    }
  };

 public:
  explicit UnknownAttributeNode(UnknownAttributeNode* absl_nullable parent,
                                const UnknownAttributeKey& key)
      : ArenaTreeNodeCrtp(),
        parent_(parent),
        key_(key),
        depth_(parent != nullptr ? parent->depth() + 1 : 0) {}

  [[nodiscard]]
  const UnknownAttributeKey& key() const {
    return key_;
  }

  [[nodiscard]]
  UnknownAttributeNode* absl_nullable parent() const {
    return parent_;
  }

  [[nodiscard]]
  size_t depth() const {
    return depth_;
  }

  [[nodiscard]]
  UnknownAttributeNode* Step(UnknownTree* absl_nonnull tree,
                             const AttributeQualifierView& key);

  [[nodiscard]]
  UnknownAttributeNode* Step(UnknownTree* absl_nonnull tree,
                             const UnknownAttributeKey& key);

  [[nodiscard]]
  UnknownAttributeNode* Find(const AttributeQualifierView& key) const {
    return internal::ArenaTreeFind(children_, key, Compare{});
  }

 private:
  // Pointer to our parent node. If this is null, then this node is the root
  // node and its key is invalid. If this is a pointer to the root node then
  // this node is a variable and the key is guaranteed to be a string.
  UnknownAttributeNode* const absl_nullable parent_ = nullptr;
  const UnknownAttributeKey key_;
  // How many nodes between us and the root node.
  const size_t depth_;
  // Root of the red black tree for children.
  UnknownAttributeNode* absl_nullable children_ = nullptr;
};

// Node in an arena-based red black tree. Leaf nodes are matches to unknown
// functions. The root node is owned by UnknownRoot.
class UnknownFunctionNode
    : public internal::ArenaTreeNodeCrtp<UnknownFunctionNode> {
 public:
  struct Compare {
    using is_transparent = void;

    int operator()(const char* absl_nonnull lhs,
                   const char* absl_nonnull rhs) const {
      return internal::cstring_view(lhs).compare(internal::cstring_view(rhs));
    }

    int operator()(const char* absl_nonnull lhs, absl::string_view rhs) const {
      return internal::cstring_view(lhs).compare(rhs);
    }

    int operator()(absl::string_view lhs, const char* absl_nonnull rhs) const {
      return -internal::cstring_view(rhs).compare(lhs);
    }

    int operator()(const char* absl_nonnull lhs,
                   const UnknownFunctionNode& rhs) const {
      return (*this)(lhs, rhs.key());
    }

    int operator()(absl::string_view lhs,
                   const UnknownFunctionNode& rhs) const {
      return (*this)(lhs, rhs.key());
    }

    int operator()(const UnknownFunctionNode& lhs,
                   const char* absl_nonnull rhs) const {
      return (*this)(lhs.key(), rhs);
    }

    int operator()(const UnknownFunctionNode& lhs,
                   absl::string_view rhs) const {
      return (*this)(lhs.key(), rhs);
    }

    int operator()(const UnknownFunctionNode& lhs,
                   const UnknownFunctionNode& rhs) const {
      return (*this)(lhs.key(), rhs.key());
    }
  };

  explicit UnknownFunctionNode(const char* absl_nonnull key)
      : ArenaTreeNodeCrtp(), key_(key) {}

  [[nodiscard]]
  const char* absl_nonnull key() const {
    return key_;
  }

 private:
  const char* const absl_nonnull key_;
};

class UnknownTree;

[[nodiscard]]
inline const char* absl_nonnull ArenaCString(google::protobuf::Arena* absl_nonnull arena,
                                             absl::string_view string) {
  if (string.empty()) {
    return "";
  }
  char* cstr =
      reinterpret_cast<char*>(arena->AllocateAligned(string.size() + 1));
  ::memcpy(cstr, string.data(), string.size());
  cstr[string.size()] = '\0';
  return cstr;
}

// Holds the the red black tree for functions and the root attribute node which
// holds a red black tree for variables. The root is lazily created on the arena
// by UnknownTree as needed.
class UnknownRoot {
 public:
  explicit UnknownRoot(google::protobuf::Arena* absl_nonnull arena)
      : arena_(arena), attributes_(nullptr, UnknownAttributeKey()) {}

  [[nodiscard]]
  google::protobuf::Arena* absl_nonnull GetArena() const {
    return arena_;
  }

  [[nodiscard]]
  UnknownAttributeNode* absl_nonnull Attributes() {
    return &attributes_;
  }

  [[nodiscard]]
  const UnknownAttributeNode* absl_nonnull Attributes() const {
    return &attributes_;
  }

  [[nodiscard]]
  UnknownFunctionNode* absl_nonnull SetFunction(UnknownTree* absl_nonnull tree,
                                                absl::string_view name);

  [[nodiscard]]
  UnknownFunctionNode* absl_nullable FindFunction(absl::string_view name) const;

 private:
  google::protobuf::Arena* const absl_nonnull arena_;
  UnknownAttributeNode attributes_;
  UnknownFunctionNode* functions_ = nullptr;
};

// Represents a set of unknown function nodes.
struct UnknownFunctionSet {
  size_t size;
  // Nodes are sorted in ascending order by pointer. There are no duplicates.
  UnknownFunctionNode* absl_nonnull nodes[];

  [[nodiscard]]
  static UnknownFunctionSet* Allocate(size_t size,
                                      google::protobuf::Arena* absl_nonnull arena) {
    ABSL_DCHECK_GT(size, 0);
    UnknownFunctionSet* ptr = reinterpret_cast<UnknownFunctionSet*>(
        arena->AllocateAligned(offsetof(UnknownFunctionSet, nodes) +
                               (size * sizeof(UnknownFunctionNode*))));
    ptr->size = size;
    return ptr;
  }
};

// Represents a set of unknown attribute nodes, which themselves represent a
// partial or full match to an unknown attribute.
struct UnknownAttributeSet {
  uint32_t size;
  uint32_t max_depth;
  // Nodes are sorted in ascending order by pointer. There are no duplicates.
  UnknownAttributeNode* absl_nonnull nodes[];

  [[nodiscard]]
  static UnknownAttributeSet* Allocate(size_t size,
                                       google::protobuf::Arena* absl_nonnull arena) {
    ABSL_DCHECK_GT(size, 0);
    ABSL_DCHECK_LE(size, std::numeric_limits<uint32_t>::max());
    UnknownAttributeSet* ptr = reinterpret_cast<UnknownAttributeSet*>(
        arena->AllocateAligned(offsetof(UnknownAttributeSet, nodes) +
                               (size * sizeof(UnknownAttributeNode*))));
    ptr->size = static_cast<uint32_t>(size);
    return ptr;
  }
};

// Underlying representation used by UnknownValue;
struct UnknownSet {
  const UnknownRoot* root = nullptr;
  const UnknownAttributeSet* attributes = nullptr;
  const UnknownFunctionSet* functions = nullptr;
};

// Used by the runtime to build unknown attribute and unknown function trees.
class UnknownTree {
 public:
  UnknownTree() : arena_(nullptr) {}

  UnknownTree(const UnknownTree&) = delete;
  UnknownTree& operator=(const UnknownTree&) = delete;

  explicit UnknownTree(google::protobuf::Arena* absl_nonnull arena) : arena_(arena) {}

  void MaybeReset(google::protobuf::Arena* absl_nonnull arena) {
    ABSL_DCHECK(arena != nullptr);
    if (arena_ != arena) {
      Reset(arena);
    }
  }

  void Reset(google::protobuf::Arena* absl_nonnull arena) {
    ABSL_DCHECK(arena != nullptr);
    arena_ = arena;
    root_ = nullptr;
    strings_.clear();
    scratch_.clear();
  }

  [[nodiscard]]
  google::protobuf::Arena* absl_nonnull GetArena() const {
    ABSL_DCHECK(arena_ != nullptr);
    return arena_;
  }

  [[nodiscard]]
  UnknownRoot* absl_nonnull Root() {
    UnknownRoot* root = RootIfPresent();
    if (ABSL_PREDICT_FALSE(root == nullptr)) {
      root_ = root = google::protobuf::Arena::Create<UnknownRoot>(GetArena(), GetArena());
    }
    return root;
  }

  [[nodiscard]]
  UnknownRoot* absl_nullable RootIfPresent() const {
    return root_;
  }

  // Called when an unknown attribute match is encountered to materialize the
  // match in the unknown attribute tree.
  [[nodiscard]]
  UnknownAttributeNode* absl_nonnull Convert(
      const AttributeMatcherNode* absl_nonnull matcher,
      const AttributeQualifierView& key);
  [[nodiscard]]
  UnknownAttributeNode* absl_nonnull Convert(
      const AttributeMatcherNode* absl_nonnull matcher);
  [[nodiscard]]
  UnknownAttributeNode* absl_nonnull Convert(
      const AttributeMatch& match, const AttributeQualifierView& key) {
    return Convert(common_internal::GetAttributeMatcherNode(match), key);
  }
  [[nodiscard]]
  UnknownAttributeNode* absl_nonnull Convert(const AttributeMatch& match) {
    return Convert(common_internal::GetAttributeMatcherNode(match));
  }

  // Converts an unknown attribute node to its corresponding attribute pattern
  // string, as if the node was converted to AttributePattern and
  // AttributePattern::ToString was called.
  [[nodiscard]]
  std::string ToString(const AttributeMatcherNode* absl_nonnull matcher,
                       const AttributeQualifierView& key);
  [[nodiscard]]
  std::string ToString(const AttributeMatch& match,
                       const AttributeQualifierView& key) {
    return ToString(common_internal::GetAttributeMatcherNode(match), key);
  }

  // Converts an unknown attribute node to its corresponding attribute string,
  // as if the node was converted to Attribute and Attribute::ToString was
  // called.
  [[nodiscard]]
  std::string ToString(const UnknownAttributeNode* absl_nonnull unknown);

  // In the rare event that somebody manages to pass an unknown set from one
  // expression into another, we need to be able to graft those nodes into our
  // tree. The unknown attribute set and unknown function set sort nodes by
  // address for efficiency. This function does that grafting when it is
  // required.
  [[nodiscard]]
  UnknownSet Graft(const UnknownSet& in) {
    // Fast path, should be taken almost always.
    if (ABSL_PREDICT_TRUE(in.root == nullptr || in.root == RootIfPresent())) {
      return in;
    }
    // Slow path, should be taken extremely rarely.
    return GraftImpl(in);
  }

 private:
  friend class UnknownAttributeNode;
  friend class UnknownRoot;

  [[nodiscard]]
  UnknownSet GraftImpl(const UnknownSet& in);

  [[nodiscard]]
  UnknownAttributeNode* GraftImpl(const UnknownAttributeNode* in);

  [[nodiscard]]
  UnknownFunctionNode* GraftImpl(const UnknownFunctionNode* in);

  [[nodiscard]]
  std::string ToString(const AttributeMatcherNode* absl_nonnull matcher);

  [[nodiscard]]
  UnknownAttributeKey InternKey(const AttributeQualifierView& key) {
    return std::visit(
        absl::Overload(
            [](std::monostate) -> UnknownAttributeKey {
              return UnknownAttributeKey();
            },
            [](bool value) -> UnknownAttributeKey {
              return UnknownAttributeKey(value);
            },
            [](int64_t value) -> UnknownAttributeKey {
              return UnknownAttributeKey(value);
            },
            [](uint64_t value) -> UnknownAttributeKey {
              return UnknownAttributeKey(value);
            },
            [this](absl::string_view value) -> UnknownAttributeKey {
              return UnknownAttributeKey(InternStringKey(value));
            }),
        common_internal::AsVariant(key));
  }

  [[nodiscard]]
  UnknownAttributeKey InternKey(const UnknownAttributeKey& key) {
    return std::visit(
        absl::Overload(
            [](std::monostate) -> UnknownAttributeKey {
              return UnknownAttributeKey();
            },
            [](bool value) -> UnknownAttributeKey {
              return UnknownAttributeKey(value);
            },
            [](int64_t value) -> UnknownAttributeKey {
              return UnknownAttributeKey(value);
            },
            [](uint64_t value) -> UnknownAttributeKey {
              return UnknownAttributeKey(value);
            },
            [this](internal::cstring_view value) -> UnknownAttributeKey {
              return UnknownAttributeKey(
                  InternStringKey(absl::string_view(value.c_str())));
            }),
        common_internal::AsVariant(key));
  }

  [[nodiscard]]
  const char* absl_nonnull InternStringKey(absl::string_view key) {
    if (key.empty()) {
      return "";
    }
    return strings_
        .lazy_emplace(key,
                      [this, key](const auto& constructor) {
                        constructor(absl::string_view(
                            (ArenaCString)(GetArena(), key), key.size()));
                      })
        ->data();
  }

  google::protobuf::Arena* absl_nullability_complex arena_;
  // Created as needed.
  UnknownRoot* absl_nullable root_ = nullptr;
  // These strings are actually null terminated.
  absl::flat_hash_set<absl::string_view> strings_;
  // Temporary storage for materializing AttributeMatcherNode* trail to
  // UnknownAttributeNode* or for converting either type to a string.
  absl::InlinedVector<const void* absl_nullability_unknown, 2> scratch_;
};

inline UnknownAttributeNode* UnknownAttributeNode::Step(
    UnknownTree* absl_nonnull tree, const AttributeQualifierView& key) {
  auto [node, emplaced] = internal::ArenaTreeLazyEmplace(
      tree->GetArena(), &children_, key, Compare{},
      [this, tree, &key](const auto& constructor) -> void {
        constructor(this, tree->InternKey(key));
      });
  return node;
}

inline UnknownAttributeNode* UnknownAttributeNode::Step(
    UnknownTree* absl_nonnull tree, const UnknownAttributeKey& key) {
  auto [node, emplaced] = internal::ArenaTreeLazyEmplace(
      tree->GetArena(), &children_, key, Compare{},
      [this, tree, &key](const auto& constructor) -> void {
        constructor(this, tree->InternKey(key));
      });
  return node;
}

inline UnknownFunctionNode* absl_nonnull UnknownRoot::SetFunction(
    UnknownTree* absl_nonnull tree, absl::string_view name) {
  auto [node, emplaced] = internal::ArenaTreeLazyEmplace(
      tree->GetArena(), &functions_, name, UnknownFunctionNode::Compare{},
      [tree, &name](const auto& constructor) -> void {
        constructor((ArenaCString)(tree->GetArena(), name));
      });
  return node;
}

inline UnknownFunctionNode* absl_nullable UnknownRoot::FindFunction(
    absl::string_view name) const {
  return internal::ArenaTreeFind(functions_, name,
                                 UnknownFunctionNode::Compare{});
}

[[nodiscard]]
inline const UnknownFunctionSet* absl_nonnull CreateUnknownFunctionSet(
    UnknownFunctionNode* absl_nonnull node, google::protobuf::Arena* absl_nonnull arena) {
  UnknownFunctionSet* ufs = UnknownFunctionSet::Allocate(1, arena);
  ufs->nodes[0] = node;
  return ufs;
}

// Fast two-way unknown function set merger without temporary storage.
[[nodiscard]]
const UnknownFunctionSet* absl_nonnull MergeUnknownFunctionSets(
    const UnknownFunctionSet* absl_nonnull lhs,
    const UnknownFunctionSet* absl_nonnull rhs,
    google::protobuf::Arena* absl_nonnull arena);

[[nodiscard]]
inline const UnknownAttributeSet* absl_nonnull CreateUnknownAttributeSet(
    UnknownAttributeNode* absl_nonnull node,
    google::protobuf::Arena* absl_nonnull arena) {
  UnknownAttributeSet* uas = UnknownAttributeSet::Allocate(1, arena);
  uas->max_depth = static_cast<uint32_t>(node->depth());
  uas->nodes[0] = node;
  return uas;
}

// Fast two-way unknown attribute set merger without temporary storage.
[[nodiscard]]
const UnknownAttributeSet* absl_nonnull MergeUnknownAttributeSets(
    const UnknownAttributeSet* absl_nonnull lhs,
    const UnknownAttributeSet* absl_nonnull rhs,
    google::protobuf::Arena* absl_nonnull arena);

[[nodiscard]]
inline UnknownSet AttributeMatchToUnknownValue(
    const AttributeMatcherNode* node, const AttributeQualifierView& key,
    UnknownTree* tree) {
  ABSL_DCHECK(node->IsUnknown());
  ABSL_DCHECK(node->key().IsWildcard() || node->key() == key);
  ABSL_DCHECK(tree != nullptr);
  UnknownAttributeNode* unknown = tree->Convert(node, key);
  return UnknownSet{
      .root = tree->Root(),
      .attributes =
          common_internal::CreateUnknownAttributeSet(unknown, tree->GetArena()),
      .functions = nullptr,
  };
}

[[nodiscard]]
inline UnknownSet AttributeMatchToUnknownValue(
    const AttributeMatch& match, const AttributeQualifierView& key,
    UnknownTree* tree) {
  return common_internal::AttributeMatchToUnknownValue(
      common_internal::GetAttributeMatcherNode(match), key, tree);
}

[[nodiscard]]
inline UnknownSet AttributeMatchToUnknownValue(const AttributeMatch& match,
                                               absl::string_view key,
                                               UnknownTree* tree) {
  return common_internal::AttributeMatchToUnknownValue(
      common_internal::GetAttributeMatcherNode(match),
      AttributeQualifierView::OfString(key), tree);
}

inline absl::Status AttributeMatchToMissingValue(
    const AttributeMatcherNode* node, const AttributeQualifierView& key,
    UnknownTree* tree) {
  ABSL_DCHECK(node->IsFullyMissing());
  ABSL_DCHECK(node->key().IsWildcard() || node->key() == key);
  ABSL_DCHECK(tree != nullptr);
  // We do not need to convert matcher.
  return runtime_internal::CreateMissingAttributeError(
      tree->ToString(node, key));
}

inline absl::Status AttributeMatchToMissingValue(
    const AttributeMatch& match, const AttributeQualifierView& key,
    UnknownTree* tree) {
  return common_internal::AttributeMatchToMissingValue(
      common_internal::GetAttributeMatcherNode(match), key, tree);
}

inline absl::Status AttributeMatchToMissingValue(const AttributeMatch& match,
                                                 absl::string_view key,
                                                 UnknownTree* tree) {
  return common_internal::AttributeMatchToMissingValue(
      common_internal::GetAttributeMatcherNode(match),
      AttributeQualifierView::OfString(key), tree);
}

}  // namespace cel::common_internal

#endif  // THIRD_PARTY_CEL_CPP_COMMON_INTERNAL_UNKNOWNS_H_
