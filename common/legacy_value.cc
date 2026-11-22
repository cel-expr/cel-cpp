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

#include "common/legacy_value.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/call_once.h"
#include "absl/base/no_destructor.h"
#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/functional/overload.h"
#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"
#include "base/attribute.h"
#include "common/kind.h"
#include "common/memory.h"
#include "common/native_type.h"
#include "common/type.h"
#include "common/unknown.h"
#include "common/value.h"
#include "common/value_kind.h"
#include "common/values/legacy_list_value.h"
#include "common/values/legacy_map_value.h"
#include "common/values/legacy_struct_value.h"
#include "common/values/values.h"
#include "eval/internal/cel_value_equal.h"
#include "eval/public/cel_value.h"
#include "eval/public/message_wrapper.h"
#include "eval/public/structs/cel_proto_wrap_value_to_message.h"
#include "eval/public/structs/legacy_type_info_apis.h"
#include "eval/public/structs/proto_message_type_adapter.h"
#include "eval/public/structs/trivial_legacy_type_info_internal.h"
#include "internal/casts.h"
#include "internal/json.h"
#include "internal/status_macros.h"
#include "runtime/runtime_options.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/io/zero_copy_stream.h"
#include "google/protobuf/message.h"
#include "google/protobuf/message_lite.h"

// TODO(uncreated-issue/76): improve coverage for JSON/Any handling

namespace cel {

namespace {

using ::cel::interop_internal::TrivialTypeInfo;
using ::google::api::expr::runtime::CelList;
using ::google::api::expr::runtime::CelMap;
using ::google::api::expr::runtime::CelValue;
using ::google::api::expr::runtime::GetGenericProtoTypeInfoInstance;
using ::google::api::expr::runtime::LegacyTypeInfoApis;
using ::google::api::expr::runtime::MessageWrapper;
using ::google::api::expr::runtime::internal::MaybeWrapValueToMessage;

absl::Status InvalidMapKeyTypeError(ValueKind kind) {
  return absl::InvalidArgumentError(
      absl::StrCat("Invalid map key type: '", ValueKindToString(kind), "'"));
}

MessageWrapper AsMessageWrapper(
    const google::protobuf::Message* absl_nullability_unknown message_ptr,
    const LegacyTypeInfoApis* absl_nullability_unknown type_info) {
  return MessageWrapper(message_ptr, type_info);
}

class CelListIterator final : public ValueIterator {
 public:
  explicit CelListIterator(const CelList* cel_list)
      : cel_list_(cel_list), size_(cel_list_->size()) {}

  bool HasNext() override { return index_ < size_; }

  absl::Status Next(const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
                    google::protobuf::MessageFactory* absl_nonnull message_factory,
                    google::protobuf::Arena* absl_nonnull arena,
                    Value* absl_nonnull result) override {
    if (!HasNext()) {
      return absl::FailedPreconditionError(
          "ValueIterator::Next() called when ValueIterator::HasNext() returns "
          "false");
    }
    auto cel_value = cel_list_->Get(arena, index_);
    CEL_RETURN_IF_ERROR(ModernValue(arena, cel_value, *result));
    ++index_;
    return absl::OkStatus();
  }

  absl::StatusOr<bool> Next1(
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena,
      Value* absl_nonnull key_or_value) override {
    ABSL_DCHECK(descriptor_pool != nullptr);
    ABSL_DCHECK(message_factory != nullptr);
    ABSL_DCHECK(arena != nullptr);
    ABSL_DCHECK(key_or_value != nullptr);

    if (index_ >= size_) {
      return false;
    }
    auto cel_value = cel_list_->Get(arena, index_);
    CEL_RETURN_IF_ERROR(ModernValue(arena, cel_value, *key_or_value));
    ++index_;
    return true;
  }

  absl::StatusOr<bool> Next2(
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena, Value* absl_nonnull key,
      Value* absl_nullable value) override {
    ABSL_DCHECK(descriptor_pool != nullptr);
    ABSL_DCHECK(message_factory != nullptr);
    ABSL_DCHECK(arena != nullptr);
    ABSL_DCHECK(key != nullptr);

    if (index_ >= size_) {
      return false;
    }
    if (value != nullptr) {
      auto cel_value = cel_list_->Get(arena, index_);
      CEL_RETURN_IF_ERROR(ModernValue(arena, cel_value, *value));
    }
    *key = IntValue(index_);
    ++index_;
    return true;
  }

 private:
  const CelList* const cel_list_;
  const int size_;
  int index_ = 0;
};

class CelMapIterator final : public ValueIterator {
 public:
  explicit CelMapIterator(const CelMap* cel_map)
      : cel_map_(cel_map), size_(cel_map->size()) {}

  bool HasNext() override { return index_ < size_; }

  absl::Status Next(const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
                    google::protobuf::MessageFactory* absl_nonnull message_factory,
                    google::protobuf::Arena* absl_nonnull arena,
                    Value* absl_nonnull result) override {
    if (!HasNext()) {
      return absl::FailedPreconditionError(
          "ValueIterator::Next() called when ValueIterator::HasNext() returns "
          "false");
    }
    CEL_RETURN_IF_ERROR(ProjectKeys(arena));
    auto cel_value = (*cel_list_)->Get(arena, index_);
    CEL_RETURN_IF_ERROR(ModernValue(arena, cel_value, *result));
    ++index_;
    return absl::OkStatus();
  }

  absl::StatusOr<bool> Next1(
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena,
      Value* absl_nonnull key_or_value) override {
    ABSL_DCHECK(descriptor_pool != nullptr);
    ABSL_DCHECK(message_factory != nullptr);
    ABSL_DCHECK(arena != nullptr);
    ABSL_DCHECK(key_or_value != nullptr);

    if (index_ >= size_) {
      return false;
    }
    CEL_RETURN_IF_ERROR(ProjectKeys(arena));
    auto cel_value = (*cel_list_)->Get(arena, index_);
    CEL_RETURN_IF_ERROR(ModernValue(arena, cel_value, *key_or_value));
    ++index_;
    return true;
  }

  absl::StatusOr<bool> Next2(
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena, Value* absl_nonnull key,
      Value* absl_nullable value) override {
    ABSL_DCHECK(descriptor_pool != nullptr);
    ABSL_DCHECK(message_factory != nullptr);
    ABSL_DCHECK(arena != nullptr);
    ABSL_DCHECK(key != nullptr);

    if (index_ >= size_) {
      return false;
    }
    CEL_RETURN_IF_ERROR(ProjectKeys(arena));
    auto cel_key = (*cel_list_)->Get(arena, index_);
    if (value != nullptr) {
      auto cel_value = cel_map_->Get(arena, cel_key);
      if (!cel_value) {
        return absl::DataLossError(
            "map iterator returned key that was not present in the map");
      }
      CEL_RETURN_IF_ERROR(ModernValue(arena, *cel_value, *value));
    }
    CEL_RETURN_IF_ERROR(ModernValue(arena, cel_key, *key));
    ++index_;
    return true;
  }

 private:
  absl::Status ProjectKeys(google::protobuf::Arena* arena) {
    if (cel_list_.ok() && *cel_list_ == nullptr) {
      cel_list_ = cel_map_->ListKeys(arena);
    }
    return cel_list_.status();
  }

  const CelMap* const cel_map_;
  const int size_ = 0;
  absl::StatusOr<const CelList*> cel_list_ = nullptr;
  int index_ = 0;
};

}  // namespace

namespace common_internal {

namespace {

class EmptyCompatListValueImpl final : public CompatListValue {
 public:
  static const EmptyCompatListValueImpl& Get() {
    static const absl::NoDestructor<EmptyCompatListValueImpl> empty;
    return *empty;
  }

  EmptyCompatListValueImpl() = default;

  google::protobuf::Arena* GetOwningArena() const override { return nullptr; }

  std::string DebugString() const override { return "[]"; }

  bool IsEmpty() const override { return true; }

  size_t Size() const override { return 0; }

  absl::Status ConvertToJsonArray(
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Message* absl_nonnull json) const override {
    ABSL_DCHECK(descriptor_pool != nullptr);
    ABSL_DCHECK(message_factory != nullptr);
    ABSL_DCHECK(json != nullptr);
    ABSL_DCHECK_EQ(json->GetDescriptor()->well_known_type(),
                   google::protobuf::Descriptor::WELLKNOWNTYPE_LISTVALUE);

    json->Clear();
    return absl::OkStatus();
  }

  CustomListValue Clone(google::protobuf::Arena* absl_nonnull arena) const override {
    return CustomListValue();
  }

  int size() const override { return 0; }

  CelValue operator[](int index) const override {
    static const absl::NoDestructor<absl::Status> error(
        absl::InvalidArgumentError("index out of bounds"));
    return CelValue::CreateError(&*error);
  }

  CelValue Get(google::protobuf::Arena* arena, int index) const override {
    if (arena == nullptr) {
      return (*this)[index];
    }
    return CelValue::CreateError(google::protobuf::Arena::Create<absl::Status>(
        arena, absl::InvalidArgumentError("index out of bounds")));
  }

