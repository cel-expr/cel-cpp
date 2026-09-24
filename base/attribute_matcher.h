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
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "base/attribute.h"

namespace cel {

class AttributeMatcher;
class AttributeMatch;

namespace common_internal {

struct MissingAttributeClearer;
struct UnknownAttributeClearer;

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
  bool IsUnknown() const {
    return unknown_;
  }

  [[nodiscard]]
  bool IsMissing() const {
    return missing_;
  }

  [[nodiscard]]
  bool HasUnknownChildren() const {
    return unknown_children_;
  }

  [[nodiscard]]
  bool HasMissingChildren() const {
    return missing_children_;
  }

  [[nodiscard]]
  bool IsKnown() const {
    return !IsUnknown();
  }

  [[nodiscard]]
  bool IsFound() const {
    return !IsMissing();
  }

  [[nodiscard]]
  bool IsFullyUnknown() const {
    return IsUnknown() && !HasUnknownChildren();
  }

  [[nodiscard]]
  bool IsFullyMissing() const {
    return IsMissing() && !HasMissingChildren();
  }

  [[nodiscard]]
  bool IsPartiallyUnknown() const {
    return IsUnknown() && HasUnknownChildren();
  }

  [[nodiscard]]
  bool IsPartiallyMissing() const {
    return IsMissing() && HasMissingChildren();
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
  uint32_t depth() const {
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
    }
    if (wildcard_ != nullptr) {
      return wildcard_.get();
    }
    return nullptr;
  }

  [[nodiscard]]
  std::pair<AttributeMatcherNode*, bool> AddMissing(
      const AttributeQualifier& pattern, bool prev_set);
  [[nodiscard]]
  std::pair<AttributeMatcherNode*, bool> AddMissing(
      const AttributeQualifierPattern& pattern, bool prev_set);

  [[nodiscard]]
  std::pair<AttributeMatcherNode*, bool> AddUnknown(
      const AttributeQualifier& pattern, bool prev_set);
  [[nodiscard]]
  std::pair<AttributeMatcherNode*, bool> AddUnknown(
      const AttributeQualifierPattern& pattern, bool prev_set);

  void ClearMissing() {
    missing_ = false;
    ClearMissingChildren();
  }

  void ClearUnknown() {
    unknown_ = false;
    ClearUnknownChildren();
  }

  [[nodiscard]]
  bool RemoveUnknown();

  [[nodiscard]]
  bool RemoveMissing();

 private:
  friend class cel::AttributeMatcher;
  friend struct MissingAttributeClearer;
  friend struct UnknownAttributeClearer;

  [[nodiscard]]
  bool UnknownChildRemoved();

  [[nodiscard]]
  bool MissingChildRemoved();

  bool SetMissing() {
    if (missing_) {
      return false;
    }
    missing_ = true;
    return true;
  }

  bool SetUnknown() {
    if (unknown_) {
      return false;
    }
    unknown_ = true;
    return true;
  }

  bool SetMissingChildren() {
    if (missing_children_) {
      return false;
    }
    missing_children_ = true;
    return true;
  }

  bool SetUnknownChildren() {
    if (unknown_children_) {
      return false;
    }
    unknown_children_ = true;
    return true;
  }

  void ClearMissingChildren() { missing_children_ = false; }

  void ClearUnknownChildren() { unknown_children_ = false; }

  AttributeMatcherNode* const parent_;
  const AttributeQualifierPattern key_;
  const uint32_t depth_;
  bool unknown_ = false;
  bool missing_ = false;
  bool unknown_children_ = false;
  bool missing_children_ = false;
  // Pointer stability is required.
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
  Type GetUnknownType() const {
    return node_ == nullptr || !node_->IsUnknown() ? Type::NONE
           : node_->HasUnknownChildren()           ? Type::PARTIAL
                                                   : Type::FULL;
  }

  [[nodiscard]]
  Type GetMissingType() const {
    return node_ == nullptr || !node_->IsMissing() ? Type::NONE
           : node_->HasMissingChildren()           ? Type::PARTIAL
                                                   : Type::FULL;
  }

  [[nodiscard]]
  bool IsNone() const {
    return node_ == nullptr;
  }

  [[nodiscard]]
  bool IsKnown() const {
    return IsNone() || node_->IsKnown();
  }

  [[nodiscard]]
  bool IsFound() const {
    return IsNone() || node_->IsFound();
  }

