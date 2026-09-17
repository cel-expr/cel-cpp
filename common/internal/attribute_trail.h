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

// This header should ideally be moved to the runtime. Its here temporarily.

#ifndef THIRD_PARTY_CEL_CPP_COMMON_INTERNAL_ATTRIBUTE_TRAIL_H_
#define THIRD_PARTY_CEL_CPP_COMMON_INTERNAL_ATTRIBUTE_TRAIL_H_

#include <optional>
#include <utility>

#include "absl/base/macros.h"
#include "absl/base/optimization.h"
#include "absl/container/btree_set.h"
#include "absl/log/absl_check.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "base/attribute.h"
#include "base/attribute_matcher.h"
#include "common/internal/unknowns.h"
#include "common/value.h"
#include "runtime/internal/errors.h"

namespace cel::common_internal {

[[nodiscard]]
inline UnknownValue AttributeMatchToUnknownValue(
    const AttributeMatcherNode* node, UnknownTree* tree) {
  ABSL_DCHECK(!node->key().IsWildcard());
  ABSL_DCHECK(tree != nullptr);
  UnknownAttributeNode* unknown = tree->Convert(node);
  return common_internal::MakeUnknownValue(UnknownSet{
      .root = tree->Root(),
      .attributes =
          common_internal::CreateUnknownAttributeSet(unknown, tree->GetArena()),
      .functions = nullptr,
  });
}

[[nodiscard]]
inline UnknownValue AttributeMatchToUnknownValue(AttributeMatch match,
                                                 UnknownTree* tree) {
  return common_internal::AttributeMatchToUnknownValue(
      common_internal::GetAttributeMatcherNode(match), tree);
}

[[nodiscard]]
inline UnknownValue AttributeMatchToUnknownValue(
    const AttributeMatcherNode* node, const AttributeQualifierView& key,
    UnknownTree* tree) {
  ABSL_DCHECK(node->key().IsWildcard() || node->key() == key);
  ABSL_DCHECK(tree != nullptr);
  UnknownAttributeNode* unknown = tree->Convert(node, key);
  return common_internal::MakeUnknownValue(UnknownSet{
      .root = tree->Root(),
      .attributes =
          common_internal::CreateUnknownAttributeSet(unknown, tree->GetArena()),
      .functions = nullptr,
  });
}

[[nodiscard]]
inline UnknownValue AttributeMatchToUnknownValue(
    AttributeMatch match, const AttributeQualifierView& key,
    UnknownTree* tree) {
  return common_internal::AttributeMatchToUnknownValue(
      common_internal::GetAttributeMatcherNode(match), key, tree);
}

[[nodiscard]]
inline UnknownValue AttributeMatchToUnknownValue(AttributeMatch match,
                                                 absl::string_view key,
                                                 UnknownTree* tree) {
  return common_internal::AttributeMatchToUnknownValue(
      common_internal::GetAttributeMatcherNode(match),
      AttributeQualifierView::OfString(key), tree);
}

[[nodiscard]]
inline ErrorValue AttributeMatchToMissingValue(
    const AttributeMatcherNode* node, const AttributeQualifierView& key,
    UnknownTree* tree) {
  ABSL_DCHECK(node->IsFull());
  ABSL_DCHECK(node->key().IsWildcard() || node->key() == key);
  ABSL_DCHECK(tree != nullptr);
  // We do not need to convert matcher.
  return ErrorValue::From(
      runtime_internal::CreateMissingAttributeError(tree->ToString(node, key)),
      tree->GetArena());
}

[[nodiscard]]
inline ErrorValue AttributeMatchToMissingValue(
    AttributeMatch match, const AttributeQualifierView& key,
    UnknownTree* tree) {
  return common_internal::AttributeMatchToMissingValue(
      common_internal::GetAttributeMatcherNode(match), key, tree);
}

[[nodiscard]]
inline ErrorValue AttributeMatchToMissingValue(AttributeMatch match,
                                               absl::string_view key,
                                               UnknownTree* tree) {
  return common_internal::AttributeMatchToMissingValue(
      common_internal::GetAttributeMatcherNode(match),
      AttributeQualifierView::OfString(key), tree);
}

class AttributeTrail;
class UnknownAccumulator;

[[nodiscard]]
bool AttributeLeadMatch(const AttributeMatcher& unknown_attributes,
                        const AttributeMatcher& known_attributes,
                        const AttributeMatcher& missing_attributes,
                        absl::string_view variable, Value& value,
                        AttributeTrail& trail, UnknownTree* unknown_tree);

class AttributeTrail {
 public:
  enum MatchMode {
    kFull = 0,
    kPartial,
  };

