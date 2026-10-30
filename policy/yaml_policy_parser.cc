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

#include "policy/yaml_policy_parser.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "common/source.h"
#include "internal/status_macros.h"
#include "policy/cel_policy.h"
#include "policy/cel_policy_parse_context.h"
#include "policy/cel_policy_parse_result.h"
#include "policy/cel_policy_parser.h"
#include "policy/internal/yaml_string_element_scanner.h"
#include "yaml-cpp/exceptions.h"
#include "yaml-cpp/mark.h"
#include "yaml-cpp/node/node.h"
#include "yaml-cpp/node/parse.h"
#include "yaml-cpp/null.h"
#include "yaml-cpp/yaml.h"  // IWYU pragma: keep

namespace cel {
namespace {

SourcePosition GetMarkCodepointPosition(const CelPolicyParseContext& ctx,
                                        const YAML::Mark& mark) {
  if (mark.is_null() || mark.pos < 0 ||
      ctx.policy_source().content() == nullptr) {
    return -1;
  }
  const Source& source = *ctx.policy_source().content();
  return source.GetPositionFromByteOffset(mark.pos).value_or(
      source.content().size());
}

}  // namespace

CelPolicyElementId YamlPolicyParser::CollectMetadata(
    CelPolicyParseContext& ctx, const YAML::Node& node) const {
  CelPolicyElementId element_id = ctx.next_element_id();
  if (!node.Mark().is_null()) {
    ctx.policy_source().NoteSourcePosition(
        element_id, GetMarkCodepointPosition(ctx, node.Mark()));
  }
  return element_id;
}

std::optional<ValueString> YamlPolicyParser::GetValueString(
    CelPolicyParseContext& ctx, const YAML::Node& node,
    std::string_view error_message) const {
  if (!node.IsDefined()) {
    // This should never happen since the YAML syntax has already been checked.
    return std::nullopt;
  }

  CelPolicyElementId id = CollectMetadata(ctx, node);
  if (!node.IsScalar()) {
    ctx.ReportError(id, error_message);
    return std::nullopt;
  }

  if (!node.Mark().is_null() && ctx.policy_source().content() != nullptr) {
    SourcePosition codepoint_pos = GetMarkCodepointPosition(ctx, node.Mark());
    policy_internal::YamlStringElement element =
        policy_internal::ScanYamlStringElement(
            ctx.policy_source().content()->content(), codepoint_pos,
            node.as<std::string>());

    ctx.policy_source().NoteSourcePosition(id, element.starting_position);
    ctx.policy_source().NoteSourceRange(id, element.source_range,
                                        element.quoted);
  }

  try {
    return ValueString(id, node.as<std::string>());
  } catch (YAML::Exception& e) {
    // This should never happen since we already checked that the node is a
    // scalar and all scalars can be converted to strings.
    return std::nullopt;
  }
}

absl::Status YamlPolicyParser::ParsePolicy(CelPolicyParseContext& ctx) const {
  const Source* source = ctx.policy_source().content();
  if (source == nullptr) {
    return absl::OkStatus();
  }

  ctx.policy().set_description(ValueString(-1, source->description()));
  YAML::Node node;
  try {
    node = YAML::Load(source->content().ToString());
  } catch (YAML::Exception& e) {
    if (!e.mark.is_null()) {
      ctx.policy_source().NoteSourcePosition(
          0, GetMarkCodepointPosition(ctx, e.mark));
    }
    ctx.ReportError(0, "Invalid CEL policy YAML syntax");
    return absl::OkStatus();
  }

  if (!node.IsMap()) {
    ctx.ReportError(CollectMetadata(ctx, node), "Policy is not a map");
    return absl::OkStatus();
  }

  for (auto it = node.begin(); it != node.end(); ++it) {
    const YAML::Node key_node = it->first;
    const YAML::Node value_node = it->second;
    std::optional<ValueString> key =
        GetValueString(ctx, key_node, "Policy tag is not a string");
    if (!key.has_value()) {
      continue;
    }
    CEL_ASSIGN_OR_RETURN(bool handled, ParsePolicyTag(ctx, *key, value_node));
    if (!handled) {
      ctx.ReportError(
          key->id(),
          absl::StrCat("Unrecognized top-level policy tag: ", key->value()));
    }
  }

  return absl::OkStatus();
}

absl::StatusOr<bool> YamlPolicyParser::ParsePolicyTag(
    CelPolicyParseContext& ctx, const ValueString& tag_name,
    const YAML::Node& node) const {
  if (tag_name.value() == "imports") {
    CEL_RETURN_IF_ERROR(ParseImports(ctx, node));
    return true;
  }
  if (tag_name.value() == "name") {
    std::optional<ValueString> name =
        GetValueString(ctx, node, "Policy 'name' is not a string");
    if (name.has_value()) {
      ctx.policy().set_name(*name);
    }
    return true;
  }
  if (tag_name.value() == "description") {
    std::optional<ValueString> description =
        GetValueString(ctx, node, "Policy 'description' is not a string");
    if (description.has_value()) {
      ctx.policy().set_description(*description);
    }
    return true;
  }
  if (tag_name.value() == "display_name") {
    std::optional<ValueString> display_name =
        GetValueString(ctx, node, "Policy 'display_name' is not a string");
    if (display_name.has_value()) {
      ctx.policy().set_display_name(*display_name);
    }
    return true;
  }
  if (tag_name.value() == "rule") {
    CEL_RETURN_IF_ERROR(ParseRule(ctx, node, ctx.policy().mutable_rule()));
    return true;
  }
  return false;
}

absl::Status YamlPolicyParser::ParseImports(CelPolicyParseContext& ctx,
                                            const YAML::Node& node) const {
  if (!node.IsSequence()) {
    ctx.ReportError(CollectMetadata(ctx, node),
                    "Policy 'imports' is not a sequence");
    return absl::OkStatus();
  }

  for (const YAML::Node& import : node) {
    CelPolicyElementId import_id = CollectMetadata(ctx, import);
    if (!import.IsMap()) {
      ctx.ReportError(import_id, "Import is not a map");
      continue;
    }
    const YAML::Node& name_node = import["name"];
    if (!name_node.IsDefined()) {
      ctx.ReportError(import_id, "No 'name' tag in import");
      continue;
    }
    std::optional<ValueString> import_name =
        GetValueString(ctx, name_node, "Import name is not a string");
    if (import_name.has_value()) {
      ctx.policy().mutable_imports().push_back(Import(import_id, *import_name));
    }
  }
  return absl::OkStatus();
}

absl::Status YamlPolicyParser::ParseRule(CelPolicyParseContext& ctx,
                                         const YAML::Node& node,
                                         Rule& rule) const {
  if (!node.IsMap()) {
    ctx.ReportError(CollectMetadata(ctx, node), "Policy 'rule' is not a map");
    return absl::OkStatus();
  }
  rule.set_id(CollectMetadata(ctx, node));

  for (auto it = node.begin(); it != node.end(); ++it) {
    const YAML::Node key_node = it->first;
    const YAML::Node value_node = it->second;
    std::optional<ValueString> key =
        GetValueString(ctx, key_node, "Policy rule tag is not a string");
    if (!key.has_value()) {
      continue;
    }
    CEL_ASSIGN_OR_RETURN(bool handled,
                         ParseRuleTag(ctx, *key, value_node, rule));
    if (!handled) {
      ctx.ReportError(key->id(), absl::StrCat("Unrecognized policy rule tag: ",
                                              key->value()));
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<bool> YamlPolicyParser::ParseRuleTag(CelPolicyParseContext& ctx,
                                                    const ValueString& tag_name,
                                                    const YAML::Node& node,
                                                    Rule& rule) const {
  if (tag_name.value() == "id") {
    std::optional<ValueString> rule_id =
        GetValueString(ctx, node, "Policy rule 'id' is not a string");
    if (rule_id.has_value()) {
      rule.set_rule_id(*rule_id);
    }
    return true;
  }
  if (tag_name.value() == "description") {
    std::optional<ValueString> description =
        GetValueString(ctx, node, "Policy rule 'description' is not a string");
    if (description.has_value()) {
      rule.set_description(*description);
    }
    return true;
  }
  if (tag_name.value() == "variables") {
    if (!node.IsSequence()) {
      ctx.ReportError(CollectMetadata(ctx, node),
                      "Policy rule 'variables' is not a sequence");
      return true;
    }
    for (const YAML::Node& variable_node : node) {
      CEL_ASSIGN_OR_RETURN(Variable variable,
                           ParseVariable(ctx, variable_node, rule));
      rule.mutable_variables().push_back(std::move(variable));
    }
    return true;
  }
  if (tag_name.value() == "match" || tag_name.value() == "aggregate") {
    const bool is_aggregate = (tag_name.value() == "aggregate");
    if (!rule.matches().empty() || (rule.is_aggregate() && !is_aggregate)) {
      ctx.ReportError(
          tag_name.id(),
          "Only one of 'match' or 'aggregate' may be set in a rule");
      return true;
    }
    rule.set_is_aggregate(is_aggregate);
    if (!node.IsSequence()) {
      ctx.ReportError(CollectMetadata(ctx, node),
                      absl::StrCat("Policy rule '", tag_name.value(),
                                   "' is not a sequence"));
      return true;
    }
    for (const YAML::Node& match_node : node) {
      CEL_ASSIGN_OR_RETURN(Match match, ParseMatch(ctx, match_node, rule));
      rule.mutable_matches().push_back(std::move(match));
    }
    return true;
  }
  return false;
}

absl::StatusOr<Variable> YamlPolicyParser::ParseVariable(
    CelPolicyParseContext& ctx, const YAML::Node& node, Rule& rule) const {
  Variable variable;
  if (!node.IsMap()) {
    ctx.ReportError(CollectMetadata(ctx, node),
                    "Policy rule 'variable' is not a map");
    return variable;
  }
  for (auto it = node.begin(); it != node.end(); ++it) {
    const YAML::Node key_node = it->first;
    const YAML::Node value_node = it->second;
    std::optional<ValueString> key =
        GetValueString(ctx, key_node, "Policy variable tag is not a string");
    if (!key.has_value()) {
      continue;
    }
    CEL_ASSIGN_OR_RETURN(bool handled,
                         ParseVariableTag(ctx, *key, value_node, variable));
    if (!handled) {
      ctx.ReportError(
          key->id(),
          absl::StrCat("Unrecognized policy variable tag: ", key->value()));
    }
  }
  return variable;
}

absl::StatusOr<bool> YamlPolicyParser::ParseVariableTag(
    CelPolicyParseContext& ctx, const ValueString& tag_name,
    const YAML::Node& node, Variable& variable) const {
  if (tag_name.value() == "name") {
    std::optional<ValueString> name =
        GetValueString(ctx, node, "Policy variable 'name' is not a string");
    if (name.has_value()) {
      variable.set_name(*name);
    }
    return true;
  }
  if (tag_name.value() == "expression") {
    std::optional<ValueString> expression = GetValueString(
        ctx, node, "Policy variable 'expression' is not a string");
    if (expression.has_value()) {
      variable.set_expression(*expression);
    }
    return true;
  }
  return false;
}

absl::StatusOr<Match> YamlPolicyParser::ParseMatch(CelPolicyParseContext& ctx,
                                                   const YAML::Node& node,
                                                   Rule& rule) const {
  Match match;
  match.set_id(CollectMetadata(ctx, node));
  if (!node.IsMap()) {
    ctx.ReportError(match.id(), "Policy rule 'match' is not a map");
    return match;
  }
  for (auto it = node.begin(); it != node.end(); ++it) {
    const YAML::Node key_node = it->first;
    const YAML::Node value_node = it->second;
    std::optional<ValueString> key =
        GetValueString(ctx, key_node, "Policy match tag is not a string");
    if (!key.has_value()) {
      continue;
    }
    CEL_ASSIGN_OR_RETURN(bool handled,
                         ParseMatchTag(ctx, *key, value_node, match, rule));
    if (!handled) {
      ctx.ReportError(key->id(), absl::StrCat("Unrecognized policy match tag: ",
                                              key->value()));
    }
  }

  if (match.has_output_block()) {
    if (match.output_block().output().value().empty() &&
        match.output_block().explanation().has_value()) {
      ctx.ReportError(match.id(), "Match specifies explanation but no output");
    }
  }

  return match;
}

absl::StatusOr<bool> YamlPolicyParser::ParseMatchTag(
    CelPolicyParseContext& ctx, const ValueString& tag_name,
    const YAML::Node& node, Match& match, Rule& rule) const {
  if (tag_name.value() == "condition") {
    std::optional<ValueString> condition =
        GetValueString(ctx, node, "Policy match 'condition' is not a string");
    if (condition.has_value()) {
      match.set_condition(*condition);
    }
    return true;
  }
  if (tag_name.value() == "explanation") {
    std::optional<ValueString> explanation =
        GetValueString(ctx, node, "Policy match 'explanation' is not a string");
    if (explanation.has_value()) {
      if (match.has_rule()) {
        ctx.ReportError(
            tag_name.id(),
            "Cannot specify explanation when a nested rule is present");
      } else {
        match.mutable_output_block().set_explanation(*explanation);
      }
    }
    return true;
  }
  if (tag_name.value() == "output") {
    std::optional<ValueString> output =
        GetValueString(ctx, node, "Policy match 'output' is not a string");
    if (output.has_value()) {
      if (match.has_rule()) {
        ctx.ReportError(tag_name.id(),
                        "Cannot specify output when a nested rule is present");
      } else {
        match.mutable_output_block().set_output(*output);
      }
    }
    return true;
  }
  if (tag_name.value() == "rule") {
    if (match.has_output_block()) {
      ctx.ReportError(tag_name.id(),
                      "Cannot specify nested rule when output/explanation is "
                      "present");
    }
    auto nested_rule = std::make_unique<Rule>();
    CEL_RETURN_IF_ERROR(ParseRule(ctx, node, *nested_rule));
    match.set_result(std::move(nested_rule));
    return true;
  }
  return false;
}

const CelPolicyParser<YAML::Node>& GetDefaultYamlPolicyParser() {
  static const auto* const parser = new YamlPolicyParser();
  return *parser;
}

absl::StatusOr<CelPolicyParseResult> ParseYamlCelPolicy(
    std::shared_ptr<CelPolicySource> policy_source) {
  return ParseYamlCelPolicy(std::move(policy_source),
                            GetDefaultYamlPolicyParser());
}

absl::StatusOr<CelPolicyParseResult> ParseYamlCelPolicy(
    std::shared_ptr<CelPolicySource> policy_source,
    const CelPolicyParser<YAML::Node>& parser) {
  CelPolicyParseContext ctx(std::move(policy_source));
  CEL_RETURN_IF_ERROR(parser.ParsePolicy(ctx));
  return ctx.GetResult();
}

}  // namespace cel
