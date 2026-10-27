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

#include "policy/compiler.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/nullability.h"
#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "checker/type_check_issue.h"
#include "checker/validation_result.h"
#include "common/ast.h"
#include "common/ast_rewrite.h"
#include "common/constant.h"
#include "common/container.h"
#include "common/decl.h"
#include "common/expr.h"
#include "common/format_type_name.h"
#include "common/navigable_ast.h"
#include "common/source.h"
#include "common/type.h"
#include "common/type_kind.h"
#include "compiler/compiler.h"
#include "internal/status_macros.h"
#include "policy/cel_policy.h"
#include "policy/cel_policy_parse_result.h"
#include "policy/cel_policy_validation_result.h"
#include "policy/internal/issue_reporter.h"
#include "policy/internal/optimizer_expr_factory.h"
#include "google/protobuf/arena.h"

namespace cel {
namespace {

constexpr absl::string_view kCelBlock = "cel.@block";

enum class RuleSemantics {
  // TODO(b/506179116): will also need "aggregate" or similar concept.
  kFirstMatch,

  kNotForUseWithExhaustiveSwitchStatements,
};

template <typename Sink>
void AbslStringify(Sink& s, RuleSemantics semantics) {
  switch (semantics) {
    case RuleSemantics::kFirstMatch:
      s.Append("first_match");
      return;
    default:
      s.Append("<unspecified>");
      return;
  }
}

struct EmbeddedAst {
  CelPolicyElementId id;
  std::unique_ptr<Ast> ast;
};

struct CompiledVariable {
  std::string ident;
  EmbeddedAst ast;
};

struct CompiledOutputBlock {
  EmbeddedAst output_ast;
  cel::Type result_type;
  std::optional<EmbeddedAst> explanation_ast;
};

struct CompiledRule;

struct CompiledMatch {
  using Production =
      std::variant<std::monostate, std::unique_ptr<CompiledRule> absl_nonnull,
                   CompiledOutputBlock>;

  CelPolicyElementId id;
  std::optional<EmbeddedAst> condition;
  Production production;
};

struct CompiledRule {
  CelPolicyElementId id;
  std::vector<CompiledVariable> variables;
  std::vector<CompiledMatch> matches;
  // Not set if cannot be determined.
  std::optional<cel::Type> result_type;
};

std::optional<cel::Type> GetOutputType(
    const CompiledMatch::Production& production) {
  return std::visit(
      [](const auto& production) -> std::optional<cel::Type> {
        if constexpr (std::is_same_v<std::decay_t<decltype(production)>,
                                     CompiledOutputBlock>) {
          return production.result_type;
        } else if constexpr (std::is_same_v<std::decay_t<decltype(production)>,
                                            std::unique_ptr<CompiledRule>>) {
          return production->result_type;
        }
        return std::nullopt;
      },
      production);
}

// Internal representation of the compiled policy elements.
//
// This is used for checking the component expression before composing into the
// final AST based on the provided rule semantics.
class IntermediateCompiledPolicy {
 public:
  CompiledRule& mutable_root_rule() { return root_rule_; }

  const CompiledRule& root_rule() const { return root_rule_; }

  void set_name(absl::string_view name) { name_ = name; }
  absl::string_view name() const { return name_; }
  void set_display_name(absl::string_view display_name) {
    display_name_ = display_name;
  }
  absl::string_view display_name() const { return display_name_; }
  void set_description(absl::string_view description) {
    description_ = description;
  }
  absl::string_view description() const { return description_; }

  void set_semantics(RuleSemantics semantics) { semantics_ = semantics; }
  RuleSemantics semantics() const { return semantics_; }

  void set_policy_source(const CelPolicySource* absl_nullable src) {
    policy_source_ = src;
  }
  const CelPolicySource* absl_nullable policy_source() const {
    return policy_source_;
  }

 private:
  std::string name_;
  std::string display_name_;
  std::string description_;
  RuleSemantics semantics_ = RuleSemantics::kFirstMatch;
  const CelPolicySource* absl_nullable policy_source_ = nullptr;