  AttributeTrail() = default;
  AttributeTrail(const AttributeTrail&) = default;
  AttributeTrail& operator=(const AttributeTrail&) = default;

  template <MatchMode KnownUnknownMode,
            MatchMode MissingMode = MatchMode::kFull>
  [[nodiscard]]
  bool Match(const AttributeQualifierView& qualifier, Value& value,
             AttributeTrail& trail, UnknownTree* unknown_tree) const {
    // !!!WARNING!!!
    // trail can alias *this be careful when updating this code.
    // !!!WARNING!!!
    static_assert(MissingMode == MatchMode::kFull,
                  "we do not support partial matching for missing attributes");
    if (known_.IsNone()) {
      // Known is not being used or the default outcome for the trail is
      // unknown.
      ABSL_DCHECK(!unknown_.IsFull());
      trail.unknown_ = unknown_.MatchQualifier(qualifier);
      if (KnownUnknownMode == MatchMode::kPartial ? !trail.unknown_.IsNone()
                                                  : trail.unknown_.IsFull()) {
        trail.missing_ = AttributeMatch();
        value = AttributeMatchToUnknownValue(
            std::exchange(trail.unknown_, AttributeMatch()), qualifier,
            unknown_tree);
        return true;
      }
    } else {
      // Known is partial or full.
      if (unknown_.IsNone()) {
        // Unknown is none, it does not matter what known is the outcome is
        // known.
        trail.known_ = trail.unknown_ = AttributeMatch();
      } else {
        if (MatchSlow<KnownUnknownMode, MissingMode>(qualifier, value, trail,
                                                     unknown_tree)) {
          return true;
        }
      }
    }
    trail.missing_ = missing_.MatchQualifier(qualifier);
    if (trail.missing_.IsFull()) {
      trail.known_ = trail.unknown_ = AttributeMatch();
      value = AttributeMatchToMissingValue(
          std::exchange(trail.missing_, AttributeMatch()), qualifier,
          unknown_tree);
      return true;
    }
    return false;
  }
  template <MatchMode KnownUnknownMode,
            MatchMode MissingMode = MatchMode::kFull>
  [[nodiscard]]
  bool Match(const AttributeQualifierView& qualifier, Value& value,
             UnknownTree* unknown_tree) {
    return Match<KnownUnknownMode, MissingMode>(qualifier, value, *this,
                                                unknown_tree);
  }

  ABSL_DEPRECATE_AND_INLINE()
  const AttributeTrail& attribute() const { return *this; }

  [[nodiscard]]
  bool IsNone() const {
    return missing_.IsNone() && unknown_.IsNone();
  }

  [[nodiscard]]
  bool IsFullMatch() const {
    return missing_.IsFull() || (unknown_.IsFull() && known_.IsNone());
  }

  [[nodiscard]]
  std::optional<UnknownValue> PartialUnknownMatch(
      UnknownTree* unknown_tree) const {
    if (!unknown_.IsNone()) {
      return AttributeMatchToUnknownValue(unknown_, unknown_tree);
    }
    return std::nullopt;
  }

 private:
  friend class UnknownAccumulator;
  friend bool AttributeLeadMatch(const AttributeMatcher& unknown_attributes,
                                 const AttributeMatcher& known_attributes,
                                 const AttributeMatcher& missing_attributes,
                                 absl::string_view variable, Value& value,
                                 AttributeTrail& trail,
                                 UnknownTree* unknown_tree);

