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

#include "eval/public/structs/cel_proto_wrap_value_to_message.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

#include "google/protobuf/any.pb.h"
#include "google/protobuf/duration.pb.h"
#include "google/protobuf/struct.pb.h"
#include "google/protobuf/timestamp.pb.h"
#include "google/protobuf/wrappers.pb.h"
#include "absl/log/absl_log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "eval/public/cel_value.h"
#include "internal/overflow.h"
#include "internal/proto_time_encoding.h"
#include "internal/status_macros.h"
#include "internal/time.h"
#include "internal/well_known_types.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/json/json.h"
#include "google/protobuf/message.h"

namespace google::api::expr::runtime::internal {

namespace {

using google::protobuf::BoolValue;
using google::protobuf::BytesValue;
using google::protobuf::DoubleValue;
using google::protobuf::Duration;
using google::protobuf::Int64Value;
using google::protobuf::ListValue;
using google::protobuf::StringValue;
using google::protobuf::Struct;
using google::protobuf::Timestamp;
using google::protobuf::UInt64Value;
using google::protobuf::Value;
using google::protobuf::Arena;
using google::protobuf::Descriptor;
using google::protobuf::Message;
using google::protobuf::MessageFactory;
using google::protobuf::json::MessageToJsonString;
using google::protobuf::json::PrintOptions;

// kMaxIntJSON is defined as the Number.MAX_SAFE_INTEGER value per EcmaScript 6.
constexpr int64_t kMaxIntJSON = (1ll << 53) - 1;

// kMinIntJSON is defined as the Number.MIN_SAFE_INTEGER value per EcmaScript 6.
constexpr int64_t kMinIntJSON = -kMaxIntJSON;

// IsJSONSafe indicates whether the int is safely representable as a floating
// point value in JSON.
static bool IsJSONSafe(int64_t i) {
  return i >= kMinIntJSON && i <= kMaxIntJSON;
}

// IsJSONSafe indicates whether the uint is safely representable as a floating
// point value in JSON.
static bool IsJSONSafe(uint64_t i) {
  return i <= static_cast<uint64_t>(kMaxIntJSON);
}

static bool IsEmptyProto(const google::protobuf::Descriptor* descriptor) {
  return descriptor->full_name() == "google.protobuf.Empty";
}

static bool IsFieldMaskProto(const google::protobuf::Descriptor* descriptor) {
  return descriptor->full_name() == "google.protobuf.FieldMask";
}

static std::optional<std::string> GetFieldMaskJsonString(
    const google::protobuf::Message& message) {
  // TODO(b/540507668): Refactor to pipe descriptor_pool through
  // ValueFromValue to use internal::MessageToJson.
  PrintOptions json_options;
  std::string json_str;
  auto status = MessageToJsonString(message, &json_str, json_options);
  if (!status.ok()) {
    ABSL_LOG(ERROR) << "Failed to convert FieldMask to JSON: " << status;
    return std::nullopt;
  }
  // If JSON marshalling is correct, we know we'll always get a plain
  // JSON string value and it shouldn't contain any escapes that we need
  // to interpret.
  if (json_str.size() >= 2 && json_str.front() == '"' &&
      json_str.back() == '"') {
    return json_str.substr(1, json_str.size() - 2);
  }
  return json_str;
}

struct IgnoreErrorAndReturnNullptr {
  std::nullptr_t operator()(const absl::Status& status) const {
    status.IgnoreError();
    return nullptr;
  }
};

google::protobuf::Message* DurationFromValue(const google::protobuf::Message* prototype,
                                   const CelValue& value,
                                   google::protobuf::Arena* arena) {
  absl::Duration val;
  if (!value.GetValue(&val)) {
    return nullptr;
  }
  if (!cel::internal::ValidateDuration(val).ok()) {
    return nullptr;
  }
  auto* message = prototype->New(arena);
  CEL_ASSIGN_OR_RETURN(
      auto reflection,
      cel::well_known_types::GetDurationReflection(message->GetDescriptor()),
      _.With(IgnoreErrorAndReturnNullptr()));
  reflection.UnsafeSetFromAbslDuration(message, val);
  return message;
}

google::protobuf::Message* BoolFromValue(const google::protobuf::Message* prototype,
                               const CelValue& value, google::protobuf::Arena* arena) {
  bool val;
  if (!value.GetValue(&val)) {
    return nullptr;
  }
  auto* message = prototype->New(arena);
  CEL_ASSIGN_OR_RETURN(
      auto reflection,
      cel::well_known_types::GetBoolValueReflection(message->GetDescriptor()),
      _.With(IgnoreErrorAndReturnNullptr()));
  reflection.SetValue(message, val);
  return message;
}

google::protobuf::Message* BytesFromValue(const google::protobuf::Message* prototype,
                                const CelValue& value, google::protobuf::Arena* arena) {
  CelValue::BytesHolder view_val;
  if (!value.GetValue(&view_val)) {
    return nullptr;
  }
  auto* message = prototype->New(arena);
  CEL_ASSIGN_OR_RETURN(
      auto reflection,
      cel::well_known_types::GetBytesValueReflection(message->GetDescriptor()),
      _.With(IgnoreErrorAndReturnNullptr()));
  reflection.SetValue(message, view_val.value());
  return message;
}

google::protobuf::Message* DoubleFromValue(const google::protobuf::Message* prototype,
                                 const CelValue& value, google::protobuf::Arena* arena) {
  double val;
  if (!value.GetValue(&val)) {
    return nullptr;
  }
  auto* message = prototype->New(arena);
  CEL_ASSIGN_OR_RETURN(
      auto reflection,
      cel::well_known_types::GetDoubleValueReflection(message->GetDescriptor()),
      _.With(IgnoreErrorAndReturnNullptr()));
  reflection.SetValue(message, val);
  return message;
}

google::protobuf::Message* FloatFromValue(const google::protobuf::Message* prototype,
                                const CelValue& value, google::protobuf::Arena* arena) {
  double val;
  if (!value.GetValue(&val)) {
    return nullptr;
  }
  float fval = val;
  // Abort the conversion if the value is outside the float range.
  if (val > std::numeric_limits<float>::max()) {
    fval = std::numeric_limits<float>::infinity();
  } else if (val < std::numeric_limits<float>::lowest()) {
    fval = -std::numeric_limits<float>::infinity();
  }
  auto* message = prototype->New(arena);
  CEL_ASSIGN_OR_RETURN(
      auto reflection,
      cel::well_known_types::GetFloatValueReflection(message->GetDescriptor()),
      _.With(IgnoreErrorAndReturnNullptr()));
  reflection.SetValue(message, static_cast<float>(fval));
  return message;
}

google::protobuf::Message* Int32FromValue(const google::protobuf::Message* prototype,
                                const CelValue& value, google::protobuf::Arena* arena) {
  int64_t val;
  if (!value.GetValue(&val)) {
    return nullptr;
  }
  if (!cel::internal::CheckedInt64ToInt32(val).ok()) {
    return nullptr;
  }
  int32_t ival = static_cast<int32_t>(val);
  auto* message = prototype->New(arena);
  CEL_ASSIGN_OR_RETURN(
      auto reflection,
      cel::well_known_types::GetInt32ValueReflection(message->GetDescriptor()),
      _.With(IgnoreErrorAndReturnNullptr()));
  reflection.SetValue(message, ival);
  return message;
}

google::protobuf::Message* Int64FromValue(const google::protobuf::Message* prototype,
                                const CelValue& value, google::protobuf::Arena* arena) {
  int64_t val;
  if (!value.GetValue(&val)) {
    return nullptr;
  }
  auto* message = prototype->New(arena);
  CEL_ASSIGN_OR_RETURN(
      auto reflection,
      cel::well_known_types::GetInt64ValueReflection(message->GetDescriptor()),
      _.With(IgnoreErrorAndReturnNullptr()));
  reflection.SetValue(message, val);
  return message;
}

google::protobuf::Message* StringFromValue(const google::protobuf::Message* prototype,
                                 const CelValue& value, google::protobuf::Arena* arena) {
  CelValue::StringHolder view_val;
  if (!value.GetValue(&view_val)) {
    return nullptr;
  }
  auto* message = prototype->New(arena);
  CEL_ASSIGN_OR_RETURN(
      auto reflection,
      cel::well_known_types::GetStringValueReflection(message->GetDescriptor()),
      _.With(IgnoreErrorAndReturnNullptr()));
  reflection.SetValue(message, view_val.value());
  return message;
}

google::protobuf::Message* TimestampFromValue(const google::protobuf::Message* prototype,
                                    const CelValue& value,
                                    google::protobuf::Arena* arena) {
  absl::Time val;
  if (!value.GetValue(&val)) {
    return nullptr;
  }
  if (!cel::internal::ValidateTimestamp(val).ok()) {
    return nullptr;
  }
  auto* message = prototype->New(arena);
  CEL_ASSIGN_OR_RETURN(
      auto reflection,
      cel::well_known_types::GetTimestampReflection(message->GetDescriptor()),
      _.With(IgnoreErrorAndReturnNullptr()));
  reflection.UnsafeSetFromAbslTime(message, val);
  return message;
}

google::protobuf::Message* UInt32FromValue(const google::protobuf::Message* prototype,
                                 const CelValue& value, google::protobuf::Arena* arena) {
  uint64_t val;
  if (!value.GetValue(&val)) {
    return nullptr;
  }
  if (!cel::internal::CheckedUint64ToUint32(val).ok()) {
    return nullptr;
  }
  uint32_t ival = static_cast<uint32_t>(val);
  auto* message = prototype->New(arena);
  CEL_ASSIGN_OR_RETURN(
      auto reflection,
      cel::well_known_types::GetUInt32ValueReflection(message->GetDescriptor()),
      _.With(IgnoreErrorAndReturnNullptr()));
  reflection.SetValue(message, ival);
  return message;
}

google::protobuf::Message* UInt64FromValue(const google::protobuf::Message* prototype,
                                 const CelValue& value, google::protobuf::Arena* arena) {
  uint64_t val;
  if (!value.GetValue(&val)) {
    return nullptr;
  }
  auto* message = prototype->New(arena);
  CEL_ASSIGN_OR_RETURN(
      auto reflection,
      cel::well_known_types::GetUInt64ValueReflection(message->GetDescriptor()),
      _.With(IgnoreErrorAndReturnNullptr()));
  reflection.SetValue(message, val);
  return message;
}

google::protobuf::Message* ValueFromValue(google::protobuf::Message* message, const CelValue& value,
                                google::protobuf::Arena* arena);

google::protobuf::Message* ValueFromValue(const google::protobuf::Message* prototype,
                                const CelValue& value, google::protobuf::Arena* arena) {
  return ValueFromValue(prototype->New(arena), value, arena);
}

google::protobuf::Message* ListFromValue(google::protobuf::Message* message, const CelValue& value,
                               google::protobuf::Arena* arena) {
  if (!value.IsList()) {
    return nullptr;
  }
  const CelList& list = *value.ListOrDie();
  CEL_ASSIGN_OR_RETURN(
      auto reflection,
      cel::well_known_types::GetListValueReflection(message->GetDescriptor()),
      _.With(IgnoreErrorAndReturnNullptr()));
  for (int i = 0; i < list.size(); i++) {
    auto e = list.Get(arena, i);
    auto* elem = reflection.AddValues(message);
    if (ValueFromValue(elem, e, arena) == nullptr) {
      return nullptr;
    }
  }
  return message;
}

google::protobuf::Message* ListFromValue(const google::protobuf::Message* prototype,
                               const CelValue& value, google::protobuf::Arena* arena) {
  if (!value.IsList()) {
    return nullptr;
  }
  return ListFromValue(prototype->New(arena), value, arena);
}

google::protobuf::Message* StructFromValue(google::protobuf::Message* message,
                                 const CelValue& value, google::protobuf::Arena* arena) {
  if (!value.IsMap()) {
    return nullptr;
  }
  const CelMap& map = *value.MapOrDie();
  absl::StatusOr<const CelList*> keys_or = map.ListKeys(arena);
  if (!keys_or.ok()) {
    // If map doesn't support listing keys, it can't pack into a Struct value.
    // This will surface as a CEL error when the object creation expression
    // fails.
    return nullptr;
  }
  const CelList& keys = **keys_or;
  CEL_ASSIGN_OR_RETURN(
      auto reflection,
      cel::well_known_types::GetStructReflection(message->GetDescriptor()),
      _.With(IgnoreErrorAndReturnNullptr()));
  for (int i = 0; i < keys.size(); i++) {
    auto k = keys.Get(arena, i);
    // If the key is not a string type, abort the conversion.
    if (!k.IsString()) {
      return nullptr;
    }
    absl::string_view key = k.StringOrDie().value();

    auto v = map.Get(arena, k);
    if (!v.has_value()) {
      return nullptr;
    }
    auto* field = reflection.InsertField(message, key);
    if (ValueFromValue(field, *v, arena) == nullptr) {
      return nullptr;
    }
  }
  return message;
}

google::protobuf::Message* StructFromValue(const google::protobuf::Message* prototype,
                                 const CelValue& value, google::protobuf::Arena* arena) {
  if (!value.IsMap()) {
    return nullptr;
  }
  return StructFromValue(prototype->New(arena), value, arena);
}

google::protobuf::Message* ValueFromValue(google::protobuf::Message* message, const CelValue& value,
                                google::protobuf::Arena* arena) {
  CEL_ASSIGN_OR_RETURN(
      auto reflection,
      cel::well_known_types::GetValueReflection(message->GetDescriptor()),
      _.With(IgnoreErrorAndReturnNullptr()));
  switch (value.type()) {
    case CelValue::Type::kBool: {
      bool val;
      if (value.GetValue(&val)) {
        reflection.SetBoolValue(message, val);
        return message;
      }
    } break;
    case CelValue::Type::kBytes: {
      // Base64 encode byte strings to ensure they can safely be transported
      // in a JSON string.
      CelValue::BytesHolder val;
      if (value.GetValue(&val)) {
        reflection.SetStringValueFromBytes(message, val.value());
        return message;
      }
    } break;
    case CelValue::Type::kDouble: {
      double val;
      if (value.GetValue(&val)) {
        reflection.SetNumberValue(message, val);
        return message;
      }
    } break;
    case CelValue::Type::kDuration: {
      // Convert duration values to a protobuf JSON format.
      absl::Duration val;
      if (value.GetValue(&val)) {
        CEL_RETURN_IF_ERROR(cel::internal::ValidateDuration(val))
            .With(IgnoreErrorAndReturnNullptr());
        reflection.SetStringValueFromDuration(message, val);
        return message;
      }
    } break;
    case CelValue::Type::kInt64: {
      int64_t val;
      // Convert int64_t values within the int53 range to doubles, otherwise
      // serialize the value to a string.
      if (value.GetValue(&val)) {
        reflection.SetNumberValue(message, val);
        return message;
      }
    } break;
    case CelValue::Type::kString: {
      CelValue::StringHolder val;
      if (value.GetValue(&val)) {
        reflection.SetStringValue(message, val.value());
        return message;
      }
    } break;
    case CelValue::Type::kTimestamp: {
      // Convert timestamp values to a protobuf JSON format.
      absl::Time val;
      if (value.GetValue(&val)) {
        CEL_RETURN_IF_ERROR(cel::internal::ValidateTimestamp(val))
            .With(IgnoreErrorAndReturnNullptr());
        reflection.SetStringValueFromTimestamp(message, val);
        return message;
      }
    } break;
    case CelValue::Type::kUint64: {
      uint64_t val;
      // Convert uint64_t values within the int53 range to doubles, otherwise
      // serialize the value to a string.
      if (value.GetValue(&val)) {
        reflection.SetNumberValue(message, val);
        return message;
      }
    } break;
    case CelValue::Type::kList: {
      if (ListFromValue(reflection.MutableListValue(message), value, arena) !=
          nullptr) {
        return message;
      }
    } break;
    case CelValue::Type::kMap: {
      if (StructFromValue(reflection.MutableStructValue(message), value,
                          arena) != nullptr) {
        return message;
      }
    } break;
    case CelValue::Type::kMessage: {
      const google::protobuf::Message* message_ptr = value.MessageOrDie();
      if (IsEmptyProto(message_ptr->GetDescriptor())) {
        reflection.MutableStructValue(message);
        return message;
      }
      if (IsFieldMaskProto(message_ptr->GetDescriptor())) {
        std::optional<std::string> fm_str =
            GetFieldMaskJsonString(*message_ptr);
        if (fm_str.has_value()) {
          reflection.SetStringValue(message, *fm_str);
          return message;
        }
        return nullptr;
      }
      return nullptr;
    } break;
    case CelValue::Type::kNullType:
      reflection.SetNullValue(message);
      return message;
      break;
    default:
      return nullptr;
  }
  return nullptr;
}

bool ValueFromValue(Value* json, const CelValue& value, google::protobuf::Arena* arena);

bool ListFromValue(ListValue* json_list, const CelValue& value,
                   google::protobuf::Arena* arena) {
  if (!value.IsList()) {
    return false;
  }
  const CelList& list = *value.ListOrDie();
  for (int i = 0; i < list.size(); i++) {
    auto e = list.Get(arena, i);
    Value* elem = json_list->add_values();
    if (!ValueFromValue(elem, e, arena)) {
      return false;
    }
  }
  return true;
}

bool StructFromValue(Struct* json_struct, const CelValue& value,
                     google::protobuf::Arena* arena) {
  if (!value.IsMap()) {
    return false;
  }
  const CelMap& map = *value.MapOrDie();
  absl::StatusOr<const CelList*> keys_or = map.ListKeys(arena);
  if (!keys_or.ok()) {
    // If map doesn't support listing keys, it can't pack into a Struct value.
    // This will surface as a CEL error when the object creation expression
    // fails.
    return false;
  }
  const CelList& keys = **keys_or;
  auto fields = json_struct->mutable_fields();
  for (int i = 0; i < keys.size(); i++) {
    auto k = keys.Get(arena, i);
    // If the key is not a string type, abort the conversion.
    if (!k.IsString()) {
      return false;
    }
    absl::string_view key = k.StringOrDie().value();

    auto v = map.Get(arena, k);
    if (!v.has_value()) {
      return false;
    }
    Value field_value;
    if (!ValueFromValue(&field_value, *v, arena)) {
      return false;
    }
    (*fields)[std::string(key)] = field_value;
  }
  return true;
}

bool ValueFromValue(Value* json, const CelValue& value, google::protobuf::Arena* arena) {
  switch (value.type()) {
    case CelValue::Type::kBool: {
      bool val;
      if (value.GetValue(&val)) {
        json->set_bool_value(val);
        return true;
      }
    } break;
    case CelValue::Type::kBytes: {
      // Base64 encode byte strings to ensure they can safely be transported
      // in a JSON string.
      CelValue::BytesHolder val;
      if (value.GetValue(&val)) {
        json->set_string_value(absl::Base64Escape(val.value()));
        return true;
      }
    } break;
    case CelValue::Type::kDouble: {
      double val;
      if (value.GetValue(&val)) {
        json->set_number_value(val);
        return true;
      }
    } break;
    case CelValue::Type::kDuration: {
      // Convert duration values to a protobuf JSON format.
      absl::Duration val;
      if (value.GetValue(&val)) {
        auto encode = cel::internal::EncodeDurationToString(val);
        if (!encode.ok()) {
          return false;
        }
        json->set_string_value(*encode);
        return true;
      }
    } break;
    case CelValue::Type::kInt64: {
      int64_t val;
      // Convert int64_t values within the int53 range to doubles, otherwise
      // serialize the value to a string.
      if (value.GetValue(&val)) {
        if (IsJSONSafe(val)) {
          json->set_number_value(val);
        } else {
          json->set_string_value(absl::StrCat(val));
        }
        return true;
      }
    } break;
    case CelValue::Type::kString: {
      CelValue::StringHolder val;
      if (value.GetValue(&val)) {
        json->set_string_value(val.value());
        return true;
      }
    } break;
    case CelValue::Type::kTimestamp: {
      // Convert timestamp values to a protobuf JSON format.
      absl::Time val;
      if (value.GetValue(&val)) {
        auto encode = cel::internal::EncodeTimeToString(val);
        if (!encode.ok()) {
          return false;
        }
        json->set_string_value(*encode);
        return true;
      }
    } break;
    case CelValue::Type::kUint64: {
      uint64_t val;
      // Convert uint64_t values within the int53 range to doubles, otherwise
      // serialize the value to a string.
      if (value.GetValue(&val)) {
        if (IsJSONSafe(val)) {
          json->set_number_value(val);
        } else {
          json->set_string_value(absl::StrCat(val));
        }
        return true;
      }
    } break;
    case CelValue::Type::kList:
      return ListFromValue(json->mutable_list_value(), value, arena);
    case CelValue::Type::kMap:
      return StructFromValue(json->mutable_struct_value(), value, arena);
    case CelValue::Type::kMessage: {
      const google::protobuf::Message* message_ptr = value.MessageOrDie();
      if (IsEmptyProto(message_ptr->GetDescriptor())) {
        json->mutable_struct_value();
        return true;
      }
      if (IsFieldMaskProto(message_ptr->GetDescriptor())) {
        std::optional<std::string> fm_str =
            GetFieldMaskJsonString(*message_ptr);
        if (fm_str.has_value()) {
          json->set_string_value(*fm_str);
          return true;
        }
        return false;
      }
      return false;
    }
    case CelValue::Type::kNullType:
      json->set_null_value(protobuf::NULL_VALUE);
      return true;
    default:
      return false;
  }
  return false;
}

google::protobuf::Message* AnyFromValue(const google::protobuf::Message* prototype,
                              const CelValue& value, google::protobuf::Arena* arena) {
  std::string type_name;
  absl::Cord payload;

  // In open source, any->PackFrom() returns void rather than boolean.
  switch (value.type()) {
    case CelValue::Type::kBool: {
      BoolValue v;
      type_name = v.GetTypeName();
      v.set_value(value.BoolOrDie());
      payload = v.SerializeAsCord();
    } break;
    case CelValue::Type::kBytes: {
      BytesValue v;
      type_name = v.GetTypeName();
      v.set_value(value.BytesOrDie().value());
      payload = v.SerializeAsCord();
    } break;
    case CelValue::Type::kDouble: {
      DoubleValue v;
      type_name = v.GetTypeName();
      v.set_value(value.DoubleOrDie());
      payload = v.SerializeAsCord();
    } break;
    case CelValue::Type::kDuration: {
      Duration v;
      if (!cel::internal::EncodeDuration(value.DurationOrDie(), &v).ok()) {
        return nullptr;
      }
      type_name = v.GetTypeName();
      payload = v.SerializeAsCord();
    } break;
    case CelValue::Type::kInt64: {
      Int64Value v;
      type_name = v.GetTypeName();
      v.set_value(value.Int64OrDie());
      payload = v.SerializeAsCord();
    } break;
    case CelValue::Type::kString: {
      StringValue v;
      type_name = v.GetTypeName();
      v.set_value(std::string(value.StringOrDie().value()));
      payload = v.SerializeAsCord();
    } break;
    case CelValue::Type::kTimestamp: {
      Timestamp v;
      if (!cel::internal::EncodeTime(value.TimestampOrDie(), &v).ok()) {
        return nullptr;
      }
      type_name = v.GetTypeName();
      payload = v.SerializeAsCord();
    } break;
    case CelValue::Type::kUint64: {
      UInt64Value v;
      type_name = v.GetTypeName();
      v.set_value(value.Uint64OrDie());
      payload = v.SerializeAsCord();
    } break;
    case CelValue::Type::kList: {
      ListValue v;
      if (!ListFromValue(&v, value, arena)) {
        return nullptr;
      }
      type_name = v.GetTypeName();
      payload = v.SerializeAsCord();
    } break;
    case CelValue::Type::kMap: {
      Struct v;
      if (!StructFromValue(&v, value, arena)) {
        return nullptr;
      }
      type_name = v.GetTypeName();
      payload = v.SerializeAsCord();
    } break;
    case CelValue::Type::kNullType: {
      Value v;
      type_name = v.GetTypeName();
      v.set_null_value(google::protobuf::NULL_VALUE);
      payload = v.SerializeAsCord();
    } break;
    case CelValue::Type::kMessage: {
      type_name = value.MessageWrapperOrDie().message_ptr()->GetTypeName();
      payload = value.MessageWrapperOrDie().message_ptr()->SerializeAsCord();
    } break;
    default:
      return nullptr;
  }

  auto* message = prototype->New(arena);
  CEL_ASSIGN_OR_RETURN(
      auto reflection,
      cel::well_known_types::GetAnyReflection(message->GetDescriptor()),
      _.With(IgnoreErrorAndReturnNullptr()));
  reflection.SetTypeUrl(message,
                        absl::StrCat("type.googleapis.com/", type_name));
  reflection.SetValue(message, payload);
  return message;
}

bool IsAlreadyWrapped(google::protobuf::Descriptor::WellKnownType wkt,
                      const CelValue& value) {
  if (value.IsMessage()) {
    const auto* msg = value.MessageOrDie();
    if (wkt == msg->GetDescriptor()->well_known_type()) {
      return true;
    }
  }
  return false;
}

// MessageFromValueMaker makes a specific protobuf Message instance based on
// the desired protobuf type name and an input CelValue.
//
// It holds a registry of CelValue factories for specific subtypes of Message.
// If message does not match any of types stored in registry, an the factory
// returns an absent value.
class MessageFromValueMaker {
 public:
  // Non-copyable, non-assignable
  MessageFromValueMaker(const MessageFromValueMaker&) = delete;
  MessageFromValueMaker& operator=(const MessageFromValueMaker&) = delete;