  CompiledRule root_rule_;
};

CelPolicyIssue::Severity MapSeverity(cel::TypeCheckIssue::Severity severity) {
  switch (severity) {
    case cel::TypeCheckIssue::Severity::kError:
      return CelPolicyIssue::Severity::kError;
    case cel::TypeCheckIssue::Severity::kWarning:
      return CelPolicyIssue::Severity::kWarning;
    case cel::TypeCheckIssue::Severity::kDeprecated:
      return CelPolicyIssue::Severity::kDeprecated;
    default:
      return CelPolicyIssue::Severity::kError;
  }
}

bool IsWrapperOf(cel::TypeKind wrapper_kind, cel::TypeKind primitive_kind) {
  switch (wrapper_kind) {
    case cel::TypeKind::kBoolWrapper:
      return primitive_kind == cel::TypeKind::kBool;
    case cel::TypeKind::kIntWrapper:
      return primitive_kind == cel::TypeKind::kInt;
    case cel::TypeKind::kUintWrapper:
      return primitive_kind == cel::TypeKind::kUint;
    case cel::TypeKind::kDoubleWrapper:
      return primitive_kind == cel::TypeKind::kDouble;
    case cel::TypeKind::kStringWrapper:
      return primitive_kind == cel::TypeKind::kString;
    case cel::TypeKind::kBytesWrapper:
      return primitive_kind == cel::TypeKind::kBytes;
    default:
      return false;
  }
}

cel::Type FilterSpecialTypes(cel::Type type) {
  if (type.IsTypeParam()) {
    // Free type param should not appear in the output type, but if it does,
    // force it to dyn.
    return DynType();
  }
  if (type.IsEnum()) {
    return IntType{};
  }
  if (type.IsError()) {
    return DynType();
  }
  if (type.IsType()) {
    // drop parameters so all type types are compatible.
    return TypeType{};
  }
  return type;
}

// Returns true if `from` is assignable to `to`.
//
// Slightly adjusted from the standard routine to cover some edge cases around
// null and wrappers.
//
// TODO(b/522391716): try to standardize assignability checks.
bool OutputTypeIsAssignable(cel::Type from, cel::Type to) {
  from = FilterSpecialTypes(from);
  to = FilterSpecialTypes(to);

  // Any and dyn are assignable to/from everything.
  if (from.kind() == cel::TypeKind::kAny ||
      from.kind() == cel::TypeKind::kDyn || to.kind() == cel::TypeKind::kAny ||
      to.kind() == cel::TypeKind::kDyn) {
    return true;
  }

  // Wrappers auto-unwrap.
  if (IsWrapperOf(from.kind(), to.kind()) ||
      IsWrapperOf(to.kind(), from.kind())) {
    return true;
  }

  // Null is assignable to anything that is message-like.
  if (from.kind() == cel::TypeKind::kNull) {
    switch (to.kind()) {
      case cel::TypeKind::kNull:
      case cel::TypeKind::kStruct:
      case cel::TypeKind::kOpaque:
      case cel::TypeKind::kTimestamp:
      case cel::TypeKind::kDuration:
      case cel::TypeKind::kBytesWrapper:
      case cel::TypeKind::kBoolWrapper:
      case cel::TypeKind::kIntWrapper:
      case cel::TypeKind::kUintWrapper:
      case cel::TypeKind::kDoubleWrapper:
      case cel::TypeKind::kStringWrapper:
        return true;
      default:
        return false;
    }
  }

  if (from.kind() != to.kind()) {
    return false;
  }

  if (from.name() != to.name()) {
    return false;
  }

  if (from.GetParameters().size() != to.GetParameters().size()) {
    return false;
  }

  for (int i = 0; i < from.GetParameters().size(); ++i) {
    if (!OutputTypeIsAssignable(from.GetParameters()[i],
                                to.GetParameters()[i])) {
      return false;
    }
  }

  return true;
}

bool OutputTypeIsCompatible(cel::Type from, cel::Type to) {
  // We don't handle widening like in a self-contained CEL expression, but
  // permit some cases where one type is more specific than the other.
  return OutputTypeIsAssignable(from, to) || OutputTypeIsAssignable(to, from);
}

bool HasErrors(const policy_internal::IssueReporter& issues) {
  for (const auto& issue : issues.issues()) {
    if (issue.severity() == CelPolicyIssue::Severity::kError) {
      return true;
    }
  }
  return false;
}

void AdaptTypeCheckIssues(CelPolicyElementId id, const ValidationResult& r,
                          const CelPolicySource* absl_nullable policy_source,
                          policy_internal::IssueReporter& issues) {
  const Source* source = r.GetSource();
  if (source == nullptr && policy_source != nullptr) {
    source = policy_source->content();
  }

  for (const auto& iss : r.GetIssues()) {
    std::optional<SourcePosition> offset;
    if (source != nullptr) {
      offset = source->GetPosition(iss.location());
    }
    if (offset.has_value()) {
      issues.ReportOffsetIssue(id, *offset, MapSeverity(iss.severity()),
                               iss.message());
      continue;
    }
    issues.ReportIssue(id, MapSeverity(iss.severity()), iss.message());
  }
}

// Note on lifetime safety:
//
// The output policy will contain references to types that are owned by the
// arena member of this class. This is safe as long as the policy compiler lives
// as long as the output policies.
class PolicyCompiler {
 public:
  explicit PolicyCompiler(policy_internal::IssueReporter* issues,
                          std::unique_ptr<Compiler> base_compiler)
      : issues_(*issues), base_compiler_(std::move(base_compiler)) {}

  absl::string_view GetSourceDescription() const {
    if (src_ == nullptr) {
      return "<input>";
    }
    return src_->content()->description();
  }

