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

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/call_once.h"
#include "absl/base/casts.h"
#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/container/flat_hash_map.h"
#include "absl/hash/hash.h"
#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "common/allocator.h"
#include "common/native_type.h"
#include "common/type.h"
#include "common/value.h"
#include "common/value_kind.h"
#include "common/values/list_value_builder.h"
#include "common/values/map_value_builder.h"
#include "internal/casts.h"
#include "internal/manual.h"
#include "internal/status_macros.h"
#include "internal/well_known_types.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"

namespace cel {

namespace common_internal {

namespace {

using ::cel::well_known_types::ListValueReflection;
using ::cel::well_known_types::StructReflection;
using ::cel::well_known_types::ValueReflection;

using ValueVector = std::vector<Value, ArenaAllocator<Value>>;

absl::Status CheckListElement(const Value& value) {
  if (auto error_value = value.AsError(); ABSL_PREDICT_FALSE(error_value)) {
    return error_value->ToStatus();
  }
  if (auto unknown_value = value.AsUnknown();
      ABSL_PREDICT_FALSE(unknown_value)) {
    return absl::InvalidArgumentError("cannot add unknown value to list");
  }
  return absl::OkStatus();
}

template <typename Vector>
absl::Status ListValueToJsonArray(
    const Vector& vector,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Message* absl_nonnull json) {
  ABSL_DCHECK(descriptor_pool != nullptr);
  ABSL_DCHECK(message_factory != nullptr);
  ABSL_DCHECK(json != nullptr);
  ABSL_DCHECK_EQ(json->GetDescriptor()->well_known_type(),
                 google::protobuf::Descriptor::WELLKNOWNTYPE_LISTVALUE);

  ListValueReflection reflection;
  CEL_RETURN_IF_ERROR(reflection.Initialize(json->GetDescriptor()));

  json->Clear();

  if (vector.empty()) {
    return absl::OkStatus();
  }

  for (const auto& element : vector) {
    CEL_RETURN_IF_ERROR(element->ConvertToJson(descriptor_pool, message_factory,
                                               reflection.AddValues(json)));
  }
  return absl::OkStatus();
}

template <typename Vector>
absl::Status ListValueToJson(
    const Vector& vector,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Message* absl_nonnull json) {
  ABSL_DCHECK(descriptor_pool != nullptr);
  ABSL_DCHECK(message_factory != nullptr);
  ABSL_DCHECK(json != nullptr);
  ABSL_DCHECK_EQ(json->GetDescriptor()->well_known_type(),
                 google::protobuf::Descriptor::WELLKNOWNTYPE_VALUE);

  ValueReflection reflection;
  CEL_RETURN_IF_ERROR(reflection.Initialize(json->GetDescriptor()));
  return ListValueToJsonArray(vector, descriptor_pool, message_factory,
                              reflection.MutableListValue(json));
}

class ListValueImplIterator final : public ValueIterator {
 public:
  explicit ListValueImplIterator(absl::Span<const Value> elements)
      : elements_(elements) {}

  bool HasNext() override { return index_ < elements_.size(); }

  absl::Status Next(const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
                    google::protobuf::MessageFactory* absl_nonnull message_factory,
                    google::protobuf::Arena* absl_nonnull arena,
                    Value* absl_nonnull result) override {
    if (ABSL_PREDICT_FALSE(index_ >= elements_.size())) {
      return absl::FailedPreconditionError(
          "ValueManager::Next called after ValueManager::HasNext returned "
          "false");
    }
    *result = elements_[index_++];
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

    if (index_ >= elements_.size()) {
      return false;
    }
    *key_or_value = elements_[index_];
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

    if (index_ >= elements_.size()) {
      return false;
    }
    if (value != nullptr) {
      *value = elements_[index_];
    }
    *key = IntValue(index_++);
    return true;
  }

 private:
  const absl::Span<const Value> elements_;
  size_t index_ = 0;
};

struct ValueFormatter {
  void operator()(std::string* out,
                  const std::pair<const Value, Value>& value) const {
    (*this)(out, value.first);
    out->append(": ");
    (*this)(out, value.second);
  }

  void operator()(std::string* out, const Value& value) const {
    out->append(value.DebugString());
  }
};

class ListValueImpl;

class ListValueBuilderImpl final : public ListValueBuilder {
 public:
  explicit ListValueBuilderImpl(google::protobuf::Arena* absl_nonnull arena)
      : arena_(arena) {
    elements_.Construct(arena);
  }

