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

#ifndef THIRD_PARTY_CEL_CPP_POLICY_CEL_POLICY_H_
#define THIRD_PARTY_CEL_CPP_POLICY_CEL_POLICY_H_

#include <any>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/absl_check.h"
#include "absl/strings/string_view.h"
#include "common/source.h"

namespace cel {

using CelPolicyElementId = int32_t;

struct ElementSourceInfo {
  SourcePosition position = -1;
  std::optional<SourceRange> range;
  bool quoted = false;
};

class CelPolicySource {
 public:
  explicit CelPolicySource(cel::SourcePtr policy_source)
      : policy_source_(std::move(policy_source)) {}

  const Source* absl_nonnull content() const { return policy_source_.get(); }

  void NoteSourcePosition(CelPolicyElementId id, SourcePosition position);

  void NoteSourceRange(CelPolicyElementId id, std::optional<SourceRange> range,
                       bool quoted);

  std::optional<SourcePosition> GetSourcePosition(CelPolicyElementId id) const;

  std::optional<SourceRange> GetSourceRange(CelPolicyElementId id) const;

  std::optional<bool> IsQuoted(CelPolicyElementId id) const;

  std::optional<SourceLocation> GetSourceLocation(CelPolicyElementId id) const;

  std::string DebugString() const;

 private:
  cel::SourcePtr policy_source_;
  absl::flat_hash_map<CelPolicyElementId, ElementSourceInfo> source_info_;
};

class ValueString {
 public:
  ValueString() : id_(-1) {}

  explicit ValueString(CelPolicyElementId id, absl::string_view value)
      : id_(id), value_(value) {}

  CelPolicyElementId id() const { return id_; }
  absl::string_view value() const { return value_; }

  std::string DebugString() const;

 private:
  CelPolicyElementId id_;
  std::string value_;
};

class Import {
 public:
  Import(CelPolicyElementId id, ValueString name)
      : id_(id), name_(std::move(name)) {}
  CelPolicyElementId id() const { return id_; }
  const ValueString& name() const { return name_; }

  std::string DebugString() const;

 private:
  CelPolicyElementId id_;
  ValueString name_;
};

// Defines a variable that can be used in CEL expressions within the policy.
// Variables are evaluated once and stored in the activation context.
class Variable {
 public:
  const ValueString& name() const { return name_; }
  void set_name(ValueString name) { name_ = std::move(name); }

  const ValueString& expression() const { return expression_; }
  void set_expression(ValueString expression) {
    expression_ = std::move(expression);
  }

  std::optional<ValueString> description() const { return description_; }
  void set_description(ValueString description) {
    description_ = std::move(description);
  }

  std::optional<ValueString> display_name() const { return display_name_; }
  void set_display_name(ValueString display_name) {
    display_name_ = std::move(display_name);
  }

  std::string DebugString() const;

 private:
  ValueString name_;
  ValueString expression_;
  std::optional<ValueString> description_;
  std::optional<ValueString> display_name_;
};

class Rule;

class OutputBlock {
 public:
  OutputBlock() = default;
  OutputBlock(ValueString output, std::optional<ValueString> explanation)
      : output_(std::move(output)), explanation_(std::move(explanation)) {}

  const ValueString& output() const { return output_; }
  void set_output(ValueString output) { output_ = std::move(output); }

  const std::optional<ValueString>& explanation() const { return explanation_; }
  void set_explanation(ValueString explanation) {
    explanation_ = std::move(explanation);
  }

  std::string DebugString() const;

 private:
  ValueString output_;
  std::optional<ValueString> explanation_;
};

// Defines a match condition and result.
// If the result is a Rule, it is considered a sub-rule and will be evaluated
// only if the match condition evaluates to true.
class Match {
 public:
  Match() = default;
  Match(const Match& other);
  Match& operator=(const Match& other);

  CelPolicyElementId id() const;
  void set_id(CelPolicyElementId id);

  bool has_condition() const;
  std::optional<ValueString> condition() const;
  void set_condition(ValueString condition);

  bool has_output_block() const;
  const OutputBlock& output_block() const;
  OutputBlock& mutable_output_block();

  bool has_rule() const;
  const Rule& rule() const;
  Rule& mutable_rule();

  void set_result(OutputBlock result);
  void set_result(std::unique_ptr<Rule> result);

  std::string DebugString() const;

 private:
  CelPolicyElementId id_ = -1;
  std::optional<ValueString> condition_;
  std::variant<std::monostate, OutputBlock, std::unique_ptr<Rule>> result_;
};

// Rule is the body of the policy and contains a list of variables and matches.
// Variables are evaluated once and stored in the evaluation context.
//
// Rule kinds:
//
// First-match rule (default):
// Matches are evaluated in order and the first match is returned. If the
// match contains a sub-rule, the sub-rule is evaluated only if the match
// condition evaluates to true.
//
// Aggregate rule:
// All matches are evaluated and the results are aggregated into a list. If the
// match condition holds, it is added to the aggregate result.
// There can only be one aggregate rule per policy.
class Rule {
 public:
  Rule() = default;
  Rule(const Rule& other) = default;

