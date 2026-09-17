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

#include "common/internal/attribute_trail.h"

#include <algorithm>
#include <cstdint>
#include <optional>

#include "absl/container/btree_set.h"
#include "absl/log/absl_check.h"
#include "common/internal/unknowns.h"
#include "common/value.h"

namespace cel::common_internal {

void UnknownAccumulator::Add(const UnknownSet& value) {
  ABSL_DCHECK_EQ(value.root, tree_->RootIfPresent());
  if (value.attributes != nullptr) {
    empty_ = false;
    if (largest_unknown_attribute_set_ != nullptr) {
      if (value.attributes->size > largest_unknown_attribute_set_->size) {
        largest_unknown_attribute_set_ = value.attributes;
      }
    } else {
      largest_unknown_attribute_set_ = value.attributes;
    }
    if (unknown_attribute_set_pair_[0] == nullptr) {
      unknown_attribute_set_pair_[0] = value.attributes;
    } else if (unknown_attribute_set_pair_[1] == nullptr) {
      unknown_attribute_set_pair_[1] = value.attributes;
    } else {
      if (!added_unknown_attribute_set_pair_) {
        Add(unknown_attribute_set_pair_[0]);
        Add(unknown_attribute_set_pair_[1]);
        added_unknown_attribute_set_pair_ = true;
      }
      Add(value.attributes);
    }
  }
  if (value.functions != nullptr) {
    empty_ = false;
    if (largest_unknown_function_set_ != nullptr) {
      if (value.functions->size > largest_unknown_function_set_->size) {
        largest_unknown_function_set_ = value.functions;
      }
    } else {
      largest_unknown_function_set_ = value.functions;
    }
    if (unknown_function_set_pair_[0] == nullptr) {
      unknown_function_set_pair_[0] = value.functions;
    } else if (unknown_function_set_pair_[1] == nullptr) {
      unknown_function_set_pair_[1] = value.functions;
    } else {
      if (!added_unknown_function_set_pair_) {
        Add(unknown_function_set_pair_[0]);
        Add(unknown_function_set_pair_[1]);
        added_unknown_function_set_pair_ = true;
      }
      Add(value.functions);
    }
  }
}

std::optional<UnknownValue> UnknownAccumulator::Accumulate() && {
  if (IsEmpty()) {
    return std::nullopt;
  }
  const UnknownAttributeSet* unknown_attribute_set;
  if (unknown_attributes_.empty()) {
    if (unknown_function_set_pair_[0] == nullptr) {
      ABSL_DCHECK(unknown_attribute_set_pair_[1] == nullptr);
      unknown_attribute_set = nullptr;
    } else if (unknown_attribute_set_pair_[1] == nullptr) {
      unknown_attribute_set = unknown_attribute_set_pair_[0];
    } else {
      // Two way merger can be done with no additional memory.
      unknown_attribute_set = MergeUnknownAttributeSets(
          unknown_attribute_set_pair_[0], unknown_attribute_set_pair_[1],
          tree_->GetArena());
    }
  } else {
    if (largest_unknown_attribute_set_ != nullptr &&
        largest_unknown_attribute_set_->size == unknown_attributes_.size()) {
      unknown_attribute_set = largest_unknown_attribute_set_;
    } else {
      UnknownAttributeSet* mutable_unknown_attribute_set =
          UnknownAttributeSet::Allocate(unknown_attributes_.size(),
                                        tree_->GetArena());
      mutable_unknown_attribute_set->max_depth = 0;
      uint32_t i = 0;
      for (UnknownAttributeNode* unknown_attribute : unknown_attributes_) {
        mutable_unknown_attribute_set->nodes[i++] = unknown_attribute;
        mutable_unknown_attribute_set->max_depth =
            std::max(mutable_unknown_attribute_set->max_depth,
                     static_cast<uint32_t>(unknown_attribute->depth()));
      }
      unknown_attribute_set = mutable_unknown_attribute_set;
    }
    unknown_attributes_.clear();
  }
  const UnknownFunctionSet* unknown_function_set;
  if (unknown_functions_.empty()) {
    if (unknown_function_set_pair_[0] == nullptr) {
      ABSL_DCHECK(unknown_function_set_pair_[1] == nullptr);
      unknown_function_set = nullptr;
    } else if (unknown_function_set_pair_[1] == nullptr) {
      unknown_function_set = unknown_function_set_pair_[0];
    } else {
      // Two way merger can be done with no additional memory.
      unknown_function_set = MergeUnknownFunctionSets(
          unknown_function_set_pair_[0], unknown_function_set_pair_[1],
          tree_->GetArena());
    }
  } else {
    if (largest_unknown_function_set_ != nullptr &&
        largest_unknown_function_set_->size == unknown_functions_.size()) {
      unknown_function_set = largest_unknown_function_set_;
    } else {
      UnknownFunctionSet* mutable_unknown_function_set =
          UnknownFunctionSet::Allocate(unknown_functions_.size(),
                                       tree_->GetArena());
      uint32_t i = 0;
      for (UnknownFunctionNode* unknown_function : unknown_functions_) {
        mutable_unknown_function_set->nodes[i++] = unknown_function;
      }
      unknown_function_set = mutable_unknown_function_set;
    }
    unknown_functions_.clear();
  }
  // One or both of them should be non-null if we got here.
  ABSL_DCHECK(unknown_attribute_set != nullptr ||
              unknown_function_set != nullptr);
  return common_internal::MakeUnknownValue(tree_->Root(), unknown_attribute_set,
                                           unknown_function_set);
}

void UnknownAccumulator::Add(const UnknownAttributeSet* set) {
  for (uint32_t i = 0; i < set->size; ++i) {
    unknown_attributes_.insert(set->nodes[i]);
  }
}

void UnknownAccumulator::Add(const UnknownFunctionSet* set) {
  for (uint32_t i = 0; i < set->size; ++i) {
    unknown_functions_.insert(set->nodes[i]);
  }
}

}  // namespace cel::common_internal
