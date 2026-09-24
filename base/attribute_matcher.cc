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

#include "base/attribute_matcher.h"

#include <cstddef>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/functional/function_ref.h"
#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/source_location.h"
#include "absl/types/span.h"
#include "base/attribute.h"

namespace cel {

namespace common_internal {

std::pair<AttributeMatcherNode*, bool> AttributeMatcherNode::AddMissing(
    const AttributeQualifier& pattern, bool prev_set) {
  ABSL_DCHECK(IsMissing());
  const bool set_missing_children = SetMissingChildren();
  if (set_missing_children && !prev_set) {
    ClearMissingChildren();
    return {nullptr, false};
  }
  if (!set_missing_children && wildcard_ != nullptr && wildcard_->IsMissing()) {
    return {nullptr, false};
  }
  if (children_ == nullptr) {
    children_ = std::make_unique<std::set<AttributeMatcherNode, Less>>();
  }
  AttributeMatcherNode* child = const_cast<AttributeMatcherNode*>(
      &*children_->emplace(this, pattern).first);
  return {child, child->SetMissing()};
}

std::pair<AttributeMatcherNode*, bool> AttributeMatcherNode::AddMissing(
    const AttributeQualifierPattern& pattern, bool prev_set) {
  if (auto qualifier = pattern.ToQualifier(); qualifier.has_value()) {
    return AddMissing(*qualifier, prev_set);
  }
  ABSL_DCHECK(IsMissing());
  const bool set_missing_children = SetMissingChildren();
  if (set_missing_children && !prev_set) {
    ClearMissingChildren();
    return {nullptr, false};
  }
  if (!set_missing_children) {
    return {nullptr, false};
  }
  if (wildcard_ == nullptr) {
    wildcard_ = std::make_unique<AttributeMatcherNode>(
        this, AttributeQualifierPattern::Wildcard());
  }
  return {wildcard_.get(), wildcard_->SetMissing()};
}

std::pair<AttributeMatcherNode*, bool> AttributeMatcherNode::AddUnknown(
    const AttributeQualifier& pattern, bool prev_set) {
  ABSL_DCHECK(IsUnknown());
  const bool set_unknown_children = SetUnknownChildren();
  if (set_unknown_children && !prev_set) {
    ClearUnknownChildren();
    return {nullptr, false};
  }
  if (!set_unknown_children && wildcard_ != nullptr && wildcard_->IsUnknown()) {
    return {nullptr, false};
  }
  if (children_ == nullptr) {
    children_ = std::make_unique<std::set<AttributeMatcherNode, Less>>();
  }
  AttributeMatcherNode* child = const_cast<AttributeMatcherNode*>(
      &*children_->emplace(this, pattern).first);
  return {child, child->SetUnknown()};
}

std::pair<AttributeMatcherNode*, bool> AttributeMatcherNode::AddUnknown(
    const AttributeQualifierPattern& pattern, bool prev_set) {
  if (auto qualifier = pattern.ToQualifier(); qualifier.has_value()) {
    return AddUnknown(*qualifier, prev_set);
  }
  ABSL_DCHECK(IsUnknown());
  const bool set_unknown_children = SetUnknownChildren();
  if (set_unknown_children && !prev_set) {
    ClearUnknownChildren();
    return {nullptr, false};
  }
  if (!set_unknown_children) {
    return {nullptr, false};
  }
  if (wildcard_ == nullptr) {
    wildcard_ = std::make_unique<AttributeMatcherNode>(
        this, AttributeQualifierPattern::Wildcard());
  }
  return {wildcard_.get(), wildcard_->SetUnknown()};
}

struct MissingAttributeClearer {
  [[nodiscard]]
  static std::pair<size_t, bool> Clear(AttributeMatcherNode* node) {
    if (node->IsUnknown() && node->IsFound()) {
      // Only unknown, skip.
      return {0, false};
    }
    // Must have missing.
    ABSL_DCHECK(node->IsMissing());
    if (!node->HasMissingChildren()) {
      // Leaf.
      node->ClearMissing();
      return {1, node->IsKnown()};
    }
    // Must have children.
    size_t count = 0;
    if (node->children_ != nullptr) {
      auto children_begin = node->children_->begin();
      const auto children_end = node->children_->end();
      while (children_begin != children_end) {
        auto [removed, erase] =
            Clear(const_cast<AttributeMatcherNode*>(&*children_begin));
        count += removed;
        if (erase) {
          children_begin = node->children_->erase(children_begin);
        } else {
          ++children_begin;
        }
      }
    }
    if (node->wildcard_ != nullptr) {
      auto [removed, erase] = Clear(node->wildcard_.get());
      count += removed;
      if (erase) {
        node->wildcard_.reset();
      }
    }
    node->ClearMissing();
    return {count, node->IsKnown()};
  }
};

struct UnknownAttributeClearer {
  [[nodiscard]]
  static std::pair<size_t, bool> Clear(AttributeMatcherNode* node) {
    if (node->IsKnown() && node->IsMissing()) {
      // Only missing, skip.
      return {0, false};
    }
    // Must have missing.
    ABSL_DCHECK(node->IsUnknown());
    if (!node->HasUnknownChildren()) {
      // Leaf.
      node->ClearUnknown();
      return {1, node->IsFound()};
    }
    // Must have children.
    size_t count = 0;
    if (node->children_ != nullptr) {
      auto children_begin = node->children_->begin();
      const auto children_end = node->children_->end();
      while (children_begin != children_end) {
        auto [removed, erase] =
            Clear(const_cast<AttributeMatcherNode*>(&*children_begin));
        count += removed;
        if (erase) {
          children_begin = node->children_->erase(children_begin);
        } else {
          ++children_begin;
        }
      }
    }
    if (node->wildcard_ != nullptr) {
      auto [removed, erase] = Clear(node->wildcard_.get());
      count += removed;
      if (erase) {
        node->wildcard_.reset();
      }
    }
    node->ClearUnknown();
    return {count, node->IsFound()};
  }
};

bool AttributeMatcherNode::RemoveUnknown() {
  ABSL_DCHECK(IsUnknown());
  ABSL_DCHECK(!HasUnknownChildren());
  ClearUnknown();
  AttributeMatcherNode* parent = this->parent();
  if (parent == nullptr) {
    return IsFound();
  }
  if (IsFound()) {
    if (key_.IsWildcard()) {
      ABSL_DCHECK_EQ(parent->wildcard_.get(), this);
      parent->wildcard_.reset();
    } else {
      parent->children_->erase(*this);
      if (parent->children_->empty()) {
        parent->children_.reset();
      }
    }
  }
  return parent->UnknownChildRemoved();
}

bool AttributeMatcherNode::UnknownChildRemoved() {
  ABSL_DCHECK(IsUnknown());
  ABSL_DCHECK(HasUnknownChildren());
  bool unknown_children = false;
  if (children_ != nullptr) {
    for (auto& child : *children_) {
      if (child.IsUnknown()) {
        unknown_children = true;
        break;
      }
    }
  }
  if (!unknown_children && wildcard_ != nullptr && wildcard_->IsUnknown()) {
    unknown_children = true;
  }
  if (unknown_children) {
    return false;
  }
  ClearUnknownChildren();
  return RemoveUnknown();
}

bool AttributeMatcherNode::RemoveMissing() {
  ABSL_DCHECK(IsMissing());
  ABSL_DCHECK(!HasMissingChildren());
  ClearMissing();
  AttributeMatcherNode* parent = this->parent();
  if (parent == nullptr) {
    return IsKnown();
  }
  if (IsKnown()) {
    if (key_.IsWildcard()) {
      ABSL_DCHECK_EQ(parent->wildcard_.get(), this);
      parent->wildcard_.reset();
    } else {
      parent->children_->erase(*this);
      if (parent->children_->empty()) {
        parent->children_.reset();
      }
    }
  }
  return parent->MissingChildRemoved();
}

bool AttributeMatcherNode::MissingChildRemoved() {
  ABSL_DCHECK(IsMissing());
  ABSL_DCHECK(HasMissingChildren());
  bool missing_children = false;
  if (children_ != nullptr) {
    for (auto& child : *children_) {
      if (child.IsMissing()) {
        missing_children = true;
        break;
      }
    }
  }
  if (!missing_children && wildcard_ != nullptr && wildcard_->IsMissing()) {
    missing_children = true;
  }
  if (missing_children) {
    return false;
  }
  ClearMissingChildren();
  return RemoveMissing();
}

}  // namespace common_internal

void AttributeMatcher::ClearMissingAttributes() {
  auto children_begin = children_.begin();
  const auto children_end = children_.end();
  while (children_begin != children_end) {
    if (children_begin->IsUnknown() && children_begin->IsFound()) {
      // Only unknown, skip.
      ++children_begin;
      continue;
    }
    // Must have missing.
    ABSL_DCHECK(children_begin->IsMissing());
    auto [removed, erase] = common_internal::MissingAttributeClearer::Clear(
        &const_cast<Node&>(*children_begin));
    if (erase) {
      children_begin = children_.erase(children_begin);
    } else {
      ++children_begin;
    }
    missing_attributes_ -= removed;
  }
}

void AttributeMatcher::ClearUnknownAttributes() {
  auto children_begin = children_.begin();
  const auto children_end = children_.end();
  while (children_begin != children_end) {
    if (children_begin->IsKnown() && children_begin->IsMissing()) {
      // Only missing, skip.
      ++children_begin;
      continue;
    }
    // Must have unknown.
    ABSL_DCHECK(children_begin->IsUnknown());
    auto [removed, erase] = common_internal::UnknownAttributeClearer::Clear(
        &const_cast<Node&>(*children_begin));
    if (erase) {
      children_begin = children_.erase(children_begin);
    } else {
      ++children_begin;
    }
    unknown_attributes_ -= removed;
  }
}

namespace {

absl::Status AttributePatternNotFound(
    absl::string_view type, absl::string_view pattern,
    absl::SourceLocation source_location = absl::SourceLocation::current()) {
  return absl::NotFoundError(
      absl::StrCat(type, " attribute pattern ", pattern, " not found"),
      source_location);
}

bool IsWildcard(const AttributeQualifierPattern& qualifier) {
  return qualifier.IsWildcard();
}

bool IsWildcard(const AttributeQualifier&) { return false; }

}  // namespace

template <typename Q>
absl::Status AttributeMatcher::AddUnknownAttribute(
    absl::string_view variable, absl::Span<const Q> qualifiers,
    absl::FunctionRef<std::string()> to_string) {
  if (variable.empty()) {
    return absl::InvalidArgumentError(
        "attribute pattern variable name is required");
  }
  for (size_t i = 0; i < qualifiers.size(); ++i) {
    if (!qualifiers[i]) {
      return absl::InvalidArgumentError("bad attribute qualifier pattern");
    }
    if (IsWildcard(qualifiers[i]) && i != qualifiers.size() - 1) {
      return absl::InvalidArgumentError(
          "wildcard qualifier in attribute pattern must be last");
    }
  }
  common_internal::AttributeMatcherNode* node =
      const_cast<common_internal::AttributeMatcherNode*>(
          &*children_
                .emplace(nullptr, AttributeQualifierPattern::OfString(variable))
                .first);
  bool mutated = node->SetUnknown();
  for (const auto& qualifier : qualifiers) {
    std::tie(node, mutated) = node->AddUnknown(qualifier, mutated);
    if (node == nullptr) {
      return absl::AlreadyExistsError(
          absl::StrCat("attribute pattern which is a superset of ", to_string(),
                       " already exists"));
    }
  }
  if (mutated) {
    ++unknown_attributes_;
  } else {
    return absl::AlreadyExistsError(absl::StrCat(
        "attribute pattern covering ", to_string(), " already exists"));
  }
  return absl::OkStatus();
}

template <typename Q>
absl::Status AttributeMatcher::AddMissingAttribute(
    absl::string_view variable, absl::Span<const Q> qualifiers,
    absl::FunctionRef<std::string()> to_string) {
  if (variable.empty()) {
    return absl::InvalidArgumentError(
        "attribute pattern variable name is required");
  }
  for (size_t i = 0; i < qualifiers.size(); ++i) {
    if (!qualifiers[i]) {
      return absl::InvalidArgumentError("bad attribute qualifier pattern");
    }
    if (IsWildcard(qualifiers[i]) && i != qualifiers.size() - 1) {
      return absl::InvalidArgumentError(
          "wildcard qualifier in attribute pattern must be last");
    }
  }
  common_internal::AttributeMatcherNode* node =
      const_cast<common_internal::AttributeMatcherNode*>(
          &*children_
                .emplace(nullptr, AttributeQualifierPattern::OfString(variable))
                .first);
  bool mutated = node->SetMissing();
  for (const auto& qualifier : qualifiers) {
    std::tie(node, mutated) = node->AddMissing(qualifier, mutated);
    if (node == nullptr) {
      return absl::AlreadyExistsError(
          absl::StrCat("attribute pattern which is a superset of ", to_string(),
                       " already exists"));
    }
  }
  if (mutated) {
    ++missing_attributes_;
  } else {
    return absl::AlreadyExistsError(absl::StrCat(
        "attribute pattern covering ", to_string(), " already exists"));
  }
  return absl::OkStatus();
}

absl::Status AttributeMatcher::AddUnknownAttribute(
    const AttributePattern& pattern) {
  return AddUnknownAttribute(
      pattern.variable(), pattern.qualifier_path(),
      [&pattern]() -> std::string { return pattern.ToString(); });
}

absl::Status AttributeMatcher::AddUnknownAttribute(const Attribute& attribute) {
  return AddUnknownAttribute(
      attribute.variable_name(), attribute.qualifier_path(),
      [&attribute]() -> std::string { return attribute.ToString(); });
}

absl::Status AttributeMatcher::AddMissingAttribute(
    const AttributePattern& pattern) {
  return AddMissingAttribute(
      pattern.variable(), pattern.qualifier_path(),
      [&pattern]() -> std::string { return pattern.ToString(); });
}

absl::Status AttributeMatcher::AddMissingAttribute(const Attribute& attribute) {
  return AddMissingAttribute(
      attribute.variable_name(), attribute.qualifier_path(),
      [&attribute]() -> std::string { return attribute.ToString(); });
}

template <typename Q>
absl::Status AttributeMatcher::RemoveUnknownAttribute(
    absl::string_view variable, absl::Span<const Q> qualifiers,
    absl::FunctionRef<std::string()> to_string) {
  auto children_it = children_.find(AttributeQualifierView::OfString(variable));
  if (children_it == children_.end() || children_it->IsKnown()) {
    return AttributePatternNotFound("unknown", to_string());
  }
  Node* variable_node = const_cast<Node*>(&*children_it);
  Node* node = variable_node;
  for (const auto& qualifier : qualifiers) {
    if (node->children_ == nullptr || !node->HasUnknownChildren()) {
      return AttributePatternNotFound("unknown", to_string());
    }
    if (IsWildcard(qualifier)) {
      if (node->wildcard_ == nullptr || node->wildcard_->IsKnown()) {
        return AttributePatternNotFound("unknown", to_string());
      }
      node = node->wildcard_.get();
    } else {
      children_it = node->children_->find(qualifier);
      if (children_it == node->children_->end() || children_it->IsKnown()) {
        return AttributePatternNotFound("unknown", to_string());
      }
      node = const_cast<Node*>(&*children_it);
    }
  }
  if (node->HasUnknownChildren()) {
    return AttributePatternNotFound("unknown", to_string());
  }
  --unknown_attributes_;
  if (node->RemoveUnknown()) {
    children_.erase(*variable_node);
  }
  return absl::OkStatus();
}

template <typename Q>
absl::Status AttributeMatcher::RemoveMissingAttribute(
    absl::string_view variable, absl::Span<const Q> qualifiers,
    absl::FunctionRef<std::string()> to_string) {
  auto children_it = children_.find(AttributeQualifierView::OfString(variable));
  if (children_it == children_.end() || children_it->IsFound()) {
    return AttributePatternNotFound("missing", to_string());
  }
  Node* variable_node = const_cast<Node*>(&*children_it);
  Node* node = variable_node;
  for (const auto& qualifier : qualifiers) {
    if (node->children_ == nullptr || !node->HasMissingChildren()) {
      return AttributePatternNotFound("missing", to_string());
    }
    if (IsWildcard(qualifier)) {
      if (node->wildcard_ == nullptr || node->wildcard_->IsFound()) {
        return AttributePatternNotFound("missing", to_string());
      }
      node = node->wildcard_.get();
    } else {
      children_it = node->children_->find(qualifier);
      if (children_it == node->children_->end() || children_it->IsFound()) {
        return AttributePatternNotFound("missing", to_string());
      }
      node = const_cast<Node*>(&*children_it);
    }
  }
  if (node->HasMissingChildren()) {
    return AttributePatternNotFound("missing", to_string());
  }
  --missing_attributes_;
  if (node->RemoveMissing()) {
    children_.erase(*variable_node);
  }
  return absl::OkStatus();
}

absl::Status AttributeMatcher::RemoveUnknownAttribute(
    const AttributePattern& pattern) {
  return RemoveUnknownAttribute(
      pattern.variable(), pattern.qualifier_path(),
      [&pattern]() -> std::string { return pattern.ToString(); });
}

absl::Status AttributeMatcher::RemoveUnknownAttribute(
    const Attribute& attribute) {
  return RemoveUnknownAttribute(
      attribute.variable_name(), attribute.qualifier_path(),
      [&attribute]() -> std::string { return attribute.ToString(); });
}

absl::Status AttributeMatcher::RemoveMissingAttribute(
    const AttributePattern& pattern) {
  return RemoveMissingAttribute(
      pattern.variable(), pattern.qualifier_path(),
      [&pattern]() -> std::string { return pattern.ToString(); });
}

absl::Status AttributeMatcher::RemoveMissingAttribute(
    const Attribute& attribute) {
  return RemoveMissingAttribute(
      attribute.variable_name(), attribute.qualifier_path(),
      [&attribute]() -> std::string { return attribute.ToString(); });
}

struct AttributeMatcher::MissingAttributeCollector {
  std::vector<AttributePattern> result;
  absl::string_view variable;
  std::vector<AttributeQualifierPattern> qualifiers;