  [[nodiscard]]
  bool IsFullyUnknown() const {
    return !IsNone() && node_->IsFullyUnknown();
  }

  [[nodiscard]]
  bool IsFullyMissing() const {
    return !IsNone() && node_->IsFullyMissing();
  }

  [[nodiscard]]
  bool IsPartiallyUnknown() const {
    return !IsNone() && node_->IsPartiallyUnknown();
  }

  [[nodiscard]]
  bool IsPartiallyMissing() const {
    return !IsNone() && node_->IsPartiallyMissing();
  }

  AttributeMatch MatchQualifier(const AttributeQualifierView& qualifier) const {
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

  // Adds an unknown attribute to the matcher. Returns an error if the attribute
  // or attribute pattern being added overlaps with a previously added unknown
  // attribute.
  absl::Status AddUnknownAttribute(const AttributePattern& pattern);
  absl::Status AddUnknownAttribute(const Attribute& attribute);

  // Adds a missing attribute to the matcher. Returns an error if the attribute
  // or attribute pattern being added overlaps with a previously added missing
  // attribute.
  absl::Status AddMissingAttribute(const AttributePattern& pattern);
  absl::Status AddMissingAttribute(const Attribute& attribute);

  // Removes a previously added unknown attribute from the matcher.
  absl::Status RemoveUnknownAttribute(const AttributePattern& pattern);
  absl::Status RemoveUnknownAttribute(const Attribute& attribute);

  // Removes a previously added missing attribute from the matcher.
  absl::Status RemoveMissingAttribute(const AttributePattern& pattern);
  absl::Status RemoveMissingAttribute(const Attribute& attribute);

  // Resets the matcher such that all observable behavior is equivalent to a
  // default constructed matcher.
  void ClearAttributes() {
    children_.clear();
    missing_attributes_ = 0;
    unknown_attributes_ = 0;
  }

  // Removes all previously added missing attributes.
  void ClearMissingAttributes();

  // Removes all previously added unknown attributes.
  void ClearUnknownAttributes();

  AttributeMatch MatchVariable(absl::string_view variable) const
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    auto it = children_.find(AttributeQualifierView::OfString(variable));
    if (it != children_.end()) {
      return common_internal::MakeAttributeMatch(&*it);
    }
    return AttributeMatch();
  }

  // Returns an array of missing attribute patterns corresponding to the
  // previously added missing attribute patterns. The order of the attribute
  // patterns in the array is not guaranteed.
  [[nodiscard]]
  std::vector<AttributePattern> GetMissingAttributes() const;

  // Returns an array of unknown attribute patterns corresponding to the
  // previously added unknown attribute patterns. The order of the attribute
  // patterns in the array is not guaranteed.
  [[nodiscard]]
  std::vector<AttributePattern> GetUnknownAttributes() const;

  // Tests whether any missing attribute patterns have been added to this
  // matcher.
  [[nodiscard]]
  bool HasMissingAttributes() const {
    return missing_attributes_ != 0;
  }

  // Tests whether any unknown attribute patterns have been added to this
  // matcher.
  [[nodiscard]]
  bool HasUnknownAttributes() const {
    return unknown_attributes_ != 0;
  }

 private:
  struct MissingAttributeCollector;
  struct UnknownAttributeCollector;
  friend struct MissingAttributeClearer;
  friend struct UnknownAttributeClearer;

  template <typename Q>
  absl::Status AddUnknownAttribute(absl::string_view variable,
                                   absl::Span<const Q> qualifiers,
                                   absl::FunctionRef<std::string()> to_string);

  template <typename Q>
  absl::Status AddMissingAttribute(absl::string_view variable,
                                   absl::Span<const Q> qualifiers,
                                   absl::FunctionRef<std::string()> to_string);

  template <typename Q>
  absl::Status RemoveUnknownAttribute(
      absl::string_view variable, absl::Span<const Q> qualifiers,
      absl::FunctionRef<std::string()> to_string);

  template <typename Q>
  absl::Status RemoveMissingAttribute(
      absl::string_view variable, absl::Span<const Q> qualifiers,
      absl::FunctionRef<std::string()> to_string);

  // Pointer stability is required.
  std::set<Node, Node::Less> children_;
  size_t missing_attributes_ = 0;
  size_t unknown_attributes_ = 0;
};

[[nodiscard]]
const AttributeMatcher& EmptyAttributeMatcher();

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_BASE_ATTRIBUTE_MATCHER_H_