  // Out of line remainder of the logic for Match to allow the compiler to more
  // easily inline Match.
  template <MatchMode KnownUnknownMode,
            MatchMode MissingMode = MatchMode::kFull>
  [[nodiscard]]
  bool MatchSlow(const AttributeQualifierView& qualifier, Value& value,
                 AttributeTrail& trail, UnknownTree* unknown_tree) const {
    static_assert(MissingMode == MatchMode::kFull,
                  "we do not support partial matching for missing attributes");
    // Unknown is partial or full.
    if (known_.IsFull()) {
      ABSL_DCHECK(!unknown_.IsFull())
          << "attribute match cannot be both known and unknown; "
             "overlapping attribute patterns were added for both";
      // The default from here on out is known.
      trail.known_ = trail.unknown_ = AttributeMatch();
    } else {
      // Partial known.
      ABSL_DCHECK(known_.IsPartial());
      if (unknown_.IsFull()) {
        // The default from here on out is unknown unless we get full known.
        // To signal this we leave unknown as full and only match known.
        trail.unknown_ = unknown_;
        trail.known_ = known_.MatchQualifier(qualifier);
        switch (trail.known_.GetType()) {
          case AttributeMatch::Type::NONE:
            // Result is unknown.
            trail.missing_ = AttributeMatch();
            value = AttributeMatchToUnknownValue(
                std::exchange(trail.unknown_, AttributeMatch()), qualifier,
                unknown_tree);
            return true;
          case AttributeMatch::Type::PARTIAL:
            if (KnownUnknownMode != MatchMode::kPartial) {
              break;
            }
            // Result is unknown.
            trail.known_ = AttributeMatch();
            trail.missing_ = AttributeMatch();
            value = AttributeMatchToUnknownValue(
                std::exchange(trail.unknown_, AttributeMatch()), qualifier,
                unknown_tree);
            return true;
          case AttributeMatch::Type::FULL:
            // Result is known.
            trail.known_ = trail.unknown_ = AttributeMatch();
            break;
        }
      } else {
        ABSL_DCHECK(unknown_.IsPartial());
        // Partial known and unknown.
        trail.known_ = known_.MatchQualifier(qualifier);
        trail.unknown_ = unknown_.MatchQualifier(qualifier);
        ABSL_DCHECK(!trail.known_.IsFull() && !trail.unknown_.IsFull())
            << "attribute match cannot be both known and unknown; "
               "overlapping attribute patterns were added for both";
        if (trail.known_.IsNone()) {
          if (KnownUnknownMode == MatchMode::kPartial
                  ? !trail.unknown_.IsNone()
                  : trail.unknown_.IsFull()) {
            trail.missing_ = AttributeMatch();
            value = AttributeMatchToUnknownValue(
                std::exchange(trail.unknown_, AttributeMatch()), qualifier,
                unknown_tree);
            return true;
          }
        } else {
          if (trail.unknown_.IsNone()) {
            trail.known_ = AttributeMatch();
          }
        }
      }
    }
  }

  // Should only be called from when the unknown accumulator.
  AttributeMatch GetNotFullUnknownMatch() const {
    ABSL_DCHECK(!unknown_.IsFull());
    return unknown_;
  }

  // Due to the complicated logic required to correctly perform attribute
  // matching with missing, known, and unknown attributes we keep all this an
  // implementation detail and return only what the caller needs to see.

