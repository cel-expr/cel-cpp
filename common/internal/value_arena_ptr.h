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

#ifndef THIRD_PARTY_CEL_CPP_COMMON_INTERNAL_VALUE_ARENA_PTR_H_
#define THIRD_PARTY_CEL_CPP_COMMON_INTERNAL_VALUE_ARENA_PTR_H_

#include "absl/base/nullability.h"
#include "common/internal/value_arena_ptr_tag.h"
#include "internal/tagged_arena_ptr.h"
#include "google/protobuf/arena.h"

namespace cel::internal {

template <>
struct TaggedArenaPtrTagTraits<common_internal::ValueArenaPtrTag> {
  [[nodiscard]]
  static bool CheckTag(google::protobuf::Arena* absl_nullable arena,
                       common_internal::ValueArenaPtrTag tag) {
    return common_internal::CheckValueArenaPtrTag(arena, tag);
  }
};

}  // namespace cel::internal

namespace cel::common_internal {

// Tagged arena pointer whose tag conveys additional information about the
// lifetime of the underlying data contained in the same value. This is meant to
// be stored inside Value implementations.
using ValueArenaPtr = internal::TaggedArenaPtr<ValueArenaPtrTag>;

}  // namespace cel::common_internal

#endif  // THIRD_PARTY_CEL_CPP_COMMON_INTERNAL_VALUE_ARENA_PTR_H_