  explicit MissingAttributeCollector(size_t capacity) {
    result.reserve(capacity);
  }

  void CollectVariable(absl::string_view variable,
                       const common_internal::AttributeMatcherNode* node) {
    if (!node->IsMissing()) {
      return;
    }
    if (!node->HasMissingChildren()) {
      result.push_back(AttributePattern(std::string(variable), {}));
      return;
    }
    this->variable = variable;
    qualifiers.clear();
    for (const auto& child : *node->children_) {
      CollectQualifier(child.key(), &child);
    }
    if (node->wildcard_ != nullptr) {
      CollectQualifier(AttributeQualifierPattern::Wildcard(),
                       node->wildcard_.get());
    }
  }

  void CollectQualifier(const AttributeQualifierPattern& qualifier,
                        const common_internal::AttributeMatcherNode* node) {
    if (!node->IsMissing()) {
      return;
    }
    qualifiers.emplace_back(qualifier);
    if (node->HasMissingChildren()) {
      for (const auto& child : *node->children_) {
        CollectQualifier(child.key(), &child);
      }
      if (node->wildcard_ != nullptr) {
        CollectQualifier(AttributeQualifierPattern::Wildcard(),
                         node->wildcard_.get());
      }
    } else {
      result.push_back(AttributePattern(std::string(variable), qualifiers));
    }
    qualifiers.pop_back();
  }
};

struct AttributeMatcher::UnknownAttributeCollector {
  std::vector<AttributePattern> result;
  absl::string_view variable;
  std::vector<AttributeQualifierPattern> qualifiers;

