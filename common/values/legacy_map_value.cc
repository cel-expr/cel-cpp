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

#include "common/values/legacy_map_value.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "common/legacy_value.h"
#include "common/memory.h"
#include "common/native_type.h"
#include "common/value.h"
#include "common/value_kind.h"
#include "common/values/legacy_list_value.h"
#include "common/values/legacy_struct_value.h"
#include "common/values/map_value_builder.h"
#include "common/values/values.h"
#include "eval/public/cel_value.h"
#include "eval/public/structs/proto_message_type_adapter.h"
#include "internal/casts.h"
#include "internal/status_macros.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"

namespace cel::common_internal {

namespace {

LegacyStructValue ParsedMessageToLegacyStructValue(
    const ParsedMessageValue& parsed_message) {
  return LegacyStructValue(
      cel::to_address(parsed_message),
      &google::api::expr::runtime::GetGenericProtoTypeInfoInstance());
}

bool MatchesMapKeyType(const google::protobuf::FieldDescriptor* absl_nonnull key_desc,
                       const Value& key) {
  switch (key_desc->cpp_type()) {
    case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
      return key.IsBool();
    case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
      if (key.IsInt()) {
        auto val = key.GetInt().NativeValue();
        return val >= std::numeric_limits<int32_t>::min() &&
               val <= std::numeric_limits<int32_t>::max();
      }
      return false;
    case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
      return key.IsInt();
    case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
      if (key.IsUint()) {
        auto val = key.GetUint().NativeValue();
        return val <= std::numeric_limits<uint32_t>::max();
      }
      return false;
    case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
      return key.IsUint();
    case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
      return key.IsString();
    default:
      return false;
  }
}

absl::Status InvalidMapKeyType(absl::string_view key_type) {
  return absl::InvalidArgumentError(
      absl::StrCat("Invalid map key type: '", key_type, "'"));
}

}  // namespace

class LegacyParsedMapFieldMapValue final
    : public CustomMapValueInterface,
      public google::api::expr::runtime::CelMap {
 public:
  // `arena` is expected to be the same arena as the one that the object is
  // allocated on.
  explicit LegacyParsedMapFieldMapValue(ParsedMapFieldValue value,
                                        google::protobuf::Arena* absl_nonnull arena)
      : value_(std::move(value)), arena_(arena) {
    ABSL_DCHECK(arena != nullptr);
    ABSL_DCHECK(value_.field() != nullptr);
  }

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
    return value_.Equal(other, descriptor_pool, message_factory, arena, result);
  }

  bool IsZeroValue() const override { return value_.IsZeroValue(); }

  bool IsEmpty() const override { return value_.IsEmpty(); }

  size_t Size() const override { return value_.Size(); }

  absl::StatusOr<bool> Find(
      const Value& key,
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena,
      Value* absl_nonnull result) const override {
    // Mimic the legacy behavior of complaining about unexpected key type.
    const auto* key_field = value_.field()->message_type()->map_key();
    if (!MatchesMapKeyType(key_field, key)) {
      return InvalidMapKeyType(cel::ValueKindToString(key.kind()));
    }

    CEL_ASSIGN_OR_RETURN(
        auto found,
        value_.Find(key, descriptor_pool, message_factory, arena, result));
    if (found) {
      interop_internal::WrapLegacyFieldAccessResult(arena, result);
    }
    return found;
  }

