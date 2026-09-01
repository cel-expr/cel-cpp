// Copyright 2025 Google LLC
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

#include "common/standard_definitions.h"

#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "internal/testing.h"

namespace cel {
namespace {

TEST(GetAllIds, ReturnsAllIds) {
  absl::Span<const absl::string_view> all_ids =
      StandardOverloadIds::GetAllIds();
  absl::flat_hash_set<absl::string_view> id_set(all_ids.begin(), all_ids.end());

  EXPECT_EQ(all_ids.size(), id_set.size());
}

TEST(HasId, ReturnsTrueForExistingIds) {
  for (absl::string_view id : StandardOverloadIds::GetAllIds()) {
    EXPECT_TRUE(StandardOverloadIds::HasId(id));
  }

  EXPECT_FALSE(StandardOverloadIds::HasId("foo"));
}

}  // namespace
}  // namespace cel