  CelPolicyElementId id() const { return id_; }
  void set_id(CelPolicyElementId id) { id_ = id; }

  const std::optional<ValueString>& rule_id() const { return rule_id_; }
  void set_rule_id(ValueString rule_id) { rule_id_ = std::move(rule_id); }

  const std::optional<ValueString>& description() const { return description_; }
  void set_description(ValueString description) {
    description_ = std::move(description);
  }

  const std::vector<Variable>& variables() const { return variables_; }
  std::vector<Variable>& mutable_variables() { return variables_; }

  const std::vector<Match>& matches() const { return matches_; }
  std::vector<Match>& mutable_matches() { return matches_; }

  bool is_aggregate() const { return is_aggregate_; }
  void set_is_aggregate(bool is_aggregate) { is_aggregate_ = is_aggregate; }

  std::string DebugString() const;

 private:
  CelPolicyElementId id_ = -1;
  std::optional<ValueString> rule_id_;
  std::optional<ValueString> description_;
  std::vector<Variable> variables_;
  std::vector<Match> matches_;
  bool is_aggregate_ = false;
};

// CelPolicy is the top-level policy object.
// It contains a source, name, description, display name, imports, and a rule.
// The source is the CEL policy source code.
// The name, description, and display name are metadata about the policy.
// The rule is the main body of the policy.
class CelPolicy {
 public:
  explicit CelPolicy(std::shared_ptr<CelPolicySource> source)
      : source_(std::move(source)) {}

  CelPolicy(const CelPolicy& other) = default;
  CelPolicy& operator=(const CelPolicy& other) = default;

  const CelPolicySource* absl_nullable source() const { return source_.get(); }
  const std::shared_ptr<CelPolicySource>& source_ptr() const { return source_; }

  const ValueString& name() const { return name_; }
  void set_name(ValueString name) { name_ = std::move(name); }

  std::optional<ValueString> description() const { return description_; }
  void set_description(ValueString description) {
    description_ = std::move(description);
  }
  std::optional<ValueString> display_name() const { return display_name_; }
  void set_display_name(ValueString display_name) {
    display_name_ = std::move(display_name);
  }
  const absl::flat_hash_map<std::string, std::any>& metadata() const {
    return metadata_;
  }
  absl::flat_hash_map<std::string, std::any>& mutable_metadata() {
    return metadata_;
  }
  const std::vector<Import>& imports() const { return imports_; }
  std::vector<Import>& mutable_imports() { return imports_; }

  const Rule& rule() const { return rule_; }
  Rule& mutable_rule() { return rule_; }

  std::string DebugString() const;

 private:
  std::shared_ptr<CelPolicySource> source_;
  ValueString name_;
  std::optional<ValueString> description_;
  std::optional<ValueString> display_name_;
  absl::flat_hash_map<std::string, std::any> metadata_;
  std::vector<Import> imports_;
  Rule rule_;
};

// Implementation details.

inline CelPolicyElementId Match::id() const { return id_; }
inline void Match::set_id(CelPolicyElementId id) { id_ = id; }

inline bool Match::has_condition() const { return condition_.has_value(); }

inline std::optional<ValueString> Match::condition() const {
  return condition_;
}

inline void Match::set_condition(ValueString condition) {
  condition_ = std::move(condition);
}

inline bool Match::has_output_block() const {
  return std::holds_alternative<OutputBlock>(result_);
}

inline const OutputBlock& Match::output_block() const {
  ABSL_DCHECK(std::holds_alternative<OutputBlock>(result_));
  return std::get<OutputBlock>(result_);
}

inline OutputBlock& Match::mutable_output_block() {
  if (!std::holds_alternative<OutputBlock>(result_)) {
    result_ = OutputBlock();
  }
  return std::get<OutputBlock>(result_);
}

inline bool Match::has_rule() const {
  return std::holds_alternative<std::unique_ptr<Rule>>(result_);
}

inline const Rule& Match::rule() const {
  ABSL_DCHECK(std::holds_alternative<std::unique_ptr<Rule>>(result_));
  return *std::get<std::unique_ptr<Rule>>(result_);
}

inline Rule& Match::mutable_rule() {
  ABSL_DCHECK(std::holds_alternative<std::unique_ptr<Rule>>(result_));
  return *std::get<std::unique_ptr<Rule>>(result_);
}

inline void Match::set_result(OutputBlock result) {
  result_ = std::move(result);
}

inline void Match::set_result(std::unique_ptr<Rule> result) {
  result_ = std::move(result);
}

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_POLICY_CEL_POLICY_H_
