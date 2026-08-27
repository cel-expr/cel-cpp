// Copyright 2021 Google LLC
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

#include "eval/public/structs/cel_proto_wrapper.h"

#include <optional>

#include "absl/base/no_destructor.h"
#include "absl/base/nullability.h"
#include "absl/log/absl_check.h"
#include "absl/log/absl_log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/legacy_value.h"
#include "common/value.h"
#include "eval/public/cel_value.h"
#include "eval/public/message_wrapper.h"
#include "eval/public/structs/cel_proto_wrap_value_to_message.h"
#include "eval/public/structs/proto_message_type_adapter.h"
#include "eval/public/structs/trivial_legacy_type_info_internal.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"

namespace google::api::expr::runtime {

namespace {

using ::cel::interop_internal::TrivialTypeInfo;
using ::google::protobuf::Arena;
using ::google::protobuf::Descriptor;
using ::google::protobuf::DescriptorPool;
using ::google::protobuf::Message;
using ::google::protobuf::MessageFactory;

// Returns the arena for the given message, or the fallback arena if the
// message does not have an arena.
//
// A global fallback arena is used to avoid allocation when the arena is not
// specified. This is effectively a memory leak, but won't trigger leak
// check analyzers.
//
// This emulates the old behavior of tolerating a nullptr arena without
// triggering a crash.
google::protobuf::Arena* GetArena(const Message* absl_nonnull message,
                        google::protobuf::Arena* absl_nullable arena) {
  if (arena != nullptr) {
    return arena;
  }
  if (message->GetArena() != nullptr) {
    return message->GetArena();
  }
  static absl::NoDestructor<google::protobuf::Arena> fallback_arena;
  ABSL_LOG(WARNING)
      << "CelValue: using fallback global arena for wrapping message: "
      << message->GetTypeName();
  return fallback_arena.get();
}

}  // namespace

CelValue CelProtoWrapper::InternalWrapMessage(const Message* message) {
  return CelValue::CreateMessageWrapper(
      MessageWrapper(message, &GetGenericProtoTypeInfoInstance()));
}

CelValue CelProtoWrapper::CreateMessage(
    const Message* absl_nonnull value,
    const google::protobuf::DescriptorPool* absl_nonnull pool,
    MessageFactory* absl_nonnull factory, Arena* absl_nonnull arena) {
  ABSL_DCHECK(value != nullptr);
  if (value->GetDescriptor() == nullptr || value->GetReflection() == nullptr) {
    // This only happens for custom google::protobuf::Message subclasses that CEL can't
    // support.
    return CelValue::CreateMessageWrapper(
        MessageWrapper(value, TrivialTypeInfo::GetInstance()));
  }

  auto modern_value =
      cel::Value::WrapMessageUnsafe(value, pool, factory, arena);

  absl::StatusOr<CelValue> cel_value = cel::LegacyValue(arena, modern_value);
  if (!cel_value.ok()) {
    // This only happens for custom google::protobuf::Message subclasses that CEL can't
    // support.
    auto* status =
        google::protobuf::Arena::Create<absl::Status>(arena, cel_value.status());
    return CelValue::CreateError(status);
  }
  return *cel_value;
}

CelValue CelProtoWrapper::CreateMessage(const Message* absl_nullable value,
                                        Arena* absl_nullable arena) {
  if (value == nullptr) {
    return CelValue::CreateNull();
  }

  if (value->GetDescriptor() == nullptr || value->GetReflection() == nullptr) {
    // This only happens for custom messages subclasses that CEL can't support.
    return CelValue::CreateMessageWrapper(
        MessageWrapper(value, TrivialTypeInfo::GetInstance()));
  }
  const auto* pool = value->GetDescriptor()->file()->pool();
  auto* factory = value->GetReflection()->GetMessageFactory();
  arena = GetArena(value, arena);
  return CreateMessage(value, pool, factory, arena);
}

std::optional<CelValue> CelProtoWrapper::MaybeWrapValue(
    const Descriptor* descriptor, google::protobuf::MessageFactory* factory,
    const CelValue& value, Arena* arena) {
  const Message* msg =
      internal::MaybeWrapValueToMessage(descriptor, factory, value, arena);
  if (msg != nullptr) {
    return InternalWrapMessage(msg);
  } else {
    return std::nullopt;
  }
}

}  // namespace google::api::expr::runtime
