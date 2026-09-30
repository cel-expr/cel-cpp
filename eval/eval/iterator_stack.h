// Copyright 2025 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//       https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef THIRD_PARTY_CEL_CPP_EVAL_EVAL_ITERATOR_STACK_H_
#define THIRD_PARTY_CEL_CPP_EVAL_EVAL_ITERATOR_STACK_H_

#include <cstddef>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/log/absl_check.h"
#include "common/value.h"

namespace cel::runtime_internal {

class IteratorStack final {
 public:
  struct Entry {
    absl_nonnull ValueIteratorPtr iterator;
    size_t iter_slot;
    size_t iter2_slot;
    size_t accu_slot;
  };

  explicit IteratorStack(size_t max_size) : max_size_(max_size) {
    entries_.reserve(max_size_);
  }

  IteratorStack(const IteratorStack&) = delete;
  IteratorStack(IteratorStack&&) = delete;

  IteratorStack& operator=(const IteratorStack&) = delete;
  IteratorStack& operator=(IteratorStack&&) = delete;

  size_t size() const { return entries_.size(); }

  bool empty() const { return entries_.empty(); }

  bool full() const { return entries_.size() == max_size_; }

  size_t max_size() const { return max_size_; }

  void Clear() { entries_.clear(); }

  void Push(absl_nonnull ValueIteratorPtr iterator, size_t iter_slot,
            size_t iter2_slot, size_t accu_slot) {
    ABSL_DCHECK(!full());
    ABSL_DCHECK(iterator != nullptr);

    entries_.push_back(
        Entry{std::move(iterator), iter_slot, iter2_slot, accu_slot});
  }

  void Push(absl_nonnull ValueIteratorPtr iterator, size_t iter_slot,
            size_t accu_slot) {
    ABSL_DCHECK(!full());
    ABSL_DCHECK(iterator != nullptr);

    entries_.push_back(Entry{std::move(iterator), iter_slot, 0, accu_slot});
  }

  ValueIterator* absl_nonnull PeekIterator() {
    ABSL_DCHECK(!empty());

    return entries_.back().iterator.get();
  }

  // Returns a pointer to the top entry in the stack.
  // Invalidated by Pop() and Push().
  Entry* absl_nonnull Peek() {
    ABSL_DCHECK(!empty());

    return &entries_.back();
  }

  void Pop() {
    ABSL_DCHECK(!empty());

    entries_.pop_back();
  }

 private:
  std::vector<Entry> entries_;
  size_t max_size_;
};

}  // namespace cel::runtime_internal

#endif  // THIRD_PARTY_CEL_CPP_EVAL_EVAL_ITERATOR_STACK_H_