  absl::Status Add(Value value) override {
    CEL_RETURN_IF_ERROR(CheckListElement(value));
    UnsafeAdd(std::move(value));
    return absl::OkStatus();
  }

  void UnsafeAdd(Value value) override {
    ABSL_DCHECK_OK(CheckListElement(value));
    elements_->emplace_back(std::move(value));
  }

  size_t Size() const override { return elements_->size(); }

  void Reserve(size_t capacity) override { elements_->reserve(capacity); }

  ListValue Build() && override;

  CustomListValue BuildCustom() &&;

  const ListValueImpl* absl_nonnull BuildImpl() &&;

  const ListValueImpl* absl_nonnull BuildAt(void* absl_nonnull address) &&;

 private:
  google::protobuf::Arena* absl_nonnull const arena_;
  internal::Manual<ValueVector> elements_;
};

class ListValueImpl final : public CustomListValueInterface {
 public:
  explicit ListValueImpl(ValueVector&& elements)
      : elements_(std::move(elements)) {}

  std::string DebugString() const override {
    return absl::StrCat("[", absl::StrJoin(elements_, ", ", ValueFormatter{}),
                        "]");
  }

  absl::Status ConvertToJsonArray(
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Message* absl_nonnull json) const override {
    return ListValueToJsonArray(elements_, descriptor_pool, message_factory,
                                json);
  }

  CustomListValue Clone(google::protobuf::Arena* absl_nonnull arena) const override {
    ABSL_DCHECK(arena != nullptr);

    ListValueBuilderImpl builder(arena);
    builder.Reserve(elements_.size());
    for (const auto& element : elements_) {
      builder.UnsafeAdd(element.Clone(arena));
    }
    return std::move(builder).BuildCustom();
  }

  size_t Size() const override { return elements_.size(); }

  absl::Status ForEach(
      ForEachWithIndexCallback callback,
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena) const override {
    const size_t size = elements_.size();
    for (size_t i = 0; i < size; ++i) {
      CEL_ASSIGN_OR_RETURN(auto ok, callback(i, elements_[i]));
      if (!ok) {
        break;
      }
    }
    return absl::OkStatus();
  }

  absl::StatusOr<absl_nonnull ValueIteratorPtr> NewIterator() const override {
    return std::make_unique<ListValueImplIterator>(
        absl::MakeConstSpan(elements_));
  }

 protected:
  absl::Status Get(size_t index,
                   const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
                   google::protobuf::MessageFactory* absl_nonnull message_factory,
                   google::protobuf::Arena* absl_nonnull arena,
                   Value* absl_nonnull result) const override {
    if (index >= elements_.size()) {
      *result = IndexOutOfBoundsError(index, arena);
    } else {
      *result = elements_[index];
    }
    return absl::OkStatus();
  }

 private:
  NativeTypeId GetNativeTypeId() const override {
    return NativeTypeId::For<ListValueImpl>();
  }

  const ValueVector elements_;
};

ListValue ListValueBuilderImpl::Build() && {
  if (elements_->empty()) {
    return ListValue();
  }
  return std::move(*this).BuildCustom();
}

CustomListValue ListValueBuilderImpl::BuildCustom() && {
  if (elements_->empty()) {
    return CustomListValue();
  }
  return CustomListValue(std::move(*this).BuildImpl(), arena_);
}

const ListValueImpl* absl_nonnull ListValueBuilderImpl::BuildImpl() && {
  return std::move(*this).BuildAt(
      arena_->AllocateAligned(sizeof(ListValueImpl), alignof(ListValueImpl)));
}

const ListValueImpl* absl_nonnull ListValueBuilderImpl::BuildAt(
    void* absl_nonnull address) && {
  ListValueImpl* absl_nonnull impl =
      ::new (address) ListValueImpl(std::move(*elements_));
  return impl;
}

class MutableListValueImpl final : public MutableListValue {
 public:
  explicit MutableListValueImpl(google::protobuf::Arena* absl_nonnull arena)
      : elements_(arena) {}

  std::string DebugString() const override {
    return absl::StrCat("[", absl::StrJoin(elements_, ", ", ValueFormatter{}),
                        "]");
  }