  static google::protobuf::Message* MaybeWrapMessage(const google::protobuf::Descriptor* descriptor,
                                           google::protobuf::MessageFactory* factory,
                                           const CelValue& value,
                                           Arena* arena) {
    switch (descriptor->well_known_type()) {
      case google::protobuf::Descriptor::WELLKNOWNTYPE_DOUBLEVALUE:
        if (IsAlreadyWrapped(descriptor->well_known_type(), value)) {
          return nullptr;
        }
        return DoubleFromValue(factory->GetPrototype(descriptor), value, arena);
      case google::protobuf::Descriptor::WELLKNOWNTYPE_FLOATVALUE:
        if (IsAlreadyWrapped(descriptor->well_known_type(), value)) {
          return nullptr;
        }
        return FloatFromValue(factory->GetPrototype(descriptor), value, arena);
      case google::protobuf::Descriptor::WELLKNOWNTYPE_INT64VALUE:
        if (IsAlreadyWrapped(descriptor->well_known_type(), value)) {
          return nullptr;
        }
        return Int64FromValue(factory->GetPrototype(descriptor), value, arena);
      case google::protobuf::Descriptor::WELLKNOWNTYPE_UINT64VALUE:
        if (IsAlreadyWrapped(descriptor->well_known_type(), value)) {
          return nullptr;
        }
        return UInt64FromValue(factory->GetPrototype(descriptor), value, arena);
      case google::protobuf::Descriptor::WELLKNOWNTYPE_INT32VALUE:
        if (IsAlreadyWrapped(descriptor->well_known_type(), value)) {
          return nullptr;
        }
        return Int32FromValue(factory->GetPrototype(descriptor), value, arena);
      case google::protobuf::Descriptor::WELLKNOWNTYPE_UINT32VALUE:
        if (IsAlreadyWrapped(descriptor->well_known_type(), value)) {
          return nullptr;
        }
        return UInt32FromValue(factory->GetPrototype(descriptor), value, arena);
      case google::protobuf::Descriptor::WELLKNOWNTYPE_STRINGVALUE:
        if (IsAlreadyWrapped(descriptor->well_known_type(), value)) {
          return nullptr;
        }
        return StringFromValue(factory->GetPrototype(descriptor), value, arena);
      case google::protobuf::Descriptor::WELLKNOWNTYPE_BYTESVALUE:
        if (IsAlreadyWrapped(descriptor->well_known_type(), value)) {
          return nullptr;
        }
        return BytesFromValue(factory->GetPrototype(descriptor), value, arena);
      case google::protobuf::Descriptor::WELLKNOWNTYPE_BOOLVALUE:
        if (IsAlreadyWrapped(descriptor->well_known_type(), value)) {
          return nullptr;
        }
        return BoolFromValue(factory->GetPrototype(descriptor), value, arena);
      case google::protobuf::Descriptor::WELLKNOWNTYPE_ANY:
        if (IsAlreadyWrapped(descriptor->well_known_type(), value)) {
          return nullptr;
        }
        return AnyFromValue(factory->GetPrototype(descriptor), value, arena);
      case google::protobuf::Descriptor::WELLKNOWNTYPE_DURATION:
        if (IsAlreadyWrapped(descriptor->well_known_type(), value)) {
          return nullptr;
        }
        return DurationFromValue(factory->GetPrototype(descriptor), value,
                                 arena);
      case google::protobuf::Descriptor::WELLKNOWNTYPE_TIMESTAMP:
        if (IsAlreadyWrapped(descriptor->well_known_type(), value)) {
          return nullptr;
        }
        return TimestampFromValue(factory->GetPrototype(descriptor), value,
                                  arena);
      case google::protobuf::Descriptor::WELLKNOWNTYPE_VALUE:
        if (IsAlreadyWrapped(descriptor->well_known_type(), value)) {
          return nullptr;
        }
        return ValueFromValue(factory->GetPrototype(descriptor), value, arena);
      case google::protobuf::Descriptor::WELLKNOWNTYPE_LISTVALUE:
        if (IsAlreadyWrapped(descriptor->well_known_type(), value)) {
          return nullptr;
        }
        return ListFromValue(factory->GetPrototype(descriptor), value, arena);
      case google::protobuf::Descriptor::WELLKNOWNTYPE_STRUCT:
        if (IsAlreadyWrapped(descriptor->well_known_type(), value)) {
          return nullptr;
        }
        return StructFromValue(factory->GetPrototype(descriptor), value, arena);
      // WELLKNOWNTYPE_FIELDMASK has no special CelValue type
      default:
        return nullptr;
    }
  }
};

}  // namespace

const google::protobuf::Message* MaybeWrapValueToMessage(
    const google::protobuf::Descriptor* descriptor, google::protobuf::MessageFactory* factory,
    const CelValue& value, Arena* arena) {
  google::protobuf::Message* msg = MessageFromValueMaker::MaybeWrapMessage(
      descriptor, factory, value, arena);
  return msg;
}

}  // namespace google::api::expr::runtime::internal