  absl::StatusOr<ValidationResult> CompileExpression(CelPolicyElementId id,
                                                     absl::string_view val,
                                                     const Compiler* env) {
    std::unique_ptr<cel::Source> source;
    if (src_ != nullptr && src_->content() != nullptr) {
      std::optional<SourceRange> range;
      range = src_->GetSourceRange(id);
      bool use_subrange = !(src_->IsQuoted(id).value_or(true));
      if (range.has_value() && use_subrange) {
        source = std::make_unique<SourceSubrange>(*src_->content(), *range);
      }
    }

    if (source == nullptr) {
      // For quoted strings, the source should be generated from the interpreted
      // YAML value.
      CEL_ASSIGN_OR_RETURN(
          source, cel::NewSource(val, std::string(GetSourceDescription())));
    }
    auto result = env->Compile(*source, &arena_);
    if (!result.ok()) {
      return result;
    }
    result->SetSource(std::move(source));
    return result;
  }

  void AdaptTypeCheckIssues(CelPolicyElementId id, const ValidationResult& r) {
    cel::AdaptTypeCheckIssues(id, r, src_, issues_);
  }

  absl::StatusOr<CompiledOutputBlock> CompileOutputBlock(
      const cel::OutputBlock& output_block, const Compiler* env) {
    CompiledOutputBlock output;
    CEL_ASSIGN_OR_RETURN(auto output_validation,
                         CompileExpression(output_block.output().id(),
                                           output_block.output().value(), env));
    AdaptTypeCheckIssues(output_block.output().id(), output_validation);

    cel::Type result_type = DynType();
    if (output_validation.IsValid()) {
      CEL_ASSIGN_OR_RETURN(auto ast, output_validation.ReleaseAst());
      auto root_expr_id = ast->root_expr().id();
      output.output_ast =
          EmbeddedAst{output_block.output().id(), std::move(ast)};
      if (auto it = output_validation.GetResolvedTypeMap().find(root_expr_id);
          it != output_validation.GetResolvedTypeMap().end()) {
        result_type = it->second;
      }
    }
    if (output_block.explanation().has_value()) {
      CEL_ASSIGN_OR_RETURN(
          auto explanation_validation,
          CompileExpression(output_block.explanation()->id(),
                            output_block.explanation()->value(), env));
      AdaptTypeCheckIssues(output_block.explanation()->id(),
                           explanation_validation);
      if (explanation_validation.IsValid()) {
        CEL_ASSIGN_OR_RETURN(auto ast, explanation_validation.ReleaseAst());
        if (ast->GetReturnType().primitive() != PrimitiveType::kString) {
          issues_.ReportError(output_block.explanation()->id(),
                              "explanation must evaluate to string");
        } else {
          output.explanation_ast =
              EmbeddedAst{output_block.explanation()->id(), std::move(ast)};
        }
      }
    }
    output.result_type = result_type;
    return output;
  }

  absl::Status CompileMatch(const Match& match, const Compiler* env,
                            CompiledRule* out) {
    CompiledMatch c_match;
    c_match.id = match.id();
    if (match.condition().has_value()) {
      CEL_ASSIGN_OR_RETURN(auto validation,
                           CompileExpression(match.condition()->id(),
                                             match.condition()->value(), env));
      AdaptTypeCheckIssues(match.condition()->id(), validation);
      if (validation.IsValid()) {
        CEL_ASSIGN_OR_RETURN(auto ast, validation.ReleaseAst());
        if (ast->GetReturnType().primitive() != PrimitiveType::kBool) {
          issues_.ReportError(match.condition()->id(),
                              "condition must evaluate to bool");
        }
        c_match.condition =
            EmbeddedAst{match.condition()->id(), std::move(ast)};
      }
    }

    if (match.has_output_block()) {
      CEL_ASSIGN_OR_RETURN(c_match.production,
                           CompileOutputBlock(match.output_block(), env));
    } else if (match.has_rule()) {
      auto rule = std::make_unique<CompiledRule>();
      CEL_RETURN_IF_ERROR(CompileRule(match.rule(), env, rule.get()));
      c_match.production = std::move(rule);
    } else {
      issues_.ReportError(match.id(), "match must specify an output or rule");
    }
    out->matches.push_back(std::move(c_match));
    return absl::OkStatus();
  }

