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

#ifndef THIRD_PARTY_CEL_CPP_EVAL_PUBLIC_STRUCTS_CEL_PROTO_WRAP_VALUE_TO_MESSAGE_H_
#define THIRD_PARTY_CEL_CPP_EVAL_PUBLIC_STRUCTS_CEL_PROTO_WRAP_VALUE_TO_MESSAGE_H_

#include "eval/public/cel_value.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"

namespace google::api::expr::runtime::internal {

// MaybeWrapValue attempts to wrap the input value in a proto message with
// the given type_name. If the value can be wrapped, it is returned as a
// protobuf message. Otherwise, the result will be nullptr.
//
// This method is the complement to UnwrapMessageToValue which may unwrap a
// protobuf message to native CelValue representation during a protobuf field
// read.
// Just as CreateMessage should only be used when reading protobuf values,
// MaybeWrapValueToMessage should only be used when assigning protobuf fields.
const google::protobuf::Message* MaybeWrapValueToMessage(
    const google::protobuf::Descriptor* descriptor, google::protobuf::MessageFactory* factory,
    const CelValue& value, google::protobuf::Arena* arena);

}  // namespace google::api::expr::runtime::internal

#endif  // THIRD_PARTY_CEL_CPP_EVAL_PUBLIC_STRUCTS_CEL_PROTO_WRAP_VALUE_TO_MESSAGE_H_