  absl::Status ConvertToJsonArray(
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Message* absl_nonnull json) const override {
    return ListValueToJsonArray(elements_, descriptor_pool, message_factory,
                                json);
  }

  CustomListValue Clone(google::protobuf::Arena* absl_nonnull arena) const override {
    ABSL_DCHECK(arena != nullptr);

    ListValueBuilderImpl builder(arena);
    builder.Reserve(elements_.size());
    for (const auto& element : elements_) {
      builder.UnsafeAdd(element.Clone(arena));
    }
    return std::move(builder).BuildCustom();
  }

  size_t Size() const override { return elements_.size(); }

  absl::Status ForEach(
      ForEachWithIndexCallback callback,
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena) const override {
    const size_t size = elements_.size();
    for (size_t i = 0; i < size; ++i) {
      CEL_ASSIGN_OR_RETURN(auto ok, callback(i, elements_[i]));
      if (!ok) {
        break;
      }
    }
    return absl::OkStatus();
  }

  absl::StatusOr<absl_nonnull ValueIteratorPtr> NewIterator() const override {
    return std::make_unique<ListValueImplIterator>(
        absl::MakeConstSpan(elements_));
  }

  absl::Status Append(Value value) const override {
    CEL_RETURN_IF_ERROR(CheckListElement(value));
    elements_.emplace_back(std::move(value));
    return absl::OkStatus();
  }

  void Reserve(size_t capacity) const override { elements_.reserve(capacity); }

 protected:
  absl::Status Get(size_t index,
                   const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
                   google::protobuf::MessageFactory* absl_nonnull message_factory,
                   google::protobuf::Arena* absl_nonnull arena,
                   Value* absl_nonnull result) const override {
    if (index >= elements_.size()) {
      *result = IndexOutOfBoundsError(index, arena);
    } else {
      *result = elements_[index];
    }
    return absl::OkStatus();
  }

 private:
  mutable ValueVector elements_;
};

}  // namespace

MutableListValue* absl_nonnull NewMutableListValue(
    google::protobuf::Arena* absl_nonnull arena) {
  return ::new (arena->AllocateAligned(sizeof(MutableListValueImpl),
                                       alignof(MutableListValueImpl)))
      MutableListValueImpl(arena);
}

bool IsMutableListValue(const Value& value) {
  if (auto custom_list_value = value.AsCustomList(); custom_list_value) {
    return custom_list_value->GetTypeId() ==
           NativeTypeId::For<MutableListValue>();
  }
  return false;
}

bool IsMutableListValue(const ListValue& value) {
  if (auto custom_list_value = value.AsCustom(); custom_list_value) {
    return custom_list_value->GetTypeId() ==
           NativeTypeId::For<MutableListValue>();
  }
  return false;
}

const MutableListValue* absl_nullable AsMutableListValue(const Value& value) {
  if (auto custom_list_value = value.AsCustomList(); custom_list_value) {
    if (custom_list_value->GetTypeId() ==
        NativeTypeId::For<MutableListValue>()) {
      return cel::internal::down_cast<const MutableListValue*>(
          custom_list_value->interface());
    }
  }
  return nullptr;
}

const MutableListValue* absl_nullable AsMutableListValue(
    const ListValue& value) {
  if (auto custom_list_value = value.AsCustom(); custom_list_value) {
    if (custom_list_value->GetTypeId() ==
        NativeTypeId::For<MutableListValue>()) {
      return cel::internal::down_cast<const MutableListValue*>(
          custom_list_value->interface());
    }
  }
  return nullptr;
}

const MutableListValue& GetMutableListValue(const Value& value) {
  ABSL_DCHECK(IsMutableListValue(value)) << value;
  return cel::internal::down_cast<const MutableListValue&>(
      *value.GetCustomList().interface());
}

const MutableListValue& GetMutableListValue(const ListValue& value) {
  ABSL_DCHECK(IsMutableListValue(value)) << value;
  return cel::internal::down_cast<const MutableListValue&>(
      *value.GetCustom().interface());
}

absl_nonnull cel::ListValueBuilderPtr NewListValueBuilder(
    google::protobuf::Arena* absl_nonnull arena) {
  return std::make_unique<ListValueBuilderImpl>(arena);
}

}  // namespace common_internal

}  // namespace cel