 private:
  absl::Status Get(size_t index, const google::protobuf::DescriptorPool* absl_nonnull,
                   google::protobuf::MessageFactory* absl_nonnull,
                   google::protobuf::Arena* absl_nonnull arena,
                   Value* absl_nonnull result) const override {
    *result = IndexOutOfBoundsError(index, arena);
    return absl::OkStatus();
  }
};

class CompatListValueImpl final : public CompatListValue {
 public:
  CompatListValueImpl(
      CustomListValue value,
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena)
      : value_(std::move(value)),
        descriptor_pool_(descriptor_pool),
        message_factory_(message_factory),
        arena_(arena) {
    ABSL_DCHECK(descriptor_pool_ != nullptr);
    ABSL_DCHECK(message_factory_ != nullptr);
    ABSL_DCHECK(arena_ != nullptr);
  }

  google::protobuf::Arena* GetOwningArena() const override { return arena_; }

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
    return value_.Equal(Value(other), descriptor_pool, message_factory, arena,
                        result);
  }

  bool IsZeroValue() const override { return value_.IsZeroValue(); }

  CustomListValue Clone(google::protobuf::Arena* absl_nonnull arena) const override {
    ABSL_DCHECK(arena != nullptr);
    if (value_.IsEmpty()) {
      return CustomListValue();
    }
    return CustomListValue(google::protobuf::Arena::Create<CompatListValueImpl>(
                               arena, value_.Clone(arena), descriptor_pool_,
                               message_factory_, arena),
                           arena);
  }

  bool IsEmpty() const override { return value_.IsEmpty(); }

  size_t Size() const override { return value_.Size(); }

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

  CelValue operator[](int index) const override { return Get(arena_, index); }

  CelValue Get(google::protobuf::Arena* arena, int index) const override {
    if (arena == nullptr) {
      arena = arena_;
    }
    if (ABSL_PREDICT_FALSE(index < 0 || index >= size())) {
      return CelValue::CreateError(google::protobuf::Arena::Create<absl::Status>(
          arena, common_internal::MakeIndexOutOfBoundsError(index)));
    }
    Value element;
    absl::Status status =
        value_.Get(static_cast<size_t>(index), descriptor_pool_,
                   message_factory_, arena, &element);
    if (ABSL_PREDICT_FALSE(!status.ok())) {
      return CelValue::CreateError(
          google::protobuf::Arena::Create<absl::Status>(arena, std::move(status)));
    }
    return common_internal::UnsafeLegacyValue(element, /*stable=*/false, arena);
  }

  int size() const override { return static_cast<int>(Size()); }

 protected:
  absl::Status Get(size_t index,
                   const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
                   google::protobuf::MessageFactory* absl_nonnull message_factory,
                   google::protobuf::Arena* absl_nonnull arena,
                   Value* absl_nonnull result) const override {
    return value_.Get(index, descriptor_pool, message_factory, arena, result);
  }

 private:
  const CustomListValue value_;
  const google::protobuf::DescriptorPool* absl_nonnull const descriptor_pool_;
  google::protobuf::MessageFactory* absl_nonnull const message_factory_;
  google::protobuf::Arena* absl_nonnull const arena_;
};

class EmptyCompatMapValueImpl final : public CompatMapValue {
 public:
  static const EmptyCompatMapValueImpl& Get() {
    static const absl::NoDestructor<EmptyCompatMapValueImpl> empty;
    return *empty;
  }

  EmptyCompatMapValueImpl() = default;

  google::protobuf::Arena* GetOwningArena() const override { return nullptr; }

  std::string DebugString() const override { return "{}"; }

  bool IsEmpty() const override { return true; }

  size_t Size() const override { return 0; }

  absl::Status ListKeys(
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena,
      ListValue* absl_nonnull result) const override {
    *result = ListValue();
    return absl::OkStatus();
  }

  absl::StatusOr<absl_nonnull ValueIteratorPtr> NewIterator() const override {
    return NewEmptyValueIterator();
  }

  absl::Status ConvertToJsonObject(
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Message* absl_nonnull json) const override {
    ABSL_DCHECK(descriptor_pool != nullptr);
    ABSL_DCHECK(message_factory != nullptr);
    ABSL_DCHECK(json != nullptr);
    ABSL_DCHECK_EQ(json->GetDescriptor()->well_known_type(),
                   google::protobuf::Descriptor::WELLKNOWNTYPE_STRUCT);

    json->Clear();
    return absl::OkStatus();
  }

  CustomMapValue Clone(google::protobuf::Arena* absl_nonnull) const override {
    return CustomMapValue();
  }

  absl::optional<CelValue> operator[](CelValue key) const override {
    return std::nullopt;
  }

  using CompatMapValue::Get;
  absl::optional<CelValue> Get(google::protobuf::Arena* arena,
                               CelValue key) const override {
    return std::nullopt;
  }

  absl::StatusOr<bool> Has(const CelValue& key) const override { return false; }

  int size() const override { return static_cast<int>(Size()); }

  absl::StatusOr<const CelList*> ListKeys() const override {
    return EmptyCompatListValue();
  }

  absl::StatusOr<const CelList*> ListKeys(google::protobuf::Arena*) const override {
    return ListKeys();
  }

 private:
  absl::StatusOr<bool> Find(
      const Value& key,
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena,
      Value* absl_nonnull result) const override {
    return false;
  }

  absl::StatusOr<bool> Has(
      const Value& key,
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena) const override {
    return false;
  }
};

class CompatMapValueImpl final : public CompatMapValue {
 public:
  CompatMapValueImpl(CustomMapValue value,
                     const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
                     google::protobuf::MessageFactory* absl_nonnull message_factory,
                     google::protobuf::Arena* absl_nonnull arena)
      : value_(std::move(value)),
        descriptor_pool_(descriptor_pool),
        message_factory_(message_factory),
        arena_(arena) {
    ABSL_DCHECK(descriptor_pool_ != nullptr);
    ABSL_DCHECK(message_factory_ != nullptr);
    ABSL_DCHECK(arena_ != nullptr);
  }

  google::protobuf::Arena* GetOwningArena() const override { return arena_; }

  std::string DebugString() const override { return value_.DebugString(); }

  absl::Status SerializeTo(
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::io::ZeroCopyOutputStream* absl_nonnull output) const override {
    return value_.SerializeTo(descriptor_pool, message_factory, output);
  }

  absl::Status ConvertToJsonObject(
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Message* absl_nonnull json) const override {
    return value_.ConvertToJsonObject(descriptor_pool, message_factory, json);
  }

  absl::Status Equal(const MapValue& other,
                     const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
                     google::protobuf::MessageFactory* absl_nonnull message_factory,
                     google::protobuf::Arena* absl_nonnull arena,
                     Value* absl_nonnull result) const override {
    return value_.Equal(Value(other), descriptor_pool, message_factory, arena,
                        result);
  }

  bool IsZeroValue() const override { return value_.IsZeroValue(); }

  CustomMapValue Clone(google::protobuf::Arena* absl_nonnull arena) const override {
    ABSL_DCHECK(arena != nullptr);
    if (value_.IsEmpty()) {
      return CustomMapValue();
    }
    return CustomMapValue(google::protobuf::Arena::Create<CompatMapValueImpl>(
                              arena, value_.Clone(arena), descriptor_pool_,
                              message_factory_, arena),
                          arena);
  }

  bool IsEmpty() const override { return value_.IsEmpty(); }

  size_t Size() const override { return value_.Size(); }

  absl::Status ListKeys(
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena,
      ListValue* absl_nonnull result) const override {
    return value_.ListKeys(descriptor_pool, message_factory, arena, result);
  }

  absl::Status ForEach(
      ForEachCallback callback,
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena) const override {
    return value_.ForEach(callback, descriptor_pool, message_factory, arena);
  }

  absl::StatusOr<absl_nonnull ValueIteratorPtr> NewIterator() const override {
    return value_.NewIterator();
  }

  absl::optional<CelValue> operator[](CelValue key) const override {
    return Get(arena_, key);
  }

  using CompatMapValue::Get;
  absl::optional<CelValue> Get(google::protobuf::Arena* arena,
                               CelValue key) const override {
    if (arena == nullptr) {
      arena = arena_;
    }
    if (auto status = CelValue::CheckMapKeyType(key); !status.ok()) {
      status.IgnoreError();
      return std::nullopt;
    }
    Value modern_key;
    if (!ModernValue(arena, key, modern_key).ok()) {
      return std::nullopt;
    }
    Value modern_val;
    absl::StatusOr<bool> found = value_.Find(
        modern_key, descriptor_pool_, message_factory_, arena, &modern_val);
    if (!found.ok()) {
      return CelValue::CreateError(google::protobuf::Arena::Create<absl::Status>(
          arena, std::move(found).status()));
    }
    if (!*found) {
      return std::nullopt;
    }
    return common_internal::UnsafeLegacyValue(modern_val, /*stable=*/false,
                                              arena);
  }

  absl::StatusOr<bool> Has(const CelValue& key) const override {
    CEL_RETURN_IF_ERROR(CelValue::CheckMapKeyType(key));
    Value modern_key;
    CEL_RETURN_IF_ERROR(ModernValue(arena_, key, modern_key));
    return Has(modern_key, descriptor_pool_, message_factory_, arena_);
  }