  explicit UnknownAttributeCollector(size_t capacity) {
    result.reserve(capacity);
  }

  void CollectVariable(absl::string_view variable,
                       const common_internal::AttributeMatcherNode* node) {
    if (!node->IsUnknown()) {
      return;
    }
    if (!node->HasUnknownChildren()) {
      result.push_back(AttributePattern(std::string(variable), {}));
      return;
    }
    this->variable = variable;
    qualifiers.clear();
    for (const auto& child : *node->children_) {
      CollectQualifier(child.key(), &child);
    }
    if (node->wildcard_ != nullptr) {
      CollectQualifier(AttributeQualifierPattern::Wildcard(),
                       node->wildcard_.get());
    }
  }

  void CollectQualifier(const AttributeQualifierPattern& qualifier,
                        const common_internal::AttributeMatcherNode* node) {
    if (!node->IsUnknown()) {
      return;
    }
    qualifiers.emplace_back(qualifier);
    if (node->HasUnknownChildren()) {
      for (const auto& child : *node->children_) {
        CollectQualifier(child.key(), &child);
      }
      if (node->wildcard_ != nullptr) {
        CollectQualifier(AttributeQualifierPattern::Wildcard(),
                         node->wildcard_.get());
      }
    } else {
      result.push_back(AttributePattern(std::string(variable), qualifiers));
    }
    qualifiers.pop_back();
  }
};

[[nodiscard]]
std::vector<AttributePattern> AttributeMatcher::GetMissingAttributes() const {
  MissingAttributeCollector collector(missing_attributes_);
  for (const auto& child : children_) {
    // Guaranteed to be string.
    collector.CollectVariable(child.key().GetString(), &child);
  }
  return std::move(collector.result);
}

[[nodiscard]]
std::vector<AttributePattern> AttributeMatcher::GetUnknownAttributes() const {
  UnknownAttributeCollector collector(unknown_attributes_);
  for (const auto& child : children_) {
    // Guaranteed to be string.
    collector.CollectVariable(child.key().GetString(), &child);
  }
  return std::move(collector.result);
}

const AttributeMatcher& EmptyAttributeMatcher() {
  static const absl::NoDestructor<AttributeMatcher> empty;
  return *empty;
}

}  // namespace cel
