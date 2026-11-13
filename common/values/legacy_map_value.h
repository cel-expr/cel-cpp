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

// IWYU pragma: private, include "common/values/map_value.h"
// IWYU pragma: friend "common/values/map_value.h"

#ifndef THIRD_PARTY_CEL_CPP_COMMON_VALUES_LEGACY_MAP_VALUE_H_
#define THIRD_PARTY_CEL_CPP_COMMON_VALUES_LEGACY_MAP_VALUE_H_

#include <cstddef>
#include <ostream>
#include <string>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "common/value_kind.h"
#include "common/values/custom_map_value.h"
#include "common/values/parsed_json_map_value.h"
#include "common/values/parsed_map_field_value.h"
#include "common/values/values.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/io/zero_copy_stream.h"
#include "google/protobuf/message.h"

namespace google::api::expr::runtime {
class CelMap;
}

namespace cel {

class Value;

namespace common_internal {

class LegacyMapValue;

struct LegacyMapValueDispatcher {
  using DebugString = std::string (*)(
      const google::api::expr::runtime::CelMap* absl_nullability_unknown impl);

  using SerializeTo = absl::Status (*)(
      const google::api::expr::runtime::CelMap* absl_nullability_unknown impl,
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::io::ZeroCopyOutputStream* absl_nonnull output);

  using ConvertToJson = absl::Status (*)(
      const google::api::expr::runtime::CelMap* absl_nullability_unknown impl,
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Message* absl_nonnull json);

  using ConvertToJsonObject = absl::Status (*)(
      const google::api::expr::runtime::CelMap* absl_nullability_unknown impl,
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Message* absl_nonnull json);

  using IsEmpty = bool (*)(
      const google::api::expr::runtime::CelMap* absl_nullability_unknown impl);

  using Size = size_t (*)(
      const google::api::expr::runtime::CelMap* absl_nullability_unknown impl);

  using Get = absl::Status (*)(
      const google::api::expr::runtime::CelMap* absl_nullability_unknown impl,
      const Value& key,
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena, Value* absl_nonnull result);

  using Find = absl::StatusOr<bool> (*)(
      const google::api::expr::runtime::CelMap* absl_nullability_unknown impl,
      const Value& key,
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena, Value* absl_nonnull result);

  using Has = absl::Status (*)(
      const google::api::expr::runtime::CelMap* absl_nullability_unknown impl,
      const Value& key,
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena, Value* absl_nonnull result);

  using ListKeys = absl::Status (*)(
      const google::api::expr::runtime::CelMap* absl_nullability_unknown impl,
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena, ListValue* absl_nonnull result);

  using ForEach = absl::Status (*)(
      const google::api::expr::runtime::CelMap* absl_nullability_unknown impl,
      CustomMapValueInterface::ForEachCallback callback,
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena);

  using NewIterator = absl::StatusOr<absl_nonnull ValueIteratorPtr> (*)(
      const google::api::expr::runtime::CelMap* absl_nullability_unknown impl);