  absl::Status CompileRule(const Rule& rule, const cel::Compiler* env,
                           CompiledRule* out) {
    out->id = rule.id();
    std::unique_ptr<Compiler> buf;

    absl::flat_hash_set<std::string> seen_variables;
    for (const auto& variable : rule.variables()) {
      std::string name(variable.name().value());
      if (!seen_variables.insert(name).second) {
        issues_.ReportError(
            variable.expression().id(),
            absl::StrCat("overlapping identifier for name 'variables.", name,
                         "'"));
        continue;
      }
      std::string ident = absl::StrCat("variables.", name);
      CEL_ASSIGN_OR_RETURN(
          auto validation,
          CompileExpression(variable.expression().id(),
                            variable.expression().value(), env));
      AdaptTypeCheckIssues(variable.expression().id(), validation);
      if (!validation.IsValid()) {
        continue;
      }
      CEL_ASSIGN_OR_RETURN(auto ast, validation.ReleaseAst());
      cel::Type result_type = DynType();

      if (auto it = validation.GetResolvedTypeMap().find(ast->root_expr().id());
          it != validation.GetResolvedTypeMap().end()) {
        result_type = it->second;
      }
      out->variables.push_back(CompiledVariable{
          ident,
          EmbeddedAst{variable.expression().id(), std::move(ast)},
      });
      auto next = env->ToBuilder();
      auto status = next->GetCheckerBuilder().AddOrReplaceVariable(
          MakeVariableDecl(ident, result_type));
      if (!status.ok()) {
        issues_.ReportError(variable.expression().id(), status.message());
        continue;
      }
      CEL_ASSIGN_OR_RETURN(buf, next->Build());
      env = buf.get();
    }

    std::optional<cel::Type> overall_type;
    for (const auto& match : rule.matches()) {
      CEL_RETURN_IF_ERROR(CompileMatch(match, env, out));
      if (!overall_type.has_value()) {
        overall_type = GetOutputType(out->matches.back().production);
        continue;
      }

      if (std::optional<cel::Type> match_type =
              GetOutputType(out->matches.back().production);
          match_type.has_value()) {
        if (!OutputTypeIsCompatible(*match_type, *overall_type)) {
          issues_.ReportError(
              match.id(),
              absl::StrCat("incompatible output types: block has output type ",
                           FormatTypeName(*match_type),
                           ", but previous outputs have type ",
                           FormatTypeName(*overall_type)));
        }
      }
    }

    out->result_type = overall_type;
    return absl::OkStatus();
  }

  absl::Status CompilePolicy(const CelPolicy& policy,
                             IntermediateCompiledPolicy* out) {
    src_ = policy.source();
    out->set_policy_source(src_);
    out->set_semantics(RuleSemantics::kFirstMatch);
    out->set_name(policy.name().value());
    out->set_display_name(
        policy.display_name().value_or(ValueString{}).value());
    out->set_description(policy.description().value_or(ValueString{}).value());

    return CompileRule(policy.rule(), base_compiler_.get(),
                       &out->mutable_root_rule());
  }

 private:
  google::protobuf::Arena arena_;
  const CelPolicySource* absl_nullable src_;
  policy_internal::IssueReporter& issues_;
  std::unique_ptr<Compiler> base_compiler_;
};

bool IsExhaustive(const CompiledRule& rule);

class FirstMatchComposer {
 public:
  FirstMatchComposer(const IntermediateCompiledPolicy& icp,
                     const Compiler& compiler,
                     policy_internal::IssueReporter& issues)
      : issues_(issues), icp_(icp), compiler_(compiler) {}

  absl::Status Compose();

  bool success() const { return ast_ != nullptr; }

  std::unique_ptr<cel::Ast> ReleaseAst() { return std::move(ast_); }

 private:
  SourcePosition GetAstOffset(CelPolicyElementId id) const {
    if (icp_.policy_source() == nullptr) {
      return 0;
    }
    if (auto range = icp_.policy_source()->GetSourceRange(id);
        range.has_value()) {
      return range->begin;
    }
    if (auto pos = icp_.policy_source()->GetSourcePosition(id);
        pos.has_value()) {
      return *pos;
    }
    return 0;
  }

  using VariableScope = absl::flat_hash_map<std::string, int>;

  std::optional<int> ResolvePolicyVariable(absl::string_view reference);

  absl::flat_hash_map<int64_t, int> ResolveBlockIndexes(const Ast& ast);

  bool CheckMatchStructure(const CompiledRule& rule);

  // Returns true if already optional wrapped.
  absl::StatusOr<bool> ComposeRule(const CompiledRule& rule, Expr& init,
                                   Expr& insertion_expr);

  // returns true if already optional wrapped.
  absl::StatusOr<bool> ComposeProduction(
      const CompiledRule& rule, const CompiledMatch::Production& production,
      Expr& init, Expr& insertion_expr);

  void MapVariables(Ast& ast);

  void ComposeRuleVariables(const CompiledRule& rule, Expr& init,
                            Expr& insertion_expr);