namespace cel {

namespace common_internal {

namespace {

absl::Status CheckMapValue(const Value& value) {
  if (auto error_value = value.AsError(); ABSL_PREDICT_FALSE(error_value)) {
    return error_value->ToStatus();
  }
  if (auto unknown_value = value.AsUnknown();
      ABSL_PREDICT_FALSE(unknown_value)) {
    return absl::InvalidArgumentError("cannot add unknown value to list");
  }
  return absl::OkStatus();
}

size_t ValueHash(const Value& value) {
  switch (value.kind()) {
    case ValueKind::kBool:
      return absl::HashOf(value.kind(), value.GetBool());
    case ValueKind::kInt:
      return absl::HashOf(ValueKind::kInt,
                          absl::implicit_cast<int64_t>(value.GetInt()));
    case ValueKind::kUint:
      return absl::HashOf(ValueKind::kUint,
                          absl::implicit_cast<uint64_t>(value.GetUint()));
    case ValueKind::kString:
      return absl::HashOf(value.kind(), value.GetString());
    default:
      ABSL_UNREACHABLE();
  }
}

bool ValueEquals(const Value& lhs, const Value& rhs) {
  switch (lhs.kind()) {
    case ValueKind::kBool:
      switch (rhs.kind()) {
        case ValueKind::kBool:
          return lhs.GetBool() == rhs.GetBool();
        case ValueKind::kInt:
          return false;
        case ValueKind::kUint:
          return false;
        case ValueKind::kString:
          return false;
        default:
          ABSL_UNREACHABLE();
      }
    case ValueKind::kInt:
      switch (rhs.kind()) {
        case ValueKind::kBool:
          return false;
        case ValueKind::kInt:
          return lhs.GetInt() == rhs.GetInt();
        case ValueKind::kUint:
          return false;
        case ValueKind::kString:
          return false;
        default:
          ABSL_UNREACHABLE();
      }
    case ValueKind::kUint:
      switch (rhs.kind()) {
        case ValueKind::kBool:
          return false;
        case ValueKind::kInt:
          return false;
        case ValueKind::kUint:
          return lhs.GetUint() == rhs.GetUint();
        case ValueKind::kString:
          return false;
        default:
          ABSL_UNREACHABLE();
      }
    case ValueKind::kString:
      switch (rhs.kind()) {
        case ValueKind::kBool:
          return false;
        case ValueKind::kInt:
          return false;
        case ValueKind::kUint:
          return false;
        case ValueKind::kString:
          return lhs.GetString() == rhs.GetString();
        default:
          ABSL_UNREACHABLE();
      }
    default:
      ABSL_UNREACHABLE();
  }
}

absl::StatusOr<std::string> ValueToJsonString(const Value& value) {
  switch (value.kind()) {
    case ValueKind::kString:
      return value.GetString().NativeString();
    default:
      return common_internal::MakeTypeConversionError(value.GetRuntimeType(),
                                                      StringType());
  }
}

template <typename Map>
absl::Status MapValueToJsonObject(
    const Map& map, const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Message* absl_nonnull json) {
  ABSL_DCHECK(descriptor_pool != nullptr);
  ABSL_DCHECK(message_factory != nullptr);
  ABSL_DCHECK(json != nullptr);
  ABSL_DCHECK_EQ(json->GetDescriptor()->well_known_type(),
                 google::protobuf::Descriptor::WELLKNOWNTYPE_STRUCT);

  StructReflection reflection;
  CEL_RETURN_IF_ERROR(reflection.Initialize(json->GetDescriptor()));

  json->Clear();

  if (map.empty()) {
    return absl::OkStatus();
  }

  for (const auto& entry : map) {
    CEL_ASSIGN_OR_RETURN(auto key, ValueToJsonString(entry.first));
    CEL_RETURN_IF_ERROR(entry.second.ConvertToJson(
        descriptor_pool, message_factory, reflection.InsertField(json, key)));
  }
  return absl::OkStatus();
}

template <typename Map>
absl::Status MapValueToJson(
    const Map& map, const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Message* absl_nonnull json) {
  ABSL_DCHECK(descriptor_pool != nullptr);
  ABSL_DCHECK(message_factory != nullptr);
  ABSL_DCHECK(json != nullptr);
  ABSL_DCHECK_EQ(json->GetDescriptor()->well_known_type(),
                 google::protobuf::Descriptor::WELLKNOWNTYPE_VALUE);

  ValueReflection reflection;
  CEL_RETURN_IF_ERROR(reflection.Initialize(json->GetDescriptor()));
  return MapValueToJsonObject(map, descriptor_pool, message_factory,
                              reflection.MutableStructValue(json));
}

struct ValueHasher {
  size_t operator()(const Value& value) const { return (ValueHash)(value); }
};

struct ValueEqualer {
  bool operator()(const Value& lhs, const Value& rhs) const {
    return (ValueEquals)(lhs, rhs);
  }
};

using ValueFlatHashMapAllocator = ArenaAllocator<std::pair<const Value, Value>>;

using ValueFlatHashMap =
    absl::flat_hash_map<Value, Value, ValueHasher, ValueEqualer,
                        ValueFlatHashMapAllocator>;

class MapValueImplIterator final : public ValueIterator {
 public:
  explicit MapValueImplIterator(const ValueFlatHashMap* absl_nonnull map)
      : begin_(map->begin()), end_(map->end()) {}

