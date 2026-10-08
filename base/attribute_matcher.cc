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
#include "internal/status_macros.h"

namespace cel {

namespace common_internal {

std::pair<AttributeMatcherNode*, bool> AttributeMatcherNode::Add(
    const AttributeQualifier& pattern, bool prev_emplaced) {
  // If this node was not just created and it has no children, it is a full
  // match and additional children cannot be added.
  if (wildcard_ == nullptr && children_ == nullptr && !prev_emplaced) {
    return {nullptr, false};
  }
  // If this node has a wildcard already, no more children can be added.
  if (wildcard_ != nullptr) {
    return {nullptr, false};
  }
  if (children_ == nullptr) {
    children_ = std::make_unique<std::set<AttributeMatcherNode, Less>>();
  }
  auto [child, emplaced] = children_->emplace(this, pattern);
  return {const_cast<AttributeMatcherNode*>(&*child), emplaced};
}

std::pair<AttributeMatcherNode*, bool> AttributeMatcherNode::Add(
    const AttributeQualifierPattern& pattern, bool prev_emplaced) {
  if (auto qualifier = pattern.ToQualifier(); qualifier.has_value()) {
    return Add(*qualifier, prev_emplaced);
  }
  // If this node was not just created and it has no children, it is a full
  // match and additional children cannot be added.
  if (wildcard_ == nullptr && children_ == nullptr && !prev_emplaced) {
    return {nullptr, false};
  }
  // If this node has children already, no wildcard can be added.
  if (children_ != nullptr) {
    return {nullptr, false};
  }
  if (wildcard_ == nullptr) {
    wildcard_ = std::make_unique<AttributeMatcherNode>(
        this, AttributeQualifierPattern::Wildcard());
    return {wildcard_.get(), true};
  }
  return {wildcard_.get(), false};
}

size_t AttributeMatcherNode::RemoveChildren() {
  if (children_ == nullptr && wildcard_ == nullptr) {
    return 0;
  }
  return RemoveChildrenInternal();
}

size_t AttributeMatcherNode::RemoveChildrenInternal() {
  if (children_ == nullptr && wildcard_ == nullptr) {
    return 1;
  }
  size_t count = 0;
  if (children_ != nullptr) {
    auto children_begin = children_->begin();
    const auto children_end = children_->end();
    while (children_begin != children_end) {
      count += const_cast<AttributeMatcherNode*>(&*children_begin)
                   ->RemoveChildrenInternal();
      children_begin = children_->erase(children_begin);
    }
    children_.reset();
  }
  if (wildcard_ != nullptr) {
    count += wildcard_->RemoveChildrenInternal();
    wildcard_.reset();
  }
  return count;
}

bool AttributeMatcherNode::Remove() {
  AttributeMatcherNode* parent = this->parent();
  if (parent == nullptr) {
    return children_ == nullptr && wildcard_ == nullptr;
  }
  if (children_ == nullptr && wildcard_ == nullptr) {
    if (key_.IsWildcard()) {
      ABSL_DCHECK_EQ(parent->wildcard_.get(), this);
      parent->wildcard_.reset();
    } else {
      parent->children_->erase(*this);
      if (parent->children_->empty()) {
        parent->children_.reset();
      }
    }
    return parent->ChildRemoved();
  }
  return false;
}

bool AttributeMatcherNode::ChildRemoved() { return Remove(); }

}  // namespace common_internal

namespace {

absl::Status AttributePatternNotFound(
    absl::string_view pattern,
    absl::SourceLocation source_location = absl::SourceLocation::current()) {
  return absl::NotFoundError(
      absl::StrCat("attribute pattern ", pattern, " not found"),
      source_location);
}

bool IsWildcard(const AttributeQualifierPattern& qualifier) {
  return qualifier.IsWildcard();
}

bool IsWildcard(const AttributeQualifier&) { return false; }

template <typename Q>
absl::Status ValidateAttribute(absl::string_view variable,
                               absl::Span<const Q> qualifiers,
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
  return absl::OkStatus();
}

}  // namespace

template <typename Q>
absl::Status AttributeMatcher::InsertAttribute(
    absl::string_view variable, absl::Span<const Q> qualifiers,
    absl::FunctionRef<std::string()> to_string) {
  CEL_RETURN_IF_ERROR(ValidateAttribute(variable, qualifiers, to_string));
  auto [node, emplaced] = AddVariable(variable);
  for (const auto& qualifier : qualifiers) {
    std::tie(node, emplaced) = node->Add(qualifier, emplaced);
    if (node == nullptr) {
      break;
    }
  }
  if (node == nullptr) {
    return absl::AlreadyExistsError(
        absl::StrCat("attribute pattern which is a superset of ", to_string(),
                     " already exists"));
  }
  if (!emplaced) {
    return absl::AlreadyExistsError(absl::StrCat(
        "attribute pattern covering ", to_string(), " already exists"));
  }
  ++attributes_;
  return absl::OkStatus();
}

absl::Status AttributeMatcher::InsertAttribute(
    const AttributePattern& pattern) {
  return InsertAttribute(
      pattern.variable(), pattern.qualifier_path(),
      [&pattern]() -> std::string { return pattern.ToString(); });
}

absl::Status AttributeMatcher::InsertAttribute(const Attribute& attribute) {
  return InsertAttribute(
      attribute.variable_name(), attribute.qualifier_path(),
      [&attribute]() -> std::string { return attribute.ToString(); });
}

template <typename Q>
absl::Status AttributeMatcher::UpsertAttribute(
    absl::string_view variable, absl::Span<const Q> qualifiers,
    absl::FunctionRef<std::string()> to_string) {
  CEL_RETURN_IF_ERROR(ValidateAttribute(variable, qualifiers, to_string));
  auto [node, emplaced] = AddVariable(variable);
  for (const auto& qualifier : qualifiers) {
    std::tie(node, emplaced) = node->Add(qualifier, emplaced);
    if (node == nullptr) {
      // subset
      return absl::OkStatus();
    }
  }
  if (node->children_ == nullptr && node->wildcard_ == nullptr) {
    // duplicate.
    return absl::OkStatus();
  }
  if (!emplaced) {
    // superset
    attributes_ -= node->RemoveChildren();
    ++attributes_;
  }
  return absl::OkStatus();
}

absl::Status AttributeMatcher::UpsertAttribute(
    const AttributePattern& pattern) {
  return UpsertAttribute(
      pattern.variable(), pattern.qualifier_path(),
      [&pattern]() -> std::string { return pattern.ToString(); });
}

absl::Status AttributeMatcher::UpsertAttribute(const Attribute& attribute) {
  return UpsertAttribute(
      attribute.variable_name(), attribute.qualifier_path(),
      [&attribute]() -> std::string { return attribute.ToString(); });
}

template <typename Q>
absl::Status AttributeMatcher::RemoveAttribute(
    absl::string_view variable, absl::Span<const Q> qualifiers,
    absl::FunctionRef<std::string()> to_string) {
  CEL_RETURN_IF_ERROR(ValidateAttribute(variable, qualifiers, to_string));
  auto children_it = children_.find(AttributeQualifierView::OfString(variable));
  if (children_it == children_.end()) {
    return AttributePatternNotFound(to_string());
  }
  Node* variable_node = const_cast<Node*>(&*children_it);
  Node* node = variable_node;
  for (const auto& qualifier : qualifiers) {
    if (IsWildcard(qualifier)) {
      if (node->wildcard_ == nullptr) {
        return AttributePatternNotFound(to_string());
      }
      node = node->wildcard_.get();
    } else {
      if (node->children_ == nullptr) {
        return AttributePatternNotFound(to_string());
      }
      children_it = node->children_->find(qualifier);
      if (children_it == node->children_->end()) {
        return AttributePatternNotFound(to_string());
      }
      node = const_cast<Node*>(&*children_it);
    }
  }
  if (node->children_ != nullptr || node->wildcard_ != nullptr) {
    return AttributePatternNotFound(to_string());
  }
  --attributes_;
  if (node->Remove()) {
    children_.erase(*variable_node);
  }
  return absl::OkStatus();
}

absl::Status AttributeMatcher::RemoveAttribute(
    const AttributePattern& pattern) {
  return RemoveAttribute(
      pattern.variable(), pattern.qualifier_path(),
      [&pattern]() -> std::string { return pattern.ToString(); });
}

absl::Status AttributeMatcher::RemoveAttribute(const Attribute& attribute) {
  return RemoveAttribute(
      attribute.variable_name(), attribute.qualifier_path(),
      [&attribute]() -> std::string { return attribute.ToString(); });
}

struct AttributeMatcher::AttributeCollector {
  std::vector<AttributePattern> result;
  absl::string_view variable;
  std::vector<AttributeQualifierPattern> qualifiers;