  policy_internal::IssueReporter& issues_;
  OptimizerExprFactory factory_;
  const IntermediateCompiledPolicy& icp_;
  const Compiler& compiler_;
  std::vector<VariableScope> scopes_;
  bool optionalize_ = false;
  std::unique_ptr<Ast> ast_;
};

absl::Status FirstMatchComposer::Compose() {
  ABSL_DCHECK(icp_.semantics() == RuleSemantics::kFirstMatch);

  if (icp_.policy_source() != nullptr) {
    ABSL_DCHECK(icp_.policy_source()->content() != nullptr);
    absl::Span<const SourcePosition> line_offsets =
        icp_.policy_source()->content()->line_offsets();
    factory_.mutable_ast().mutable_source_info().set_line_offsets(
        std::vector<int32_t>(line_offsets.begin(), line_offsets.end()));
  }

  factory_.mutable_ast().mutable_root_expr() = factory_.NewCall(
      "cel.@block", factory_.NewList(), factory_.NewUnspecified());
  auto& block_init_list = factory_.mutable_ast()
                              .mutable_root_expr()
                              .mutable_call_expr()
                              .mutable_args()[0];
  auto& insertion_expr = factory_.mutable_ast()
                             .mutable_root_expr()
                             .mutable_call_expr()
                             .mutable_args()[1];
  optionalize_ = !IsExhaustive(icp_.root_rule());
  if (!CheckMatchStructure(icp_.root_rule())) {
    return absl::OkStatus();
  }
  CEL_ASSIGN_OR_RETURN(
      bool optional_wrapped,
      ComposeRule(icp_.root_rule(), block_init_list, insertion_expr));

  if (optional_wrapped != optionalize_) {
    return absl::InternalError(
        "composition failed to handle non-exhaustive rules");
  }

  CEL_ASSIGN_OR_RETURN(cel::ValidationResult result,
                       compiler_.GetTypeChecker().Check(factory_.ast()));
  if (!result.IsValid()) {
    AdaptTypeCheckIssues(icp_.root_rule().id, result, icp_.policy_source(),
                         issues_);
    return absl::OkStatus();
  }

  CEL_ASSIGN_OR_RETURN(ast_, result.ReleaseAst());

  return absl::OkStatus();
}

bool IsTriviallyTrueCondition(const CompiledMatch& match) {
  if (!match.condition.has_value() || match.condition->ast == nullptr) {
    return true;
  }
  const cel::Expr& expr = match.condition->ast->root_expr();
  if (expr.has_const_expr()) {
    const cel::Constant& const_expr = expr.const_expr();
    if (const_expr.has_bool_value() && const_expr.bool_value()) {
      return true;
    }
  }
  return false;
}

bool IsExhaustive(const CompiledRule& rule);

bool IsExhaustive(const CompiledMatch& match) {
  if (std::holds_alternative<CompiledOutputBlock>(match.production)) {
    return true;
  }

  const auto* nested_rule_ptr =
      std::get_if<std::unique_ptr<CompiledRule>>(&match.production);
  ABSL_DCHECK(nested_rule_ptr != nullptr);
  const CompiledRule& nested_rule = **nested_rule_ptr;
  return IsExhaustive(nested_rule);
}

bool IsExhaustive(const CompiledRule& rule) {
  if (rule.matches.empty()) {
    // Validation should fail, but generalization would be false.
    return false;
  }
  bool has_default = false;
  for (const auto& match : rule.matches) {
    if (IsTriviallyTrueCondition(match) && IsExhaustive(match)) {
      // If this isn't the last match in the rule, it should get flagged
      // during validation since it means there are trivially unreachable
      // matches.
      has_default = true;
    }
    if (!IsTriviallyTrueCondition(match) && !IsExhaustive(match)) {
      // There is a nested rule that might return an optional.none().
      return false;
    }
  }
  // Otherwise, everything in this branch is exhaustive so we can defer
  // wrapping.
  return has_default;
}

bool FirstMatchComposer::CheckMatchStructure(const CompiledRule& rule) {
  if (rule.matches.empty()) {
    issues_.ReportError(rule.id, "rule does not specify match conditions");
    return false;
  }

  bool valid = true;
  bool seen_trivially_true = false;

  for (const auto& match : rule.matches) {
    if (seen_trivially_true) {
      if (std::holds_alternative<CompiledOutputBlock>(match.production)) {
        issues_.ReportError(match.id, "match creates unreachable outputs");
      } else if (std::holds_alternative<std::unique_ptr<CompiledRule>>(
                     match.production)) {
        issues_.ReportError(match.id, "rule creates unreachable outputs");
      }
      valid = false;
    }

    if (IsTriviallyTrueCondition(match) && IsExhaustive(match)) {
      seen_trivially_true = true;
    }

    if (auto* nested_rule =
            std::get_if<std::unique_ptr<CompiledRule>>(&match.production);
        nested_rule != nullptr) {
      ABSL_DCHECK(*nested_rule != nullptr);
      if (!CheckMatchStructure(**nested_rule)) {
        valid = false;
      }
    }
  }

  return valid;
}

std::optional<int> FirstMatchComposer::ResolvePolicyVariable(
    absl::string_view reference) {
  for (auto scope_iter = scopes_.rbegin(); scope_iter != scopes_.rend();
       ++scope_iter) {
    if (auto it = scope_iter->find(reference); it != scope_iter->end()) {
      return it->second;
    }
  }
  return std::nullopt;
}

class IndexRewrite : public AstRewriterBase {
 public:
  explicit IndexRewrite(absl::flat_hash_map<int64_t, int> expr_id_to_index,
                        OptimizerExprFactory& factory)
      : expr_id_to_index_(std::move(expr_id_to_index)), factory_(factory) {}

