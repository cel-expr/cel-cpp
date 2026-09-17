// Copyright 2024 Google LLC
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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "base/attribute.h"
#include "base/attribute_set.h"
#include "base/function_result.h"
#include "base/function_result_set.h"
#include "common/internal/unknowns.h"
#include "common/unknown.h"
#include "common/value.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/io/zero_copy_stream.h"
#include "google/protobuf/message.h"

namespace cel {

absl::Status UnknownValue::SerializeTo(
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::io::ZeroCopyOutputStream* absl_nonnull output) const {
  ABSL_DCHECK(descriptor_pool != nullptr);
  ABSL_DCHECK(message_factory != nullptr);
  ABSL_DCHECK(output != nullptr);

  return absl::FailedPreconditionError(
      absl::StrCat(GetTypeName(), " is unserializable"));
}

absl::Status UnknownValue::ConvertToJson(
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Message* absl_nonnull json) const {
  ABSL_DCHECK(descriptor_pool != nullptr);
  ABSL_DCHECK(message_factory != nullptr);
  ABSL_DCHECK(json != nullptr);
  ABSL_DCHECK_EQ(json->GetDescriptor()->well_known_type(),
                 google::protobuf::Descriptor::WELLKNOWNTYPE_VALUE);

  return absl::FailedPreconditionError(
      absl::StrCat(GetTypeName(), " is not convertable to JSON"));
}

absl::Status UnknownValue::Equal(
    const Value&, const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena, Value* absl_nonnull result) const {
  ABSL_DCHECK(descriptor_pool != nullptr);
  ABSL_DCHECK(message_factory != nullptr);
  ABSL_DCHECK(arena != nullptr);
  ABSL_DCHECK(result != nullptr);

  *result = FalseValue();
  return absl::OkStatus();
}

[[nodiscard]]
AttributeSet UnknownValue::ToAttributeSet() const {
  AttributeSet attribute_set;
  if (rep_.attributes == nullptr || rep_.attributes->size == 0) {
    return attribute_set;
  }
  std::vector<const common_internal::UnknownAttributeNode*> path;
  path.resize(rep_.attributes->max_depth - 1);
  for (uint32_t i = 0; i < rep_.attributes->size; ++i) {
    const common_internal::UnknownAttributeNode* leaf =
        rep_.attributes->nodes[i];
    const size_t num_qualifiers = leaf->depth() - 1;
    size_t j = num_qualifiers;
    while (leaf->parent() != rep_.root->Attributes()) {
      path[--j] = leaf;
      leaf = leaf->parent();
    }
    // leaf is now the ancestor which is a child of root_, also known as our
    // variable name.
    std::vector<AttributeQualifier> qualifiers;
    qualifiers.reserve(num_qualifiers);
    for (j = 0; j < num_qualifiers; ++j) {
      qualifiers.push_back(AttributeQualifier(path[j]->key()));
    }
    attribute_set.Add(
        Attribute(std::string(leaf->key().GetString()), std::move(qualifiers)));
  }
  return attribute_set;
}

FunctionResultSet UnknownValue::ToFunctionResultSet() const {
  FunctionResultSet function_set;
  if (rep_.functions == nullptr || rep_.functions->size == 0) {
    return function_set;
  }
  for (size_t i = 0; i < rep_.functions->size; ++i) {
    function_set.Add(FunctionResult(rep_.functions->nodes[i]->key()));
  }
  return function_set;
}

namespace common_internal {

UnknownValue MakeUnknownValue(const Unknown& value,
                              google::protobuf::Arena* absl_nonnull arena) {
  if (value.unknown_attributes().empty() &&
      value.unknown_function_results().empty()) {
    return UnknownValue();
  }
  common_internal::UnknownTree tree(arena);
  common_internal::UnknownRoot* root = tree.Root();
  common_internal::UnknownAttributeSet* attributes = nullptr;
  common_internal::UnknownFunctionSet* functions = nullptr;
  if (!value.unknown_attributes().empty()) {
    attributes = common_internal::UnknownAttributeSet::Allocate(
        value.unknown_attributes().size(), arena);
    attributes->size = static_cast<uint32_t>(value.unknown_attributes().size());
    attributes->max_depth = 0;
    size_t i = 0;
    for (const auto& attribute : value.unknown_attributes()) {
      common_internal::UnknownAttributeNode* node = root->Attributes()->Step(
          &tree, AttributeQualifierView::OfString(attribute.variable_name()));
      for (const auto& attribute_qualifier : attribute.qualifier_path()) {
        node = node->Step(&tree, attribute_qualifier);
      }
      attributes->max_depth =
          std::max(attributes->max_depth, static_cast<uint32_t>(node->depth()));
      attributes->nodes[i++] = node;
    }
    std::sort(attributes->nodes, attributes->nodes + attributes->size);
  }
  if (!value.unknown_function_results().empty()) {
    functions = common_internal::UnknownFunctionSet::Allocate(
        value.unknown_attributes().size(), arena);
    functions->size = value.unknown_attributes().size();
    size_t i = 0;
    for (const auto& function_result : value.unknown_function_results()) {
      functions->nodes[i++] = root->SetFunction(&tree, function_result.name());
    }
    std::sort(functions->nodes, functions->nodes + functions->size);
  }
  return MakeUnknownValue(root, attributes, functions);
}

}  // namespace common_internal

}  // namespace cel