  absl::StatusOr<bool> Has(
      const Value& key,
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena) const override {
    const auto* key_field = value_.field()->message_type()->map_key();
    if (!MatchesMapKeyType(key_field, key)) {
      return InvalidMapKeyType(key_field->cpp_type_name());
    }
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

  CustomMapValue Clone(google::protobuf::Arena* absl_nonnull arena) const override {
    return CustomMapValue(google::protobuf::Arena::Create<LegacyParsedMapFieldMapValue>(
                              arena, value_.Clone(arena), arena),
                          arena);
  }

  // CelMap implementation
  int size() const override { return static_cast<int>(value_.Size()); }

  bool empty() const override { return value_.IsEmpty(); }

  std::optional<google::api::expr::runtime::CelValue> operator[](
      google::api::expr::runtime::CelValue key) const override {
    return Get(arena_, key);
  }

  std::optional<google::api::expr::runtime::CelValue> Get(
      google::protobuf::Arena* arena,
      google::api::expr::runtime::CelValue key) const override {
    if (arena == nullptr) {
      arena = arena_;
    }
    Value modern_key;
    if (ABSL_PREDICT_FALSE(!ModernValue(arena, key, modern_key).ok())) {
      // Legacy to modern should succeed for a valid CelValue.
      return std::nullopt;
    }
    Value modern_val;
    // Call custom map Find directly. MapValue normally handles wrapping
    // non-ok result to error value types, so emulate that here.
    //
    // Use the descriptor pool and message factory from the value. This is not
    // totally consistent with modern APIs, but this should behave the same as
    // the legacy map did.
    const google::protobuf::Message* msg = value_.message_;
    ABSL_DCHECK(msg->GetDescriptor() != nullptr);
    ABSL_DCHECK(msg->GetReflection() != nullptr);

    const google::protobuf::DescriptorPool* descriptor_pool =
        msg->GetDescriptor()->file()->pool();
    google::protobuf::MessageFactory* message_factory =
        msg->GetReflection()->GetMessageFactory();
    auto found =
        Find(modern_key, descriptor_pool, message_factory, arena, &modern_val);
    if (!found.ok()) {
      return google::api::expr::runtime::CreateErrorValue(arena,
                                                          found.status());
    }
    if (!(*found) && !modern_val.IsError()) {
      return std::nullopt;
    }
    return UnsafeLegacyValue(modern_val, /*stable=*/false, arena);
  }

  absl::StatusOr<bool> Has(
      const google::api::expr::runtime::CelValue& key) const override {
    CEL_RETURN_IF_ERROR(
        google::api::expr::runtime::CelValue::CheckMapKeyType(key));
    google::protobuf::Arena scratch_arena;
    Value modern_key;
    CEL_RETURN_IF_ERROR(ModernValue(&scratch_arena, key, modern_key));
    return Has(modern_key, google::protobuf::DescriptorPool::generated_pool(),
               google::protobuf::MessageFactory::generated_factory(), &scratch_arena);
  }

  absl::StatusOr<const google::api::expr::runtime::CelList*> ListKeys()
      const override {
    return ListKeys(arena_);
  }

  absl::StatusOr<const google::api::expr::runtime::CelList*> ListKeys(
      google::protobuf::Arena* arena) const override {
    if (arena == nullptr) {
      arena = arena_;
    }
    ListValue keys;
    CEL_RETURN_IF_ERROR(value_.ListKeys(
        google::protobuf::DescriptorPool::generated_pool(),
        google::protobuf::MessageFactory::generated_factory(), arena, &keys));
    auto legacy_list = AsLegacyListValue(keys);
    if (!legacy_list.has_value()) {
      return absl::InternalError("failed to convert list keys to legacy list");
    }
    return legacy_list->cel_list();
  }

 private:
  NativeTypeId GetNativeTypeId() const override {
    return NativeTypeId::For<LegacyParsedMapFieldMapValue>();
  }

  ParsedMapFieldValue value_;
  google::protobuf::Arena* const arena_;
};

class LegacyParsedJsonMapValue final
    : public CustomMapValueInterface,
      public google::api::expr::runtime::CelMap {
 public:
  // `arena` is expected to be the same arena as the one that the object is
  // allocated on.
  explicit LegacyParsedJsonMapValue(ParsedJsonMapValue value,
                                    google::protobuf::Arena* absl_nonnull arena)
      : value_(std::move(value)), arena_(arena) {
    ABSL_DCHECK(arena != nullptr);
  }

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
    return value_.Equal(other, descriptor_pool, message_factory, arena, result);
  }

  bool IsZeroValue() const override { return value_.IsZeroValue(); }

  bool IsEmpty() const override { return value_.IsEmpty(); }

  size_t Size() const override { return value_.Size(); }

  absl::StatusOr<bool> Find(
      const Value& key,
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena,
      Value* absl_nonnull result) const override {
    if (!key.IsString()) {
      return InvalidMapKeyType(cel::ValueKindToString(key.kind()));
    }
    CEL_ASSIGN_OR_RETURN(
        auto found,
        value_.Find(key, descriptor_pool, message_factory, arena, result));
    if (found && result->IsParsedMessage()) {
      *result = ParsedMessageToLegacyStructValue(result->GetParsedMessage());
    }
    return found;
  }

