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

#ifndef THIRD_PARTY_CEL_CPP_CONFORMANCE_ENV_YAML_MATCHER_H_
#define THIRD_PARTY_CEL_CPP_CONFORMANCE_ENV_YAML_MATCHER_H_

#include "absl/strings/string_view.h"
#include "internal/testing.h"

namespace cel {

// Returns a matcher that parses both the actual and expected YAML strings and
// recursively compares the resulting YAML node trees for semantic equality
// (ignoring formatting, comments, quoting style, and map key ordering).
// Map keys must be unique scalars; non-scalar or duplicate map keys are
// unsupported and fail to match.
testing::Matcher<absl::string_view> EqualsYaml(absl::string_view expected_yaml);

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_CONFORMANCE_ENV_YAML_MATCHER_H_