  int size() const override { return static_cast<int>(Size()); }

  absl::StatusOr<const CelList*> ListKeys() const override {
    return ListKeys(arena_);
  }

  absl::StatusOr<const CelList*> ListKeys(google::protobuf::Arena* arena) const override {
    if (arena == nullptr) {
      arena = arena_;
    }
    if (arena == arena_) {
      absl::call_once(keys_once_, [this]() {
        ListValue modern_keys;
        absl::Status status = value_.ListKeys(
            descriptor_pool_, message_factory_, arena_, &modern_keys);
        if (!status.ok()) {
          keys_ = std::move(status);
          return;
        }
        if (auto legacy_list = AsLegacyListValue(modern_keys);
            legacy_list.has_value()) {
          keys_ = legacy_list->cel_list();
          return;
        }
        if (auto custom_list = modern_keys.AsCustom();
            custom_list.has_value()) {
          keys_ = MakeCompatListValue(*custom_list, descriptor_pool_,
                                      message_factory_, arena_);
          return;
        }
        keys_ =
            absl::InternalError("failed to convert list keys to legacy list");
      });
      return keys_;
    }
    ListValue modern_keys;
    CEL_RETURN_IF_ERROR(value_.ListKeys(descriptor_pool_, message_factory_,
                                        arena, &modern_keys));
    if (auto legacy_list = AsLegacyListValue(modern_keys);
        legacy_list.has_value()) {
      return legacy_list->cel_list();
    }
    if (auto custom_list = modern_keys.AsCustom(); custom_list.has_value()) {
      return MakeCompatListValue(*custom_list, descriptor_pool_,
                                 message_factory_, arena);
    }
    return absl::InternalError("failed to convert list keys to legacy list");
  }

 protected:
  absl::StatusOr<bool> Find(
      const Value& key,
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena,
      Value* absl_nonnull result) const override {
    return value_.Find(key, descriptor_pool, message_factory, arena, result);
  }

  absl::StatusOr<bool> Has(
      const Value& key,
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena) const override {
    Value result;
    CEL_RETURN_IF_ERROR(
        value_.Has(key, descriptor_pool, message_factory, arena, &result));
    if (result.IsBool()) {
      return result.GetBool().NativeValue();
    }
    if (result.IsError()) {
      return result.GetError().NativeValue();
    }
    return false;
  }