  explicit AttributeCollector(size_t capacity) { result.reserve(capacity); }

  void CollectVariable(absl::string_view variable,
                       const common_internal::AttributeMatcherNode* node) {
    if (node->children_ == nullptr && node->wildcard_ == nullptr) {
      result.push_back(AttributePattern(std::string(variable), {}));
      return;
    }
    this->variable = variable;
    qualifiers.clear();
    if (node->children_ != nullptr) {
      for (const auto& child : *node->children_) {
        CollectQualifier(child.key(), &child);
      }
    }
    if (node->wildcard_ != nullptr) {
      CollectQualifier(AttributeQualifierPattern::Wildcard(),
                       node->wildcard_.get());
    }
  }

  void CollectQualifier(const AttributeQualifierPattern& qualifier,
                        const common_internal::AttributeMatcherNode* node) {
    qualifiers.emplace_back(qualifier);
    if (node->children_ == nullptr && node->wildcard_ == nullptr) {
      result.push_back(AttributePattern(std::string(variable), qualifiers));
    } else {
      if (node->children_ != nullptr) {
        for (const auto& child : *node->children_) {
          CollectQualifier(child.key(), &child);
        }
      }
      if (node->wildcard_ != nullptr) {
        CollectQualifier(AttributeQualifierPattern::Wildcard(),
                         node->wildcard_.get());
      }
    }
    qualifiers.pop_back();
  }
};

[[nodiscard]]
std::vector<AttributePattern> AttributeMatcher::GetAttributes() const {
  AttributeCollector collector(attributes_);
  for (const auto& child : children_) {
    // Guaranteed to be string.
    collector.CollectVariable(child.key().GetString(), &child);
  }
  return std::move(collector.result);
}

std::pair<typename AttributeMatcher::Node*, bool> AttributeMatcher::AddVariable(
    absl::string_view variable) {
  auto [child, emplaced] =
      children_.emplace(nullptr, AttributeQualifierPattern::OfString(variable));
  return {const_cast<Node*>(&*child), emplaced};
}

const AttributeMatcher& EmptyAttributeMatcher() {
  static const absl::NoDestructor<AttributeMatcher> empty;
  return *empty;
}

}  // namespace cel
