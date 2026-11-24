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

#ifndef THIRD_PARTY_CEL_CPP_POLICY_YAML_POLICY_PARSER_H_
#define THIRD_PARTY_CEL_CPP_POLICY_YAML_POLICY_PARSER_H_

#include <memory>
#include <optional>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "policy/cel_policy.h"
#include "policy/cel_policy_parse_context.h"
#include "policy/cel_policy_parse_result.h"
#include "policy/cel_policy_parser.h"
#include "yaml-cpp/node/node.h"

namespace cel {

// A parser for YAML-based CEL policies.
//
// Security note: Only pass trusted (or vetted, semi-trusted) YAML input.
// `YamlPolicyParser` delegates YAML parsing to `yaml-cpp` and is not hardened
// against malicious YAML inputs. See SECURITY.md for the full threat model.
//
// To support additional or alternative YAML elements, subclass
// `YamlPolicyParser` and override specific parsing methods, `Parse*`
class YamlPolicyParser : public CelPolicyParser<YAML::Node> {
 public:
  std::optional<ValueString> GetValueString(
      CelPolicyParseContext& ctx, const YAML::Node& node,
      std::string_view error_message) const;

  absl::Status ParsePolicy(CelPolicyParseContext& ctx) const override;

 protected:
  // Collects metadata (e.g. source position) for the given YAML node, stores it
  // in the context, and returns an ID that can be used to refer to it.
  virtual CelPolicyElementId CollectMetadata(CelPolicyParseContext& ctx,
                                             const YAML::Node& node) const;

  // Parses a top-level tag in the policy YAML.
  // Returns true if the tag was handled.
  //
  // Note that an OkStatus does not necessarily mean that parsing was successful
  // - only that it can continue.
  virtual absl::StatusOr<bool> ParsePolicyTag(CelPolicyParseContext& ctx,
                                              const ValueString& tag_name,
                                              const YAML::Node& node) const;

  // Parses the imports section of the policy YAML.
  //
  // Note that an OkStatus does not necessarily mean that parsing was successful
  // - only that it can continue.
  virtual absl::Status ParseImports(CelPolicyParseContext& ctx,
                                    const YAML::Node& node) const;

  // Parses a rule element of the policy YAML, which may be the top-level rule
  // or a sub-rule of a match.
  //
  // Note that an OkStatus does not necessarily mean that parsing was successful
  // - only that it can continue.
  virtual absl::Status ParseRule(CelPolicyParseContext& ctx,
                                 const YAML::Node& node, Rule& rule) const;

  // Parses a tag in a policy YAML rule.
  // Returns true if the tag was handled.
  //
  // Note that an OkStatus does not necessarily mean that parsing was successful
  // - only that it can continue.
  virtual absl::StatusOr<bool> ParseRuleTag(CelPolicyParseContext& ctx,
                                            const ValueString& tag_name,
                                            const YAML::Node& node,
                                            Rule& rule) const;

  // Parses a variable element of the policy YAML.
  //
  // Note that an OkStatus does not necessarily mean that parsing was successful
  // - only that it can continue.
  virtual absl::StatusOr<Variable> ParseVariable(CelPolicyParseContext& ctx,
                                                 const YAML::Node& node,
                                                 Rule& rule) const;

  // Parses a tag in a policy YAML variable.
  // Returns true if the tag was handled.
  //
  // Note that an OkStatus does not necessarily mean that parsing was successful
  // - only that it can continue.
  virtual absl::StatusOr<bool> ParseVariableTag(CelPolicyParseContext& ctx,
                                                const ValueString& tag_name,
                                                const YAML::Node& node,
                                                Variable& variable) const;

  // Parses a match element of the policy YAML.
  //
  // Note that an OkStatus does not necessarily mean that parsing was successful
  // - only that it can continue.
  virtual absl::StatusOr<Match> ParseMatch(CelPolicyParseContext& ctx,
                                           const YAML::Node& node,
                                           Rule& rule) const;

  // Parses a tag in a policy YAML match.
  // Returns true if the tag was handled.
  //
  // Note that an OkStatus does not necessarily mean that parsing was successful
  // - only that it can continue.
  virtual absl::StatusOr<bool> ParseMatchTag(CelPolicyParseContext& ctx,
                                             const ValueString& tag_name,
                                             const YAML::Node& node,
                                             Match& match, Rule& rule) const;
};

// Returns a default implementation of YamlPolicyParser.
const CelPolicyParser<YAML::Node>& GetDefaultYamlPolicyParser();

absl::StatusOr<CelPolicyParseResult> ParseYamlCelPolicy(
    std::shared_ptr<CelPolicySource> policy_source,
    const CelPolicyParser<YAML::Node>& parser);

// YAML CelPolicy parser that uses the default format as implemented by
// `YamlPolicyParser`.
absl::StatusOr<CelPolicyParseResult> ParseYamlCelPolicy(
    std::shared_ptr<CelPolicySource> policy_source);

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_POLICY_YAML_POLICY_PARSER_H_