 private:
  const CustomMapValue value_;
  const google::protobuf::DescriptorPool* absl_nonnull const descriptor_pool_;
  google::protobuf::MessageFactory* absl_nonnull const message_factory_;
  google::protobuf::Arena* absl_nonnull const arena_;
  mutable absl::once_flag keys_once_;
  mutable absl::StatusOr<const CelList*> keys_;
};

CelValue LegacyTrivialStructValue(google::protobuf::Arena* absl_nonnull arena,
                                  const Value& value) {
  if (auto legacy_struct_value = common_internal::AsLegacyStructValue(value);
      legacy_struct_value) {
    return CelValue::CreateMessageWrapper(
        AsMessageWrapper(legacy_struct_value->message_ptr(),
                         legacy_struct_value->legacy_type_info()));
  }
  if (auto parsed_message_value = value.AsParsedMessage();
      parsed_message_value) {
    if (interop_internal::IsUnsafeParsedMessageValue(*parsed_message_value)) {
      return CelValue::CreateMessageWrapper(
          AsMessageWrapper(cel::to_address(*parsed_message_value),
                           &GetGenericProtoTypeInfoInstance()));
    }
    auto maybe_cloned = parsed_message_value->Clone(arena);
    return CelValue::CreateMessageWrapper(MessageWrapper(
        cel::to_address(maybe_cloned), &GetGenericProtoTypeInfoInstance()));
  }
  return CelValue::CreateError(google::protobuf::Arena::Create<absl::Status>(
      arena, absl::InvalidArgumentError(absl::StrCat(
                 "unsupported conversion from cel::StructValue to CelValue: ",
                 value.GetRuntimeType().DebugString()))));
}

CelValue LegacyTrivialListValue(google::protobuf::Arena* absl_nonnull arena,
                                const Value& value) {
  if (auto legacy_list_value = common_internal::AsLegacyListValue(value);
      legacy_list_value) {
    return CelValue::CreateList(legacy_list_value->cel_list());
  }
  if (auto parsed_repeated_field_value = value.AsParsedRepeatedField();
      parsed_repeated_field_value) {
    auto wrapped = common_internal::WrapLegacyParsedRepeatedField(
        *parsed_repeated_field_value, arena);
    return CelValue::CreateList(
        common_internal::AsLegacyListValue(wrapped)->cel_list());
  }
  if (auto parsed_json_list_value = value.AsParsedJsonList();
      parsed_json_list_value) {
    auto wrapped = common_internal::WrapLegacyParsedJsonList(
        *parsed_json_list_value, arena);
    return CelValue::CreateList(
        common_internal::AsLegacyListValue(wrapped)->cel_list());
  }
  if (auto custom_list_value = value.AsCustomList(); custom_list_value) {
    auto status_or_compat_list = common_internal::MakeCompatListValue(
        *custom_list_value, google::protobuf::DescriptorPool::generated_pool(),
        google::protobuf::MessageFactory::generated_factory(), arena);
    if (!status_or_compat_list.ok()) {
      return CelValue::CreateError(google::protobuf::Arena::Create<absl::Status>(
          arena, std::move(status_or_compat_list).status()));
    }
    return CelValue::CreateList(*status_or_compat_list);
  }
  return CelValue::CreateError(google::protobuf::Arena::Create<absl::Status>(
      arena, absl::InvalidArgumentError(absl::StrCat(
                 "unsupported conversion from cel::ListValue to CelValue: ",
                 value.GetRuntimeType().DebugString()))));
}

CelValue LegacyTrivialMapValue(google::protobuf::Arena* absl_nonnull arena,
                               const Value& value) {
  if (auto legacy_map_value = common_internal::AsLegacyMapValue(value);
      legacy_map_value) {
    return CelValue::CreateMap(legacy_map_value->cel_map());
  }
  if (auto parsed_map_field_value = value.AsParsedMapField();
      parsed_map_field_value) {
    auto wrapped = common_internal::WrapLegacyParsedMapField(
        *parsed_map_field_value, arena);
    return CelValue::CreateMap(
        common_internal::AsLegacyMapValue(wrapped)->cel_map());
  }
  if (auto parsed_json_map_value = value.AsParsedJsonMap();
      parsed_json_map_value) {
    auto wrapped =
        common_internal::WrapLegacyParsedJsonMap(*parsed_json_map_value, arena);
    return CelValue::CreateMap(
        common_internal::AsLegacyMapValue(wrapped)->cel_map());
  }
  if (auto custom_map_value = value.AsCustomMap(); custom_map_value) {
    auto status_or_compat_map = common_internal::MakeCompatMapValue(
        *custom_map_value, google::protobuf::DescriptorPool::generated_pool(),
        google::protobuf::MessageFactory::generated_factory(), arena);
    if (!status_or_compat_map.ok()) {
      return CelValue::CreateError(google::protobuf::Arena::Create<absl::Status>(
          arena, std::move(status_or_compat_map).status()));
    }
    return CelValue::CreateMap(*status_or_compat_map);
  }
  return CelValue::CreateError(google::protobuf::Arena::Create<absl::Status>(
      arena, absl::InvalidArgumentError(absl::StrCat(
                 "unsupported conversion from cel::MapValue to CelValue: ",
                 value.GetRuntimeType().DebugString()))));
}

LegacyStructValue ParsedMessageToLegacyStructValue(
    const ParsedMessageValue& parsed_message) {
  return LegacyStructValue(cel::to_address(parsed_message),
                           &GetGenericProtoTypeInfoInstance());
}

LegacyStructValue MakeLegacyStructValue(
    const google::protobuf::Message* absl_nonnull message,
    const LegacyTypeInfoApis* legacy_type_info) {
  // Guard against edge cases where a custom implementation of Message
  // misbehaves.
  // Modern value handles this with DCHECKs on value creation, legacy value
  // would allow it and just report an ErrorValue on accesses.
  if (message->GetReflection() == nullptr || legacy_type_info == nullptr) {
    legacy_type_info = TrivialTypeInfo::GetInstance();
  }
  return LegacyStructValue(message, legacy_type_info);
}

}  // namespace

const CompatListValue* absl_nonnull EmptyCompatListValue() {
  return &EmptyCompatListValueImpl::Get();
}

absl::StatusOr<const CompatListValue* absl_nonnull> MakeCompatListValue(
    const CustomListValue& value,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena) {
  ABSL_DCHECK(descriptor_pool != nullptr);
  ABSL_DCHECK(message_factory != nullptr);
  ABSL_DCHECK(arena != nullptr);
  if (value.IsEmpty()) {
    return EmptyCompatListValue();
  }
  if (value.GetTypeId() == NativeTypeId::For<CompatListValue>()) {
    return cel::internal::down_cast<const CompatListValue*>(value.interface());
  }
  return google::protobuf::Arena::Create<CompatListValueImpl>(
      arena, value, descriptor_pool, message_factory, arena);
}

const CompatMapValue* absl_nonnull EmptyCompatMapValue() {
  return &EmptyCompatMapValueImpl::Get();
}

absl::StatusOr<const CompatMapValue* absl_nonnull> MakeCompatMapValue(
    const CustomMapValue& value,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena) {
  ABSL_DCHECK(descriptor_pool != nullptr);
  ABSL_DCHECK(message_factory != nullptr);
  ABSL_DCHECK(arena != nullptr);
  if (value.IsEmpty()) {
    return EmptyCompatMapValue();
  }
  if (NativeTypeId::Of(value) == NativeTypeId::For<CompatMapValue>()) {
    return cel::internal::down_cast<const CompatMapValue*>(value.interface());
  }
  return google::protobuf::Arena::Create<CompatMapValueImpl>(
      arena, value, descriptor_pool, message_factory, arena);
}

google::api::expr::runtime::CelValue UnsafeLegacyValue(
    const Value& value, bool stable, google::protobuf::Arena* absl_nonnull arena) {
  switch (value.kind()) {
    case ValueKind::kNull:
      return CelValue::CreateNull();
    case ValueKind::kBool:
      return CelValue::CreateBool(value.GetBool());
    case ValueKind::kInt:
      return CelValue::CreateInt64(value.GetInt());
    case ValueKind::kUint:
      return CelValue::CreateUint64(value.GetUint());
    case ValueKind::kDouble:
      return CelValue::CreateDouble(value.GetDouble());
    case ValueKind::kString:
      return CelValue::CreateStringView(
          LegacyStringValue(value.GetString(), stable, arena));
    case ValueKind::kBytes:
      return CelValue::CreateBytesView(
          LegacyBytesValue(value.GetBytes(), stable, arena));
    case ValueKind::kStruct:
      return LegacyTrivialStructValue(arena, value);
    case ValueKind::kDuration:
      return CelValue::CreateDuration(value.GetDuration().ToDuration());
    case ValueKind::kTimestamp:
      return CelValue::CreateTimestamp(value.GetTimestamp().ToTime());
    case ValueKind::kList:
      return LegacyTrivialListValue(arena, value);
    case ValueKind::kMap:
      return LegacyTrivialMapValue(arena, value);
    case ValueKind::kType:
      return CelValue::CreateCelTypeView(value.GetType().name());
    default:
      // Everything else is unsupported.
      return CelValue::CreateError(google::protobuf::Arena::Create<absl::Status>(
          arena, absl::InvalidArgumentError(absl::StrCat(
                     "unsupported conversion from cel::Value to CelValue: ",
                     value->GetRuntimeType().DebugString()))));
  }
}

namespace {

std::string LegacyListDebugString(
    const CelList* absl_nullability_unknown impl) {
  return CelValue::CreateList(impl).DebugString();
}

absl::Status LegacyListSerializeTo(
    const CelList* absl_nullability_unknown impl,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::io::ZeroCopyOutputStream* absl_nonnull output) {
  ABSL_DCHECK(descriptor_pool != nullptr);
  ABSL_DCHECK(message_factory != nullptr);
  ABSL_DCHECK(output != nullptr);

  const google::protobuf::Descriptor* descriptor =
      descriptor_pool->FindMessageTypeByName("google.protobuf.ListValue");
  if (descriptor == nullptr) {
    return absl::InternalError(
        "unable to locate descriptor for message type: "
        "google.protobuf.ListValue");
  }

  google::protobuf::Arena arena;
  const google::protobuf::Message* wrapped = MaybeWrapValueToMessage(
      descriptor, message_factory, CelValue::CreateList(impl), &arena);
  if (wrapped == nullptr) {
    return absl::UnknownError("failed to convert legacy map to JSON");
  }
  if (!wrapped->SerializePartialToZeroCopyStream(output)) {
    return absl::UnknownError(
        absl::StrCat("failed to serialize message: ", wrapped->GetTypeName()));
  }
  return absl::OkStatus();
}

absl::Status LegacyListConvertToJson(
    const CelList* absl_nullability_unknown impl,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Message* absl_nonnull json) {
  ABSL_DCHECK(descriptor_pool != nullptr);
  ABSL_DCHECK(message_factory != nullptr);
  ABSL_DCHECK(json != nullptr);
  ABSL_DCHECK_EQ(json->GetDescriptor()->well_known_type(),
                 google::protobuf::Descriptor::WELLKNOWNTYPE_VALUE);

  google::protobuf::Arena arena;
  const google::protobuf::Message* wrapped =
      MaybeWrapValueToMessage(json->GetDescriptor(), message_factory,
                              CelValue::CreateList(impl), &arena);
  if (wrapped == nullptr) {
    return absl::UnknownError("failed to convert legacy list to JSON");
  }

  if (wrapped->GetDescriptor() == json->GetDescriptor()) {
    json->CopyFrom(*wrapped);
  } else {
    absl::Cord serialized;
    if (!wrapped->SerializePartialToString(&serialized)) {
      return absl::UnknownError(absl::StrCat("failed to serialize message: ",
                                             wrapped->GetTypeName()));
    }
    if (!json->ParsePartialFromString(serialized)) {
      return absl::UnknownError(
          absl::StrCat("failed to parsed message: ", json->GetTypeName()));
    }
  }
  return absl::OkStatus();
}

absl::Status LegacyListConvertToJsonArray(
    const CelList* absl_nullability_unknown impl,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Message* absl_nonnull json) {
  ABSL_DCHECK(descriptor_pool != nullptr);
  ABSL_DCHECK(message_factory != nullptr);
  ABSL_DCHECK(json != nullptr);
  ABSL_DCHECK_EQ(json->GetDescriptor()->well_known_type(),
                 google::protobuf::Descriptor::WELLKNOWNTYPE_LISTVALUE);

  google::protobuf::Arena arena;
  const google::protobuf::Message* wrapped =
      MaybeWrapValueToMessage(json->GetDescriptor(), message_factory,
                              CelValue::CreateList(impl), &arena);
  if (wrapped == nullptr) {
    return absl::UnknownError("failed to convert legacy list to JSON");
  }

  if (wrapped->GetDescriptor() == json->GetDescriptor()) {
    json->CopyFrom(*wrapped);
  } else {
    absl::Cord serialized;
    if (!wrapped->SerializePartialToString(&serialized)) {
      return absl::UnknownError(absl::StrCat("failed to serialize message: ",
                                             wrapped->GetTypeName()));
    }
    if (!json->ParsePartialFromString(serialized)) {
      return absl::UnknownError(
          absl::StrCat("failed to parsed message: ", json->GetTypeName()));
    }
  }
  return absl::OkStatus();
}

bool LegacyListIsEmpty(const CelList* absl_nullability_unknown impl) {
  return impl->empty();
}

size_t LegacyListSize(const CelList* absl_nullability_unknown impl) {
  return static_cast<size_t>(impl->size());
}

absl::Status LegacyListGet(
    const CelList* absl_nullability_unknown impl, size_t index,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena, Value* absl_nonnull result) {
  if (ABSL_PREDICT_FALSE(impl->size() < 0 ||
                         index >= static_cast<size_t>(impl->size()))) {
    *result = ErrorValue::From(
        absl::InvalidArgumentError("index out of bounds"), arena);
    return absl::OkStatus();
  }
  CEL_RETURN_IF_ERROR(
      ModernValue(arena, impl->Get(arena, static_cast<int>(index)), *result));
  return absl::OkStatus();
}

absl::Status LegacyListForEach(
    const CelList* absl_nullability_unknown impl,
    CustomListValueInterface::ForEachWithIndexCallback callback,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena) {
  const auto size = impl->size();
  Value element;
  for (int index = 0; index < size; ++index) {
    CEL_RETURN_IF_ERROR(ModernValue(arena, impl->Get(arena, index), element));
    CEL_ASSIGN_OR_RETURN(auto ok, callback(index, Value(element)));
    if (!ok) {
      break;
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<absl_nonnull ValueIteratorPtr> LegacyListNewIterator(
    const CelList* absl_nullability_unknown impl) {
  return std::make_unique<CelListIterator>(impl);
}

absl::Status LegacyListContains(
    const CelList* absl_nullability_unknown impl, const Value& other,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena, Value* absl_nonnull result) {
  CEL_ASSIGN_OR_RETURN(auto legacy_other, LegacyValue(arena, other));
  for (int i = 0; i < impl->size(); ++i) {
    auto element = impl->Get(arena, i);
    absl::optional<bool> equal =
        interop_internal::CelValueEqualImpl(element, legacy_other);
    // Heterogeneous equality behavior is to just return false if equality
    // undefined.
    if (equal.has_value() && *equal) {
      *result = TrueValue();
      return absl::OkStatus();
    }
  }
  *result = FalseValue();
  return absl::OkStatus();
}

constexpr LegacyListValueDispatcher kLegacyListValueDispatcher = {
    /*debug_string=*/&LegacyListDebugString,
    /*serialize_to=*/&LegacyListSerializeTo,
    /*convert_to_json=*/&LegacyListConvertToJson,
    /*convert_to_json_array=*/&LegacyListConvertToJsonArray,
    /*is_empty=*/&LegacyListIsEmpty,
    /*size=*/&LegacyListSize,
    /*get=*/&LegacyListGet,
    /*for_each=*/&LegacyListForEach,
    /*new_iterator=*/&LegacyListNewIterator,
    /*contains=*/&LegacyListContains,
};

std::string LegacyMapDebugString(const CelMap* absl_nullability_unknown impl) {
  return CelValue::CreateMap(impl).DebugString();
}

absl::Status LegacyMapSerializeTo(
    const CelMap* absl_nullability_unknown impl,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::io::ZeroCopyOutputStream* absl_nonnull output) {
  ABSL_DCHECK(descriptor_pool != nullptr);
  ABSL_DCHECK(message_factory != nullptr);
  ABSL_DCHECK(output != nullptr);

  const google::protobuf::Descriptor* descriptor =
      descriptor_pool->FindMessageTypeByName("google.protobuf.Struct");
  if (descriptor == nullptr) {
    return absl::InternalError(
        "unable to locate descriptor for message type: google.protobuf.Struct");
  }

  google::protobuf::Arena arena;
  const google::protobuf::Message* wrapped = MaybeWrapValueToMessage(
      descriptor, message_factory, CelValue::CreateMap(impl), &arena);
  if (wrapped == nullptr) {
    return absl::UnknownError("failed to convert legacy map to JSON");
  }
  if (!wrapped->SerializePartialToZeroCopyStream(output)) {
    return absl::UnknownError(
        absl::StrCat("failed to serialize message: ", wrapped->GetTypeName()));
  }
  return absl::OkStatus();
}

absl::Status LegacyMapConvertToJson(
    const CelMap* absl_nullability_unknown impl,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Message* absl_nonnull json) {
  ABSL_DCHECK(descriptor_pool != nullptr);
  ABSL_DCHECK(message_factory != nullptr);
  ABSL_DCHECK(json != nullptr);
  ABSL_DCHECK_EQ(json->GetDescriptor()->well_known_type(),
                 google::protobuf::Descriptor::WELLKNOWNTYPE_VALUE);

  google::protobuf::Arena arena;
  const google::protobuf::Message* wrapped =
      MaybeWrapValueToMessage(json->GetDescriptor(), message_factory,
                              CelValue::CreateMap(impl), &arena);
  if (wrapped == nullptr) {
    return absl::UnknownError("failed to convert legacy map to JSON");
  }

  if (wrapped->GetDescriptor() == json->GetDescriptor()) {
    json->CopyFrom(*wrapped);
  } else {
    absl::Cord serialized;
    if (!wrapped->SerializePartialToString(&serialized)) {
      return absl::UnknownError(absl::StrCat("failed to serialize message: ",
                                             wrapped->GetTypeName()));
    }
    if (!json->ParsePartialFromString(serialized)) {
      return absl::UnknownError(
          absl::StrCat("failed to parsed message: ", json->GetTypeName()));
    }
  }
  return absl::OkStatus();
}

absl::Status LegacyMapConvertToJsonObject(
    const CelMap* absl_nullability_unknown impl,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Message* absl_nonnull json) {
  ABSL_DCHECK(descriptor_pool != nullptr);
  ABSL_DCHECK(message_factory != nullptr);
  ABSL_DCHECK(json != nullptr);
  ABSL_DCHECK_EQ(json->GetDescriptor()->well_known_type(),
                 google::protobuf::Descriptor::WELLKNOWNTYPE_STRUCT);

  google::protobuf::Arena arena;
  const google::protobuf::Message* wrapped =
      MaybeWrapValueToMessage(json->GetDescriptor(), message_factory,
                              CelValue::CreateMap(impl), &arena);
  if (wrapped == nullptr) {
    return absl::UnknownError("failed to convert legacy map to JSON");
  }

  if (wrapped->GetDescriptor() == json->GetDescriptor()) {
    json->CopyFrom(*wrapped);
  } else {
    absl::Cord serialized;
    if (!wrapped->SerializePartialToString(&serialized)) {
      return absl::UnknownError(absl::StrCat("failed to serialize message: ",
                                             wrapped->GetTypeName()));
    }
    if (!json->ParsePartialFromString(serialized)) {
      return absl::UnknownError(
          absl::StrCat("failed to parsed message: ", json->GetTypeName()));
    }
  }
  return absl::OkStatus();
}

bool LegacyMapIsEmpty(const CelMap* absl_nullability_unknown impl) {
  return impl->empty();
}

size_t LegacyMapSize(const CelMap* absl_nullability_unknown impl) {
  return static_cast<size_t>(impl->size());
}

absl::Status LegacyMapGet(
    const CelMap* absl_nullability_unknown impl, const Value& key,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena, Value* absl_nonnull result) {
  switch (key.kind()) {
    case ValueKind::kError:
      ABSL_FALLTHROUGH_INTENDED;
    case ValueKind::kUnknown:
      *result = Value{key};
      return absl::OkStatus();
    case ValueKind::kBool:
      ABSL_FALLTHROUGH_INTENDED;
    case ValueKind::kInt:
      ABSL_FALLTHROUGH_INTENDED;
    case ValueKind::kUint:
      ABSL_FALLTHROUGH_INTENDED;
    case ValueKind::kString:
      break;
    default:
      *result = ErrorValue::From(InvalidMapKeyTypeError(key.kind()), arena);
      return absl::OkStatus();
  }
  CEL_ASSIGN_OR_RETURN(auto cel_key, LegacyValue(arena, key));
  auto cel_value = impl->Get(arena, cel_key);
  if (!cel_value.has_value()) {
    *result = NoSuchKeyError(key.DebugString(), arena);
    return absl::OkStatus();
  }
  CEL_RETURN_IF_ERROR(ModernValue(arena, *cel_value, *result));
  return absl::OkStatus();
}

absl::StatusOr<bool> LegacyMapFind(
    const CelMap* absl_nullability_unknown impl, const Value& key,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena, Value* absl_nonnull result) {
  switch (key.kind()) {
    case ValueKind::kError:
      ABSL_FALLTHROUGH_INTENDED;
    case ValueKind::kUnknown:
      *result = Value{key};
      return false;
    case ValueKind::kBool:
      ABSL_FALLTHROUGH_INTENDED;
    case ValueKind::kInt:
      ABSL_FALLTHROUGH_INTENDED;
    case ValueKind::kUint:
      ABSL_FALLTHROUGH_INTENDED;
    case ValueKind::kString:
      break;
    default:
      *result = ErrorValue::From(InvalidMapKeyTypeError(key.kind()), arena);
  }
  CEL_ASSIGN_OR_RETURN(auto cel_key, LegacyValue(arena, key));
  auto cel_value = impl->Get(arena, cel_key);
  if (!cel_value.has_value()) {
    *result = NullValue{};
    return false;
  }
  CEL_RETURN_IF_ERROR(ModernValue(arena, *cel_value, *result));
  return true;
}

absl::Status LegacyMapHas(
    const CelMap* absl_nullability_unknown impl, const Value& key,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena, Value* absl_nonnull result) {
  switch (key.kind()) {
    case ValueKind::kError:
      ABSL_FALLTHROUGH_INTENDED;
    case ValueKind::kUnknown:
      *result = Value{key};
      return absl::OkStatus();
    case ValueKind::kBool:
      ABSL_FALLTHROUGH_INTENDED;
    case ValueKind::kInt:
      ABSL_FALLTHROUGH_INTENDED;
    case ValueKind::kUint:
      ABSL_FALLTHROUGH_INTENDED;
    case ValueKind::kString:
      break;
    default:
      *result = ErrorValue::From(InvalidMapKeyTypeError(key.kind()), arena);
      return absl::OkStatus();
  }
  CEL_ASSIGN_OR_RETURN(auto cel_key, LegacyValue(arena, key));
  absl::StatusOr<bool> has = impl->Has(cel_key);
  if (!has.ok()) {
    *result = ErrorValue::From(std::move(has).status(), arena);
    return absl::OkStatus();
  }

  *result = BoolValue(*has);
  return absl::OkStatus();
}

absl::Status LegacyMapListKeys(
    const CelMap* absl_nullability_unknown impl,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena, ListValue* absl_nonnull result) {
  CEL_ASSIGN_OR_RETURN(auto keys, impl->ListKeys(arena));
  *result = ListValue{common_internal::LegacyListValue(keys)};
  return absl::OkStatus();
}

absl::Status LegacyMapForEach(
    const CelMap* absl_nullability_unknown impl,
    CustomMapValueInterface::ForEachCallback callback,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena) {
  CEL_ASSIGN_OR_RETURN(auto keys, impl->ListKeys(arena));
  const auto size = keys->size();
  Value key;
  Value value;
  for (int index = 0; index < size; ++index) {
    auto cel_key = keys->Get(arena, index);
    absl::optional<CelValue> cel_value = impl->Get(arena, cel_key);
    if (!cel_value.has_value()) {
      return absl::NotFoundError("Key from ListKeys not found in CelMap");
    }
    CEL_RETURN_IF_ERROR(ModernValue(arena, cel_key, key));
    CEL_RETURN_IF_ERROR(ModernValue(arena, *cel_value, value));
    CEL_ASSIGN_OR_RETURN(auto ok, callback(key, value));
    if (!ok) {
      break;
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<absl_nonnull ValueIteratorPtr> LegacyMapNewIterator(
    const CelMap* absl_nullability_unknown impl) {
  return std::make_unique<CelMapIterator>(impl);
}

constexpr LegacyMapValueDispatcher kLegacyMapValueDispatcher = {
    /*debug_string=*/&LegacyMapDebugString,
    /*serialize_to=*/&LegacyMapSerializeTo,
    /*convert_to_json=*/&LegacyMapConvertToJson,
    /*convert_to_json_object=*/&LegacyMapConvertToJsonObject,
    /*is_empty=*/&LegacyMapIsEmpty,
    /*size=*/&LegacyMapSize,
    /*get=*/&LegacyMapGet,
    /*find=*/&LegacyMapFind,
    /*has=*/&LegacyMapHas,
    /*list_keys=*/&LegacyMapListKeys,
    /*for_each=*/&LegacyMapForEach,
    /*new_iterator=*/&LegacyMapNewIterator,
};

absl::string_view LegacyStructGetTypeName(
    const google::protobuf::Message* absl_nullability_unknown message_ptr,
    const LegacyTypeInfoApis* absl_nullability_unknown legacy_type_info) {
  auto message_wrapper = AsMessageWrapper(message_ptr, legacy_type_info);
  return message_wrapper.legacy_type_info()->GetTypename(message_wrapper);
}

std::string LegacyStructDebugString(
    const google::protobuf::Message* absl_nullability_unknown message_ptr,
    const LegacyTypeInfoApis* absl_nullability_unknown legacy_type_info) {
  auto message_wrapper = AsMessageWrapper(message_ptr, legacy_type_info);
  return message_wrapper.legacy_type_info()->DebugString(message_wrapper);
}

absl::Status LegacyStructSerializeTo(
    const google::protobuf::Message* absl_nullability_unknown message_ptr,
    const LegacyTypeInfoApis* absl_nullability_unknown legacy_type_info,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::io::ZeroCopyOutputStream* absl_nonnull output) {
  ABSL_DCHECK(descriptor_pool != nullptr);
  ABSL_DCHECK(message_factory != nullptr);
  ABSL_DCHECK(output != nullptr);

  if (ABSL_PREDICT_TRUE(
          message_ptr->SerializePartialToZeroCopyStream(output))) {
    return absl::OkStatus();
  }
  return absl::UnknownError("failed to serialize protocol buffer message");
}

absl::Status LegacyStructConvertToJson(
    const google::protobuf::Message* absl_nullability_unknown message_ptr,
    const LegacyTypeInfoApis* absl_nullability_unknown legacy_type_info,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Message* absl_nonnull json) {
  ABSL_DCHECK(message_ptr != nullptr);
  ABSL_DCHECK(descriptor_pool != nullptr);
  ABSL_DCHECK(message_factory != nullptr);
  ABSL_DCHECK(json != nullptr);
  ABSL_DCHECK_EQ(json->GetDescriptor()->well_known_type(),
                 google::protobuf::Descriptor::WELLKNOWNTYPE_VALUE);

  return internal::MessageToJson(*message_ptr, descriptor_pool, message_factory,
                                 json);
}

absl::Status LegacyStructConvertToJsonObject(
    const google::protobuf::Message* absl_nullability_unknown message_ptr,
    const LegacyTypeInfoApis* absl_nullability_unknown legacy_type_info,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Message* absl_nonnull json) {
  ABSL_DCHECK(message_ptr != nullptr);
  ABSL_DCHECK(descriptor_pool != nullptr);
  ABSL_DCHECK(message_factory != nullptr);
  ABSL_DCHECK(json != nullptr);
  ABSL_DCHECK_EQ(json->GetDescriptor()->well_known_type(),
                 google::protobuf::Descriptor::WELLKNOWNTYPE_STRUCT);

  return internal::MessageToJson(*message_ptr, descriptor_pool, message_factory,
                                 json);
}

absl::Status LegacyStructEqual(
    const google::protobuf::Message* absl_nullability_unknown message_ptr,
    const LegacyTypeInfoApis* absl_nullability_unknown legacy_type_info,
    const Value& other,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena, Value* absl_nonnull result) {
  ABSL_DCHECK(message_ptr != nullptr);
  if (ABSL_PREDICT_FALSE(legacy_type_info == TrivialTypeInfo::GetInstance())) {
    return absl::UnimplementedError(
        absl::StrCat("legacy access APIs missing for ",
                     LegacyStructGetTypeName(message_ptr, legacy_type_info)));
  }
  auto modern_value = UnsafeParsedMessageValue(message_ptr);

  // Unwrap the rhs if it's a legacy struct so the normal implementation
  // doesn't hit the fallback abstract struct comparison.
  if (auto other_legacy = common_internal::AsLegacyStructValue(other);
      other_legacy) {
    if (other_legacy->legacy_type_info() == TrivialTypeInfo::GetInstance()) {
      return absl::UnimplementedError(absl::StrCat(
          "legacy access APIs missing for ", other_legacy->GetTypeName()));
    }
    auto other_message = UnsafeParsedMessageValue(other_legacy->message_ptr());
    return modern_value.Equal(other_message, descriptor_pool, message_factory,
                              arena, result);
  }

  return modern_value.Equal(other, descriptor_pool, message_factory, arena,
                            result);
}

bool LegacyStructIsZeroValue(
    const google::protobuf::Message* absl_nullability_unknown message_ptr,
    const LegacyTypeInfoApis* absl_nullability_unknown legacy_type_info) {
  ABSL_DCHECK(message_ptr != nullptr);
  if (ABSL_PREDICT_FALSE(legacy_type_info == TrivialTypeInfo::GetInstance())) {
    return false;
  }
  return UnsafeParsedMessageValue(message_ptr).IsZeroValue();
}

absl::Status LegacyStructGetFieldByName(
    const google::protobuf::Message* absl_nullability_unknown message_ptr,
    const LegacyTypeInfoApis* absl_nullability_unknown legacy_type_info,
    absl::string_view name, ProtoWrapperTypeOptions unboxing_options,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena, Value* absl_nonnull result) {
  if (ABSL_PREDICT_FALSE(legacy_type_info == TrivialTypeInfo::GetInstance())) {
    *result = NoSuchFieldError(name, arena);
    return absl::OkStatus();
  }

  ParsedMessageValue parsed_message = UnsafeParsedMessageValue(message_ptr);
  const auto* descriptor = parsed_message.GetDescriptor();
  const auto* field = descriptor->FindFieldByName(name);
  if (field == nullptr) {
    field = descriptor->file()->pool()->FindExtensionByPrintableName(descriptor,
                                                                     name);
    if (field == nullptr) {
      *result = NoSuchFieldError(name, arena);
      return absl::OkStatus();
    }
  }

  return interop_internal::WrapLegacyMessageField(
      message_ptr, field, unboxing_options, descriptor_pool, message_factory,
      arena, result);
}

absl::Status LegacyStructGetFieldByNumber(
    const google::protobuf::Message* absl_nullability_unknown message_ptr,
    const LegacyTypeInfoApis* absl_nullability_unknown legacy_type_info,
    int64_t number, ProtoWrapperTypeOptions unboxing_options,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena, Value* absl_nonnull result) {
  return absl::UnimplementedError(
      "access to fields by numbers is not available for legacy structs");
}

absl::StatusOr<bool> LegacyStructHasFieldByName(
    const google::protobuf::Message* absl_nullability_unknown message_ptr,
    const LegacyTypeInfoApis* absl_nullability_unknown legacy_type_info,
    absl::string_view name) {
  ABSL_DCHECK(message_ptr != nullptr);
  if (ABSL_PREDICT_FALSE(legacy_type_info == TrivialTypeInfo::GetInstance())) {
    return common_internal::MakeNoSuchFieldError(name);
  }
  return UnsafeParsedMessageValue(message_ptr).HasFieldByName(name);
}

absl::StatusOr<bool> LegacyStructHasFieldByNumber(
    const google::protobuf::Message* absl_nullability_unknown message_ptr,
    const LegacyTypeInfoApis* absl_nullability_unknown legacy_type_info,
    int64_t number) {
  ABSL_DCHECK(message_ptr != nullptr);
  if (ABSL_PREDICT_FALSE(legacy_type_info == TrivialTypeInfo::GetInstance())) {
    return common_internal::MakeNoSuchFieldError(absl::StrCat(number));
  }
  return UnsafeParsedMessageValue(message_ptr).HasFieldByNumber(number);
}

absl::Status LegacyStructForEachField(
    const google::protobuf::Message* absl_nullability_unknown message_ptr,
    const LegacyTypeInfoApis* absl_nullability_unknown legacy_type_info,
    CustomStructValueInterface::ForEachFieldCallback callback,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena) {
  ABSL_DCHECK(message_ptr != nullptr);
  if (ABSL_PREDICT_FALSE(legacy_type_info == TrivialTypeInfo::GetInstance())) {
    return absl::UnimplementedError(
        absl::StrCat("legacy access APIs missing for ",
                     LegacyStructGetTypeName(message_ptr, legacy_type_info)));
  }
  return UnsafeParsedMessageValue(message_ptr)
      .ForEachField(callback, descriptor_pool, message_factory, arena);
}

absl::Status LegacyStructQualify(
    const google::protobuf::Message* absl_nullability_unknown message_ptr,
    const LegacyTypeInfoApis* absl_nullability_unknown legacy_type_info,
    absl::Span<const SelectQualifier> qualifiers, bool presence_test,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena, Value* absl_nonnull result,
    int* absl_nonnull count) {
  if (ABSL_PREDICT_FALSE(qualifiers.empty())) {
    return absl::InvalidArgumentError("invalid select qualifier path.");
  }
  if (ABSL_PREDICT_FALSE(legacy_type_info == TrivialTypeInfo::GetInstance())) {
    absl::string_view field_name = absl::visit(
        absl::Overload(
            [](const FieldSpecifier& field) -> absl::string_view {
              return field.name;
            },
            [](const AttributeQualifier& field) -> absl::string_view {
              return field.GetStringKey().value_or("<invalid field>");
            }),
        qualifiers.front());
    *result = NoSuchFieldError(field_name, arena);
    *count = -1;
    return absl::OkStatus();
  }

  ParsedMessageValue parsed_message = UnsafeParsedMessageValue(message_ptr);
  CEL_RETURN_IF_ERROR(parsed_message.Qualify(qualifiers, presence_test,
                                             descriptor_pool, message_factory,
                                             arena, result, count));

  interop_internal::WrapLegacyFieldAccessResult(arena, result);
  return absl::OkStatus();
}

const google::protobuf::Message* absl_nullable LegacyStructGetLegacyMessage(
    const google::protobuf::Message* absl_nullability_unknown message_ptr,
    const LegacyTypeInfoApis* absl_nullability_unknown legacy_type_info) {
  if (message_ptr == nullptr || legacy_type_info == nullptr ||
      legacy_type_info == TrivialTypeInfo::GetInstance()) {
    return nullptr;
  }
  // This should not be possible using the normal public APIs, but possible
  // if someone used the MessageWrapper class directly.
  if (IsWellKnownMessageType(message_ptr->GetDescriptor())) {
    return nullptr;
  }
  return message_ptr;
}

constexpr LegacyStructValueDispatcher kLegacyStructValueDispatcher = {
    /*get_type_name=*/&LegacyStructGetTypeName,
    /*debug_string=*/&LegacyStructDebugString,
    /*serialize_to=*/&LegacyStructSerializeTo,
    /*convert_to_json=*/&LegacyStructConvertToJson,
    /*convert_to_json_object=*/&LegacyStructConvertToJsonObject,
    /*equal=*/&LegacyStructEqual,
    /*is_zero_value=*/&LegacyStructIsZeroValue,
    /*get_field_by_name=*/&LegacyStructGetFieldByName,
    /*get_field_by_number=*/&LegacyStructGetFieldByNumber,
    /*has_field_by_name=*/&LegacyStructHasFieldByName,
    /*has_field_by_number=*/&LegacyStructHasFieldByNumber,
    /*for_each_field=*/&LegacyStructForEachField,
    /*qualify=*/&LegacyStructQualify,
    /*get_legacy_message=*/&LegacyStructGetLegacyMessage,
    /*wrap_legacy_message_field=*/&interop_internal::WrapLegacyMessageField,
};

}  // namespace

LegacyListValue::LegacyListValue(
    const google::api::expr::runtime::CelList* absl_nullability_unknown impl)
    : dispatcher_(&kLegacyListValueDispatcher), impl_(impl) {}

LegacyMapValue::LegacyMapValue(
    const google::api::expr::runtime::CelMap* absl_nullability_unknown impl)
    : dispatcher_(&kLegacyMapValueDispatcher), impl_(impl) {}

LegacyStructValue::LegacyStructValue(
    const google::protobuf::Message* absl_nullability_unknown message_ptr,
    const google::api::expr::runtime::
        LegacyTypeInfoApis* absl_nullability_unknown legacy_type_info)
    : dispatcher_(&kLegacyStructValueDispatcher),
      message_ptr_(message_ptr),
      legacy_type_info_(legacy_type_info) {}

}  // namespace common_internal

absl::Status ModernValue(google::protobuf::Arena* arena,
                         google::api::expr::runtime::CelValue legacy_value,
                         Value& result) {
  switch (legacy_value.type()) {
    case CelValue::Type::kNullType:
      result = NullValue{};
      return absl::OkStatus();
    case CelValue::Type::kBool:
      result = BoolValue{legacy_value.BoolOrDie()};
      return absl::OkStatus();
    case CelValue::Type::kInt64:
      result = IntValue{legacy_value.Int64OrDie()};
      return absl::OkStatus();
    case CelValue::Type::kUint64:
      result = UintValue{legacy_value.Uint64OrDie()};
      return absl::OkStatus();
    case CelValue::Type::kDouble:
      result = DoubleValue{legacy_value.DoubleOrDie()};
      return absl::OkStatus();
    case CelValue::Type::kString:
      result = StringValue::Wrap(legacy_value.StringOrDie().value(), nullptr);
      return absl::OkStatus();
    case CelValue::Type::kBytes:
      result = BytesValue::Wrap(legacy_value.BytesOrDie().value(), nullptr);
      return absl::OkStatus();
    case CelValue::Type::kMessage: {
      auto message_wrapper = legacy_value.MessageWrapperOrDie();
      result = common_internal::MakeLegacyStructValue(
          google::protobuf::DownCastMessage<google::protobuf::Message>(
              message_wrapper.message_ptr()),
          message_wrapper.legacy_type_info());
      return absl::OkStatus();
    }
    case CelValue::Type::kDuration:
      result = UnsafeDurationValue(legacy_value.DurationOrDie());
      return absl::OkStatus();
    case CelValue::Type::kTimestamp:
      result = UnsafeTimestampValue(legacy_value.TimestampOrDie());
      return absl::OkStatus();
    case CelValue::Type::kList:
      result = common_internal::FromLegacyList(legacy_value.ListOrDie());
      return absl::OkStatus();
    case CelValue::Type::kMap:
      result = common_internal::FromLegacyMap(legacy_value.MapOrDie());
      return absl::OkStatus();
    case CelValue::Type::kUnknownSet:
      result = common_internal::MakeUnknownValue(
          *legacy_value.UnknownSetOrDie(), arena);
      return absl::OkStatus();
    case CelValue::Type::kCelType: {
      auto type_name = legacy_value.CelTypeOrDie().value();
      if (type_name.empty()) {
        return absl::InvalidArgumentError("empty type name in CelValue");
      }
      result = TypeValue(common_internal::LegacyRuntimeType(type_name));
      return absl::OkStatus();
    }
    case CelValue::Type::kError:
      result = ErrorValue::From(*legacy_value.ErrorOrDie(), arena);
      return absl::OkStatus();
    case CelValue::Type::kAny:
      return absl::InternalError(absl::StrCat(
          "illegal attempt to convert special CelValue type ",
          CelValue::TypeName(legacy_value.type()), " to cel::Value"));
    default:
      break;
  }
  return absl::InvalidArgumentError(absl::StrCat(
      "cel::Value does not support ", KindToString(legacy_value.type())));
}

absl::StatusOr<google::api::expr::runtime::CelValue> LegacyValue(
    google::protobuf::Arena* arena, const Value& modern_value) {
  switch (modern_value.kind()) {
    case ValueKind::kNull:
      return CelValue::CreateNull();
    case ValueKind::kBool:
      return CelValue::CreateBool(modern_value.Get<BoolValue>().NativeValue());
    case ValueKind::kInt:
      return CelValue::CreateInt64(modern_value.Get<IntValue>().NativeValue());
    case ValueKind::kUint:
      return CelValue::CreateUint64(
          modern_value.Get<UintValue>().NativeValue());
    case ValueKind::kDouble:
      return CelValue::CreateDouble(
          modern_value.Get<DoubleValue>().NativeValue());
    case ValueKind::kString:
      return CelValue::CreateStringView(common_internal::LegacyStringValue(
          modern_value.GetString(), /*stable=*/false, arena));
    case ValueKind::kBytes:
      return CelValue::CreateBytesView(common_internal::LegacyBytesValue(
          modern_value.GetBytes(), /*stable=*/false, arena));
    case ValueKind::kStruct:
      return common_internal::LegacyTrivialStructValue(arena, modern_value);
    case ValueKind::kDuration:
      return CelValue::CreateUncheckedDuration(
          modern_value.GetDuration().NativeValue());
    case ValueKind::kTimestamp:
      return CelValue::CreateTimestamp(
          modern_value.GetTimestamp().NativeValue());
    case ValueKind::kList:
      return common_internal::LegacyTrivialListValue(arena, modern_value);
    case ValueKind::kMap:
      return common_internal::LegacyTrivialMapValue(arena, modern_value);
    case ValueKind::kUnknown:
      return CelValue::CreateUnknownSet(google::protobuf::Arena::Create<Unknown>(
          arena, common_internal::ToUnknown(modern_value.Get<UnknownValue>())));
    case ValueKind::kType:
      return CelValue::CreateCelType(
          CelValue::CelTypeHolder(google::protobuf::Arena::Create<std::string>(
              arena, modern_value.Get<TypeValue>().NativeValue().name())));
    case ValueKind::kError:
      return CelValue::CreateError(google::protobuf::Arena::Create<absl::Status>(
          arena, modern_value.Get<ErrorValue>().NativeValue()));
    default:
      return absl::InvalidArgumentError(
          absl::StrCat("google::api::expr::runtime::CelValue does not support ",
                       ValueKindToString(modern_value.kind())));
  }
}

namespace interop_internal {

absl::StatusOr<Value> FromLegacyValue(google::protobuf::Arena* arena,
                                      const CelValue& legacy_value, bool) {
  switch (legacy_value.type()) {
    case CelValue::Type::kNullType:
      return NullValue{};
    case CelValue::Type::kBool:
      return BoolValue(legacy_value.BoolOrDie());
    case CelValue::Type::kInt64:
      return IntValue(legacy_value.Int64OrDie());
    case CelValue::Type::kUint64:
      return UintValue(legacy_value.Uint64OrDie());
    case CelValue::Type::kDouble:
      return DoubleValue(legacy_value.DoubleOrDie());
    case CelValue::Type::kString:
      return StringValue::Wrap(legacy_value.StringOrDie().value(), nullptr);
    case CelValue::Type::kBytes:
      return BytesValue::Wrap(legacy_value.BytesOrDie().value(), nullptr);
    case CelValue::Type::kMessage: {
      auto message_wrapper = legacy_value.MessageWrapperOrDie();
      return common_internal::MakeLegacyStructValue(
          google::protobuf::DownCastMessage<google::protobuf::Message>(
              message_wrapper.message_ptr()),
          message_wrapper.legacy_type_info());
    }
    case CelValue::Type::kDuration:
      return UnsafeDurationValue(legacy_value.DurationOrDie());
    case CelValue::Type::kTimestamp:
      return UnsafeTimestampValue(legacy_value.TimestampOrDie());
    case CelValue::Type::kList:
      return common_internal::FromLegacyList(legacy_value.ListOrDie());
    case CelValue::Type::kMap:
      return common_internal::FromLegacyMap(legacy_value.MapOrDie());
    case CelValue::Type::kUnknownSet:
      return common_internal::MakeUnknownValue(*legacy_value.UnknownSetOrDie(),
                                               arena);
    case CelValue::Type::kCelType:
      return CreateTypeValueFromView(arena,
                                     legacy_value.CelTypeOrDie().value());
    case CelValue::Type::kError:
      return ErrorValue::From(*legacy_value.ErrorOrDie(), arena);
    case CelValue::Type::kAny:
      return absl::InternalError(absl::StrCat(
          "illegal attempt to convert special CelValue type ",
          CelValue::TypeName(legacy_value.type()), " to cel::Value"));
    default:
      break;
  }
  return absl::UnimplementedError(absl::StrCat(
      "conversion from CelValue to cel::Value for type ",
      CelValue::TypeName(legacy_value.type()), " is not yet implemented"));
}

absl::StatusOr<google::api::expr::runtime::CelValue> ToLegacyValue(
    google::protobuf::Arena* arena, const Value& value, bool) {
  switch (value.kind()) {
    case ValueKind::kNull:
      return CelValue::CreateNull();
    case ValueKind::kBool:
      return CelValue::CreateBool(value.Get<BoolValue>().NativeValue());
    case ValueKind::kInt:
      return CelValue::CreateInt64(value.Get<IntValue>().NativeValue());
    case ValueKind::kUint:
      return CelValue::CreateUint64(value.Get<UintValue>().NativeValue());
    case ValueKind::kDouble:
      return CelValue::CreateDouble(value.Get<DoubleValue>().NativeValue());
    case ValueKind::kString:
      return CelValue::CreateStringView(common_internal::LegacyStringValue(
          value.GetString(), /*stable=*/false, arena));
    case ValueKind::kBytes:
      return CelValue::CreateBytesView(common_internal::LegacyBytesValue(
          value.GetBytes(), /*stable=*/false, arena));
    case ValueKind::kStruct:
      return common_internal::LegacyTrivialStructValue(arena, value);
    case ValueKind::kDuration:
      return CelValue::CreateUncheckedDuration(
          value.Get<DurationValue>().NativeValue());
    case ValueKind::kTimestamp:
      return CelValue::CreateTimestamp(
          value.Get<TimestampValue>().NativeValue());
    case ValueKind::kList:
      return common_internal::LegacyTrivialListValue(arena, value);
    case ValueKind::kMap:
      return common_internal::LegacyTrivialMapValue(arena, value);
    case ValueKind::kUnknown:
      return CelValue::CreateUnknownSet(google::protobuf::Arena::Create<Unknown>(
          arena, common_internal::ToUnknown(value.Get<UnknownValue>())));
    case ValueKind::kType:
      return CelValue::CreateCelType(
          CelValue::CelTypeHolder(google::protobuf::Arena::Create<std::string>(
              arena, value.Get<TypeValue>().NativeValue().name())));
    case ValueKind::kError:
      return CelValue::CreateError(google::protobuf::Arena::Create<absl::Status>(
          arena, value.Get<ErrorValue>().NativeValue()));
    default:
      return absl::InvalidArgumentError(
          absl::StrCat("google::api::expr::runtime::CelValue does not support ",
                       ValueKindToString(value.kind())));
  }
}

Value LegacyValueToModernValueOrDie(
    google::protobuf::Arena* arena, const google::api::expr::runtime::CelValue& value,
    bool unchecked) {
  auto status_or_value = FromLegacyValue(arena, value, unchecked);
  ABSL_CHECK_OK(status_or_value.status());  // Crash OK
  return std::move(*status_or_value);
}

std::vector<Value> LegacyValueToModernValueOrDie(
    google::protobuf::Arena* arena,
    absl::Span<const google::api::expr::runtime::CelValue> values,
    bool unchecked) {
  std::vector<Value> modern_values;
  modern_values.reserve(values.size());
  for (const auto& value : values) {
    modern_values.push_back(
        LegacyValueToModernValueOrDie(arena, value, unchecked));
  }
  return modern_values;
}

google::api::expr::runtime::CelValue ModernValueToLegacyValueOrDie(
    google::protobuf::Arena* arena, const Value& value, bool unchecked) {
  auto status_or_value = ToLegacyValue(arena, value, unchecked);
  ABSL_CHECK_OK(status_or_value.status());  // Crash OK
  return std::move(*status_or_value);
}

void WrapLegacyFieldAccessResult(google::protobuf::Arena* absl_nonnull arena,
                                 Value* absl_nonnull result) {
  if (result->IsParsedMessage()) {
    *result = common_internal::ParsedMessageToLegacyStructValue(
        result->GetParsedMessage());
  } else if (result->IsParsedRepeatedField()) {
    *result =
        WrapLegacyParsedRepeatedField(result->GetParsedRepeatedField(), arena);
  } else if (result->IsParsedJsonList()) {
    *result = WrapLegacyParsedJsonList(result->GetParsedJsonList(), arena);
  } else if (result->IsParsedMapField()) {
    *result = WrapLegacyParsedMapField(result->GetParsedMapField(), arena);
  } else if (result->IsParsedJsonMap()) {
    *result = WrapLegacyParsedJsonMap(result->GetParsedJsonMap(), arena);
  } else if (result->IsCustomList() && result->GetCustomList().interface() ==
                                           CustomListValue().interface()) {
    *result = CustomListValue(common_internal::EmptyCompatListValue(), arena);
  } else if (result->IsCustomMap() && result->GetCustomMap().interface() ==
                                          CustomMapValue().interface()) {
    *result = CustomMapValue(common_internal::EmptyCompatMapValue(), arena);
  }
}

TypeValue CreateTypeValueFromView(google::protobuf::Arena* arena,
                                  absl::string_view input) {
  return TypeValue(common_internal::LegacyRuntimeType(input));
}

const google::protobuf::Message* absl_nullable GetLegacyMessage(const Value& value) {
  if (!common_internal::IsLegacyStructValue(value)) {
    return nullptr;
  }

  auto legacy = common_internal::GetLegacyStructValue(value);
  const auto* legacy_type_info = legacy.legacy_type_info();
  if (legacy_type_info == nullptr ||
      legacy_type_info == TrivialTypeInfo::GetInstance()) {
    return nullptr;
  }
  // This should not be possible using the normal public APIs, but possible
  // if someone used the MessageWrapper class directly.
  if (IsWellKnownMessageType(legacy.message_ptr()->GetDescriptor())) {
    return nullptr;
  }
  return legacy.message_ptr();
}

absl::Status WrapLegacyMessageField(
    const google::protobuf::Message* absl_nonnull message,
    const google::protobuf::FieldDescriptor* absl_nonnull field_descriptor,
    ProtoWrapperTypeOptions unboxing_option,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory, google::protobuf::Arena* arena,
    Value* absl_nonnull out) {
  ParsedMessageValue parsed_message = UnsafeParsedMessageValue(message);
  CEL_RETURN_IF_ERROR(parsed_message.GetField(field_descriptor, unboxing_option,
                                              descriptor_pool, message_factory,
                                              arena, out));
  WrapLegacyFieldAccessResult(arena, out);

  return absl::OkStatus();
}

}  // namespace interop_internal

}  // namespace cel
