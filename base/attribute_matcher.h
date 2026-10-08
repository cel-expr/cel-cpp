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

#ifndef THIRD_PARTY_CEL_CPP_BASE_ATTRIBUTE_MATCHER_H_
#define THIRD_PARTY_CEL_CPP_BASE_ATTRIBUTE_MATCHER_H_

#include <cstddef>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/functional/function_ref.h"
#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "base/attribute.h"

namespace cel {

class AttributeMatcher;
class AttributeMatch;

namespace common_internal {

// Node in the tree used by AttributeMatcher, represents either a variable or a
// qualifier. Nodes with no parent are variables and all others are qualifiers.
class AttributeMatcherNode {
 public:
  struct Less {
    using is_transparent = void;

    bool operator()(const AttributeMatcherNode& lhs,
                    const AttributeMatcherNode& rhs) const {
      return lhs.key() < rhs.key();
    }

    bool operator()(const AttributeMatcherNode& lhs,
                    const AttributeQualifierPattern& rhs) const {
      return lhs.key() < rhs;
    }

    bool operator()(const AttributeQualifierPattern& lhs,
                    const AttributeMatcherNode& rhs) const {
      return lhs < rhs.key();
    }

    bool operator()(const AttributeMatcherNode& lhs,
                    const AttributeQualifierView& rhs) const {
      return lhs.key() < rhs;
    }

    bool operator()(const AttributeQualifierView& lhs,
                    const AttributeMatcherNode& rhs) const {
      return lhs < rhs.key();
    }

    bool operator()(const AttributeMatcherNode& lhs,
                    const AttributeQualifier& rhs) const {
      return lhs.key() < rhs;
    }

    bool operator()(const AttributeQualifier& lhs,
                    const AttributeMatcherNode& rhs) const {
      return lhs < rhs.key();
    }
  };

  AttributeMatcherNode(AttributeMatcherNode* parent,
                       const AttributeQualifierPattern& key)
      : parent_(parent),
        key_(key),
        depth_(parent != nullptr ? parent->depth_ + 1 : 0) {}

  [[nodiscard]]
  bool IsPartial() const {
    return children_ != nullptr || wildcard_ != nullptr;
  }

  [[nodiscard]]
  bool IsFull() const {
    return !IsPartial();
  }

  [[nodiscard]]
  const AttributeQualifierPattern& key() const {
    return key_;
  }

  [[nodiscard]]
  AttributeMatcherNode* parent() const {
    return parent_;
  }

  [[nodiscard]]
  size_t depth() const {
    return depth_;
  }

  [[nodiscard]]
  const AttributeMatcherNode* MatchQualifier(
      const cel::AttributeQualifierView& qualifier) const {
    if (children_ != nullptr) {
      auto it = children_->find(qualifier);
      if (it != children_->end()) {
        return &*it;
      }
    } else if (wildcard_ != nullptr) {
      return wildcard_.get();
    }
    return nullptr;
  }

  [[nodiscard]]
  std::pair<AttributeMatcherNode*, bool> Add(const AttributeQualifier& pattern,
                                             bool prev_emplaced);
  [[nodiscard]]
  std::pair<AttributeMatcherNode*, bool> Add(
      const AttributeQualifierPattern& pattern, bool prev_emplaced);

  [[nodiscard]]
  bool Remove();

  [[nodiscard]]
  size_t RemoveChildren();

 private:
  friend class cel::AttributeMatcher;

  [[nodiscard]]
  bool ChildRemoved();

  [[nodiscard]]
  size_t RemoveChildrenInternal();

  AttributeMatcherNode* const parent_;
  const AttributeQualifierPattern key_;
  const size_t depth_;
  // Pointer stability is required, so use std::set.
  std::unique_ptr<std::set<AttributeMatcherNode, Less>> children_;
  std::unique_ptr<AttributeMatcherNode> wildcard_;
};

AttributeMatch MakeAttributeMatch(
    const common_internal::AttributeMatcherNode* node);

[[nodiscard]]
const AttributeMatcherNode* GetAttributeMatcherNode(
    const AttributeMatch& match);

}  // namespace common_internal

class [[nodiscard]] AttributeMatch {
 public:
  using Type = AttributePattern::MatchType;

  AttributeMatch() = default;
  AttributeMatch(const AttributeMatch&) = default;
  AttributeMatch& operator=(const AttributeMatch&) = default;

  [[nodiscard]]
  Type GetType() const {
    return node_ == nullptr     ? Type::NONE
           : node_->IsPartial() ? Type::PARTIAL
                                : Type::FULL;
  }

  [[nodiscard]]
  bool IsNone() const {
    return node_ == nullptr;
  }

