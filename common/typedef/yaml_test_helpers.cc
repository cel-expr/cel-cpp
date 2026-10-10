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

#include "common/typedef/yaml_test_helpers.h"

#include <cstddef>
#include <string>
#include <vector>

#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

namespace cel {

std::string Unindent(absl::string_view yaml) {
  std::vector<std::string> lines = absl::StrSplit(yaml, '\n');
  int indent = -1;
  std::vector<std::string> unindented_lines;
  for (const auto& line : lines) {
    std::size_t pos = line.find_first_not_of(" \t");
    if (pos == std::string::npos) {
      continue;
    }
    if (indent == -1) {
      indent = pos;
    }
    if (static_cast<int>(pos) >= indent) {
      unindented_lines.push_back(line.substr(indent));
    } else {
      unindented_lines.push_back(line);
    }
  }
  return absl::StrJoin(unindented_lines, "\n");
}

}  // namespace cel