  bool PreVisitRewrite(Expr& e) override {
    if (auto it = expr_id_to_index_.find(e.id());
        it != expr_id_to_index_.end()) {
      e.mutable_ident_expr().set_name(absl::StrCat("@index", it->second));
      factory_.RecordReplacement(e.id(), e);
      return true;
    }
    return false;
  }

 private:
  absl::flat_hash_map<int64_t, int> expr_id_to_index_;
  OptimizerExprFactory& factory_;
};

absl::StatusOr<bool> FirstMatchComposer::ComposeRule(const CompiledRule& rule,
                                                     Expr& init,
                                                     Expr& insertion_expr) {
  scopes_.emplace_back();
  auto pop_scope = absl::MakeCleanup([this]() { scopes_.pop_back(); });
  ComposeRuleVariables(rule, init, insertion_expr);
  Expr* insertion_point = &insertion_expr;
  const bool has_default = IsTriviallyTrueCondition(rule.matches.back());
  const bool needs_wrap = !IsExhaustive(rule);
  size_t end = rule.matches.size() - (has_default ? 1 : 0);
  for (size_t i = 0; i < end; i++) {
    const auto& match = rule.matches[i];
    if (IsTriviallyTrueCondition(match) && IsExhaustive(match)) {
      return absl::InternalError("detected unreachable match after validation");
    }

    Expr production;
    CEL_ASSIGN_OR_RETURN(
        bool is_wrapped,
        ComposeProduction(rule, match.production, init, production));
    if (needs_wrap && !is_wrapped) {
      production = factory_.NewCall("optional.of", std::move(production));
    }

    if (!IsTriviallyTrueCondition(match)) {
      Ast condition = *match.condition->ast;
      MapVariables(condition);
      factory_.StartCopyContext();
      auto copy = factory_.Copy(condition.root_expr());
      auto source_info = factory_.RemapSourceInfo(
          condition.source_info(), GetAstOffset(match.condition->id));
      factory_.MergeSourceInfo(source_info);
      *insertion_point = factory_.NewCall("_?_:_", std::move(copy));
      insertion_point->mutable_call_expr().mutable_args().push_back(
          std::move(production));
      ABSL_DCHECK(!(!needs_wrap && is_wrapped))
          << "unexpected wrapping in exhaustive policy.";
      insertion_point = &insertion_point->mutable_call_expr().add_args();
      continue;
    }

    if (!is_wrapped) {
      return absl::InternalError(
          "composition failed. expected optional wrapped rule but got a plain "
          "value");
    }
    auto fn = needs_wrap ? "or" : "orValue";
    *insertion_point = factory_.NewMemberCall(fn, std::move(production));
    insertion_point = &insertion_point->mutable_call_expr().add_args();
  }

  if (has_default) {
    const auto& match = rule.matches.back();
    Expr production;
    CEL_ASSIGN_OR_RETURN(
        bool is_wrapped,
        ComposeProduction(rule, match.production, init, production));
    if (needs_wrap && !is_wrapped) {
      production = factory_.NewCall("optional.of", std::move(production));
    }
    *insertion_point = std::move(production);
    ABSL_DCHECK(!(!needs_wrap && is_wrapped))
        << "unexpected wrapping in exhaustive policy.";

    return needs_wrap;
  }

  // Otherwise, we fell through a non-exhaustive rule.
  *insertion_point = factory_.NewCall("optional.none");
  return true;
}

absl::StatusOr<bool> FirstMatchComposer::ComposeProduction(
    const CompiledRule& rule, const CompiledMatch::Production& production,
    Expr& init, Expr& insertion_expr) {
  if (auto* nested_rule =
          std::get_if<std::unique_ptr<CompiledRule>>(&production);
      nested_rule != nullptr) {
    return ComposeRule(**nested_rule, init, insertion_expr);
  }
  auto* output = std::get_if<CompiledOutputBlock>(&production);
  if (output == nullptr) {
    return absl::InternalError("unexpected rule production type");
  }
  const EmbeddedAst& output_ast = output->output_ast;
  Ast ast = *output_ast.ast;
  MapVariables(ast);
  factory_.StartCopyContext();
  Expr to_insert = factory_.Copy(ast.root_expr());
  auto source_info =
      factory_.RemapSourceInfo(ast.source_info(), GetAstOffset(output_ast.id));
  factory_.MergeSourceInfo(source_info);
  insertion_expr = std::move(to_insert);

  return false;
}

absl::flat_hash_map<int64_t, int> FirstMatchComposer::ResolveBlockIndexes(
    const Ast& ast) {
  absl::flat_hash_map<int64_t, int> out;
  for (auto it = ast.reference_map().begin(); it != ast.reference_map().end();
       it++) {
    const Reference& ref = it->second;
    if (!it->second.overload_id().empty()) {
      continue;
    }
    if (!absl::StartsWith(ref.name(), "variable")) {
      continue;
    }
    if (auto index = ResolvePolicyVariable(ref.name()); index.has_value()) {
      out[it->first] = *index;
    }
  }
  return out;
}

void FirstMatchComposer::MapVariables(Ast& ast) {
  absl::flat_hash_map<int64_t, int> edit_map = ResolveBlockIndexes(ast);
  IndexRewrite rewriter(std::move(edit_map), factory_);
  AstRewrite(ast.mutable_root_expr(), rewriter);
}

void FirstMatchComposer::ComposeRuleVariables(const CompiledRule& rule,
                                              Expr& init,
                                              Expr& insertion_expr) {
  for (const auto& variable : rule.variables) {
    Ast ast = *variable.ast.ast;
    MapVariables(ast);
    factory_.StartCopyContext();
    auto insertion = factory_.Copy(ast.root_expr());
    auto info = factory_.RemapSourceInfo(ast.source_info(),
                                         GetAstOffset(variable.ast.id));
    factory_.MergeSourceInfo(info);
    ABSL_DCHECK(init.has_list_expr());
    int index = init.mutable_list_expr().elements().size();
    init.mutable_list_expr().mutable_elements().push_back(
        factory_.NewListElement(std::move(insertion)));
    scopes_.back()[variable.ident] = index;
  }
}

bool HasComprehensionParent(const NavigableAstNode& node) {
  const NavigableAstNode* curr = &node;
  while (curr != nullptr) {
    if (curr->node_kind() == NodeKind::kComprehension) {
      return true;
    }
    curr = curr->parent();
  }
  return false;
}

// Unnester implementation.
class Unnester {
 public:
  Unnester(const TypeChecker& type_checker, Ast ast, int height,
           policy_internal::IssueReporter& issues)
      : type_checker_(type_checker),
        factory_(std::move(ast)),
        height_(height),
        issues_(issues) {}

