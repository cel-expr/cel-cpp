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

#include <cstdint>
#include <string>

#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "base/attribute.h"
#include "common/type.h"
#include "common/value.h"
#include "runtime/runtime_options.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/io/zero_copy_stream.h"
#include "google/protobuf/message.h"

namespace cel::common_internal {

StructType LegacyStructValue::GetRuntimeType() const {
  if (ABSL_PREDICT_FALSE(message_ptr_ == nullptr)) {
    ABSL_DCHECK(message_ptr_ != nullptr);
    return StructType();
  }
  return MessageType(message_ptr_->GetDescriptor());
}

absl::string_view LegacyStructValue::GetTypeName() const {
  if (ABSL_PREDICT_FALSE(dispatcher_ == nullptr)) {
    ABSL_DCHECK(dispatcher_ != nullptr);
    return "";
  }
  return dispatcher_->get_type_name(message_ptr_, legacy_type_info_);
}

std::string LegacyStructValue::DebugString() const {
  if (ABSL_PREDICT_FALSE(dispatcher_ == nullptr)) {
    ABSL_DCHECK(dispatcher_ != nullptr);
    return "";
  }
  return dispatcher_->debug_string(message_ptr_, legacy_type_info_);
}

absl::Status LegacyStructValue::SerializeTo(
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::io::ZeroCopyOutputStream* absl_nonnull output) const {
  if (ABSL_PREDICT_FALSE(dispatcher_ == nullptr)) {
    ABSL_DCHECK(dispatcher_ != nullptr);
    return absl::UnimplementedError(
        "legacy struct value support is not linked");
  }
  return dispatcher_->serialize_to(message_ptr_, legacy_type_info_,
                                   descriptor_pool, message_factory, output);
}

absl::Status LegacyStructValue::ConvertToJson(
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Message* absl_nonnull json) const {
  if (ABSL_PREDICT_FALSE(dispatcher_ == nullptr)) {
    ABSL_DCHECK(dispatcher_ != nullptr);
    return absl::UnimplementedError(
        "legacy struct value support is not linked");
  }
  return dispatcher_->convert_to_json(message_ptr_, legacy_type_info_,
                                      descriptor_pool, message_factory, json);
}

absl::Status LegacyStructValue::ConvertToJsonObject(
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Message* absl_nonnull json) const {
  if (ABSL_PREDICT_FALSE(dispatcher_ == nullptr)) {
    ABSL_DCHECK(dispatcher_ != nullptr);
    return absl::UnimplementedError(
        "legacy struct value support is not linked");
  }
  return dispatcher_->convert_to_json_object(
      message_ptr_, legacy_type_info_, descriptor_pool, message_factory, json);
}

absl::Status LegacyStructValue::Equal(
    const Value& other,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena, Value* absl_nonnull result) const {
  if (ABSL_PREDICT_FALSE(dispatcher_ == nullptr)) {
    ABSL_DCHECK(dispatcher_ != nullptr);
    return absl::UnimplementedError(
        "legacy struct value support is not linked");
  }
  return dispatcher_->equal(message_ptr_, legacy_type_info_, other,
                            descriptor_pool, message_factory, arena, result);
}

bool LegacyStructValue::IsZeroValue() const {
  if (ABSL_PREDICT_FALSE(dispatcher_ == nullptr)) {
    ABSL_DCHECK(dispatcher_ != nullptr);
    return false;
  }
  return dispatcher_->is_zero_value(message_ptr_, legacy_type_info_);
}

absl::Status LegacyStructValue::GetFieldByName(
    absl::string_view name, ProtoWrapperTypeOptions unboxing_options,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena, Value* absl_nonnull result) const {
  if (ABSL_PREDICT_FALSE(dispatcher_ == nullptr)) {
    ABSL_DCHECK(dispatcher_ != nullptr);
    return absl::UnimplementedError(
        "legacy struct value support is not linked");
  }
  return dispatcher_->get_field_by_name(message_ptr_, legacy_type_info_, name,
                                        unboxing_options, descriptor_pool,
                                        message_factory, arena, result);
}

absl::Status LegacyStructValue::GetFieldByNumber(
    int64_t number, ProtoWrapperTypeOptions unboxing_options,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena, Value* absl_nonnull result) const {
  if (ABSL_PREDICT_FALSE(dispatcher_ == nullptr)) {
    ABSL_DCHECK(dispatcher_ != nullptr);
    return absl::UnimplementedError(
        "legacy struct value support is not linked");
  }
  return dispatcher_->get_field_by_number(
      message_ptr_, legacy_type_info_, number, unboxing_options,
      descriptor_pool, message_factory, arena, result);
}

absl::StatusOr<bool> LegacyStructValue::HasFieldByName(
    absl::string_view name) const {
  if (ABSL_PREDICT_FALSE(dispatcher_ == nullptr)) {
    ABSL_DCHECK(dispatcher_ != nullptr);
    return absl::UnimplementedError(
        "legacy struct value support is not linked");
  }
  return dispatcher_->has_field_by_name(message_ptr_, legacy_type_info_, name);
}

absl::StatusOr<bool> LegacyStructValue::HasFieldByNumber(int64_t number) const {
  if (ABSL_PREDICT_FALSE(dispatcher_ == nullptr)) {
    ABSL_DCHECK(dispatcher_ != nullptr);
    return absl::UnimplementedError(
        "legacy struct value support is not linked");
  }
  return dispatcher_->has_field_by_number(message_ptr_, legacy_type_info_,
                                          number);
}

absl::Status LegacyStructValue::ForEachField(
    ForEachFieldCallback callback,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena) const {
  if (ABSL_PREDICT_FALSE(dispatcher_ == nullptr)) {
    ABSL_DCHECK(dispatcher_ != nullptr);
    return absl::UnimplementedError(
        "legacy struct value support is not linked");
  }
  return dispatcher_->for_each_field(message_ptr_, legacy_type_info_, callback,
                                     descriptor_pool, message_factory, arena);
}

absl::Status LegacyStructValue::Qualify(
    absl::Span<const SelectQualifier> qualifiers, bool presence_test,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena, Value* absl_nonnull result,
    int* absl_nonnull count) const {
  if (ABSL_PREDICT_FALSE(dispatcher_ == nullptr)) {
    ABSL_DCHECK(dispatcher_ != nullptr);
    return absl::UnimplementedError(
        "legacy struct value support is not linked");
  }
  return dispatcher_->qualify(message_ptr_, legacy_type_info_, qualifiers,
                              presence_test, descriptor_pool, message_factory,
                              arena, result, count);
}

const google::protobuf::Message* absl_nullable LegacyStructValue::GetLegacyMessage()
    const {
  if (ABSL_PREDICT_FALSE(dispatcher_ == nullptr)) {
    ABSL_DCHECK(dispatcher_ != nullptr);
    return nullptr;
  }
  return dispatcher_->get_legacy_message(message_ptr_, legacy_type_info_);
}

absl::Status LegacyStructValue::WrapLegacyMessageField(
    const google::protobuf::FieldDescriptor* absl_nonnull field_descriptor,
    ProtoWrapperTypeOptions unboxing_option,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena, Value* absl_nonnull out) const {
  if (ABSL_PREDICT_FALSE(dispatcher_ == nullptr)) {
    ABSL_DCHECK(dispatcher_ != nullptr);
    return absl::UnimplementedError(
        "legacy struct value support is not linked");
  }
  return dispatcher_->wrap_legacy_message_field(
      message_ptr_, field_descriptor, unboxing_option, descriptor_pool,
      message_factory, arena, out);
}

bool IsLegacyStructValue(const Value& value) {
  return value.variant_.Is<LegacyStructValue>();
}

LegacyStructValue GetLegacyStructValue(const Value& value) {
  ABSL_DCHECK(IsLegacyStructValue(value));
  return value.variant_.Get<LegacyStructValue>();
}

absl::optional<LegacyStructValue> AsLegacyStructValue(const Value& value) {
  if (IsLegacyStructValue(value)) {
    return GetLegacyStructValue(value);
  }
  return std::nullopt;
}

const google::protobuf::Message* absl_nullable GetLegacyMessage(const Value& value) {
  if (auto legacy = AsLegacyStructValue(value); legacy.has_value()) {
    return legacy->GetLegacyMessage();
  }
  return nullptr;
}

}  // namespace cel::common_internal
