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

#ifndef THIRD_PARTY_CEL_CPP_COMMON_TYPE_REFLECTOR_H_
#define THIRD_PARTY_CEL_CPP_COMMON_TYPE_REFLECTOR_H_

#include <string>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "common/type_introspector.h"
#include "common/value.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/message.h"

namespace cel {

// `TypeReflector` is an interface for constructing new instances of types are
// runtime. It handles type reflection.
class TypeReflector : public virtual TypeIntrospector {
 public:
  // `NewValueBuilder` returns a new `ValueBuilder` for the corresponding type
  // `name`.  It is primarily used to handle wrapper types which sometimes show
  // up literally in expressions.
  virtual absl::StatusOr<absl_nullable ValueBuilderPtr> NewValueBuilder(
      absl::string_view name,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena) const = 0;

  // Creates new `ValueBuilder` instances for a type that was resolved ahead of
  // time (see `NewValueBuilderFactory`).
  using ValueBuilderFactory =
      absl::AnyInvocable<absl::StatusOr<absl_nullable ValueBuilderPtr>(
          google::protobuf::MessageFactory* absl_nonnull, google::protobuf::Arena* absl_nonnull)
                             const>;

  // `NewValueBuilderFactory` returns a factory for creating new `ValueBuilder`
  // instances for the corresponding type `name`.
  //
  // This is used to eagerly resolve the dependencies for creating value
  // builders (e.g. descriptor and prototype lookups) at plan time, instead of
  // repeating them every time a struct is created. `message_factory` is the
  // message factory that is expected to be used at evaluation time;
  // implementations must fall back to the regular lookup if the factory
  // passed at evaluation time is different.
  //
  // The returned factory must not outlive this `TypeReflector`.
  virtual ValueBuilderFactory NewValueBuilderFactory(
      absl::string_view name,
      google::protobuf::MessageFactory* absl_nonnull message_factory) const
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    static_cast<void>(message_factory);
    return [this, name = std::string(name)](
               google::protobuf::MessageFactory* absl_nonnull message_factory,
               google::protobuf::Arena* absl_nonnull arena)
               -> absl::StatusOr<absl_nullable ValueBuilderPtr> {
      return NewValueBuilder(name, message_factory, arena);
    };
  }
};

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMMON_TYPE_REFLECTOR_H_