  // Run the unnesting.
  // The class cannot be reused after this is called.
  absl::StatusOr<Ast> Unnest() {
    if (height_ > 0) {
      CEL_RETURN_IF_ERROR(Slice());
    }
    CEL_RETURN_IF_ERROR(Cleanup());
    if (modified_) {
      CEL_ASSIGN_OR_RETURN(
          ValidationResult result,
          type_checker_.Check(std::move(factory_.mutable_ast())));
      ABSL_DCHECK(result.IsValid());
      CEL_ASSIGN_OR_RETURN(std::unique_ptr<Ast> checked_ast,
                           result.ReleaseAst());
      return std::move(*checked_ast);
    }
    return std::move(factory_.mutable_ast());
  }

 private:
  // The core unnest routine.
  absl::Status Slice();
  // Fixup the AST post-unnesting.
  absl::Status Cleanup();

  void ReportErrorAtId(int64_t id, absl::string_view message);

  const TypeChecker& type_checker_;
  OptimizerExprFactory factory_;
  int height_;
  policy_internal::IssueReporter& issues_;
  bool modified_ = false;
};

class UnnestRewriter : public AstRewriterBase {
 public:
  explicit UnnestRewriter(OptimizerExprFactory& f, Expr& block_list_expr,
                          absl::Span<const int64_t> cuts)
      : factory_(f), cuts_(cuts), block_list_expr_(block_list_expr) {}

  bool PostVisitRewrite(Expr& expr) override {
    using std::swap;
    // Post order so we always see children before parents.
    // No need to copy metadata since we're only moving exprs or minting
    // new ones.
    if (absl::c_contains(cuts_, expr.id())) {
      size_t idx = block_list_expr_.list_expr().elements().size();
      Expr value = factory_.NewIdent(absl::StrCat("@index", idx));
      factory_.RecordReplacement(expr.id(), value, /*keep_metadata=*/true);
      swap(value, expr);
      block_list_expr_.mutable_list_expr().mutable_elements().push_back(
          factory_.NewListElement(std::move(value)));
      return true;
    }
    return false;
  }