  [[nodiscard]]
  bool IsPartial() const {
    return node_ != nullptr && node_->IsPartial();
  }

  [[nodiscard]]
  bool IsFull() const {
    return node_ != nullptr && node_->IsFull();
  }

  // Attempts to match the next qualifier. This should not be called on full
  // matches, as those do not have children by definition. Instead the full
  // match should be handled.
  AttributeMatch MatchQualifier(const AttributeQualifierView& qualifier) const {
    ABSL_DCHECK(!IsFull());
    if (IsNone()) {
      return AttributeMatch();
    }
    return AttributeMatch(node_->MatchQualifier(qualifier));
  }

 private:
  friend AttributeMatch common_internal::MakeAttributeMatch(
      const common_internal::AttributeMatcherNode* node);
  friend const common_internal::AttributeMatcherNode*
  common_internal::GetAttributeMatcherNode(const AttributeMatch& match);

  explicit AttributeMatch(const common_internal::AttributeMatcherNode* node)
      : node_(node) {}

  const common_internal::AttributeMatcherNode* node_ = nullptr;
};

namespace common_internal {

inline AttributeMatch MakeAttributeMatch(
    const common_internal::AttributeMatcherNode* node) {
  return AttributeMatch(node);
}

[[nodiscard]]
inline const AttributeMatcherNode* GetAttributeMatcherNode(
    const AttributeMatch& match) {
  return match.node_;
}

}  // namespace common_internal

// AttributeMatcher allows for efficiently checking for unknown or missing
// attributes at runtime in a scalable manner. AttributeMatcher is
// thread-compatible however it must not be modified during any evaluation where
// it is being used.
class AttributeMatcher {
 private:
  using Node = common_internal::AttributeMatcherNode;

 public:
  AttributeMatcher() = default;
  AttributeMatcher(const AttributeMatcher&) = delete;
  AttributeMatcher(AttributeMatcher&&) = default;
  AttributeMatcher& operator=(const AttributeMatcher&) = delete;
  AttributeMatcher& operator=(AttributeMatcher&&) = default;

  // Inserts an attribute to the matcher. Returns an error if the attribute
  // or attribute pattern being added overlaps with a previously added
  // attribute.
  absl::Status InsertAttribute(const AttributePattern& pattern);
  absl::Status InsertAttribute(const Attribute& attribute);

  // Inserts or updates the attribute in the matcher. If the attribute being
  // added is a superset of others, the subsets will be removed and replaced
  // with this attribute. If the attribute being added is a subset of others,
  // no action is taken. Otherwise the attribute is inserted.
  absl::Status UpsertAttribute(const AttributePattern& pattern);
  absl::Status UpsertAttribute(const Attribute& attribute);

  // Removes a previously added unknown attribute from the matcher.
  absl::Status RemoveAttribute(const AttributePattern& pattern);
  absl::Status RemoveAttribute(const Attribute& attribute);

  // Resets the matcher such that all observable behavior is equivalent to a
  // default constructed matcher.
  void ClearAttributes() {
    children_.clear();
    attributes_ = 0;
  }

  AttributeMatch MatchVariable(absl::string_view variable) const
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    auto it = children_.find(AttributeQualifierView::OfString(variable));
    if (it != children_.end()) {
      return common_internal::MakeAttributeMatch(&*it);
    }
    return AttributeMatch();
  }

  // Returns an array of attribute patterns corresponding to the
  // previously added attribute patterns. The order of the attribute
  // patterns in the array is not guaranteed.
  [[nodiscard]]
  std::vector<AttributePattern> GetAttributes() const;

  // Tests whether any attribute patterns have been added to this
  // matcher.
  [[nodiscard]]
  bool HasAttributes() const {
    return !children_.empty();
  }

 private:
  struct AttributeCollector;

  [[nodiscard]]
  std::pair<Node*, bool> AddVariable(absl::string_view variable);

  template <typename Q>
  absl::Status InsertAttribute(absl::string_view variable,
                               absl::Span<const Q> qualifiers,
                               absl::FunctionRef<std::string()> to_string);

  template <typename Q>
  absl::Status UpsertAttribute(absl::string_view variable,
                               absl::Span<const Q> qualifiers,
                               absl::FunctionRef<std::string()> to_string);

  template <typename Q>
  absl::Status RemoveAttribute(absl::string_view variable,
                               absl::Span<const Q> qualifiers,
                               absl::FunctionRef<std::string()> to_string);

  // Pointer stability is required.
  std::set<Node, Node::Less> children_;
  size_t attributes_ = 0;
};

[[nodiscard]]
const AttributeMatcher& EmptyAttributeMatcher();

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_BASE_ATTRIBUTE_MATCHER_H_
