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

#ifndef THIRD_PARTY_CEL_CPP_EVAL_PUBLIC_CEL_VALUE_CLONE_H_
#define THIRD_PARTY_CEL_CPP_EVAL_PUBLIC_CEL_VALUE_CLONE_H_

#include "absl/status/statusor.h"
#include "eval/public/cel_value.h"
#include "google/protobuf/arena.h"

namespace google::api::expr::runtime {

// Clones (deep copy) a CelValue such that the resulting CelValue has a lifetime
// tied to the given arena. The container implementations for the output are not
// guaranteed to be the same as the input.
absl::StatusOr<CelValue> CelValueClone(google::protobuf::Arena* arena, CelValue in);
inline absl::StatusOr<CelValue> CelValueClone(google::protobuf::Arena* arena,
                                              absl::StatusOr<CelValue> in) {
  if (!in.ok()) {
    return in;
  }
  return CelValueClone(arena, *in);
}

// Clones (deep copy) a CelMap such that the resulting CelMap has a lifetime
// tied to the given arena. The container implementations for the output are not
// guaranteed to be the same as the input.
absl::StatusOr<const CelMap*> CelMapClone(google::protobuf::Arena* arena,
                                          const CelMap* in);

}  // namespace google::api::expr::runtime

#endif  // THIRD_PARTY_CEL_CPP_EVAL_PUBLIC_CEL_VALUE_CLONE_H_