  bool HasNext() override { return begin_ != end_; }

  absl::Status Next(const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
                    google::protobuf::MessageFactory* absl_nonnull message_factory,
                    google::protobuf::Arena* absl_nonnull arena,
                    Value* absl_nonnull result) override {
    if (ABSL_PREDICT_FALSE(begin_ == end_)) {
      return absl::FailedPreconditionError(
          "ValueManager::Next called after ValueManager::HasNext returned "
          "false");
    }
    *result = begin_->first;
    ++begin_;
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

    if (begin_ == end_) {
      return false;
    }
    *key_or_value = begin_->first;
    ++begin_;
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

    if (begin_ == end_) {
      return false;
    }
    *key = begin_->first;
    if (value != nullptr) {
      *value = begin_->second;
    }
    ++begin_;
    return true;
  }

 private:
  typename ValueFlatHashMap::const_iterator begin_;
  const typename ValueFlatHashMap::const_iterator end_;
};

class MapValueImpl;

class MapValueBuilderImpl final : public MapValueBuilder {
 public:
  explicit MapValueBuilderImpl(google::protobuf::Arena* absl_nonnull arena)
      : arena_(arena) {
    map_.Construct(arena_);
  }

  absl::Status Put(Value key, Value value) override {
    CEL_RETURN_IF_ERROR(CheckMapKey(key));
    CEL_RETURN_IF_ERROR(CheckMapValue(value));
    if (auto it = map_->find(key); ABSL_PREDICT_FALSE(it != map_->end())) {
      return DuplicateKeyError().ToStatus();
    }
    UnsafePut(std::move(key), std::move(value));
    return absl::OkStatus();
  }

  void UnsafePut(Value key, Value value) override {
    auto insertion = map_->insert({std::move(key), std::move(value)});
    ABSL_DCHECK(insertion.second);
  }

  size_t Size() const override { return map_->size(); }

  void Reserve(size_t capacity) override { map_->reserve(capacity); }

  MapValue Build() && override;

  CustomMapValue BuildCustom() &&;

  const MapValueImpl* absl_nonnull BuildImpl() &&;

 private:
  google::protobuf::Arena* absl_nonnull const arena_;
  internal::Manual<ValueFlatHashMap> map_;
};

class MapValueImpl final : public CustomMapValueInterface {
 public:
  explicit MapValueImpl(ValueFlatHashMap&& map) : map_(std::move(map)) {}

  std::string DebugString() const override {
    return absl::StrCat("{", absl::StrJoin(map_, ", ", ValueFormatter{}), "}");
  }

  absl::Status ConvertToJsonObject(
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Message* absl_nonnull json) const override {
    return MapValueToJsonObject(map_, descriptor_pool, message_factory, json);
  }

  CustomMapValue Clone(google::protobuf::Arena* absl_nonnull arena) const override {
    ABSL_DCHECK(arena != nullptr);

    MapValueBuilderImpl builder(arena);
    builder.Reserve(map_.size());
    for (const auto& entry : map_) {
      builder.UnsafePut(entry.first.Clone(arena), entry.second.Clone(arena));
    }
    return std::move(builder).BuildCustom();
  }

  size_t Size() const override { return map_.size(); }

