/*
 * Copyright 2026 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "eval/public/cel_value_clone.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "absl/types/optional.h"
#include "eval/public/cel_value.h"
#include "eval/public/containers/container_backed_list_impl.h"
#include "eval/public/containers/container_backed_map_impl.h"
#include "eval/public/message_wrapper.h"
#include "eval/public/unknown_set.h"
#include "internal/status_macros.h"
#include "google/protobuf/arena.h"

namespace google::api::expr::runtime {

namespace {

struct CloneVisitor {
  google::protobuf::Arena* const arena;

  absl::StatusOr<CelValue> operator()(bool arg) {
    return CelValue::CreateBool(arg);
  }
  absl::StatusOr<CelValue> operator()(int64_t arg) {
    return CelValue::CreateInt64(arg);
  }
  absl::StatusOr<CelValue> operator()(uint64_t arg) {
    return CelValue::CreateUint64(arg);
  }
  absl::StatusOr<CelValue> operator()(double arg) {
    return CelValue::CreateDouble(arg);
  }
  absl::StatusOr<CelValue> operator()(CelValue::NullType) {
    return CelValue::CreateNull();
  }

  absl::StatusOr<CelValue> operator()(CelValue::StringHolder arg) {
    if (arg.value().empty()) {
      return CelValue::CreateStringView("");
    }
    return CelValue::CreateString(CelValue::StringHolder(
        google::protobuf::Arena::Create<std::string>(arena, arg.value())));
  }

  absl::StatusOr<CelValue> operator()(CelValue::BytesHolder arg) {
    if (arg.value().empty()) {
      return CelValue::CreateBytesView("");
    }
    return CelValue::CreateBytes(CelValue::BytesHolder(
        google::protobuf::Arena::Create<std::string>(arena, arg.value())));
  }

  absl::StatusOr<CelValue> operator()(const MessageWrapper& arg) {
    if (arg.HasFullProto()) {
      google::protobuf::Message* dup =
          static_cast<const google::protobuf::Message*>(arg.message_ptr())->New(arena);
      dup->CopyFrom(*static_cast<const google::protobuf::Message*>(arg.message_ptr()));
      return CelValue::CreateMessageWrapper(
          MessageWrapper(dup, arg.legacy_type_info()));
    }
    google::protobuf::MessageLite* dup = arg.message_ptr()->New(arena);
    absl::Cord serialized;
    if (!arg.message_ptr()->SerializePartialToCord(&serialized)) {
      return absl::UnknownError(
          absl::StrCat("failed to serialize message: ", dup->GetTypeName()));
    }
    if (!dup->ParsePartialFromString(serialized)) {
      return absl::UnknownError(
          absl::StrCat("failed to parse message: ", dup->GetTypeName()));
    }
    return CelValue::CreateMessageWrapper(
        MessageWrapper(dup, arg.legacy_type_info()));
  }

  absl::StatusOr<CelValue> operator()(absl::Duration arg) {
    return CelValue::CreateUncheckedDuration(arg);
  }

  absl::StatusOr<CelValue> operator()(absl::Time arg) {
    return CelValue::CreateTimestamp(arg);
  }

  absl::StatusOr<CelValue> operator()(const CelList* arg) {
    if (arg->empty()) {
      return CelValue::CreateList();
    }
    std::vector<CelValue> elements;
    elements.reserve(arg->size());
    for (int i = 0; i < arg->size(); i++) {
      CEL_ASSIGN_OR_RETURN(auto element,
                           CelValueClone(arena, arg->Get(arena, i)));
      elements.push_back(element);
    }
    return CelValue::CreateList(google::protobuf::Arena::Create<ContainerBackedListImpl>(
        arena, std::move(elements)));
  }

  absl::StatusOr<CelValue> operator()(const CelMap* arg) {
    if (arg->empty()) {
      return CelValue::CreateMap();
    }
    CEL_ASSIGN_OR_RETURN(auto keys, arg->ListKeys(arena));
    CelMapBuilder* builder = google::protobuf::Arena::Create<CelMapBuilder>(arena);
    for (int i = 0; i < keys->size(); ++i) {
      CelValue key = keys->Get(arena, i);
      absl::optional<CelValue> value = arg->Get(arena, key);
      if (!value.has_value()) {
        return absl::UnknownError(
            "CelMap::ListKeys returned key but CelMap::Get did not find the "
            "entry");
      }
      CEL_ASSIGN_OR_RETURN(auto key_dup, CelValueClone(arena, key));
      CEL_ASSIGN_OR_RETURN(auto value_dup, CelValueClone(arena, *value));
      CEL_RETURN_IF_ERROR(builder->Add(key_dup, value_dup));
    }
    return CelValue::CreateMap(builder);
  }

  absl::StatusOr<CelValue> operator()(const UnknownSet* arg) {
    return CelValue::CreateUnknownSet(
        google::protobuf::Arena::Create<UnknownSet>(arena, *arg));
  }

  absl::StatusOr<CelValue> operator()(CelValue::CelTypeHolder arg) {
    return CelValue::CreateCelType(CelValue::CelTypeHolder(
        google::protobuf::Arena::Create<std::string>(arena, arg.value())));
  }

  absl::StatusOr<CelValue> operator()(const CelError* arg) {
    return CelValue::CreateError(google::protobuf::Arena::Create<CelError>(arena, *arg));
  }
};

}  // namespace

absl::StatusOr<CelValue> CelValueClone(google::protobuf::Arena* arena, CelValue in) {
  ABSL_DCHECK(arena != nullptr);
  return in.Visit<absl::StatusOr<CelValue>>(CloneVisitor{.arena = arena});
}

absl::StatusOr<const CelMap*> CelMapClone(google::protobuf::Arena* arena,
                                          const CelMap* in) {
  CEL_ASSIGN_OR_RETURN(CelValue out, CloneVisitor{.arena = arena}(in));
  return out.MapOrDie();  // Crash OK
}

}  // namespace google::api::expr::runtime