 private:
  OptimizerExprFactory& factory_;
  absl::Span<const int64_t> cuts_;
  Expr& block_list_expr_;
};

absl::Status Unnester::Slice() {
  Expr& root = factory_.mutable_ast().mutable_root_expr();
  if (root.call_expr().function() != kCelBlock ||
      root.call_expr().args().size() != 2 ||
      !root.call_expr().args()[0].has_list_expr()) {
    return absl::InternalError("malformed AST detected during unnesting");
  }
  // Two passes, we identify the slice points (bottom up), then cut
  // and paste the leaves into the block list.
  NavigableAst nav_ast = NavigableAst::Build(factory_.ast().root_expr());

  ABSL_DCHECK(nav_ast.IdsAreUnique());
  bool can_cut = true;
  std::vector<int64_t> cuts;
  for (const NavigableAstNode& node : nav_ast.Root().DescendantsPostorder()) {
    // Subsequent cuts will be height_ + 1 in the block, indices. Within the
    // error margin we specified.
    if (node.height() % height_ == 0) {
      if (HasComprehensionParent(node)) {
        ReportErrorAtId(
            node.expr()->id(),
            absl::StrCat(
                "cannot unnest AST due to comprehension. cannot accommodate "
                "height limit of ",
                height_));
        can_cut = false;
        continue;
      }
      if (&node == &nav_ast.Root()) {
        // If evenly divisible by height, don't cut since it will net a taller
        // AST.
        continue;
      }
      cuts.push_back(node.expr()->id());
    }
  }

  if (!can_cut || cuts.empty()) {
    return absl::OkStatus();
  }

  Expr& block_list_expr = root.mutable_call_expr().mutable_args()[0];
  Expr& insertion_expr = root.mutable_call_expr().mutable_args()[1];

  UnnestRewriter rewriter(factory_, block_list_expr, cuts);
  AstRewrite(insertion_expr, rewriter);
  modified_ = true;

  return absl::OkStatus();
}

absl::Status Unnester::Cleanup() {
  using std::swap;

  const auto& ast = factory_.ast();
  if (ast.root_expr().call_expr().function() != kCelBlock ||
      ast.root_expr().call_expr().args().size() != 2 ||
      !ast.root_expr().call_expr().args()[0].has_list_expr()) {
    return absl::InternalError("malformed AST detected during unnesting");
  }
  if (ast.root_expr().call_expr().args()[0].list_expr().elements().empty()) {
    const ExprId block_id = ast.root_expr().id();
    const ExprId list_id = ast.root_expr().call_expr().args()[0].id();

    Expr value = std::move(factory_.mutable_ast()
                               .mutable_root_expr()
                               .mutable_call_expr()
                               .mutable_args()[1]);
    factory_.mutable_ast().mutable_root_expr() = std::move(value);

    factory_.PruneMetadata(block_id);
    factory_.PruneMetadata(list_id);
    modified_ = true;
  }

  return absl::OkStatus();
}

void Unnester::ReportErrorAtId(int64_t id, absl::string_view message) {
  int32_t position = 0;
  auto it = factory_.ast().source_info().positions().find(id);
  if (it != factory_.ast().source_info().positions().end()) {
    position = it->second;
  }
  issues_.ReportError(-1, position, message);
}
}  // namespace

// Compiles a CEL policy using the provided CEL compiler as a base environment.
absl::StatusOr<CelPolicyValidationResult> CompilePolicy(
    const Compiler& compiler, const CelPolicy& policy,
    const CompilePolicyOptions& options) {
  policy_internal::IssueReporter issues;
  if (options.unnesting_height_limit != 0 &&
      options.unnesting_height_limit < 2) {
    return absl::InvalidArgumentError(
        "unnesting_height_limit must be at least 2");
  }
  auto builder = compiler.ToBuilder();
  ExpressionContainer cont;
  for (const auto& import : policy.imports()) {
    auto status = cont.AddAbbreviation(import.name().value());
    if (!status.ok()) {
      issues.ReportError(
          import.name().id(),
          absl::StrCat("'", import.name().value(), "': ", status.message()));
    }
  }

  builder->GetCheckerBuilder().SetExpressionContainer(cont);
  CEL_ASSIGN_OR_RETURN(auto base_compiler, builder->Build());

  PolicyCompiler policy_compiler(&issues, std::move(base_compiler));

  IntermediateCompiledPolicy icp;
  CEL_RETURN_IF_ERROR(policy_compiler.CompilePolicy(policy, &icp));

  if (HasErrors(issues)) {
    return CelPolicyValidationResult(issues.ReleaseIssues(),
                                     policy.source_ptr());
  }

  CEL_ASSIGN_OR_RETURN(base_compiler, builder->Build());
  switch (icp.semantics()) {
    case RuleSemantics::kFirstMatch: {
      FirstMatchComposer composer(icp, *base_compiler, issues);
      CEL_RETURN_IF_ERROR(composer.Compose());
      if (!composer.success()) {
        return CelPolicyValidationResult(issues.ReleaseIssues(),
                                         policy.source_ptr());
      }

      auto ast = composer.ReleaseAst();
      Unnester unnester(base_compiler->GetTypeChecker(), std::move(*ast),
                        options.unnesting_height_limit, issues);
      CEL_ASSIGN_OR_RETURN(Ast unnested_ast, unnester.Unnest());

      if (HasErrors(issues)) {
        return CelPolicyValidationResult(issues.ReleaseIssues(),
                                         policy.source_ptr());
      }

      return CelPolicyValidationResult(
          std::make_unique<Ast>(std::move(unnested_ast)), {},
          policy.source_ptr());
    }
    default:
      return absl::UnimplementedError(
          absl::StrCat("Unsupported RuleSemantics: ", icp.semantics()));
  }
}

}  // namespace cel