  // Current match in the attribute matcher used for missing attributes.
  AttributeMatch missing_;
  // Current match in the attribute matcher used for known attributes.
  AttributeMatch known_;
  // Current match in the attribute matcher used for unknown attributes.
  AttributeMatch unknown_;
};

[[nodiscard]]
inline bool AttributeLeadMatch(const AttributeMatcher& unknown_attributes,
                               const AttributeMatcher& known_attributes,
                               const AttributeMatcher& missing_attributes,
                               absl::string_view variable, Value& value,
                               AttributeTrail& trail,
                               UnknownTree* unknown_tree) {
  trail.known_ = known_attributes.MatchVariable(variable);
  trail.unknown_ = unknown_attributes.MatchVariable(variable);
  ABSL_DCHECK(!trail.known_.IsFull() && !trail.unknown_.IsFull())
      << "attribute match cannot be both known and unknown; "
         "overlapping attribute patterns were added for both";
  if (trail.known_.IsNone()) {
    if (trail.unknown_.IsFull()) {
      trail.missing_ = AttributeMatch();
      value = AttributeMatchToUnknownValue(
          std::exchange(trail.unknown_, AttributeMatch()), variable,
          unknown_tree);
      return true;
    }
  } else {
    if (trail.unknown_.IsNone()) {
      trail.known_ = AttributeMatch();
    } else if (trail.known_.IsFull()) {
      trail.known_ = trail.unknown_ = AttributeMatch();
    }
  }
  trail.missing_ = missing_attributes.MatchVariable(variable);
  if (trail.missing_.IsFull()) {
    trail.known_ = trail.unknown_ = AttributeMatch();
    value = AttributeMatchToMissingValue(
        std::exchange(trail.missing_, AttributeMatch()), variable,
        unknown_tree);
    return true;
  }
  return false;
}

class UnknownAccumulator {
 public:
  UnknownAccumulator(const UnknownAccumulator&) = delete;
  UnknownAccumulator(UnknownAccumulator&&) = delete;
  UnknownAccumulator& operator=(const UnknownAccumulator&) = delete;
  UnknownAccumulator& operator=(UnknownAccumulator&&) = delete;

  explicit UnknownAccumulator(UnknownTree* tree) : tree_(tree) {
    ABSL_DCHECK(tree != nullptr);
  }

  void Add(const UnknownValue& value) {
    Add(common_internal::GetUnknownValueRep(value));
  }

  void Add(const UnknownSet& value);

  void Add(AttributeMatch match) {
    ABSL_DCHECK(!match.IsNone());
    unknown_attributes_.insert(
        tree_->Convert(common_internal::GetAttributeMatcherNode(match)));
    empty_ = false;
  }

  void MaybeAdd(AttributeMatch match) {
    if (!match.IsNone()) {
      Add(match);
    }
  }

  void MaybeAdd(const Value& value) {
    if (auto unknown_value = value.AsUnknown(); unknown_value.has_value()) {
      Add(*unknown_value);
    }
  }

  void MaybeAdd(const AttributeTrail& trail) {
    MaybeAdd(trail.GetNotFullUnknownMatch());
  }

  void MaybeAdd(const Value& value, AttributeMatch match) {
    if (auto unknown_value = value.AsUnknown(); unknown_value.has_value()) {
      Add(*unknown_value);
    } else {
      MaybeAdd(match);
    }
  }

  void MaybeAdd(const Value& value, const AttributeTrail& trail) {
    MaybeAdd(value, trail.GetNotFullUnknownMatch());
  }

  [[nodiscard]]
  bool IsEmpty() const {
    return empty_;
  }

  [[nodiscard]]
  std::optional<UnknownValue> Accumulate() &&;

  [[nodiscard]]
  UnknownValue Build() && {
    return std::move(*this).Accumulate().value_or(UnknownValue());
  }

 private:
  void Add(const UnknownAttributeSet* set);

  void Add(const UnknownFunctionSet* set);