  absl::StatusOr<bool> Has(
      const Value& key,
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena) const override {
    if (!key.IsString()) {
      return InvalidMapKeyType(cel::ValueKindToString(key.kind()));
    }
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

  CustomMapValue Clone(google::protobuf::Arena* absl_nonnull arena) const override {
    return CustomMapValue(google::protobuf::Arena::Create<LegacyParsedJsonMapValue>(
                              arena, value_.Clone(arena), arena),
                          arena);
  }

  // CelMap implementation
  int size() const override { return static_cast<int>(value_.Size()); }

  bool empty() const override { return value_.IsEmpty(); }

  std::optional<google::api::expr::runtime::CelValue> operator[](
      google::api::expr::runtime::CelValue key) const override {
    return Get(arena_, key);
  }

  std::optional<google::api::expr::runtime::CelValue> Get(
      google::protobuf::Arena* arena,
      google::api::expr::runtime::CelValue key) const override {
    if (arena == nullptr) {
      arena = arena_;
    }
    Value modern_key;
    if (ABSL_PREDICT_FALSE(!ModernValue(arena, key, modern_key).ok())) {
      // Legacy to modern should succeed for a valid CelValue.
      return std::nullopt;
    }
    Value modern_val;
    // Call custom map Find directly. MapValue normally handles wrapping
    // non-ok result to error value types, so emulate that here.
    //
    // We know that the descriptor pool and message factory aren't needed here,
    // so fine to use generated.
    auto found =
        Find(modern_key, google::protobuf::DescriptorPool::generated_pool(),
             google::protobuf::MessageFactory::generated_factory(), arena, &modern_val);
    if (!found.ok()) {
      return google::api::expr::runtime::CreateErrorValue(arena,
                                                          found.status());
    }
    if (!(*found) && !modern_val.IsError()) {
      return std::nullopt;
    }
    return UnsafeLegacyValue(modern_val, /*stable=*/false, arena);
  }

  absl::StatusOr<bool> Has(
      const google::api::expr::runtime::CelValue& key) const override {
    CEL_RETURN_IF_ERROR(
        google::api::expr::runtime::CelValue::CheckMapKeyType(key));
    google::protobuf::Arena scratch_arena;
    Value modern_key;
    CEL_RETURN_IF_ERROR(ModernValue(&scratch_arena, key, modern_key));
    return Has(modern_key, google::protobuf::DescriptorPool::generated_pool(),
               google::protobuf::MessageFactory::generated_factory(), &scratch_arena);
  }

  absl::StatusOr<const google::api::expr::runtime::CelList*> ListKeys()
      const override {
    return ListKeys(arena_);
  }

  absl::StatusOr<const google::api::expr::runtime::CelList*> ListKeys(
      google::protobuf::Arena* arena) const override {
    if (arena == nullptr) {
      arena = arena_;
    }
    ListValue keys;
    CEL_RETURN_IF_ERROR(value_.ListKeys(
        google::protobuf::DescriptorPool::generated_pool(),
        google::protobuf::MessageFactory::generated_factory(), arena, &keys));
    auto legacy_list = AsLegacyListValue(keys);
    if (!legacy_list.has_value()) {
      return absl::InternalError("failed to convert list keys to legacy list");
    }
    return legacy_list->cel_list();
  }

 private:
  NativeTypeId GetNativeTypeId() const override {
    return NativeTypeId::For<LegacyParsedJsonMapValue>();
  }

  ParsedJsonMapValue value_;
  google::protobuf::Arena* const arena_;
};

CustomMapValue WrapLegacyParsedMapField(ParsedMapFieldValue value,
                                        google::protobuf::Arena* absl_nonnull arena) {
  return CustomMapValue(google::protobuf::Arena::Create<LegacyParsedMapFieldMapValue>(
                            arena, std::move(value), arena),
                        arena);
}

CustomMapValue WrapLegacyParsedJsonMap(ParsedJsonMapValue value,
                                       google::protobuf::Arena* absl_nonnull arena) {
  return CustomMapValue(google::protobuf::Arena::Create<LegacyParsedJsonMapValue>(
                            arena, std::move(value), arena),
                        arena);
}

absl::Status LegacyMapValue::Equal(
    const Value& other,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena, Value* absl_nonnull result) const {
  if (auto map_value = other.AsMap(); map_value.has_value()) {
    return MapValueEqual(*this, *map_value, descriptor_pool, message_factory,
                         arena, result);
  }
  *result = FalseValue();
  return absl::OkStatus();
}

bool IsLegacyMapValue(const Value& value) {
  return value.variant_.Is<LegacyMapValue>();
}

LegacyMapValue GetLegacyMapValue(const Value& value) {
  ABSL_DCHECK(IsLegacyMapValue(value));
  return value.variant_.Get<LegacyMapValue>();
}

std::optional<LegacyMapValue> AsLegacyMapValue(const Value& value) {
  if (IsLegacyMapValue(value)) {
    return GetLegacyMapValue(value);
  }
  if (auto custom_map_value = value.AsCustomMap(); custom_map_value) {
    NativeTypeId native_type_id = NativeTypeId::Of(*custom_map_value);
    if (native_type_id == NativeTypeId::For<CompatMapValue>()) {
      return LegacyMapValue(
          static_cast<const google::api::expr::runtime::CelMap*>(
              cel::internal::down_cast<const CompatMapValue*>(
                  custom_map_value->interface())));
    } else if (native_type_id == NativeTypeId::For<MutableCompatMapValue>()) {
      return LegacyMapValue(
          static_cast<const google::api::expr::runtime::CelMap*>(
              cel::internal::down_cast<const MutableCompatMapValue*>(
                  custom_map_value->interface())));
    } else if (native_type_id ==
               NativeTypeId::For<LegacyParsedMapFieldMapValue>()) {
      return LegacyMapValue(
          static_cast<const google::api::expr::runtime::CelMap*>(
              cel::internal::down_cast<const LegacyParsedMapFieldMapValue*>(
                  custom_map_value->interface())));
    } else if (native_type_id ==
               NativeTypeId::For<LegacyParsedJsonMapValue>()) {
      return LegacyMapValue(
          static_cast<const google::api::expr::runtime::CelMap*>(
              cel::internal::down_cast<const LegacyParsedJsonMapValue*>(
                  custom_map_value->interface())));
    }
  }
  return std::nullopt;
}

}  // namespace cel::common_internal