  absl::Status ListKeys(
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena,
      ListValue* absl_nonnull result) const override {
    *result = CustomListValue(ProjectKeys(), map_.get_allocator().arena());
    return absl::OkStatus();
  }

  absl::Status ForEach(
      ForEachCallback callback,
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena) const override {
    for (const auto& entry : map_) {
      CEL_ASSIGN_OR_RETURN(auto ok, callback(entry.first, entry.second));
      if (!ok) {
        break;
      }
    }
    return absl::OkStatus();
  }

  absl::StatusOr<absl_nonnull ValueIteratorPtr> NewIterator() const override {
    return std::make_unique<MapValueImplIterator>(&map_);
  }

 protected:
  absl::StatusOr<bool> Find(
      const Value& key,
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena,
      Value* absl_nonnull result) const override {
    CEL_RETURN_IF_ERROR(CheckMapKey(key));
    if (auto it = map_.find(key); it != map_.end()) {
      *result = it->second;
      return true;
    }
    return false;
  }

  absl::StatusOr<bool> Has(
      const Value& key,
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena) const override {
    CEL_RETURN_IF_ERROR(CheckMapKey(key));
    return map_.find(key) != map_.end();
  }

 private:
  NativeTypeId GetNativeTypeId() const override {
    return NativeTypeId::For<MapValueImpl>();
  }

  const CustomListValueInterface* absl_nonnull ProjectKeys() const {
    absl::call_once(keys_once_, [this]() {
      ListValueBuilderImpl builder(map_.get_allocator().arena());
      builder.Reserve(map_.size());

      for (const auto& entry : map_) {
        builder.UnsafeAdd(entry.first);
      }

      std::move(builder).BuildAt(&keys_[0]);
    });
    return std::launder(reinterpret_cast<const ListValueImpl*>(&keys_[0]));
  }

  const ValueFlatHashMap map_;
  mutable absl::once_flag keys_once_;
  alignas(ListValueImpl) mutable char keys_[sizeof(ListValueImpl)];
};

MapValue MapValueBuilderImpl::Build() && {
  if (map_->empty()) {
    return MapValue();
  }
  return std::move(*this).BuildCustom();
}

CustomMapValue MapValueBuilderImpl::BuildCustom() && {
  if (map_->empty()) {
    return CustomMapValue();
  }
  return CustomMapValue(std::move(*this).BuildImpl(), arena_);
}

const MapValueImpl* absl_nonnull MapValueBuilderImpl::BuildImpl() && {
  MapValueImpl* absl_nonnull impl = ::new (
      arena_->AllocateAligned(sizeof(MapValueImpl), alignof(MapValueImpl)))
      MapValueImpl(std::move(*map_));
  return impl;
}

class MutableMapValueImpl final : public MutableMapValue {
 public:
  explicit MutableMapValueImpl(google::protobuf::Arena* absl_nonnull arena)
      : map_(arena) {}

  std::string DebugString() const override {
    return absl::StrCat("{", absl::StrJoin(map_, ", ", ValueFormatter{}), "}");
  }

  absl::Status ConvertToJsonObject(
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Message* absl_nonnull json) const override {
    return MapValueToJsonObject(map_, descriptor_pool, message_factory, json);
  }

  CustomMapValue Clone(google::protobuf::Arena* absl_nonnull arena) const override {
    ABSL_DCHECK(arena != nullptr);

    MapValueBuilderImpl builder(arena);
    builder.Reserve(map_.size());
    for (const auto& entry : map_) {
      builder.UnsafePut(entry.first.Clone(arena), entry.second.Clone(arena));
    }
    return std::move(builder).BuildCustom();
  }

  size_t Size() const override { return map_.size(); }

  absl::Status ListKeys(
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena,
      ListValue* absl_nonnull result) const override {
    *result = CustomListValue(ProjectKeys(), map_.get_allocator().arena());
    return absl::OkStatus();
  }

  absl::Status ForEach(
      ForEachCallback callback,
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena) const override {
    for (const auto& entry : map_) {
      CEL_ASSIGN_OR_RETURN(auto ok, callback(entry.first, entry.second));
      if (!ok) {
        break;
      }
    }
    return absl::OkStatus();
  }