  UnknownTree* const tree_;
  // BTrees used to perform merges when more than two unknown sets are
  // encountered. Merges between two sets are performed without additional
  // memory.
  absl::btree_set<UnknownAttributeNode*> unknown_attributes_;
  absl::btree_set<UnknownFunctionNode*> unknown_functions_;
  const UnknownAttributeSet* largest_unknown_attribute_set_ = nullptr;
  const UnknownFunctionSet* largest_unknown_function_set_ = nullptr;
  const UnknownAttributeSet* unknown_attribute_set_pair_[2] = {nullptr,
                                                               nullptr};
  const UnknownFunctionSet* unknown_function_set_pair_[2] = {nullptr, nullptr};
  bool added_unknown_attribute_set_pair_ = false;
  bool added_unknown_function_set_pair_ = false;
  bool empty_ = true;
};

[[nodiscard]]
inline std::optional<UnknownValue> IdentityAndMergeUnknowns(
    absl::Span<const Value> values, absl::Span<const AttributeTrail> attributes,
    UnknownTree* tree, bool partial) {
  ABSL_DCHECK_EQ(values.size(), attributes.size());
  ABSL_DCHECK(tree != nullptr);
  UnknownAccumulator accumulator(tree);
  for (const Value& value : values) {
    accumulator.MaybeAdd(value);
  }
  // There should not be any full unknown matches in attributes, so only inspect
  // it if we want partials.
  if (partial) {
    for (const AttributeTrail& attribute : attributes) {
      accumulator.MaybeAdd(attribute);
    }
  }
  return std::move(accumulator).Accumulate();
}

[[nodiscard]]
inline std::optional<UnknownValue> PartiallyIdentityAndMergeUnknowns(
    absl::Span<const Value> values, absl::Span<const AttributeTrail> attributes,
    UnknownTree* tree) {
  return common_internal::IdentityAndMergeUnknowns(values, attributes, tree,
                                                   /*partial=*/true);
}

[[nodiscard]]
inline std::optional<UnknownValue> FullyIdentityAndMergeUnknowns(
    absl::Span<const Value> values, UnknownTree* tree) {
  return common_internal::IdentityAndMergeUnknowns(values, {}, tree,
                                                   /*partial=*/false);
}

[[nodiscard]]
inline UnknownValue GraftUnknowns(const UnknownValue& value,
                                  UnknownTree* tree) {
  ABSL_DCHECK(tree != nullptr);
  return common_internal::MakeUnknownValue(
      tree->Graft(common_internal::GetUnknownValueRep(value)));
}

[[nodiscard]]
inline Value GraftUnknowns(const Value& value, UnknownTree* tree) {
  ABSL_DCHECK(tree != nullptr);
  if (auto unknown_value = value.AsUnknown();
      ABSL_PREDICT_FALSE(unknown_value.has_value())) {
    return common_internal::GraftUnknowns(*unknown_value, tree);
  }
  return value;
}

[[nodiscard]]
inline UnknownValue MergeUnknowns(const UnknownValue& left,
                                  const UnknownValue& right,
                                  UnknownTree* unknown_tree) {
  common_internal::UnknownSet result;
  const common_internal::UnknownSet& lhs =
      common_internal::GetUnknownValueRep(left);
  const common_internal::UnknownSet& rhs =
      common_internal::GetUnknownValueRep(right);
  if (lhs.attributes == nullptr) {
    result.root = rhs.root;
    result.attributes = rhs.attributes;
  } else if (rhs.attributes == nullptr) {
    result.root = lhs.root;
    result.attributes = lhs.attributes;
  } else {
    result.root = unknown_tree->Root();
    result.attributes = common_internal::MergeUnknownAttributeSets(
        lhs.attributes, rhs.attributes, unknown_tree->GetArena());
  }
  if (lhs.functions == nullptr) {
    result.root = rhs.root;
    result.functions = rhs.functions;
  } else if (rhs.functions == nullptr) {
    result.root = lhs.root;
    result.functions = lhs.functions;
  } else {
    result.root = unknown_tree->Root();
    result.functions = common_internal::MergeUnknownFunctionSets(
        lhs.functions, rhs.functions, unknown_tree->GetArena());
  }
  return common_internal::MakeUnknownValue(result);
}

}  // namespace cel::common_internal

#endif  // THIRD_PARTY_CEL_CPP_COMMON_INTERNAL_ATTRIBUTE_TRAIL_H_