  absl_nonnull DebugString debug_string;
  absl_nonnull SerializeTo serialize_to;
  absl_nonnull ConvertToJson convert_to_json;
  absl_nonnull ConvertToJsonObject convert_to_json_object;
  absl_nonnull IsEmpty is_empty;
  absl_nonnull Size size;
  absl_nonnull Get get;
  absl_nonnull Find find;
  absl_nonnull Has has;
  absl_nonnull ListKeys list_keys;
  absl_nonnull ForEach for_each;
  absl_nonnull NewIterator new_iterator;
};

class LegacyMapValue final
    : private common_internal::MapValueMixin<LegacyMapValue> {
 public:
  static constexpr ValueKind kKind = ValueKind::kMap;

  explicit LegacyMapValue(
      const google::api::expr::runtime::CelMap* absl_nullability_unknown impl);

  // By default, this creates an empty map whose type is `map(dyn, dyn)`.
  // Unless you can help it, you should use a more specific typed map value.
  LegacyMapValue() = default;
  LegacyMapValue(const LegacyMapValue&) = default;
  LegacyMapValue(LegacyMapValue&&) = default;
  LegacyMapValue& operator=(const LegacyMapValue&) = default;
  LegacyMapValue& operator=(LegacyMapValue&&) = default;

  constexpr ValueKind kind() const { return kKind; }

  absl::string_view GetTypeName() const { return "map"; }

  std::string DebugString() const;

  // See Value::SerializeTo().
  absl::Status SerializeTo(
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::io::ZeroCopyOutputStream* absl_nonnull output) const;

  // See Value::ConvertToJson().
  absl::Status ConvertToJson(
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Message* absl_nonnull json) const;

  // See Value::ConvertToJsonObject().
  absl::Status ConvertToJsonObject(
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Message* absl_nonnull json) const;

  absl::Status Equal(const Value& other,
                     const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
                     google::protobuf::MessageFactory* absl_nonnull message_factory,
                     google::protobuf::Arena* absl_nonnull arena,
                     Value* absl_nonnull result) const;
  using MapValueMixin::Equal;

  bool IsZeroValue() const { return IsEmpty(); }

  bool IsEmpty() const;

  size_t Size() const;

  // See the corresponding member function of `MapValue` for
  // documentation.
  absl::Status Get(const Value& key,
                   const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
                   google::protobuf::MessageFactory* absl_nonnull message_factory,
                   google::protobuf::Arena* absl_nonnull arena,
                   Value* absl_nonnull result) const;
  using MapValueMixin::Get;

  // See the corresponding member function of `MapValue` for
  // documentation.
  absl::StatusOr<bool> Find(
      const Value& key,
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena, Value* absl_nonnull result) const;
  using MapValueMixin::Find;

  // See the corresponding member function of `MapValue` for
  // documentation.
  absl::Status Has(const Value& key,
                   const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
                   google::protobuf::MessageFactory* absl_nonnull message_factory,
                   google::protobuf::Arena* absl_nonnull arena,
                   Value* absl_nonnull result) const;
  using MapValueMixin::Has;

  // See the corresponding member function of `MapValue` for
  // documentation.
  absl::Status ListKeys(
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena, ListValue* absl_nonnull result) const;
  using MapValueMixin::ListKeys;

  // See the corresponding type declaration of `MapValue` for
  // documentation.
  using ForEachCallback = typename CustomMapValueInterface::ForEachCallback;

  // See the corresponding member function of `MapValue` for
  // documentation.
  absl::Status ForEach(
      ForEachCallback callback,
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena) const;

  absl::StatusOr<absl_nonnull ValueIteratorPtr> NewIterator() const;

  const google::api::expr::runtime::CelMap* absl_nonnull cel_map() const {
    return impl_;
  }

  friend void swap(LegacyMapValue& lhs, LegacyMapValue& rhs) noexcept {
    using std::swap;
    swap(lhs.dispatcher_, rhs.dispatcher_);
    swap(lhs.impl_, rhs.impl_);
  }

 private:
  friend class common_internal::ValueMixin<LegacyMapValue>;
  friend class common_internal::MapValueMixin<LegacyMapValue>;

  const LegacyMapValueDispatcher* absl_nullable dispatcher_ = nullptr;
  const google::api::expr::runtime::CelMap* absl_nullability_unknown impl_ =
      nullptr;
};

inline std::ostream& operator<<(std::ostream& out, const LegacyMapValue& type) {
  return out << type.DebugString();
}

bool IsLegacyMapValue(const Value& value);

LegacyMapValue GetLegacyMapValue(const Value& value);

absl::optional<LegacyMapValue> AsLegacyMapValue(const Value& value);

class LegacyParsedMapFieldMapValue;
class LegacyParsedJsonMapValue;

CustomMapValue WrapLegacyParsedMapField(ParsedMapFieldValue value,
                                        google::protobuf::Arena* absl_nonnull arena);

CustomMapValue WrapLegacyParsedJsonMap(ParsedJsonMapValue value,
                                       google::protobuf::Arena* absl_nonnull arena);

MapValue FromLegacyMap(
    const google::api::expr::runtime::CelMap* absl_nullable cel_map);

}  // namespace common_internal

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMMON_VALUES_LEGACY_MAP_VALUE_H_