  absl::StatusOr<absl_nonnull ValueIteratorPtr> NewIterator() const override {
    return std::make_unique<MapValueImplIterator>(&map_);
  }

  absl::Status Put(Value key, Value value) const override {
    CEL_RETURN_IF_ERROR(CheckMapKey(key));
    CEL_RETURN_IF_ERROR(CheckMapValue(value));
    if (auto it = map_.find(key); ABSL_PREDICT_FALSE(it != map_.end())) {
      return DuplicateKeyError().ToStatus();
    }
    auto insertion = map_.insert({std::move(key), std::move(value)});
    ABSL_DCHECK(insertion.second);
    return absl::OkStatus();
  }

  void Reserve(size_t capacity) const override { map_.reserve(capacity); }

 protected:
  absl::StatusOr<bool> Find(
      const Value& key,
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena,
      Value* absl_nonnull result) const override {
    CEL_RETURN_IF_ERROR(CheckMapKey(key));
    if (auto it = map_.find(key); it != map_.end()) {
      *result = it->second;
      return true;
    }
    return false;
  }

  absl::StatusOr<bool> Has(
      const Value& key,
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena) const override {
    CEL_RETURN_IF_ERROR(CheckMapKey(key));
    return map_.find(key) != map_.end();
  }

 private:
  const CustomListValueInterface* absl_nonnull ProjectKeys() const {
    absl::call_once(keys_once_, [this]() {
      ListValueBuilderImpl builder(map_.get_allocator().arena());
      builder.Reserve(map_.size());

      for (const auto& entry : map_) {
        builder.UnsafeAdd(entry.first);
      }

      std::move(builder).BuildAt(&keys_[0]);
    });
    return std::launder(reinterpret_cast<const ListValueImpl*>(&keys_[0]));
  }

  mutable ValueFlatHashMap map_;
  mutable absl::once_flag keys_once_;
  alignas(ListValueImpl) mutable char keys_[sizeof(ListValueImpl)];
};

}  // namespace

MutableMapValue* absl_nonnull NewMutableMapValue(
    google::protobuf::Arena* absl_nonnull arena) {
  return ::new (arena->AllocateAligned(sizeof(MutableMapValueImpl),
                                       alignof(MutableMapValueImpl)))
      MutableMapValueImpl(arena);
}

bool IsMutableMapValue(const Value& value) {
  if (auto custom_map_value = value.AsCustomMap(); custom_map_value) {
    return custom_map_value->GetTypeId() ==
           NativeTypeId::For<MutableMapValue>();
  }
  return false;
}

bool IsMutableMapValue(const MapValue& value) {
  if (auto custom_map_value = value.AsCustom(); custom_map_value) {
    return custom_map_value->GetTypeId() ==
           NativeTypeId::For<MutableMapValue>();
  }
  return false;
}

const MutableMapValue* absl_nullable AsMutableMapValue(const Value& value) {
  if (auto custom_map_value = value.AsCustomMap(); custom_map_value) {
    if (custom_map_value->GetTypeId() == NativeTypeId::For<MutableMapValue>()) {
      return cel::internal::down_cast<const MutableMapValue*>(
          custom_map_value->interface());
    }
  }
  return nullptr;
}

const MutableMapValue* absl_nullable AsMutableMapValue(const MapValue& value) {
  if (auto custom_map_value = value.AsCustom(); custom_map_value) {
    if (custom_map_value->GetTypeId() == NativeTypeId::For<MutableMapValue>()) {
      return cel::internal::down_cast<const MutableMapValue*>(
          custom_map_value->interface());
    }
  }
  return nullptr;
}

const MutableMapValue& GetMutableMapValue(const Value& value) {
  ABSL_DCHECK(IsMutableMapValue(value)) << value;
  return cel::internal::down_cast<const MutableMapValue&>(
      *value.GetCustomMap().interface());
}

const MutableMapValue& GetMutableMapValue(const MapValue& value) {
  ABSL_DCHECK(IsMutableMapValue(value)) << value;
  return cel::internal::down_cast<const MutableMapValue&>(
      *value.GetCustom().interface());
}

absl_nonnull cel::MapValueBuilderPtr NewMapValueBuilder(
    google::protobuf::Arena* absl_nonnull arena) {
  return std::make_unique<MapValueBuilderImpl>(arena);
}

}  // namespace common_internal

}  // namespace cel
