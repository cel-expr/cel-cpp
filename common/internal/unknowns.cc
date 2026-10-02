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
#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>

#include "absl/base/nullability.h"
#include "absl/functional/overload.h"
#include "absl/log/absl_check.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "base/attribute.h"
#include "base/attribute_matcher.h"
#include "internal/cstring_view.h"
#include "internal/strings.h"
#include "google/protobuf/arena.h"

namespace cel::common_internal {

namespace {

void AppendQualifier(std::string* out,
                     const AttributeQualifierPattern& qualifier) {
  std::visit(absl::Overload(
                 [](std::monostate) {},
                 [out](bool value) { out->append(value ? "true" : "false"); },
                 [out](int64_t value) { absl::StrAppend(out, value); },
                 [out](uint64_t value) { absl::StrAppend(out, value, "u"); },
                 [out](absl::string_view value) {
                   absl::StrAppend(out,
                                   cel::internal::FormatStringLiteral(value));
                 },
                 [out](common_internal::WildcardType) { out->push_back('*'); }),
             common_internal::AsVariant(qualifier));
}

void AppendQualifier(std::string* out,
                     const AttributeQualifierView& qualifier) {
  std::visit(absl::Overload(
                 [](std::monostate) {},
                 [out](bool value) { out->append(value ? "true" : "false"); },
                 [out](int64_t value) { absl::StrAppend(out, value); },
                 [out](uint64_t value) { absl::StrAppend(out, value, "u"); },
                 [out](absl::string_view value) {
                   absl::StrAppend(out,
                                   cel::internal::FormatStringLiteral(value));
                 }),
             common_internal::AsVariant(qualifier));
}

template <typename T>
[[nodiscard]]
size_t PreflightSetUnion(T* const* first1, T* const* last1, T* const* first2,
                         T* const* last2) {
  size_t preflight = 0;
  for (; first1 != last1; ++preflight) {
    if (first2 == last2) {
      return preflight + static_cast<size_t>(last1 - first1);
    }
    if (*first2 < *first1) {
      ++first2;
    } else {
      if (!(*first1 < *first2)) {
        ++first2;
      }
      ++first1;
    }
  }
  return preflight + static_cast<size_t>(last2 - first2);
}

template <typename T>
[[nodiscard]]
T** SetUnion(T* const* first1, T* const* last1, T* const* first2,
             T* const* last2, T** out) {
  for (; first1 != last1; ++out) {
    if (first2 == last2) {
      return std::copy(first1, last1, out);
    }
    if (*first2 < *first1) {
      *out = *first2++;
    } else {
      *out = *first1;
      if (!(*first1 < *first2)) {
        ++first2;
      }
      ++first1;
    }
  }
  return std::copy(first2, last2, out);
}

}  // namespace

UnknownAttributeNode* absl_nonnull UnknownTree::Convert(
    const AttributeMatcherNode* absl_nonnull matcher,
    const AttributeQualifierView& key) {
  const bool is_wildcard = matcher->key().IsWildcard();
  ABSL_DCHECK(is_wildcard || matcher->key() == key);
  if (is_wildcard) {
    return Convert(matcher->parent())->Step(this, key);
  }
  return Convert(matcher);
}

UnknownAttributeNode* absl_nonnull UnknownTree::Convert(
    const AttributeMatcherNode* absl_nonnull matcher) {
  ABSL_DCHECK(!matcher->key().IsWildcard());
  const size_t scratch_size = matcher->depth() + 1;
  if (scratch_size > scratch_.size()) {
    scratch_.resize(scratch_size);
  }
  const AttributeMatcherNode** scratch_data =
      reinterpret_cast<const AttributeMatcherNode**>(scratch_.data());
  const AttributeMatcherNode* node = matcher;
  for (size_t i = scratch_size; i > 0; --i) {
    scratch_data[i - 1] = node;
    node = node->parent();
  }
  UnknownAttributeNode* unknown = Root()->Attributes()->Step(
      this, *scratch_data[0]->key().ToQualifierView());
  for (size_t i = 1; i < scratch_size; ++i) {
    unknown = unknown->Step(this, *scratch_data[i]->key().ToQualifierView());
  }
  return unknown;
}

std::string UnknownTree::ToString(
    const AttributeMatcherNode* absl_nonnull matcher,
    const AttributeQualifierView& key) {
  const bool is_wildcard = matcher->key().IsWildcard();
  ABSL_DCHECK(is_wildcard || matcher->key() == key);
  if (is_wildcard) {
    std::string result = ToString(matcher->parent());
    result.push_back('[');
    AppendQualifier(&result, key);
    result.push_back(']');
  }
  return ToString(matcher);
}

std::string UnknownTree::ToString(
    const AttributeMatcherNode* absl_nonnull matcher) {
  const size_t scratch_size = matcher->depth() + 1;
  if (scratch_size > scratch_.size()) {
    scratch_.resize(scratch_size);
  }
  const AttributeMatcherNode** scratch_data =
      reinterpret_cast<const AttributeMatcherNode**>(scratch_.data());
  const AttributeMatcherNode* node = matcher;
  for (size_t i = scratch_size; i > 0; --i) {
    scratch_data[i - 1] = node;
    node = node->parent();
  }
  std::string result = scratch_data[0]->key().GetString();
  for (size_t i = 1; i < scratch_size; ++i) {
    result.push_back('[');
    AppendQualifier(&result, scratch_data[i]->key());
    result.push_back(']');
  }
  return result;
}

std::string UnknownTree::ToString(
    const UnknownAttributeNode* absl_nonnull unknown) {
  const size_t scratch_size = unknown->depth();
  if (scratch_size > scratch_.size()) {
    scratch_.resize(scratch_size);
  }
  const UnknownAttributeNode** scratch_data =
      reinterpret_cast<const UnknownAttributeNode**>(scratch_.data());
  const UnknownAttributeNode* node = unknown;
  for (size_t i = scratch_size; i > 0; --i) {
    scratch_data[i - 1] = node;
    node = node->parent();
  }
  std::string result = scratch_data[0]->key().GetString().c_str();
  for (size_t i = 1; i < scratch_size; ++i) {
    result.push_back('[');
    std::visit(
        absl::Overload(
            [](std::monostate) {},
            [&result](bool value) { result.append(value ? "true" : "false"); },
            [&result](int64_t value) { absl::StrAppend(&result, value); },
            [&result](uint64_t value) { absl::StrAppend(&result, value, "u"); },
            [&result](internal::cstring_view value) {
              absl::StrAppend(
                  &result, cel::internal::FormatStringLiteral(value.c_str()));
            }),
        common_internal::AsVariant(scratch_data[i]->key()));
    result.push_back(']');
  }
  return result;
}

UnknownSet UnknownTree::GraftImpl(const UnknownSet& in) {
  ABSL_DCHECK_NE(in.root, RootIfPresent());
  // This should only happen in rare circumstances where somebody passed an
  // UnknownValue from one expression evaluation into another.
  UnknownAttributeSet* unknown_attribute_set = nullptr;
  UnknownFunctionSet* unknown_function_set = nullptr;
  if (in.attributes != nullptr && in.attributes->size > 0) {
    unknown_attribute_set =
        UnknownAttributeSet::Allocate(in.attributes->size, GetArena());
    unknown_attribute_set->max_depth = in.attributes->max_depth;
    for (uint32_t i = 0; i < in.attributes->size; ++i) {
      unknown_attribute_set->nodes[i] = GraftImpl(in.attributes->nodes[i]);
    }
    std::sort(unknown_attribute_set->nodes,
              unknown_attribute_set->nodes + unknown_attribute_set->size);
  }
  if (in.functions != nullptr && in.functions->size > 0) {
    unknown_function_set =
        UnknownFunctionSet::Allocate(in.functions->size, GetArena());
    for (uint32_t i = 0; i < in.functions->size; ++i) {
      unknown_function_set->nodes[i] = GraftImpl(in.functions->nodes[i]);
    }
    std::sort(unknown_function_set->nodes,
              unknown_function_set->nodes + unknown_function_set->size);
  }
  return UnknownSet{
      .root = Root(),
      .attributes = unknown_attribute_set,
      .functions = unknown_function_set,
  };
}

UnknownAttributeNode* UnknownTree::GraftImpl(const UnknownAttributeNode* in) {
  const size_t scratch_size = in->depth();
  if (scratch_size > scratch_.size()) {
    scratch_.resize(scratch_size);
  }
  const UnknownAttributeNode** scratch_data =
      reinterpret_cast<const UnknownAttributeNode**>(scratch_.data());
  for (size_t i = scratch_size; i > 0; --i) {
    scratch_data[i - 1] = in;
    in = in->parent();
  }
  UnknownAttributeNode* out = Root()->Attributes();
  for (size_t i = 0; i < scratch_size; ++i) {
    out = out->Step(this, scratch_data[i]->key());
  }
  return out;
}

UnknownFunctionNode* UnknownTree::GraftImpl(const UnknownFunctionNode* in) {
  return Root()->SetFunction(this, in->key());
}

const UnknownFunctionSet* absl_nonnull MergeUnknownFunctionSets(
    const UnknownFunctionSet* absl_nonnull lhs,
    const UnknownFunctionSet* absl_nonnull rhs,
    google::protobuf::Arena* absl_nonnull arena) {
  UnknownFunctionNode* const* const lhs_begin = lhs->nodes;
  const size_t lhs_size = lhs->size;
  UnknownFunctionNode* const* const lhs_end = lhs_begin + lhs_size;
  UnknownFunctionNode* const* const rhs_begin = rhs->nodes;
  const size_t rhs_size = rhs->size;
  UnknownFunctionNode* const* const rhs_end = rhs_begin + rhs_size;
  const size_t size =
      (PreflightSetUnion)(lhs_begin, lhs_end, rhs_begin, rhs_end);
  if (lhs_size >= rhs_size && lhs_size == size) {
    return lhs;
  }
  if (rhs_size > lhs_size && rhs_size == size) {
    return rhs;
  }
  UnknownFunctionSet* ufs = UnknownFunctionSet::Allocate(size, arena);
  UnknownFunctionNode** out_begin = ufs->nodes;
  UnknownFunctionNode** out_end = out_begin + size;
  out_begin = (SetUnion)(lhs_begin, lhs_end, rhs_begin, rhs_end, out_begin);
  ABSL_DCHECK_EQ(out_begin, out_end);
  return ufs;
}

const UnknownAttributeSet* absl_nonnull MergeUnknownAttributeSets(
    const UnknownAttributeSet* absl_nonnull lhs,
    const UnknownAttributeSet* absl_nonnull rhs,
    google::protobuf::Arena* absl_nonnull arena) {
  UnknownAttributeNode* const* const lhs_begin = lhs->nodes;
  const size_t lhs_size = lhs->size;
  UnknownAttributeNode* const* const lhs_end = lhs_begin + lhs_size;
  UnknownAttributeNode* const* const rhs_begin = rhs->nodes;
  const size_t rhs_size = rhs->size;
  UnknownAttributeNode* const* const rhs_end = rhs_begin + rhs_size;
  const size_t size =
      (PreflightSetUnion)(lhs_begin, lhs_end, rhs_begin, rhs_end);
  if (lhs_size >= rhs_size && lhs_size == size) {
    return lhs;
  }
  if (rhs_size > lhs_size && rhs_size == size) {
    return rhs;
  }
  UnknownAttributeSet* uas = UnknownAttributeSet::Allocate(size, arena);
  uas->max_depth = std::max(lhs->max_depth, rhs->max_depth);
  UnknownAttributeNode** out_begin = uas->nodes;
  UnknownAttributeNode** out_end = out_begin + size;
  out_begin = (SetUnion)(lhs_begin, lhs_end, rhs_begin, rhs_end, out_begin);
  ABSL_DCHECK_EQ(out_begin, out_end);
  return uas;
}

}  // namespace cel::common_internal
