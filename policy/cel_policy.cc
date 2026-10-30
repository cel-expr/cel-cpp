// Copyright 2026 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "policy/cel_policy.h"

#include <algorithm>
#include <any>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "common/source.h"

namespace cel {

namespace {

std::string IdDebugString(CelPolicyElementId id) {
  if (id == -1) {
    return "";
  }
  return absl::StrCat("#", id, "> ");
}

std::string IndentBlock(absl::string_view text) {
  if (text.empty()) {
    return "";
  }
  std::vector<std::string> lines;
  for (absl::string_view line : absl::StrSplit(text, '\n')) {
    if (line.empty()) {
      lines.push_back("");
    } else {
      lines.push_back(absl::StrCat("  ", line));
    }
  }
  return absl::StrJoin(lines, "\n");
}

}  // namespace

void CelPolicySource::NoteSourcePosition(CelPolicyElementId id,
                                         SourcePosition position) {
  source_info_[id].position = position;
}

void CelPolicySource::NoteSourceRange(CelPolicyElementId id,
                                      std::optional<SourceRange> range,
                                      bool quoted) {
  ElementSourceInfo& info = source_info_[id];
  info.range = range;
  info.quoted = quoted;
  if (range.has_value() && info.position == -1) {
    info.position = range->begin;
  }
}

std::optional<SourcePosition> CelPolicySource::GetSourcePosition(
    CelPolicyElementId id) const {
  auto it = source_info_.find(id);
  if (it == source_info_.end() || it->second.position == -1) {
    return std::nullopt;
  }
  return it->second.position;
}

std::optional<SourceRange> CelPolicySource::GetSourceRange(
    CelPolicyElementId id) const {
  auto it = source_info_.find(id);
  if (it == source_info_.end() || !it->second.range.has_value()) {
    return std::nullopt;
  }
  return it->second.range;
}

std::optional<bool> CelPolicySource::IsQuoted(CelPolicyElementId id) const {
  auto it = source_info_.find(id);
  if (it == source_info_.end() || !it->second.range.has_value()) {
    return std::nullopt;
  }
  return it->second.quoted;
}

std::optional<SourceLocation> CelPolicySource::GetSourceLocation(
    CelPolicyElementId id) const {
  auto it = source_info_.find(id);
  if (it == source_info_.end() || it->second.position == -1) {
    return std::nullopt;
  }
  return policy_source_->GetLocation(it->second.position);
}

std::string CelPolicySource::DebugString() const {
  std::string result;

  // Sort the source elements in descending order of position
  std::vector<std::pair<CelPolicyElementId, SourcePosition>> sorted_positions;
  for (const auto& [id, info] : source_info_) {
    if (info.position != -1) {
      sorted_positions.push_back({id, info.position});
    }
  }
  std::sort(sorted_positions.begin(), sorted_positions.end(),
            [](const auto& a, const auto& b) {
              if (a.second == b.second) {
                return a.first < b.first;
              }
              return a.second > b.second;
            });

  result = policy_source_->content().ToString();
  for (const auto& [id, position] : sorted_positions) {
    result.insert(position, IdDebugString(id));
  }
  return result;
}

std::string ValueString::DebugString() const {
  return absl::StrCat(IdDebugString(id_), "\"", value_, "\"");
}

std::string Import::DebugString() const {
  std::string result;
  absl::StrAppend(&result, IdDebugString(id_), "name: ", name_.DebugString());
  return result;
}

std::string OutputBlock::DebugString() const {
  std::string result;
  absl::StrAppend(&result, "output: ", output_.DebugString());
  if (explanation_.has_value()) {
    absl::StrAppend(&result, "\nexplanation: ", explanation_->DebugString());
  }
  return result;
}

Match::Match(const Match& other)
    : id_(other.id_), condition_(other.condition_) {
  if (std::holds_alternative<std::monostate>(other.result_)) {
    result_ = std::get<std::monostate>(other.result_);
  } else if (std::holds_alternative<OutputBlock>(other.result_)) {
    result_ = std::get<OutputBlock>(other.result_);
  } else {
    result_ =
        std::make_unique<Rule>(*std::get<std::unique_ptr<Rule>>(other.result_));
  }
}

Match& Match::operator=(const Match& other) {
  if (this != &other) {
    id_ = other.id_;
    condition_ = other.condition_;
    if (std::holds_alternative<std::monostate>(other.result_)) {
      result_ = std::get<std::monostate>(other.result_);
    } else if (std::holds_alternative<OutputBlock>(other.result_)) {
      result_ = std::get<OutputBlock>(other.result_);
    } else {
      result_ = std::make_unique<Rule>(
          *std::get<std::unique_ptr<Rule>>(other.result_));
    }
  }
  return *this;
}

std::string Match::DebugString() const {
  std::string result;
  absl::StrAppend(&result, IdDebugString(id_), "match: {\n");
  if (condition_.has_value()) {
    absl::StrAppend(&result, "  condition: ", condition_->DebugString(), "\n");
  }
  if (has_rule()) {
    absl::StrAppend(&result, "  result:\n",
                    IndentBlock(IndentBlock(rule().DebugString())), "\n");
  } else {
    absl::StrAppend(&result, "  result: {\n",
                    IndentBlock(IndentBlock(output_block().DebugString())),
                    "\n  }\n");
  }
  absl::StrAppend(&result, "}");
  return result;
}

std::string Variable::DebugString() const {
  std::string result;
  absl::StrAppend(&result, "variable: {\n");
  absl::StrAppend(&result, "  name: ", name_.DebugString(), "\n");
  absl::StrAppend(&result, "  expression: ", expression_.DebugString(), "\n");
  if (description_.has_value()) {
    absl::StrAppend(&result, "  description: ", description_->DebugString(),
                    "\n");
  }
  if (display_name_.has_value()) {
    absl::StrAppend(&result, "  display_name: ", display_name_->DebugString(),
                    "\n");
  }
  absl::StrAppend(&result, "}");
  return result;
}

std::string Rule::DebugString() const {
  std::string result;
  absl::StrAppend(&result, IdDebugString(id_), "rule: {\n");
  if (rule_id_.has_value()) {
    absl::StrAppend(&result, "  rule_id: ", rule_id_->DebugString(), "\n");
  }
  if (description_.has_value()) {
    absl::StrAppend(&result, "  description: ", description_->DebugString(),
                    "\n");
  }
  if (is_aggregate_) {
    absl::StrAppend(&result, "  is_aggregate: true\n");
  }
  for (const Variable& variable : variables_) {
    absl::StrAppend(&result, IndentBlock(variable.DebugString()), "\n");
  }
  for (const Match& match : matches_) {
    absl::StrAppend(&result, IndentBlock(match.DebugString()), "\n");
  }
  absl::StrAppend(&result, "}");
  return result;
}

std::string MetadataValueDebugString(std::any value) {
  if (value.type() == typeid(std::monostate)) {
    return "null";
  }
  if (value.type() == typeid(ValueString)) {
    return std::any_cast<ValueString>(value).DebugString();
  }
  if (value.type() == typeid(bool)) {
    return std::any_cast<bool>(value) ? "true" : "false";
  }
  if (value.type() == typeid(int)) {
    return absl::StrCat(std::any_cast<int>(value));
  }
  if (value.type() == typeid(std::string)) {
    return std::any_cast<std::string>(value);
  }
  return absl::StrCat("typeid: ", value.type().name());
}

std::string CelPolicy::DebugString() const {
  std::string result;
  absl::StrAppend(&result, "CelPolicy{\n");
  absl::StrAppend(
      &result,
      "  ===========================================================\n");
  absl::StrAppend(&result, IndentBlock(IndentBlock(source_->DebugString())),
                  "\n");
  absl::StrAppend(
      &result,
      "  ===========================================================\n");
  absl::StrAppend(&result, "  name: ", name_.DebugString(), "\n");
  if (description_.has_value()) {
    absl::StrAppend(&result, "  description: ", description_->DebugString(),
                    "\n");
  }
  if (display_name_.has_value()) {
    absl::StrAppend(&result, "  display_name: ", display_name_->DebugString(),
                    "\n");
  }
  if (!metadata_.empty()) {
    std::vector<std::string> sorted_keys;
    for (const auto& [key, _] : metadata_) {
      sorted_keys.push_back(key);
    }
    std::sort(sorted_keys.begin(), sorted_keys.end());

    absl::StrAppend(&result, "  metadata: {\n");
    for (const auto& key : sorted_keys) {
      const auto& value = metadata_.at(key);
      absl::StrAppend(&result, "    ", key, ": ",
                      MetadataValueDebugString(value), "\n");
    }
    absl::StrAppend(&result, "  }\n");
  }
  if (!imports_.empty()) {
    absl::StrAppend(&result, "  imports:\n");
    for (const Import& import : imports_) {
      absl::StrAppend(&result, "    ", import.DebugString(), "\n");
    }
  }
  absl::StrAppend(&result, IndentBlock(rule_.DebugString()), "\n");
  absl::StrAppend(&result, "}");
  return result;
}

}  // namespace cel
