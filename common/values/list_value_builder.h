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

#ifndef THIRD_PARTY_CEL_CPP_COMMON_VALUES_LIST_VALUE_BUILDER_H_
#define THIRD_PARTY_CEL_CPP_COMMON_VALUES_LIST_VALUE_BUILDER_H_

#include <cstddef>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "common/native_type.h"
#include "common/value.h"
#include "google/protobuf/arena.h"

namespace cel {

class ValueFactory;

namespace common_internal {

// Extension of ParsedListValueInterface which is also mutable. Accessing this
// like a normal list before all elements are finished being appended is a bug.
// This is primarily used by the runtime to efficiently implement comprehensions
// which accumulate results into a list.
//
// IMPORTANT: This type is only meant to be utilized by the runtime.
class MutableListValue : public CustomListValueInterface {
 public:
  virtual absl::Status Append(Value value) const = 0;

  virtual void Reserve(size_t capacity) const {}

 private:
  NativeTypeId GetNativeTypeId() const override {
    return NativeTypeId::For<MutableListValue>();
  }
};

MutableListValue* absl_nonnull NewMutableListValue(
    google::protobuf::Arena* absl_nonnull arena ABSL_ATTRIBUTE_LIFETIME_BOUND);

bool IsMutableListValue(const Value& value);
bool IsMutableListValue(const ListValue& value);

const MutableListValue* absl_nullable AsMutableListValue(
    const Value& value ABSL_ATTRIBUTE_LIFETIME_BOUND);
const MutableListValue* absl_nullable AsMutableListValue(
    const ListValue& value ABSL_ATTRIBUTE_LIFETIME_BOUND);

const MutableListValue& GetMutableListValue(
    const Value& value ABSL_ATTRIBUTE_LIFETIME_BOUND);
const MutableListValue& GetMutableListValue(
    const ListValue& value ABSL_ATTRIBUTE_LIFETIME_BOUND);

absl_nonnull cel::ListValueBuilderPtr NewListValueBuilder(
    google::protobuf::Arena* absl_nonnull arena);

}  // namespace common_internal

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMMON_VALUES_LIST_VALUE_BUILDER_H_
