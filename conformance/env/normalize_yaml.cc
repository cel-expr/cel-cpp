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

#include "conformance/env/normalize_yaml.h"

#include <cstddef>
#include <string>
#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"

namespace cel {
namespace {

// Unquotes a single- or double-quoted YAML scalar value so that cosmetic
// quoting differences (e.g. `int` vs `"int"` vs `'int'`, or `"~T\\.U"` vs
// `'~T\.U'`) normalize to the same string while preserving the exact scalar
// contents (including any internal whitespace).
std::string UnquoteYamlScalar(absl::string_view value) {
  // Double-quoted scalars:
  //   "int" -> int
  //   "a\"b\\c" -> a"b\c
  //   "~T\\.U" -> ~T\.U
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
    absl::string_view inner = value.substr(1, value.size() - 2);
    std::string out;
    out.reserve(inner.size());
    for (size_t i = 0; i < inner.size(); ++i) {
      if (inner[i] == '\\' && i + 1 < inner.size()) {
        ++i;
        switch (inner[i]) {
          case '\\':
            out.push_back('\\');
            break;
          case '"':
            out.push_back('"');
            break;
          case 'n':
            out.push_back('\n');
            break;
          case 'r':
            out.push_back('\r');
            break;
          case 't':
            out.push_back('\t');
            break;
          default:
            out.push_back('\\');
            out.push_back(inner[i]);
            break;
        }
      } else {
        out.push_back(inner[i]);
      }
    }
    return out;
  }

  // Single-quoted scalars:
  //   'int' -> int
  //   'it''s' -> it's
  //   '~T\.U' -> ~T\.U
  if (value.size() >= 2 && value.front() == '\'' && value.back() == '\'') {
    absl::string_view inner = value.substr(1, value.size() - 2);
    std::string out;
    out.reserve(inner.size());
    for (size_t i = 0; i < inner.size(); ++i) {
      if (inner[i] == '\'' && i + 1 < inner.size() && inner[i + 1] == '\'') {
        out.push_back('\'');
        ++i;
      } else {
        out.push_back(inner[i]);
      }
    }
    return out;
  }

  // Unquoted scalars are returned verbatim, e.g. int -> int.
  return std::string(value);
}

}  // namespace

std::string NormalizeYaml(absl::string_view yaml) {
  std::vector<std::string> normalized_lines;
  for (absl::string_view line : absl::StrSplit(yaml, '\n')) {
    // Normalize CRLF -> LF and strip trailing spaces/tabs.
    absl::ConsumeSuffix(&line, "\r");
    while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) {
      line.remove_suffix(1);
    }
    if (line.empty()) {
      continue;
    }
    size_t colon_space = line.find(": ");
    if (colon_space != absl::string_view::npos) {
      // Mapping entry with a scalar value:
      //   type: "int" -> type: int
      //   type:   'bool' -> type: bool
      absl::string_view key_prefix = line.substr(0, colon_space + 2);
      absl::string_view value = line.substr(key_prefix.size());
      while (!value.empty() && value.front() == ' ') {
        value.remove_prefix(1);
      }
      normalized_lines.push_back(
          absl::StrCat(key_prefix, UnquoteYamlScalar(value)));
    } else {
      // `line` is non-empty and has no trailing spaces, so `first_non_space` is
      // guaranteed to be a valid index.
      size_t first_non_space = line.find_first_not_of(' ');
      if (absl::StartsWith(line.substr(first_non_space), "- ")) {
        // Sequence item with a scalar value:
        //   - "int" -> - int
        //   - 'bool' -> - bool
        absl::string_view item_prefix = line.substr(0, first_non_space + 2);
        absl::string_view value = line.substr(item_prefix.size());
        while (!value.empty() && value.front() == ' ') {
          value.remove_prefix(1);
        }
        normalized_lines.push_back(
            absl::StrCat(item_prefix, UnquoteYamlScalar(value)));
      } else {
        // Structural lines such as `variables:` are kept as-is.
        normalized_lines.push_back(std::string(line));
      }
    }
  }
  return absl::StrJoin(normalized_lines, "\n");
}

}  // namespace cel
