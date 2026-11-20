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

#ifndef THIRD_PARTY_CEL_CPP_CONFORMANCE_ENV_NORMALIZE_YAML_H_
#define THIRD_PARTY_CEL_CPP_CONFORMANCE_ENV_NORMALIZE_YAML_H_

#include <string>

#include "absl/strings/string_view.h"

namespace cel {

// Performs textual normalization on properly formatted YAML:
// - Normalizes CRLF line endings and trims trailing whitespace/blank lines.
// - Unquotes scalar values on `key: value` and `- value` sequence item lines
//   (e.g., `type: "int"` -> `type: int`, `- "int"` -> `- int`) so quoting
//   style differences do not cause false mismatches while preserving exact
//   scalar values.
std::string NormalizeYaml(absl::string_view yaml);

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_CONFORMANCE_ENV_NORMALIZE_YAML_H_
