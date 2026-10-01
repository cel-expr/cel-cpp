// Copyright 2023 Google LLC
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

#include "common/values/legacy_list_value.h"

#include <cstddef>
#include <string>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/types/optional.h"
#include "common/legacy_value.h"
#include "common/native_type.h"
#include "common/value.h"
#include "common/values/legacy_struct_value.h"
#include "common/values/list_value_builder.h"
#include "common/values/values.h"
#include "eval/public/cel_value.h"
#include "internal/casts.h"
#include "internal/status_macros.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"

namespace cel::common_internal {

class LegacyParsedRepeatedFieldListValue final
    : public CustomListValueInterface,
      public google::api::expr::runtime::CelList {
 public:
  // `arena` is expected to be the same arena as the one that the object is
  // allocated on.
  explicit LegacyParsedRepeatedFieldListValue(ParsedRepeatedFieldValue value,
                                              google::protobuf::Arena* absl_nonnull arena)
      : value_(std::move(value)), arena_(arena) {
    ABSL_DCHECK(arena != nullptr);
  }

  // CelList implementation
  int size() const override { return static_cast<int>(value_.Size()); }

  bool empty() const override { return value_.IsEmpty(); }

  google::api::expr::runtime::CelValue operator[](int index) const override {
    return Get(arena_, index);
  }

  google::api::expr::runtime::CelValue Get(google::protobuf::Arena* arena,
                                           int index) const override {
    if (arena == nullptr) {
      arena = arena_;
    }
    if (ABSL_PREDICT_FALSE(index < 0 || index >= size())) {
      return google::api::expr::runtime::CelValue::CreateError(
          google::protobuf::Arena::Create<absl::Status>(
              arena, common_internal::MakeIndexOutOfBoundsError(index)));
    }
    Value result;
    auto status = value_.Get(
        static_cast<size_t>(index), google::protobuf::DescriptorPool::generated_pool(),
        google::protobuf::MessageFactory::generated_factory(), arena, &result);
    if (ABSL_PREDICT_FALSE(!status.ok())) {
      return google::api::expr::runtime::CelValue::CreateError(
          google::protobuf::Arena::Create<absl::Status>(arena, std::move(status)));
    }
    return UnsafeLegacyValue(result, /*stable=*/false, arena);
  }

 protected:
  std::string DebugString() const override { return value_.DebugString(); }

  absl::Status SerializeTo(
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::io::ZeroCopyOutputStream* absl_nonnull output) const override {
    return value_.SerializeTo(descriptor_pool, message_factory, output);
  }

  absl::Status ConvertToJsonArray(
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Message* absl_nonnull json) const override {
    return value_.ConvertToJsonArray(descriptor_pool, message_factory, json);
  }

  absl::Status Equal(const ListValue& other,
                     const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
                     google::protobuf::MessageFactory* absl_nonnull message_factory,
                     google::protobuf::Arena* absl_nonnull arena,
                     Value* absl_nonnull result) const override {
    return value_.Equal(other, descriptor_pool, message_factory, arena, result);
  }

  bool IsZeroValue() const override { return value_.IsZeroValue(); }

  bool IsEmpty() const override { return value_.IsEmpty(); }

  size_t Size() const override { return value_.Size(); }

  absl::Status Get(size_t index,
                   const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
                   google::protobuf::MessageFactory* absl_nonnull message_factory,
                   google::protobuf::Arena* absl_nonnull arena,
                   Value* absl_nonnull result) const override {
    CEL_RETURN_IF_ERROR(
        value_.Get(index, descriptor_pool, message_factory, arena, result));
    interop_internal::WrapLegacyFieldAccessResult(arena, result);
    return absl::OkStatus();
  }

  absl::Status ForEach(
      ForEachWithIndexCallback callback,
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena) const override {
    return value_.ForEach(callback, descriptor_pool, message_factory, arena);
  }

  absl::StatusOr<absl_nonnull ValueIteratorPtr> NewIterator() const override {
    return value_.NewIterator();
  }

  absl::Status Contains(
      const Value& other,
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena,
      Value* absl_nonnull result) const override {
    return value_.Contains(other, descriptor_pool, message_factory, arena,
                           result);
  }

  CustomListValue Clone(google::protobuf::Arena* absl_nonnull arena) const override {
    return CustomListValue(
        google::protobuf::Arena::Create<LegacyParsedRepeatedFieldListValue>(
            arena, value_.Clone(arena), arena),
        arena);
  }

 private:
  NativeTypeId GetNativeTypeId() const override {
    return NativeTypeId::For<LegacyParsedRepeatedFieldListValue>();
  }

  ParsedRepeatedFieldValue value_;
  google::protobuf::Arena* const arena_;
};

class LegacyParsedJsonListValue final
    : public CustomListValueInterface,
      public google::api::expr::runtime::CelList {
 public:
  // `arena` is expected to be the same arena as the one that the object is
  // allocated on.
  explicit LegacyParsedJsonListValue(ParsedJsonListValue value,
                                     google::protobuf::Arena* absl_nonnull arena)
      : value_(std::move(value)), arena_(arena) {
    ABSL_DCHECK(arena != nullptr);
  }

  // CelList implementation
  int size() const override { return static_cast<int>(value_.Size()); }

  bool empty() const override { return value_.IsEmpty(); }

  google::api::expr::runtime::CelValue operator[](int index) const override {
    return Get(arena_, index);
  }

  google::api::expr::runtime::CelValue Get(google::protobuf::Arena* arena,
                                           int index) const override {
    if (arena == nullptr) {
      arena = arena_;
    }
    if (ABSL_PREDICT_FALSE(index < 0 || index >= size())) {
      return google::api::expr::runtime::CelValue::CreateError(
          google::protobuf::Arena::Create<absl::Status>(
              arena, common_internal::MakeIndexOutOfBoundsError(index)));
    }
    Value result;
    auto status = value_.Get(
        static_cast<size_t>(index), google::protobuf::DescriptorPool::generated_pool(),
        google::protobuf::MessageFactory::generated_factory(), arena, &result);
    if (ABSL_PREDICT_FALSE(!status.ok())) {
      return google::api::expr::runtime::CelValue::CreateError(
          google::protobuf::Arena::Create<absl::Status>(arena, std::move(status)));
    }
    return UnsafeLegacyValue(result, /*stable=*/false, arena);
  }

 protected:
  std::string DebugString() const override { return value_.DebugString(); }

  absl::Status SerializeTo(
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::io::ZeroCopyOutputStream* absl_nonnull output) const override {
    return value_.SerializeTo(descriptor_pool, message_factory, output);
  }

  absl::Status ConvertToJsonArray(
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Message* absl_nonnull json) const override {
    return value_.ConvertToJsonArray(descriptor_pool, message_factory, json);
  }

  absl::Status Equal(const ListValue& other,
                     const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
                     google::protobuf::MessageFactory* absl_nonnull message_factory,
                     google::protobuf::Arena* absl_nonnull arena,
                     Value* absl_nonnull result) const override {
    return value_.Equal(other, descriptor_pool, message_factory, arena, result);
  }

  bool IsZeroValue() const override { return value_.IsZeroValue(); }

  bool IsEmpty() const override { return value_.IsEmpty(); }

  size_t Size() const override { return value_.Size(); }

  absl::Status Get(size_t index,
                   const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
                   google::protobuf::MessageFactory* absl_nonnull message_factory,
                   google::protobuf::Arena* absl_nonnull arena,
                   Value* absl_nonnull result) const override {
    CEL_RETURN_IF_ERROR(
        value_.Get(index, descriptor_pool, message_factory, arena, result));
    interop_internal::WrapLegacyFieldAccessResult(arena, result);
    return absl::OkStatus();
  }

  absl::Status ForEach(
      ForEachWithIndexCallback callback,
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena) const override {
    return value_.ForEach(callback, descriptor_pool, message_factory, arena);
  }

  absl::StatusOr<absl_nonnull ValueIteratorPtr> NewIterator() const override {
    return value_.NewIterator();
  }

  absl::Status Contains(
      const Value& other,
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena,
      Value* absl_nonnull result) const override {
    return value_.Contains(other, descriptor_pool, message_factory, arena,
                           result);
  }

  CustomListValue Clone(google::protobuf::Arena* absl_nonnull arena) const override {
    return CustomListValue(google::protobuf::Arena::Create<LegacyParsedJsonListValue>(
                               arena, value_.Clone(arena), arena),
                           arena);
  }

 private:
  NativeTypeId GetNativeTypeId() const override {
    return NativeTypeId::For<LegacyParsedJsonListValue>();
  }

  ParsedJsonListValue value_;
  google::protobuf::Arena* const arena_;
};

CustomListValue WrapLegacyParsedRepeatedField(
    ParsedRepeatedFieldValue value, google::protobuf::Arena* absl_nonnull arena) {
  return CustomListValue(
      google::protobuf::Arena::Create<LegacyParsedRepeatedFieldListValue>(
          arena, std::move(value), arena),
      arena);
}

CustomListValue WrapLegacyParsedJsonList(ParsedJsonListValue value,
                                         google::protobuf::Arena* absl_nonnull arena) {
  return CustomListValue(google::protobuf::Arena::Create<LegacyParsedJsonListValue>(
                             arena, std::move(value), arena),
                         arena);
}

absl::Status LegacyListValue::Equal(
    const Value& other,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena, Value* absl_nonnull result) const {
  if (auto list_value = other.AsList(); list_value.has_value()) {
    return ListValueEqual(*this, *list_value, descriptor_pool, message_factory,
                          arena, result);
  }
  *result = FalseValue();
  return absl::OkStatus();
}

bool IsLegacyListValue(const Value& value) {
  return value.variant_.Is<LegacyListValue>();
}

LegacyListValue GetLegacyListValue(const Value& value) {
  ABSL_DCHECK(IsLegacyListValue(value));
  return value.variant_.Get<LegacyListValue>();
}

absl::optional<LegacyListValue> AsLegacyListValue(const Value& value) {
  if (IsLegacyListValue(value)) {
    return GetLegacyListValue(value);
  }
  if (auto custom_list_value = value.AsCustomList(); custom_list_value) {
    NativeTypeId native_type_id = custom_list_value->GetTypeId();
    if (native_type_id == NativeTypeId::For<CompatListValue>()) {
      return LegacyListValue(
          static_cast<const google::api::expr::runtime::CelList*>(
              cel::internal::down_cast<const CompatListValue*>(
                  custom_list_value->interface())));
    } else if (native_type_id == NativeTypeId::For<MutableCompatListValue>()) {
      return LegacyListValue(
          static_cast<const google::api::expr::runtime::CelList*>(
              cel::internal::down_cast<const MutableCompatListValue*>(
                  custom_list_value->interface())));
    } else if (native_type_id ==
               NativeTypeId::For<LegacyParsedRepeatedFieldListValue>()) {
      return LegacyListValue(static_cast<
                             const google::api::expr::runtime::CelList*>(
          cel::internal::down_cast<const LegacyParsedRepeatedFieldListValue*>(
              custom_list_value->interface())));
    } else if (native_type_id ==
               NativeTypeId::For<LegacyParsedJsonListValue>()) {
      return LegacyListValue(
          static_cast<const google::api::expr::runtime::CelList*>(
              cel::internal::down_cast<const LegacyParsedJsonListValue*>(
                  custom_list_value->interface())));
    }
  }
  return std::nullopt;
}

}  // namespace cel::common_internal
