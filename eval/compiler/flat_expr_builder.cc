/*
 * Copyright 2021 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "eval/compiler/flat_expr_builder.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <stack>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/absl_check.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/types/span.h"
#include "absl/types/variant.h"
#include "base/ast.h"
#include "base/builtins.h"
#include "base/type_provider.h"
#include "common/ast.h"
#include "common/ast_traverse.h"
#include "common/ast_visitor.h"
#include "common/constant.h"
#include "common/expr.h"
#include "common/kind.h"
#include "common/type.h"
#include "common/type_spec_resolver.h"
#include "common/value.h"
#include "eval/compiler/check_ast_extensions.h"
#include "eval/compiler/flat_expr_builder_extensions.h"
#include "eval/compiler/resolver.h"
#include "eval/eval/comprehension_step.h"
#include "eval/eval/container_access_step.h"
#include "eval/eval/create_list_step.h"
#include "eval/eval/create_map_step.h"
#include "eval/eval/create_struct_step.h"
#include "eval/eval/evaluator_core.h"
#include "eval/eval/expression_step_logic.h"
#include "eval/eval/function_step.h"
#include "eval/eval/optional_or_step.h"
#include "eval/eval/select_step.h"
#include "eval/eval/shadowable_value_step.h"
#include "eval/eval/ternary_step.h"
#include "internal/status_macros.h"
#include "runtime/internal/convert_constant.h"
#include "runtime/internal/issue_collector.h"
#include "runtime/runtime_issue.h"
#include "runtime/runtime_options.h"
#include "runtime/type_registry.h"
#include "google/protobuf/arena.h"

namespace google::api::expr::runtime {

namespace {

using ::cel::Ast;
using ::cel::AstTraverse;
using ::cel::RuntimeIssue;
using ::cel::Value;
using ::cel::runtime_internal::ConvertConstant;
using ::cel::runtime_internal::GetLegacyRuntimeTypeProvider;
using ::cel::runtime_internal::GetRuntimeTypeProvider;
using ::cel::runtime_internal::IssueCollector;

constexpr absl::string_view kOptionalOrFn = "or";
constexpr absl::string_view kOptionalOrValueFn = "orValue";
constexpr absl::string_view kBlock = "cel.@block";

// Forward declare to resolve circular dependency for short_circuiting visitors.
class FlatExprVisitor;

// Helper for bookkeeping variables mapped to indexes.
class IndexManager {
 public:
  IndexManager() : next_free_slot_(0), max_slot_count_(0) {}

  size_t ReserveSlots(size_t n) {
    size_t result = next_free_slot_;
    next_free_slot_ += n;
    if (next_free_slot_ > max_slot_count_) {
      max_slot_count_ = next_free_slot_;
    }
    return result;
  }

  size_t ReleaseSlots(size_t n) {
    next_free_slot_ -= n;
    return next_free_slot_;
  }

  size_t max_slot_count() const { return max_slot_count_; }

 private:
  size_t next_free_slot_;
  size_t max_slot_count_;
};

// Helper for computing jump offsets.
//
// Jumps should be self-contained to a single expression node -- jumping
// outside that range is a bug.
struct ProgramStepIndex {
  int index;
  ProgramBuilder::Subexpression* subexpression;
};

// A convenience wrapper for offset-calculating logic.
class Jump {
 public:
  static absl::StatusOr<int> CalculateOffset(ProgramStepIndex base,
                                             ProgramStepIndex target) {
    if (target.subexpression != base.subexpression) {
      return absl::InternalError(
          "Jump target must be contained in the parent"
          "subexpression");
    }

    int offset = base.subexpression->CalculateOffset(base.index, target.index);

    // The offset may be packed into a 31-bit int field. Technically, the offset
    // could be larger for a well-formed expression with more than 2**30 steps,
    // but such a case is unlikely to exist at this time.
    constexpr int kMaxJumpOffset = (1 << 30) - 1;
    constexpr int kMinJumpOffset = -(1 << 30);

    if (offset < kMinJumpOffset || offset > kMaxJumpOffset) {
      return absl::InternalError("Jump offset exceeds supported range: " +
                                 absl::StrCat(offset));
    }
    return offset;
  }
};

class CondVisitor {
 public:
  virtual ~CondVisitor() = default;
  virtual void PreVisit(const cel::Expr* expr) = 0;
  virtual void PostVisitArg(int arg_num, const cel::Expr* expr) = 0;
  virtual void PostVisit(const cel::Expr* expr) = 0;
  virtual void PostVisitTarget(const cel::Expr* expr) {}
};

// Visitor managing the "&&" and "||" (boolean logic) operations.
// Implements short-circuiting if enabled.
//
// With short-circuiting enabled, generates a program like:
//   +-------------+------------------------+-----------------------+
//   | PC          | Step                   | Stack                 |
//   +-------------+------------------------+-----------------------+
//   | i + 0       | <Arg1>                 | arg1                  |
//   | i + 1       | ConditionalJump i + 4  | arg1                  |
//   | i + 2       | <Arg2>                 | arg1, arg2            |
//   | i + 3       | BooleanOperator        | Op(arg1, arg2)        |
//   | i + 4       | <rest of program>      | arg1 | Op(arg1, arg2) |
//   +-------------+------------------------+------------------------+
class LogicalCondVisitor : public CondVisitor {
 public:
  explicit LogicalCondVisitor(FlatExprVisitor* visitor, bool is_or,
                              bool short_circuiting)
      : visitor_(visitor), is_or_(is_or), short_circuiting_(short_circuiting) {}

  void PreVisit(const cel::Expr* expr) override;
  void PostVisitArg(int arg_num, const cel::Expr* expr) override;
  void PostVisit(const cel::Expr* expr) override;

 private:
  FlatExprVisitor* visitor_;
  const bool is_or_;
  std::vector<ProgramStepIndex> jump_steps_;
  bool short_circuiting_;
};

// Visitor managing optional "or" and "orValue" operations.
// Implements short-circuiting if enabled.
class OptionalOrCondVisitor : public CondVisitor {
 public:
  explicit OptionalOrCondVisitor(FlatExprVisitor* visitor, bool is_or_value,
                                 bool short_circuiting)
      : visitor_(visitor),
        is_or_value_(is_or_value),
        short_circuiting_(short_circuiting) {}

  void PreVisit(const cel::Expr* expr) override;
  void PostVisitArg(int arg_num, const cel::Expr* expr) override {}
  void PostVisitTarget(const cel::Expr* expr) override;
  void PostVisit(const cel::Expr* expr) override;

 private:
  FlatExprVisitor* visitor_;
  const bool is_or_value_;
  OptionalHasValueJumpStep* jump_step_ = nullptr;
  ProgramStepIndex jump_step_pos_{-1, nullptr};
  bool short_circuiting_;
};

class TernaryCondVisitor : public CondVisitor {
 public:
  explicit TernaryCondVisitor(FlatExprVisitor* visitor) : visitor_(visitor) {}

  void PreVisit(const cel::Expr* expr) override;
  void PostVisitArg(int arg_num, const cel::Expr* expr) override;
  void PostVisit(const cel::Expr* expr) override;

 private:
  FlatExprVisitor* visitor_;
  std::optional<ProgramStepIndex> cond_jump_pos_;
  std::optional<ProgramStepIndex> jump_after_first_pos_;
};

class ExhaustiveTernaryCondVisitor : public CondVisitor {
 public:
  explicit ExhaustiveTernaryCondVisitor(FlatExprVisitor* visitor)
      : visitor_(visitor) {}

  void PreVisit(const cel::Expr* expr) override;
  void PostVisitArg(int arg_num, const cel::Expr* expr) override {}
  void PostVisit(const cel::Expr* expr) override;

 private:
  FlatExprVisitor* visitor_;
};

// Returns a hint for the number of program nodes (steps or subexpressions) that
// will be created for this expr.
size_t SizeHint(const cel::Expr& expr) {
  switch (expr.kind_case()) {
    case cel::ExprKindCase::kConstant:
      return 1;
    case cel::ExprKindCase::kIdentExpr:
      return 1;
    case cel::ExprKindCase::kSelectExpr:
      return 2;
    case cel::ExprKindCase::kCallExpr:
      return expr.call_expr().args().size() +
             (expr.call_expr().has_target() ? 2 : 1);
    case cel::ExprKindCase::kListExpr:
      return expr.list_expr().elements().size() + 1;
    case cel::ExprKindCase::kStructExpr:
      return expr.struct_expr().fields().size() + 1;
    case cel::ExprKindCase::kMapExpr:
      return 2 * expr.struct_expr().fields().size() + 1;
    default:
      return 1;
  }
  return 0;
}

// Returns whether this comprehension appears to be a standard map/filter
// macro implementation. It is not exhaustive, so it is unsafe to use with
// custom comprehensions outside of the standard macros or hand crafted ASTs.
bool IsOptimizableListAppend(const cel::ComprehensionExpr* comprehension,
                             bool enable_comprehension_list_append) {
  if (!enable_comprehension_list_append) {
    return false;
  }
  absl::string_view accu_var = comprehension->accu_var();
  if (accu_var.empty() ||
      comprehension->result().ident_expr().name() != accu_var) {
    return false;
  }
  if (!comprehension->accu_init().has_list_expr() ||
      !comprehension->accu_init().list_expr().elements().empty()) {
    return false;
  }

  if (!comprehension->loop_step().has_call_expr()) {
    return false;
  }

  // Macro loop_step for a filter() will contain a ternary:
  //   filter ? accu_var + [elem] : accu_var
  // Macro loop_step for a map() will contain a list concat operation:
  //   accu_var + [elem]
  const auto* call_expr = &comprehension->loop_step().call_expr();

  if (call_expr->function() == cel::builtin::kTernary &&
      call_expr->args().size() == 3) {
    if (!call_expr->args()[1].has_call_expr()) {
      return false;
    }
    call_expr = &(call_expr->args()[1].call_expr());
  }

  return !call_expr->has_target() &&
         call_expr->function() == cel::builtin::kAdd &&
         call_expr->args().size() == 2 &&
         call_expr->args()[0].has_ident_expr() &&
         call_expr->args()[0].ident_expr().name() == accu_var &&
         call_expr->args()[1].has_list_expr() &&
         call_expr->args()[1].list_expr().elements().size() == 1;
}

// Assuming `IsOptimizableListAppend()` return true, return a pointer to the
// call `accu_var + [elem]`.
const cel::CallExpr* GetOptimizableListAppendCall(
    const cel::ComprehensionExpr* comprehension) {
  ABSL_DCHECK(IsOptimizableListAppend(
      comprehension, /*enable_comprehension_list_append=*/true));

  // Macro loop_step for a filter() will contain a ternary:
  //   filter ? accu_var + [elem] : accu_var
  // Macro loop_step for a map() will contain a list concat operation:
  //   accu_var + [elem]
  const auto* call_expr = &comprehension->loop_step().call_expr();

  if (call_expr->function() == cel::builtin::kTernary &&
      call_expr->args().size() == 3) {
    call_expr = &(call_expr->args()[1].call_expr());
  }
  return call_expr;
}

// Assuming `IsOptimizableListAppend()` return true, return a pointer to the
// node `[elem]`.
const cel::Expr* GetOptimizableListAppendOperand(
    const cel::ComprehensionExpr* comprehension) {
  return &GetOptimizableListAppendCall(comprehension)->args()[1];
}

// Returns whether this comprehension appears to be a macro implementation for
// map transformations. It is not exhaustive, so it is unsafe to use with custom
// comprehensions outside of the standard macros or hand crafted ASTs.
bool IsOptimizableMapInsert(const cel::ComprehensionExpr* comprehension,
                            bool enable_comprehension_mutable_map) {
  if (!enable_comprehension_mutable_map) {
    return false;
  }
  if (comprehension->iter_var().empty() || comprehension->iter_var2().empty()) {
    return false;
  }
  absl::string_view accu_var = comprehension->accu_var();
  if (accu_var.empty() || !comprehension->has_result() ||
      !comprehension->result().has_ident_expr() ||
      comprehension->result().ident_expr().name() != accu_var) {
    return false;
  }
  if (!comprehension->accu_init().has_map_expr() ||
      !comprehension->accu_init().map_expr().entries().empty()) {
    return false;
  }
  if (!comprehension->loop_step().has_call_expr()) {
    return false;
  }
  const auto* call_expr = &comprehension->loop_step().call_expr();

  if (call_expr->function() == cel::builtin::kTernary &&
      call_expr->args().size() == 3) {
    if (!call_expr->args()[1].has_call_expr()) {
      return false;
    }
    call_expr = &(call_expr->args()[1].call_expr());
  }
  return !call_expr->has_target() &&
         call_expr->function() == "cel.@mapInsert" &&
         (call_expr->args().size() == 2 || call_expr->args().size() == 3) &&
         call_expr->args()[0].has_ident_expr() &&
         call_expr->args()[0].ident_expr().name() == accu_var;
}

bool IsBind(const cel::ComprehensionExpr* comprehension) {
  static constexpr absl::string_view kUnusedIterVar = "#unused";

  return comprehension->loop_condition().const_expr().has_bool_value() &&
         comprehension->loop_condition().const_expr().bool_value() == false &&
         comprehension->iter_var() == kUnusedIterVar &&
         comprehension->iter_var2().empty() &&
         comprehension->iter_range().has_list_expr() &&
         comprehension->iter_range().list_expr().elements().empty();
}

bool IsBlock(const cel::CallExpr* call) { return call->function() == kBlock; }

// Visitor for Comprehension expressions.
class ComprehensionVisitor {
 public:
  explicit ComprehensionVisitor(FlatExprVisitor* visitor, bool is_trivial,
                                size_t iter_slot, size_t iter2_slot,
                                size_t accu_slot)
      : visitor_(visitor),
        init_step_(nullptr),
        is_trivial_(is_trivial),
        accu_init_extracted_(false),
        iter_slot_(iter_slot),
        iter2_slot_(iter2_slot),
        accu_slot_(accu_slot) {}

  void PreVisit(const cel::Expr* expr);
  absl::Status PostVisitArg(cel::ComprehensionArg arg_num,
                            const cel::Expr* comprehension_expr) {
    if (is_trivial_) {
      PostVisitArgTrivial(arg_num, comprehension_expr);
      return absl::OkStatus();
    } else {
      return PostVisitArgDefault(arg_num, comprehension_expr);
    }
  }
  void PostVisit(const cel::Expr* expr);

  void MarkAccuInitExtracted() { accu_init_extracted_ = true; }

 private:
  void PostVisitArgTrivial(cel::ComprehensionArg arg_num,
                           const cel::Expr* comprehension_expr);

  absl::Status PostVisitArgDefault(cel::ComprehensionArg arg_num,
                                   const cel::Expr* comprehension_expr);

  ComprehensionCondStep* absl_nullable GetCondStep();
  ComprehensionNextStep* absl_nullable GetNextStep();

  FlatExprVisitor* visitor_;
  ComprehensionInitStep* init_step_;
  ProgramStepIndex init_step_pos_;
  std::optional<ProgramStepIndex> next_step_pos_;
  std::optional<ProgramStepIndex> cond_step_pos_;
  bool is_trivial_;
  bool accu_init_extracted_;
  size_t iter_slot_;
  size_t iter2_slot_;
  size_t accu_slot_;
};

absl::flat_hash_set<int32_t> MakeOptionalIndicesSet(
    const cel::StructExpr& create_struct_expr) {
  absl::flat_hash_set<int32_t> optional_indices;
  for (size_t i = 0; i < create_struct_expr.fields().size(); ++i) {
    if (create_struct_expr.fields()[i].optional()) {
      optional_indices.insert(static_cast<int32_t>(i));
    }
  }
  return optional_indices;
}

absl::flat_hash_set<int32_t> MakeOptionalIndicesSet(
    const cel::MapExpr& map_expr) {
  absl::flat_hash_set<int32_t> optional_indices;
  for (size_t i = 0; i < map_expr.entries().size(); ++i) {
    if (map_expr.entries()[i].optional()) {
      optional_indices.insert(static_cast<int32_t>(i));
    }
  }
  return optional_indices;
}

absl::flat_hash_set<size_t> MakeOptionalIndicesSet(
    const cel::ListExpr& list_expr) {
  absl::flat_hash_set<size_t> optional_indices;
  for (size_t i = 0; i < list_expr.elements().size(); ++i) {
    if (list_expr.elements()[i].optional()) {
      optional_indices.insert(i);
    }
  }
  return optional_indices;
}

class FlatExprVisitor : public cel::AstVisitor {
 public:
  enum class CallHandlerResult {
    // The call was intercepted, no additional processing is needed.
    kIntercepted,
    // The call was not intercepted, continue with the default processing.
    kNotIntercepted,
  };

  // Handler for functions with builtin implementations.
  // This is used to replace the usual dispatcher step that applies
  // the arguments to a candidate function from the function registry.
  using CallHandler = absl::AnyInvocable<CallHandlerResult(
      const cel::Expr&, const cel::CallExpr&)>;

  FlatExprVisitor(
      const Resolver& resolver, const cel::RuntimeOptions& options,
      std::vector<std::unique_ptr<ProgramOptimizer>> program_optimizers,
      const absl::flat_hash_map<int64_t, cel::TypeSpec>& type_map,
      const cel::TypeProvider& type_provider, IssueCollector& issue_collector,
      ProgramBuilder& program_builder, PlannerContext& extension_context,
      bool enable_optional_types)
      : resolver_(resolver),
        type_provider_(type_provider),
        progress_status_(absl::OkStatus()),
        resolved_select_expr_(nullptr),
        options_(options),
        program_optimizers_(std::move(program_optimizers)),
        type_map_(type_map),
        issue_collector_(issue_collector),
        program_builder_(program_builder),
        extension_context_(extension_context),
        enable_optional_types_(enable_optional_types) {
    constexpr size_t kCallHandlerSizeHint = 11;
    call_handlers_.reserve(kCallHandlerSizeHint);
    call_handlers_[cel::builtin::kIndex] = [this](const cel::Expr& expr,
                                                  const cel::CallExpr& call) {
      return HandleIndex(expr, call);
    };
    call_handlers_[kBlock] = [this](const cel::Expr& expr,
                                    const cel::CallExpr& call) {
      return HandleBlock(expr, call);
    };
    call_handlers_[cel::builtin::kAdd] = [this](const cel::Expr& expr,
                                                const cel::CallExpr& call) {
      return HandleListAppend(expr, call);
    };
    if (options_.enable_fast_builtins) {
      call_handlers_[cel::builtin::kNotStrictlyFalse] =
          [this](const cel::Expr& expr, const cel::CallExpr& call) {
            return HandleNotStrictlyFalse(expr, call);
          };
      call_handlers_[cel::builtin::kNotStrictlyFalseDeprecated] =
          [this](const cel::Expr& expr, const cel::CallExpr& call) {
            return HandleNotStrictlyFalse(expr, call);
          };
      call_handlers_[cel::builtin::kNot] = [this](const cel::Expr& expr,
                                                  const cel::CallExpr& call) {
        return HandleNot(expr, call);
      };
      if (options_.enable_heterogeneous_equality) {
        for (const auto& in_op :
             {cel::builtin::kIn, cel::builtin::kInDeprecated,
              cel::builtin::kInFunction}) {
          call_handlers_[in_op] = [this](const cel::Expr& expr,
                                         const cel::CallExpr& call) {
            return HandleHeterogeneousEqualityIn(expr, call);
          };
        }
        // Try to detect if the environment is setup with a custom equality
        // implementation.
        if (resolver_
                .FindOverloads(cel::builtin::kEqual,
                               /*receiver_style=*/false,
                               {cel::Kind::kAny, cel::Kind::kAny})
                .empty()) {
          call_handlers_[cel::builtin::kEqual] =
              [this](const cel::Expr& expr, const cel::CallExpr& call) {
                return HandleHeterogeneousEquality(expr, call,
                                                   /*inequality=*/false);
              };
          call_handlers_[cel::builtin::kInequal] =
              [this](const cel::Expr& expr, const cel::CallExpr& call) {
                return HandleHeterogeneousEquality(expr, call,
                                                   /*inequality=*/true);
              };
        }
      }
    }
  }

  void SetResolvedType(const cel::Expr& expr, cel::Type type) {
    resolved_types_[&expr] = std::move(type);
  }

  std::optional<cel::Type> GetResolvedType(const cel::Expr* expr) const {
    if (expr == nullptr) {
      return std::nullopt;
    }
    auto it = resolved_types_.find(expr);
    if (it != resolved_types_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  void PreVisitExpr(const cel::Expr& expr) override {
    ValidateOrError(!absl::holds_alternative<cel::UnspecifiedExpr>(expr.kind()),
                    "Invalid empty expression");
    if (!progress_status_.ok()) {
      return;
    }
    if (resume_from_suppressed_branch_ == nullptr &&
        suppressed_branches_.find(&expr) != suppressed_branches_.end()) {
      resume_from_suppressed_branch_ = &expr;
    }

    if (options_.enable_typed_field_access) {
      MaybeResolveType(expr);
    }

    if (block_.has_value()) {
      BlockInfo& block = *block_;
      if (block.in && block.bindings_set.contains(&expr)) {
        block.current_binding = &expr;
      }
    }

    auto* subexpression =
        program_builder_.EnterSubexpression(&expr, SizeHint(expr));
    if (subexpression == nullptr) {
      progress_status_.Update(
          absl::InternalError("same CEL expr visited twice"));
      return;
    }

    for (const std::unique_ptr<ProgramOptimizer>& optimizer :
         program_optimizers_) {
      absl::Status status = optimizer->OnPreVisit(extension_context_, expr);
      if (!status.ok()) {
        SetProgressStatusIfError(status);
      }
    }
  }

  void PostVisitExpr(const cel::Expr& expr) override {
    if (!progress_status_.ok()) {
      return;
    }
    if (&expr == resume_from_suppressed_branch_) {
      resume_from_suppressed_branch_ = nullptr;
    }

    for (const std::unique_ptr<ProgramOptimizer>& optimizer :
         program_optimizers_) {
      absl::Status status = optimizer->OnPostVisit(extension_context_, expr);
      if (!status.ok()) {
        SetProgressStatusIfError(status);
        return;
      }
    }

    program_builder_.ExitSubexpression(&expr);

    if (!comprehension_stack_.empty() &&
        comprehension_stack_.back().is_optimizable_bind &&
        (&comprehension_stack_.back().comprehension->accu_init() == &expr)) {
      SetProgressStatusIfError(
          MaybeExtractSubexpression(&expr, comprehension_stack_.back()));
    }

    if (block_.has_value()) {
      BlockInfo& block = *block_;
      if (block.current_binding == &expr) {
        int index = program_builder_.ExtractSubexpression(&expr);
        if (index == -1) {
          SetProgressStatusIfError(
              absl::InvalidArgumentError("failed to extract subexpression"));
          return;
        }
        block.subexpressions[block.current_index++] = index;
        block.current_binding = nullptr;
      }
    }
  }

  void PostVisitConst(const cel::Expr& expr,
                      const cel::Constant& const_expr) override {
    if (!progress_status_.ok()) {
      return;
    }

    absl::StatusOr<cel::Value> converted_value =
        ConvertConstant(const_expr, extension_context_.MutableArena());

    if (!converted_value.ok()) {
      SetProgressStatusIfError(converted_value.status());
      return;
    }

    AddStep(ExpressionStep::MakeConstant(std::move(converted_value).value(),
                                         expr.id()));
  }

  struct SlotLookupResult {
    int slot;
    int subexpression;
  };

  // Helper to lookup a variable mapped to a slot.
  //
  // If lazy evaluation enabled and ided as a lazy expression,
  // subexpression and slot will be set.
  SlotLookupResult LookupSlot(absl::string_view path) {
    // If there's a leading dot, it cannot resolve to a local variable.
    if (absl::StartsWith(path, ".")) {
      return {-1, -1};
    }
    if (block_.has_value()) {
      const BlockInfo& block = *block_;
      if (block.in) {
        absl::string_view index_suffix = path;
        if (absl::ConsumePrefix(&index_suffix, "@index")) {
          size_t index;
          if (!absl::SimpleAtoi(index_suffix, &index)) {
            SetProgressStatusIfError(
                issue_collector_.AddIssue(RuntimeIssue::CreateError(
                    absl::InvalidArgumentError("bad @index"))));
            return {-1, -1};
          }
          if (index >= block.size) {
            SetProgressStatusIfError(
                issue_collector_.AddIssue(RuntimeIssue::CreateError(
                    absl::InvalidArgumentError(absl::StrCat(
                        "invalid @index greater than number of bindings: ",
                        index, " >= ", block.size)))));
            return {-1, -1};
          }
          if (index >= block.current_index) {
            SetProgressStatusIfError(
                issue_collector_.AddIssue(RuntimeIssue::CreateError(
                    absl::InvalidArgumentError(absl::StrCat(
                        "@index references current or future binding: ", index,
                        " >= ", block.current_index)))));
            return {-1, -1};
          }
          return {static_cast<int>(block.index + index),
                  block.subexpressions[index]};
        }
      }
    }
    if (!comprehension_stack_.empty()) {
      for (int i = comprehension_stack_.size() - 1; i >= 0; i--) {
        const ComprehensionStackRecord& record = comprehension_stack_[i];
        if (record.iter_var_in_scope &&
            record.comprehension->iter_var() == path) {
          if (record.is_optimizable_bind) {
            SetProgressStatusIfError(issue_collector_.AddIssue(
                RuntimeIssue::CreateWarning(absl::InvalidArgumentError(
                    "Unexpected iter_var access in trivial comprehension"))));
            return {-1, -1};
          }
          return {static_cast<int>(record.iter_slot), -1};
        }
        if (record.iter_var2_in_scope &&
            record.comprehension->iter_var2() == path) {
          return {static_cast<int>(record.iter2_slot), -1};
        }
        if (record.accu_var_in_scope &&
            record.comprehension->accu_var() == path) {
          int slot = record.accu_slot;
          int subexpression = -1;
          if (record.is_optimizable_bind) {
            subexpression = record.subexpression;
          }
          return {slot, subexpression};
        }
      }
    }
    if (absl::StartsWith(path, "@it:") || absl::StartsWith(path, "@it2:") ||
        absl::StartsWith(path, "@ac:")) {
      // If we see a CSE generated comprehension variable that was not
      // resolvable through the normal comprehension scope resolution, reject it
      // now rather than surfacing errors at activation time.
      SetProgressStatusIfError(
          issue_collector_.AddIssue(RuntimeIssue::CreateError(
              absl::InvalidArgumentError("out of scope reference to CSE "
                                         "generated comprehension variable"))));
    }
    return {-1, -1};
  }

  // Ident node handler.
  // Invoked after child nodes are processed.
  void PostVisitIdent(const cel::Expr& expr,
                      const cel::IdentExpr& ident_expr) override {
    if (!progress_status_.ok()) {
      return;
    }
    absl::string_view path = ident_expr.name();
    if (!ValidateOrError(
            !path.empty(),
            "Invalid expression: identifier 'name' must not be empty")) {
      return;
    }

    // Check if this is a local variable first (since it should shadow most
    // other interpretations).
    SlotLookupResult slot = LookupSlot(path);

    if (slot.subexpression >= 0) {
      auto* subexpression =
          program_builder_.GetExtractedSubexpression(slot.subexpression);
      if (subexpression == nullptr) {
        SetProgressStatusIfError(
            absl::InternalError("bad subexpression reference"));
        return;
      }
        // Off by one since mainline expression will be index 0.
        AddStep(ExpressionStep::MakeLazyInitStep(
            slot.slot, slot.subexpression + 1, expr.id()));
      return;
    } else if (slot.slot >= 0) {
        AddStep(ExpressionStep::MakeReadSlotStep(slot.slot, expr.id()));
      return;
    }

    // Attempt to resolve a select expression as a namespaced identifier for an
    // enum or type constant value.
    std::optional<cel::Value> const_value;
    int64_t select_root_id = -1;
    std::string path_candidate;

    while (!namespace_stack_.empty()) {
      const auto& select_node = namespace_stack_.front();
      // Generate path in format "<ident>.<field 0>.<field 1>...".
      const cel::Expr* select_expr = select_node.first;
      path_candidate = absl::StrCat(path, ".", select_node.second);

      // Attempt to find a constant enum or type value which matches the
      // qualified path present in the expression. Whether the identifier
      // can be resolved to a type instance depends on whether the option to
      // 'enable_qualified_type_identifiers' is set to true.
      const_value = resolver_.FindConstant(path_candidate, select_expr->id());
      if (const_value) {
        resolved_select_expr_ = select_expr;
        select_root_id = select_expr->id();
        path = path_candidate;
        namespace_stack_.clear();
        break;
      }
      namespace_stack_.pop_front();
    }

    if (!const_value) {
      // Attempt to resolve a simple identifier as an enum or type constant
      // value.
      const_value = resolver_.FindConstant(path, expr.id());
      select_root_id = expr.id();
    }

    // TODO(issues/97): Need to add support for resolving packaged names at
    // runtime if Parse-only. For checked, checker should have reported the
    // expected interpretation.
    if (const_value) {
      // If the path starts with a dot, strip it.
      absl::string_view name = absl::StripPrefix(path, ".");
      AddStep(CreateShadowableValueStep(name, std::move(const_value).value()),
              select_root_id);
      return;
    }

    absl::string_view ident_name = absl::StripPrefix(ident_expr.name(), ".");
      AddStep(ExpressionStep::MakeIdentifierStep(ident_name, expr.id()));
  }

  void PreVisitSelect(const cel::Expr& expr,
                      const cel::SelectExpr& select_expr) override {
    if (!progress_status_.ok()) {
      return;
    }
    if (!ValidateOrError(
            !select_expr.field().empty(),
            "invalid expression: select 'field' must not be empty")) {
      return;
    }
    if (!ValidateOrError(
            select_expr.has_operand() &&
                select_expr.operand().kind_case() !=
                    cel::ExprKindCase::kUnspecifiedExpr,
            "invalid expression: select must specify an operand")) {
      return;
    }

    // Not exactly the cleanest solution - we peek into child of
    // select_expr.
    // Chain of multiple SELECT ending with IDENT can represent namespaced
    // entity.
    if (!select_expr.test_only() && (select_expr.operand().has_ident_expr() ||
                                     select_expr.operand().has_select_expr())) {
      // select expressions are pushed in reverse order:
      // google.type.Expr is pushed as:
      // - field: 'Expr'
      // - field: 'type'
      // - id: 'google'
      //
      // The search order though is as follows:
      // - id: 'google.type.Expr'
      // - id: 'google.type', field: 'Expr'
      // - id: 'google', field: 'type', field: 'Expr'
      for (size_t i = 0; i < namespace_stack_.size(); i++) {
        auto ns = namespace_stack_[i];
        namespace_stack_[i] = {
            ns.first, absl::StrCat(select_expr.field(), ".", ns.second)};
      }
      namespace_stack_.push_back({&expr, select_expr.field()});
    } else {
      namespace_stack_.clear();
    }
  }

  // Select node handler.
  // Invoked after child nodes are processed.
  void PostVisitSelect(const cel::Expr& expr,
                       const cel::SelectExpr& select_expr) override {
    if (!progress_status_.ok()) {
      return;
    }

    // Check if we are "in the middle" of namespaced name.
    // This is currently enum specific. Constant expression that corresponds
    // to resolved enum value has been already created, thus preceding chain
    // of selects is no longer relevant.
    if (resolved_select_expr_) {
      if (&expr == resolved_select_expr_) {
        resolved_select_expr_ = nullptr;
      }
      return;
    }

    std::string field = select_expr.field();
    std::optional<cel::StructType> struct_type;
    std::optional<cel::StructTypeField> field_type;
    if (options_.enable_typed_field_access) {
      std::optional<cel::Type> operand_type =
          GetResolvedType(&select_expr.operand());
      if (operand_type.has_value() && operand_type->IsStruct()) {
        struct_type = operand_type->GetStruct();
        if (struct_type.has_value()) {
          auto field_lookup =
              extension_context_.type_reflector().FindStructTypeFieldByName(
                  *struct_type, select_expr.field());
          // Swallow error to fallback to duck typing behavior.
          if (field_lookup.ok() && field_lookup->has_value()) {
            field_type = *std::move(field_lookup);
          }
        }
      }
    }

    if (field_type.has_value()) {
      AddStep(
          CreateTypedSelectStep(std::move(field), *struct_type,
                                *std::move(field_type), select_expr.test_only(),
                                options_.enable_empty_wrapper_null_unboxing,
                                enable_optional_types_),
          expr.id(), /*stack_delta=*/0);
      return;
    }
    AddStep(CreateSelectStep(std::move(field), select_expr.test_only(),
                             options_.enable_empty_wrapper_null_unboxing,
                             enable_optional_types_),
            expr.id(), /*stack_delta=*/0);
  }

  // Call node handler group.
  // We provide finer granularity for Call node callbacks to allow special
  // handling for short-circuiting
  // PreVisitCall is invoked before child nodes are processed.
  void PreVisitCall(const cel::Expr& expr,
                    const cel::CallExpr& call_expr) override {
    if (!progress_status_.ok()) {
      return;
    }

    std::unique_ptr<CondVisitor> cond_visitor;
    if (call_expr.function() == cel::builtin::kAnd) {
      cond_visitor = std::make_unique<LogicalCondVisitor>(
          this, /*is_or=*/false, options_.short_circuiting);
    } else if (call_expr.function() == cel::builtin::kOr) {
      cond_visitor = std::make_unique<LogicalCondVisitor>(
          this, /*is_or=*/true, options_.short_circuiting);
    } else if (call_expr.function() == cel::builtin::kTernary) {
      if (options_.short_circuiting) {
        cond_visitor = std::make_unique<TernaryCondVisitor>(this);
      } else {
        cond_visitor = std::make_unique<ExhaustiveTernaryCondVisitor>(this);
      }
    } else if (enable_optional_types_ &&
               call_expr.function() == kOptionalOrFn &&
               call_expr.has_target() && call_expr.args().size() == 1) {
      cond_visitor = std::make_unique<OptionalOrCondVisitor>(
          this, /*is_or_value=*/false, options_.short_circuiting);
    } else if (enable_optional_types_ &&
               call_expr.function() == kOptionalOrValueFn &&
               call_expr.has_target() && call_expr.args().size() == 1) {
      cond_visitor = std::make_unique<OptionalOrCondVisitor>(
          this, /*is_or_value=*/true, options_.short_circuiting);
    } else if (IsBlock(&call_expr)) {
      // cel.@block
      if (block_.has_value()) {
        // There can only be one for now.
        SetProgressStatusIfError(
            absl::InvalidArgumentError("multiple cel.@block are not allowed"));
        return;
      }
      block_ = BlockInfo();
      BlockInfo& block = *block_;
      block.in = true;
      if (call_expr.args().empty()) {
        SetProgressStatusIfError(absl::InvalidArgumentError(
            "malformed cel.@block: missing list of bound expressions"));
        return;
      }
      if (call_expr.args().size() != 2) {
        SetProgressStatusIfError(absl::InvalidArgumentError(
            "malformed cel.@block: missing bound expression"));
        return;
      }
      if (!call_expr.args()[0].has_list_expr()) {
        SetProgressStatusIfError(
            absl::InvalidArgumentError("malformed cel.@block: first argument "
                                       "is not a list of bound expressions"));
        return;
      }
      const auto& list_expr = call_expr.args().front().list_expr();
      block.size = list_expr.elements().size();

      block.bindings_set.reserve(block.size);
      for (const auto& list_expr_element : list_expr.elements()) {
        if (list_expr_element.optional()) {
          SetProgressStatusIfError(
              absl::InvalidArgumentError("malformed cel.@block: list of bound "
                                         "expressions contains an optional"));
          return;
        }
        block.bindings_set.insert(&list_expr_element.expr());
      }
      block.index = index_manager().ReserveSlots(block.size);
      block.slot_count = block.size;
      block.expr = &expr;
      block.bindings = &call_expr.args()[0];
      block.bound = &call_expr.args()[1];
      block.subexpressions.resize(block.size, -1);
    } else {
      return;
    }

    if (cond_visitor) {
      cond_visitor->PreVisit(&expr);
      cond_visitor_stack_.push({&expr, std::move(cond_visitor)});
    }
  }

  // Invoked after all child nodes are processed.
  void PostVisitCall(const cel::Expr& expr,
                     const cel::CallExpr& call_expr) override {
    if (!progress_status_.ok()) {
      return;
    }

    auto cond_visitor = FindCondVisitor(&expr);
    if (cond_visitor) {
      cond_visitor->PostVisit(&expr);
      cond_visitor_stack_.pop();
      return;
    }

    // Check if the call is intercepted by a custom handler.
    if (auto handler = call_handlers_.find(call_expr.function());
        handler != call_handlers_.end()) {
      CallHandlerResult result = handler->second(expr, call_expr);
      if (result == CallHandlerResult::kIntercepted) {
        return;
      }  // otherwise, apply default function handling.
    }

    AddResolvedFunctionStep(&call_expr, &expr, call_expr.function());
  }

  void PreVisitComprehension(
      const cel::Expr& expr,
      const cel::ComprehensionExpr& comprehension) override {
    if (!progress_status_.ok()) {
      return;
    }
    if (!ValidateOrError(options_.enable_comprehension,
                         "Comprehension support is disabled")) {
      return;
    }
    const auto& accu_var = comprehension.accu_var();
    const auto& iter_var = comprehension.iter_var();
    const auto& iter_var2 = comprehension.iter_var2();
    ValidateOrError(!accu_var.empty(),
                    "Invalid comprehension: 'accu_var' must not be empty");
    ValidateOrError(!iter_var.empty(),
                    "Invalid comprehension: 'iter_var' must not be empty");
    ValidateOrError(
        accu_var != iter_var,
        "Invalid comprehension: 'accu_var' must not be the same as 'iter_var'");
    ValidateOrError(accu_var != iter_var2,
                    "Invalid comprehension: 'accu_var' must not be the same as "
                    "'iter_var2'");
    ValidateOrError(iter_var2 != iter_var,
                    "Invalid comprehension: 'iter_var2' must not be the same "
                    "as 'iter_var'");
    ValidateOrError(comprehension.has_accu_init(),
                    "Invalid comprehension: 'accu_init' must be set");
    ValidateOrError(comprehension.has_loop_condition(),
                    "Invalid comprehension: 'loop_condition' must be set");
    ValidateOrError(comprehension.has_loop_step(),
                    "Invalid comprehension: 'loop_step' must be set");
    ValidateOrError(comprehension.has_result(),
                    "Invalid comprehension: 'result' must be set");

    size_t iter_slot, iter2_slot, accu_slot, slot_count;
    bool is_bind = IsBind(&comprehension);

    if (is_bind) {
      accu_slot = iter_slot = iter2_slot = index_manager_.ReserveSlots(1);
      slot_count = 1;
    } else if (comprehension.iter_var2().empty()) {
      iter_slot = iter2_slot = index_manager_.ReserveSlots(2);
      accu_slot = iter_slot + 1;
      slot_count = 2;
    } else {
      iter_slot = index_manager_.ReserveSlots(3);
      iter2_slot = iter_slot + 1;
      accu_slot = iter2_slot + 1;
      slot_count = 3;
    }

    if (block_.has_value()) {
      BlockInfo& block = *block_;
      if (block.in) {
        block.slot_count += slot_count;
        slot_count = 0;
      }
    }
    // If this is in the scope of an optimized bind accu-init, account the slots
    // to the outermost bind-init scope.
    //
    // The init expression is effectively inlined at the first usage in the
    // critical path (which is unknown at plan time), so the used slots need to
    // be dedicated for the entire scope of that bind.
    for (ComprehensionStackRecord& record : comprehension_stack_) {
      if (record.in_accu_init && record.is_optimizable_bind) {
        record.slot_count += slot_count;
        slot_count = 0;
        break;
      }
      // If no bind init subexpression, account normally.
    }

    comprehension_stack_.push_back(
        {&expr, &comprehension, iter_slot, iter2_slot, accu_slot, slot_count,
         /*subexpression=*/-1,
         /*.is_optimizable_list_append=*/
         IsOptimizableListAppend(&comprehension,
                                 options_.enable_comprehension_list_append),
         /*.is_optimizable_map_insert=*/
         IsOptimizableMapInsert(&comprehension,
                                options_.enable_comprehension_mutable_map),
         /*.is_optimizable_bind=*/is_bind,
         /*.iter_var_in_scope=*/false,
         /*.iter_var2_in_scope=*/false,
         /*.accu_var_in_scope=*/false,
         /*.in_accu_init=*/false,
         std::make_unique<ComprehensionVisitor>(this, is_bind, iter_slot,
                                                iter2_slot, accu_slot)});
    comprehension_stack_.back().visitor->PreVisit(&expr);
  }

  // Invoked after all child nodes are processed.
  void PostVisitComprehension(
      const cel::Expr& expr,
      const cel::ComprehensionExpr& comprehension_expr) override {
    if (!progress_status_.ok()) {
      return;
    }

    ComprehensionStackRecord& record = comprehension_stack_.back();
    if (comprehension_stack_.empty() ||
        record.comprehension != &comprehension_expr) {
      return;
    }

    record.visitor->PostVisit(&expr);

    index_manager_.ReleaseSlots(record.slot_count);
    comprehension_stack_.pop_back();
  }

  void PreVisitComprehensionSubexpression(
      const cel::Expr& expr, const cel::ComprehensionExpr& compr,
      cel::ComprehensionArg comprehension_arg) override {
    if (!progress_status_.ok()) {
      return;
    }

    if (comprehension_stack_.empty() ||
        comprehension_stack_.back().comprehension != &compr) {
      return;
    }

    ComprehensionStackRecord& record = comprehension_stack_.back();

    switch (comprehension_arg) {
      case cel::ITER_RANGE: {
        record.in_accu_init = false;
        record.iter_var_in_scope = false;
        record.iter_var2_in_scope = false;
        record.accu_var_in_scope = false;
        break;
      }
      case cel::ACCU_INIT: {
        record.in_accu_init = true;
        record.iter_var_in_scope = false;
        record.iter_var2_in_scope = false;
        record.accu_var_in_scope = false;
        break;
      }
      case cel::LOOP_CONDITION: {
        record.in_accu_init = false;
        record.iter_var_in_scope = true;
        record.iter_var2_in_scope = true;
        record.accu_var_in_scope = true;
        break;
      }
      case cel::LOOP_STEP: {
        record.in_accu_init = false;
        record.iter_var_in_scope = true;
        record.iter_var2_in_scope = true;
        record.accu_var_in_scope = true;
        break;
      }
      case cel::RESULT: {
        record.in_accu_init = false;
        record.iter_var_in_scope = false;
        record.iter_var2_in_scope = false;
        record.accu_var_in_scope = true;
        break;
      }
    }
  }

  void PostVisitComprehensionSubexpression(
      const cel::Expr& expr, const cel::ComprehensionExpr& compr,
      cel::ComprehensionArg comprehension_arg) override {
    if (!progress_status_.ok()) {
      return;
    }

    if (comprehension_stack_.empty() ||
        comprehension_stack_.back().comprehension != &compr) {
      return;
    }

    SetProgressStatusIfError(comprehension_stack_.back().visitor->PostVisitArg(
        comprehension_arg, comprehension_stack_.back().expr));
  }

  // Invoked after each argument node processed.
  void PostVisitArg(const cel::Expr& expr, int arg_num) override {
    if (!progress_status_.ok()) {
      return;
    }
    auto cond_visitor = FindCondVisitor(&expr);
    if (cond_visitor) {
      cond_visitor->PostVisitArg(arg_num, &expr);
    }
  }

  void PostVisitTarget(const cel::Expr& expr) override {
    if (!progress_status_.ok()) {
      return;
    }
    auto cond_visitor = FindCondVisitor(&expr);
    if (cond_visitor) {
      cond_visitor->PostVisitTarget(&expr);
    }
  }

  // CreateList node handler.
  // Invoked after child nodes are processed.
  void PostVisitList(const cel::Expr& expr,
                     const cel::ListExpr& list_expr) override {
    if (!progress_status_.ok()) {
      return;
    }

    if (block_.has_value()) {
      BlockInfo& block = *block_;
      if (block.bindings == &expr) {
        // Do nothing, this is the cel.@block bindings list.
        return;
      }
    }

    if (!comprehension_stack_.empty()) {
      const ComprehensionStackRecord& comprehension =
          comprehension_stack_.back();
      if (comprehension.is_optimizable_list_append) {
        if (&(comprehension.comprehension->accu_init()) == &expr) {
          AddStep(ExpressionStep::MakeNewMutableListStep(expr.id()));
          return;
        }
        if (GetOptimizableListAppendOperand(comprehension.comprehension) ==
            &expr) {
          // Avoid wrapping the appended element in a create list step.
          return;
        }
      }
    }
    absl::flat_hash_set<size_t> optional_indices =
        MakeOptionalIndicesSet(list_expr);
    for (size_t index : optional_indices) {
      if (!ValidateOrError(index < list_expr.elements().size(),
                           "Optional index out of range: ", index,
                           ", list size: ", list_expr.elements().size())) {
        return;
      }
    }
    AddStep(CreateCreateListStep(list_expr.elements().size(),
                                 std::move(optional_indices), expr.id()));
  }

  // CreateStruct node handler.
  // Invoked after child nodes are processed.
  void PostVisitStruct(const cel::Expr& expr,
                       const cel::StructExpr& struct_expr) override {
    if (!progress_status_.ok()) {
      return;
    }

    auto status_or_resolved_fields =
        ResolveCreateStructFields(struct_expr, expr.id());
    if (!status_or_resolved_fields.ok()) {
      SetProgressStatusIfError(status_or_resolved_fields.status());
      return;
    }
    std::string resolved_name =
        std::move(status_or_resolved_fields.value().first);
    std::vector<std::string> fields =
        std::move(status_or_resolved_fields.value().second);

    size_t num_fields = fields.size();
    int64_t stack_delta =
        num_fields <= static_cast<size_t>(std::numeric_limits<int64_t>::max())
            ? 1 - static_cast<int64_t>(num_fields)
            : std::numeric_limits<int16_t>::max();
    AddStep(CreateCreateStructStep(std::move(resolved_name), std::move(fields),
                                   MakeOptionalIndicesSet(struct_expr)),
            expr.id(), stack_delta);
  }

  void PostVisitMap(const cel::Expr& expr,
                    const cel::MapExpr& map_expr) override {
    for (const auto& entry : map_expr.entries()) {
      ValidateOrError(entry.has_key(), "Map entry missing key");
      ValidateOrError(entry.has_value(), "Map entry missing value");
    }

    if (!comprehension_stack_.empty()) {
      const ComprehensionStackRecord& comprehension =
          comprehension_stack_.back();
      if (comprehension.is_optimizable_map_insert) {
        if (&(comprehension.comprehension->accu_init()) == &expr) {
          AddStep(CreateMutableMapStep(), expr.id());
          return;
        }
      }
    }

    size_t num_entries = map_expr.entries().size();
    int64_t stack_delta =
        num_entries <=
                static_cast<size_t>(std::numeric_limits<int64_t>::max() / 2)
            ? 1 - 2 * static_cast<int64_t>(num_entries)
            : std::numeric_limits<int16_t>::max();
    AddStep(CreateCreateStructStepForMap(num_entries,
                                         MakeOptionalIndicesSet(map_expr)),
            expr.id(), stack_delta);
  }

  absl::Status progress_status() const { return progress_status_; }

  // Mark a branch as suppressed. The visitor will continue as normal, but
  // any emitted program steps are ignored.
  //
  // Only applies to branches that have not yet been visited (pre-order).
  void SuppressBranch(const cel::Expr* expr) {
    suppressed_branches_.insert(expr);
  }

  void AddResolvedFunctionStep(const cel::CallExpr* call_expr,
                               const cel::Expr* expr,
                               absl::string_view function) {
    // Establish the search criteria for a given function.
    bool receiver_style = call_expr->has_target();
    size_t num_args = call_expr->args().size() + (receiver_style ? 1 : 0);

    // First, search for lazily defined function overloads.
    // Lazy functions shadow eager functions with the same signature.
    auto lazy_overloads = resolver_.FindLazyOverloads(
        function, call_expr->has_target(), num_args, expr->id());
    if (!lazy_overloads.empty()) {
      AddStep(ExpressionStep::MakeLazyFunctionStep(
          CreateLazyFunctionStep(*call_expr, expr->id(),
                                 std::move(lazy_overloads)),
          expr->id()));
      return;
    }

    // Second, search for eagerly defined function overloads.
    auto overloads =
        resolver_.FindOverloads(function, receiver_style, num_args, expr->id());
    if (overloads.empty()) {
      // Create a warning that the overload could not be found. Depending on the
      // builder_warnings configuration, this could result in termination of the
      // CelExpression creation or an inspectable warning for use within runtime
      // logging.
      auto status = issue_collector_.AddIssue(RuntimeIssue::CreateWarning(
          absl::InvalidArgumentError(
              "No overloads provided for FunctionStep creation"),
          RuntimeIssue::ErrorCode::kNoMatchingOverload));
      if (!status.ok()) {
        SetProgressStatusIfError(status);
        return;
      }
    }

    AddStep(ExpressionStep::MakeEagerFunctionStep(
        CreateFunctionStep(*call_expr, expr->id(), std::move(overloads)),
        expr->id()));
  }

  // Add a step to the program, taking ownership. If successful, returns the
  // pointer to the step. Otherwise, returns nullptr.
  //
  // Note: the pointer is only guaranteed to stay valid until the parent
  // subexpression is finalized. Optimizers may modify the program plan which
  // may free the step at that point.
  template <typename T>
  std::enable_if_t<std::is_base_of_v<ExpressionStepLogic, T>, T*> AddStep(
      std::unique_ptr<T> step, int64_t expr_id = -1, int64_t stack_delta = 1) {
    if (progress_status_.ok() && !PlanningSuppressed()) {
      T* ptr = step.get();
      program_builder_.AddStep(ExpressionStep::MakeGenericStep(
          std::move(step), expr_id, stack_delta));
      return ptr;
    }
    return nullptr;
  }

  template <typename T>
  std::enable_if_t<std::is_base_of_v<ExpressionStepLogic, T>, T*> AddStep(
      absl::StatusOr<std::unique_ptr<T>> step, int64_t expr_id = -1,
      int64_t stack_delta = 1) {
    if (step.ok()) {
      return AddStep(*std::move(step), expr_id, stack_delta);
    } else {
      SetProgressStatusIfError(step.status());
    }
    return nullptr;
  }

  ExpressionStep* AddStep(ExpressionStep step) {
    if (progress_status_.ok() && !PlanningSuppressed()) {
      return program_builder_.AddStep(std::move(step));
    }
    return nullptr;
  }

  void SetProgressStatusIfError(const absl::Status& status) {
    if (progress_status_.ok() && !status.ok()) {
      progress_status_ = status;
    }
  }

  // Index of the next step to be inserted, in terms of the current
  // subexpression
  ProgramStepIndex GetCurrentIndex() const {
    // Nonnull while active -- nullptr indicates logic error in the builder.
    ABSL_DCHECK(program_builder_.current() != nullptr);
    return {static_cast<int>(program_builder_.current()->elements().size()),
            program_builder_.current()};
  }

  CondVisitor* FindCondVisitor(const cel::Expr* expr) const {
    if (cond_visitor_stack_.empty()) {
      return nullptr;
    }

    const auto& latest = cond_visitor_stack_.top();

    return (latest.first == expr) ? latest.second.get() : nullptr;
  }

  IndexManager& index_manager() { return index_manager_; }

  size_t slot_count() const { return index_manager_.max_slot_count(); }

  void AddOptimizer(std::unique_ptr<ProgramOptimizer> optimizer) {
    program_optimizers_.push_back(std::move(optimizer));
  }

  // Tests the boolean predicate, and if false produces an InvalidArgumentError
  // which concatenates the error_message and any optional message_parts as the
  // error status message.
  template <typename... MP>
  bool ValidateOrError(bool valid_expression, absl::string_view error_message,
                       MP... message_parts) {
    if (valid_expression) {
      return true;
    }
    SetProgressStatusIfError(absl::InvalidArgumentError(
        absl::StrCat(error_message, message_parts...)));
    return false;
  }

 private:
  struct ComprehensionStackRecord {
    const cel::Expr* expr;
    const cel::ComprehensionExpr* comprehension;
    size_t iter_slot;
    size_t iter2_slot;
    size_t accu_slot;
    size_t slot_count;
    // -1 indicates this shouldn't be used.
    int subexpression;
    bool is_optimizable_list_append;
    bool is_optimizable_map_insert;
    bool is_optimizable_bind;
    bool iter_var_in_scope;
    bool iter_var2_in_scope;
    bool accu_var_in_scope;
    bool in_accu_init;
    std::unique_ptr<ComprehensionVisitor> visitor;
  };

  struct BlockInfo {
    // True if we are currently visiting the `cel.@block` node or any of its
    // children.
    bool in = false;
    // Pointer to the `cel.@block` node.
    const cel::Expr* expr = nullptr;
    // Pointer to the `cel.@block` bindings, that is the first argument to the
    // function.
    const cel::Expr* bindings = nullptr;
    // Set of pointers to the elements of `bindings` above.
    absl::flat_hash_set<const cel::Expr*> bindings_set;
    // Pointer to the `cel.@block` bound expression, that is the second argument
    // to the function.
    const cel::Expr* bound = nullptr;
    // The number of entries in the `cel.@block`.
    size_t size = 0;
    // Starting slot index for `cel.@block`. We occupy he slot indices `index`
    // through `index + size + (var_size * 2)`.
    size_t index = 0;
    // The total number of slots needed for evaluating the bound expressions.
    size_t slot_count = 0;
    // The current slot index we are processing, any index references must be
    // less than this to be valid.
    size_t current_index = 0;
    // Pointer to the current `cel.@block` being processed, that is one of the
    // elements within the first argument.
    const cel::Expr* current_binding = nullptr;
    // Mapping between block indices and their subexpressions, fixed size with
    // exactly `size` elements. Unprocessed indices are set to `-1`.
    std::vector<int> subexpressions;
  };

  bool PlanningSuppressed() const {
    return resume_from_suppressed_branch_ != nullptr;
  }

  absl::Status MaybeExtractSubexpression(const cel::Expr* expr,
                                         ComprehensionStackRecord& record) {
    if (!record.is_optimizable_bind) {
      return absl::OkStatus();
    }

    int index = program_builder_.ExtractSubexpression(expr);
    if (index == -1) {
      return absl::InternalError("Failed to extract subexpression");
    }

    record.subexpression = index;

    record.visitor->MarkAccuInitExtracted();

    return absl::OkStatus();
  }

  // Resolve the name of the message type being created and the names of set
  // fields.
  absl::StatusOr<std::pair<std::string, std::vector<std::string>>>
  ResolveCreateStructFields(const cel::StructExpr& create_struct_expr,
                            int64_t expr_id) {
    absl::string_view ast_name = create_struct_expr.name();

    std::optional<std::pair<std::string, cel::Type>> type;
    CEL_ASSIGN_OR_RETURN(type, resolver_.FindType(ast_name, expr_id));

    if (!type.has_value()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Invalid struct creation: missing type info for '", ast_name, "'"));
    }

    std::string resolved_name = std::move(type).value().first;

    std::vector<std::string> fields;
    fields.reserve(create_struct_expr.fields().size());
    for (const auto& entry : create_struct_expr.fields()) {
      if (entry.name().empty()) {
        return absl::InvalidArgumentError("Struct field missing name");
      }
      if (!entry.has_value()) {
        return absl::InvalidArgumentError("Struct field missing value");
      }
      CEL_ASSIGN_OR_RETURN(auto field, type_provider_.FindStructTypeFieldByName(
                                           resolved_name, entry.name()));
      if (!field.has_value()) {
        return absl::InvalidArgumentError(
            absl::StrCat("Invalid message creation: field '", entry.name(),
                         "' not found in '", resolved_name, "'"));
      }
      fields.push_back(entry.name());
    }

    return std::make_pair(std::move(resolved_name), std::move(fields));
  }

  CallHandlerResult HandleIndex(const cel::Expr& expr,
                                const cel::CallExpr& call);
  CallHandlerResult HandleBlock(const cel::Expr& expr,
                                const cel::CallExpr& call);
  CallHandlerResult HandleListAppend(const cel::Expr& expr,
                                     const cel::CallExpr& call);
  CallHandlerResult HandleNot(const cel::Expr& expr, const cel::CallExpr& call);
  CallHandlerResult HandleNotStrictlyFalse(const cel::Expr& expr,
                                           const cel::CallExpr& call);

  CallHandlerResult HandleHeterogeneousEquality(const cel::Expr& expr,
                                                const cel::CallExpr& call,
                                                bool inequality);

  CallHandlerResult HandleHeterogeneousEqualityIn(const cel::Expr& expr,
                                                  const cel::CallExpr& call);

  void MaybeResolveType(const cel::Expr& expr);

  const Resolver& resolver_;
  const cel::TypeProvider& type_provider_;
  absl::Status progress_status_;
  absl::flat_hash_map<std::string, CallHandler> call_handlers_;

  std::stack<std::pair<const cel::Expr*, std::unique_ptr<CondVisitor>>>
      cond_visitor_stack_;

  // Tracks SELECT-...SELECT-IDENT chains.
  std::deque<std::pair<const cel::Expr*, std::string>> namespace_stack_;

  // When multiple SELECT-...SELECT-IDENT chain is resolved as namespace, this
  // field is used as marker suppressing CelExpression creation for SELECTs.
  const cel::Expr* resolved_select_expr_;

  const cel::RuntimeOptions& options_;

  std::vector<ComprehensionStackRecord> comprehension_stack_;
  absl::flat_hash_set<const cel::Expr*> suppressed_branches_;
  const cel::Expr* resume_from_suppressed_branch_ = nullptr;
  std::vector<std::unique_ptr<ProgramOptimizer>> program_optimizers_;
  const absl::flat_hash_map<int64_t, cel::TypeSpec>& type_map_;
  absl::flat_hash_map<const cel::Expr*, cel::Type> resolved_types_;
  IssueCollector& issue_collector_;

  ProgramBuilder& program_builder_;
  PlannerContext& extension_context_;
  IndexManager index_manager_;

  bool enable_optional_types_;
  std::optional<FlatExprVisitor::BlockInfo> block_;
};

FlatExprVisitor::CallHandlerResult FlatExprVisitor::HandleIndex(
    const cel::Expr& expr, const cel::CallExpr& call_expr) {
  ABSL_DCHECK(call_expr.function() == cel::builtin::kIndex);
  if (!ValidateOrError(
          (call_expr.args().size() == 2 && !call_expr.has_target()) ||
              // TODO(uncreated-issue/79): A few clients use the index operator with a
              // target in custom ASTs.
              (call_expr.args().size() == 1 && call_expr.has_target()),
          "unexpected number of args for builtin index operator")) {
    return CallHandlerResult::kIntercepted;
  }

  AddStep(CreateContainerAccessStep(call_expr, enable_optional_types_),
          expr.id(), /*stack_delta=*/-1);
  return CallHandlerResult::kIntercepted;
}

FlatExprVisitor::CallHandlerResult FlatExprVisitor::HandleNot(
    const cel::Expr& expr, const cel::CallExpr& call_expr) {
  ABSL_DCHECK(call_expr.function() == cel::builtin::kNot);

  if (!ValidateOrError(call_expr.args().size() == 1 && !call_expr.has_target(),
                       "unexpected number of args for builtin not operator")) {
    return CallHandlerResult::kIntercepted;
  }

  AddStep(ExpressionStep::MakeBooleanNotStep(expr.id()));
  return CallHandlerResult::kIntercepted;
}

FlatExprVisitor::CallHandlerResult FlatExprVisitor::HandleNotStrictlyFalse(
    const cel::Expr& expr, const cel::CallExpr& call_expr) {
  if (!ValidateOrError(call_expr.args().size() == 1 && !call_expr.has_target(),
                       "unexpected number of args for builtin "
                       "not_strictly_false operator")) {
    return CallHandlerResult::kIntercepted;
  }

  AddStep(ExpressionStep::MakeNotStrictlyFalseStep(expr.id()));
  return CallHandlerResult::kIntercepted;
}

FlatExprVisitor::CallHandlerResult FlatExprVisitor::HandleBlock(
    const cel::Expr& expr, const cel::CallExpr& call_expr) {
  ABSL_DCHECK(call_expr.function() == kBlock);
  if (!block_.has_value() || block_->expr != &expr ||
      call_expr.args().size() != 2 || call_expr.has_target()) {
    SetProgressStatusIfError(
        absl::InvalidArgumentError("unexpected call to internal cel.@block"));
    return CallHandlerResult::kIntercepted;
  }

  BlockInfo& block = *block_;
  block.in = false;
  index_manager().ReleaseSlots(block.slot_count);

  // Otherwise, iterative plan.
  if (block.slot_count > 0) {
    AddStep(ExpressionStep::MakeClearSlotsStep(block.index, block.slot_count,
                                               expr.id()));
  }

  return CallHandlerResult::kIntercepted;
}

FlatExprVisitor::CallHandlerResult FlatExprVisitor::HandleListAppend(
    const cel::Expr& expr, const cel::CallExpr& call_expr) {
  ABSL_DCHECK(call_expr.function() == cel::builtin::kAdd);

  // Check to see if this is a special case of add that should really be
  // treated as a list append
  if (!comprehension_stack_.empty() &&
      comprehension_stack_.back().is_optimizable_list_append) {
    // Already checked that this is an optimizeable comprehension,
    // check that this is the correct list append node.
    const cel::ComprehensionExpr* comprehension =
        comprehension_stack_.back().comprehension;
    const cel::Expr& loop_step = comprehension->loop_step();

    // Macro loop_step for a map() will contain a list concat operation:
    //   accu_var + [elem]
    const bool is_map_loop_step = &loop_step == &expr;

    // Macro loop_step for a filter() will contain a ternary:
    //   filter ? accu_var + [elem] : accu_var
    const bool is_filter_loop_step =
        loop_step.has_call_expr() &&
        loop_step.call_expr().function() == cel::builtin::kTernary &&
        loop_step.call_expr().args().size() == 3 &&
        &(loop_step.call_expr().args()[1]) == &expr;
    if (is_map_loop_step || is_filter_loop_step) {
        AddStep(ExpressionStep::MakeMutableListAppendStep(expr.id()));
      return CallHandlerResult::kIntercepted;
    }
  }

  return CallHandlerResult::kNotIntercepted;
}

FlatExprVisitor::CallHandlerResult FlatExprVisitor::HandleHeterogeneousEquality(
    const cel::Expr& expr, const cel::CallExpr& call, bool inequality) {
  if (!ValidateOrError(
          call.args().size() == 2 && !call.has_target(),
          "unexpected number of args for builtin equality operator")) {
    return CallHandlerResult::kIntercepted;
  }

  AddStep(inequality ? ExpressionStep::MakeFastNotEqualStep(expr.id())
                     : ExpressionStep::MakeFastEqualStep(expr.id()));
  return CallHandlerResult::kIntercepted;
}

FlatExprVisitor::CallHandlerResult
FlatExprVisitor::HandleHeterogeneousEqualityIn(const cel::Expr& expr,
                                               const cel::CallExpr& call) {
  if (!ValidateOrError(call.args().size() == 2 && !call.has_target(),
                       "unexpected number of args for builtin 'in' operator")) {
    return CallHandlerResult::kIntercepted;
  }

  AddStep(ExpressionStep::MakeFastInStep(expr.id()));
  return CallHandlerResult::kIntercepted;
}

void FlatExprVisitor::MaybeResolveType(const cel::Expr& expr) {
  // Try to resolve the type from the type map, but don't fail if it's not
  // there. This permits cases where the runtime type is compatible but not
  // the same as the type checked type.
  auto it = type_map_.find(expr.id());
  if (it == type_map_.end()) {
    return;
  }
  absl::StatusOr<cel::Type> type = cel::ConvertTypeSpecToType(
      it->second, extension_context_.type_reflector(),
      extension_context_.MutableArena());
  if (!type.ok()) {
    return;
  }
  SetResolvedType(expr, *type);
}

void LogicalCondVisitor::PreVisit(const cel::Expr* expr) {
  visitor_->ValidateOrError(
      !expr->call_expr().has_target() && expr->call_expr().args().size() >= 2,
      "Invalid argument count for a binary function call.");
}

void LogicalCondVisitor::PostVisitArg(int arg_num, const cel::Expr* expr) {
  const int last_arg_index = expr->call_expr().args().size() - 1;
  const size_t num_args = expr->call_expr().args().size();
  if (arg_num == last_arg_index) {
    if (is_or_) {
      visitor_->AddStep(
          ExpressionStep::MakeBooleanOrStep(num_args, expr->id()));
    } else {
      visitor_->AddStep(
          ExpressionStep::MakeBooleanAndStep(num_args, expr->id()));
    }
    if (short_circuiting_ && !jump_steps_.empty()) {
      ProgramStepIndex target = visitor_->GetCurrentIndex();
      for (const ProgramStepIndex& jump_pos : jump_steps_) {
        absl::StatusOr<int> offset = Jump::CalculateOffset(jump_pos, target);
        if (!offset.ok()) {
          visitor_->SetProgressStatusIfError(offset.status());
          continue;
        }
        ExpressionStep* step =
            jump_pos.subexpression->GetIfExpressionStep(jump_pos.index);
        BoolJumpStepInfo* jump_info = GetIfBoolJumpStep(step);
        if (jump_info != nullptr) {
          jump_info->set = true;
          jump_info->offset = *offset;
        }
      }
    }
  }
  if (short_circuiting_ && arg_num < last_arg_index) {
    ProgramStepIndex index = visitor_->GetCurrentIndex();
    visitor_->AddStep(is_or_ ? ExpressionStep::MakeBooleanOrJumpStep(
                                   /*arg_count=*/arg_num + 1)
                             : ExpressionStep::MakeBooleanAndJumpStep(
                                   /*arg_count=*/arg_num + 1));
    jump_steps_.push_back(index);
  }
}

void LogicalCondVisitor::PostVisit(const cel::Expr* expr) {
}

void OptionalOrCondVisitor::PreVisit(const cel::Expr* expr) {
  visitor_->ValidateOrError(
      expr->call_expr().has_target() && expr->call_expr().args().size() == 1,
      "Invalid argument count for or/orValue call.");
}

void OptionalOrCondVisitor::PostVisitTarget(const cel::Expr* expr) {
  if (!short_circuiting_) {
    return;
  }

  // Keep a pointer to the jump step so we can update the target after
  // planning the second argument.
  jump_step_pos_ = visitor_->GetCurrentIndex();
  jump_step_ = visitor_->AddStep(CreateOptionalHasValueJumpStep(is_or_value_));
}

void OptionalOrCondVisitor::PostVisit(const cel::Expr* expr) {
  visitor_->AddStep(CreateOptionalOrStep(is_or_value_), expr->id());
  if (!short_circuiting_) {
    return;
  }

  ABSL_DCHECK(jump_step_ != nullptr)
      << "OptionalOrCondVisitor::PostVisit: jump_step_ is null";
  absl::StatusOr<int> offset =
      Jump::CalculateOffset(jump_step_pos_, visitor_->GetCurrentIndex());
  if (!offset.ok()) {
    visitor_->SetProgressStatusIfError(offset.status());
    return;
  }

  jump_step_->set_jump_offset(*offset);
}

void TernaryCondVisitor::PreVisit(const cel::Expr* expr) {
  visitor_->ValidateOrError(
      !expr->call_expr().has_target() && expr->call_expr().args().size() == 3,
      "Invalid argument count for a ternary function call.");
}

void TernaryCondVisitor::PostVisitArg(int arg_num, const cel::Expr* expr) {
  // Ternary operator "_?_:_" requires a special handing.
  // In contrary to regular function call, its execution affects the control
  // flow of the overall CEL expression.
  // If condition value (argument 0) is True, then control flow is unaffected
  // as it is passed to the first conditional branch. Then, at the end of this
  // branch, the jump is performed over the second conditional branch.
  // If condition value is False, then jump is performed and control is passed
  // to the beginning of the second conditional branch.
  // If condition value is Error, then jump is peformed to bypass both
  // conditional branches and provide Error as result of ternary operation.

  // condition argument for ternary operator
  if (arg_num == 0) {
    // Jump in case of error or non-bool, or jump to the second branch of
    // execution if false.
    cond_jump_pos_ = visitor_->GetCurrentIndex();
    visitor_->AddStep(ExpressionStep::MakeTernaryJumpStep());
  } else if (arg_num == 1) {
    // Jump after the first and over the second branch of execution.
    jump_after_first_pos_ = visitor_->GetCurrentIndex();
    visitor_->AddStep(ExpressionStep::MakeFixedJumpStep());

    ExpressionStep* cond_step =
        cond_jump_pos_.has_value()
            ? cond_jump_pos_->subexpression->GetIfExpressionStep(
                  cond_jump_pos_->index)
            : nullptr;
    TernaryJumpStepInfo* ternary_info = GetIfTernaryJumpStep(cond_step);
    if (!visitor_->ValidateOrError(
            ternary_info != nullptr,
            "Error configuring ternary operator: jump_to_second_ is null")) {
      return;
    }

    absl::StatusOr<int> offset =
        Jump::CalculateOffset(*cond_jump_pos_, visitor_->GetCurrentIndex());
    if (!offset.ok()) {
      visitor_->SetProgressStatusIfError(offset.status());
      return;
    }

    ternary_info->jump_to_second_offset = *offset;
  }
  // Code executed after traversing the final branch of execution
  // (arg_num == 2) is placed in PostVisitCall, to make this method less
  // clattered.
}

void TernaryCondVisitor::PostVisit(const cel::Expr* expr) {
  // Determine and set jump offset in jump instruction.
  ExpressionStep* cond_step =
      cond_jump_pos_.has_value()
          ? cond_jump_pos_->subexpression->GetIfExpressionStep(
                cond_jump_pos_->index)
          : nullptr;
  TernaryJumpStepInfo* ternary_info = GetIfTernaryJumpStep(cond_step);
  if (visitor_->ValidateOrError(
          ternary_info != nullptr,
          "Error configuring ternary operator: error_jump_ is null")) {
    absl::StatusOr<int> offset =
        Jump::CalculateOffset(*cond_jump_pos_, visitor_->GetCurrentIndex());
    if (!offset.ok()) {
      visitor_->SetProgressStatusIfError(offset.status());
    } else {
      ternary_info->set = true;
      ternary_info->error_offset = *offset;
    }
  }
  ExpressionStep* jump_after_first_step =
      jump_after_first_pos_.has_value()
          ? jump_after_first_pos_->subexpression->GetIfExpressionStep(
                jump_after_first_pos_->index)
          : nullptr;
  FixedJumpStepInfo* fixed_info = GetIfFixedJumpStep(jump_after_first_step);
  if (visitor_->ValidateOrError(
          fixed_info != nullptr,
          "Error configuring ternary operator: jump_after_first_ is null")) {
    absl::StatusOr<int> offset = Jump::CalculateOffset(
        *jump_after_first_pos_, visitor_->GetCurrentIndex());
    if (!offset.ok()) {
      visitor_->SetProgressStatusIfError(offset.status());
    } else {
      fixed_info->set = true;
      fixed_info->offset = *offset;
    }
  }
}

void ExhaustiveTernaryCondVisitor::PreVisit(const cel::Expr* expr) {
  visitor_->ValidateOrError(
      !expr->call_expr().has_target() && expr->call_expr().args().size() == 3,
      "Invalid argument count for a ternary function call.");
}

void ExhaustiveTernaryCondVisitor::PostVisit(const cel::Expr* expr) {
  visitor_->AddStep(CreateTernaryStep(), expr->id(), /*stack_delta=*/-2);
}

void ComprehensionVisitor::PreVisit(const cel::Expr* expr) {
  if (is_trivial_) {
    visitor_->SuppressBranch(&expr->comprehension_expr().iter_range());
    visitor_->SuppressBranch(&expr->comprehension_expr().loop_condition());
    visitor_->SuppressBranch(&expr->comprehension_expr().loop_step());
  }
}

ComprehensionCondStep* absl_nullable ComprehensionVisitor::GetCondStep() {
  if (!cond_step_pos_) {
    return nullptr;
  }
  ExpressionStep* step =
      cond_step_pos_->subexpression->GetIfExpressionStep(cond_step_pos_->index);
  if (!step) {
    return nullptr;
  }
  return GetIfComprehensionCondStep(*step);
}

ComprehensionNextStep* absl_nullable ComprehensionVisitor::GetNextStep() {
  if (!next_step_pos_) {
    return nullptr;
  }
  ExpressionStep* step =
      next_step_pos_->subexpression->GetIfExpressionStep(next_step_pos_->index);
  if (!step) {
    return nullptr;
  }
  return GetIfComprehensionNextStep(*step);
}

absl::Status ComprehensionVisitor::PostVisitArgDefault(
    cel::ComprehensionArg arg_num, const cel::Expr* expr) {
  switch (arg_num) {
    case cel::ITER_RANGE: {
      init_step_pos_ = visitor_->GetCurrentIndex();
      if (iter_slot_ != iter2_slot_) {
        init_step_ = visitor_->AddStep(std::make_unique<ComprehensionInitStep>(
            iter_slot_, iter2_slot_, accu_slot_));
      } else {
        init_step_ = visitor_->AddStep(
            std::make_unique<ComprehensionInitStep>(iter_slot_, accu_slot_));
      }
      break;
    }
    case cel::ACCU_INIT: {
      next_step_pos_ = visitor_->GetCurrentIndex();
      if (iter_slot_ != iter2_slot_) {
        visitor_->AddStep(ExpressionStep::MakeComprehensionNext2Step());
      } else {
        visitor_->AddStep(ExpressionStep::MakeComprehensionNextStep());
      }
      break;
    }
    case cel::LOOP_CONDITION: {
      cond_step_pos_ = visitor_->GetCurrentIndex();
      if (iter_slot_ != iter2_slot_) {
        visitor_->AddStep(ExpressionStep::MakeComprehensionCond2Step());
      } else {
        visitor_->AddStep(ExpressionStep::MakeComprehensionCondStep());
      }
      break;
    }
    case cel::LOOP_STEP: {
      ProgramStepIndex index = visitor_->GetCurrentIndex();
      ExpressionStep* jump_step =
          visitor_->AddStep(ExpressionStep::MakeFixedJumpStep());
      FixedJumpStepInfo* fixed_info = GetIfFixedJumpStep(jump_step);
      if (fixed_info == nullptr) {
        // either an error occurred earlier or planning is suppressed.
        break;
      }
      CEL_ASSIGN_OR_RETURN(int offset,
                           Jump::CalculateOffset(index, *next_step_pos_));
      fixed_info->set = true;
      fixed_info->offset = offset;

      // Set offsets jumping to the result step.
      if (auto* cond_step = GetCondStep(); cond_step != nullptr) {
        CEL_ASSIGN_OR_RETURN(int jump_from_cond,
                             Jump::CalculateOffset(
                                 *cond_step_pos_, visitor_->GetCurrentIndex()));
        cond_step->set_jump_offset(jump_from_cond);
      } else {
        return absl::InvalidArgumentError("Comprehension is malformed");
      }

      if (auto* next_step = GetNextStep(); next_step != nullptr) {
        CEL_ASSIGN_OR_RETURN(int jump_from_next,
                             Jump::CalculateOffset(
                                 *next_step_pos_, visitor_->GetCurrentIndex()));

        next_step->set_jump_offset(jump_from_next);
      } else {
        return absl::InvalidArgumentError("Comprehension is malformed");
      }
      break;
    }
    case cel::RESULT: {
      if (!init_step_ || !next_step_pos_ || !cond_step_pos_) {
        // Encountered an error earlier. Can't determine where to jump.
        break;
      }
      visitor_->AddStep(
          ExpressionStep::MakeComprehensionFinishStep(accu_slot_, expr->id()));
      // Set offsets jumping past the result step in case of errors.
      CEL_ASSIGN_OR_RETURN(
          int jump_from_init,
          Jump::CalculateOffset(init_step_pos_, visitor_->GetCurrentIndex()));
      init_step_->set_error_jump_offset(jump_from_init);

      if (auto* next_step = GetNextStep(); next_step != nullptr) {
        CEL_ASSIGN_OR_RETURN(int jump_from_next,
                             Jump::CalculateOffset(
                                 *next_step_pos_, visitor_->GetCurrentIndex()));
        next_step->set_error_jump_offset(jump_from_next);
      } else {
        return absl::InvalidArgumentError("Comprehension is malformed");
      }

      if (auto* cond_step = GetCondStep(); cond_step != nullptr) {
        CEL_ASSIGN_OR_RETURN(int jump_from_cond,
                             Jump::CalculateOffset(
                                 *cond_step_pos_, visitor_->GetCurrentIndex()));
        cond_step->set_error_jump_offset(jump_from_cond);
      } else {
        return absl::InvalidArgumentError("Comprehension is malformed");
      }
      break;
    }
  }
  return absl::OkStatus();
}

void ComprehensionVisitor::PostVisitArgTrivial(cel::ComprehensionArg arg_num,
                                               const cel::Expr* expr) {
  switch (arg_num) {
    case cel::ITER_RANGE: {
      break;
    }
    case cel::ACCU_INIT: {
      if (!accu_init_extracted_) {
        visitor_->AddStep(ExpressionStep::MakeAssignSlotAndPopStep(accu_slot_));
      }
      break;
    }
    case cel::LOOP_CONDITION: {
      break;
    }
    case cel::LOOP_STEP: {
      break;
    }
    case cel::RESULT: {
      visitor_->AddStep(
          ExpressionStep::MakeClearSlotStep(accu_slot_, expr->id()));
      break;
    }
  }
}

void ComprehensionVisitor::PostVisit(const cel::Expr* expr) {
}

// Flattens the expression table into the end of the mainline expression vector
// and returns an index to the individual sub expressions.
std::vector<ExecutionPathView> FlattenExpressionTable(
    ProgramBuilder& program_builder, ExecutionPath& main) {
  std::vector<std::pair<size_t, size_t>> ranges;
  main = program_builder.FlattenMain();
  ranges.push_back(std::make_pair(0, main.size()));

  std::vector<ExecutionPath> subexpressions =
      program_builder.FlattenSubexpressions();
  for (auto& subexpression : subexpressions) {
    ranges.push_back(std::make_pair(main.size(), subexpression.size()));
    absl::c_move(subexpression, std::back_inserter(main));
  }

  std::vector<ExecutionPathView> subexpression_indexes;
  subexpression_indexes.reserve(ranges.size());
  for (const auto& range : ranges) {
    subexpression_indexes.push_back(
        absl::MakeSpan(main).subspan(range.first, range.second));
  }
  return subexpression_indexes;
}

std::optional<int64_t> CheckedDeltaAdd(int64_t current,
                                       std::optional<int64_t> delta) {
  if (!delta.has_value()) {
    return std::nullopt;
  }
  if (*delta > 0 && current > std::numeric_limits<int64_t>::max() - *delta) {
    return std::nullopt;
  }
  if (*delta < 0 && current < std::numeric_limits<int64_t>::min() - *delta) {
    return std::nullopt;
  }
  current += *delta;
  if (current < 0) {
    return std::nullopt;
  }
  return current;
}

// Conservative estimate of the maximum value stack size needed for the given
// subexpressions.
//
// If overflow occurs, returns fallback_size, which is the total number of
// steps in the program.
size_t EstimateMaxStackSize(absl::Span<const ExecutionPathView> subexpressions,
                            size_t fallback_size) {
  size_t total_max_stack = 0;
  for (ExecutionPathView path : subexpressions) {
    int64_t current = 0;
    int64_t max_depth = 0;
    for (const ExpressionStep& step : path) {
      std::optional<int64_t> next = CheckedDeltaAdd(current, step.StackDelta());
      if (!next.has_value()) {
        return fallback_size;
      }
      current = *next;
      max_depth = std::max(max_depth, current);
    }
    if (static_cast<uint64_t>(max_depth) >
        std::numeric_limits<size_t>::max() - total_max_stack) {
      return fallback_size;
    }
    total_max_stack += static_cast<size_t>(max_depth);
  }
  return total_max_stack;
}

absl::Status CheckAstExtensions(
    const std::vector<cel::ExtensionSpec>& extensions) {
  for (const cel::ExtensionSpec& extension : extensions) {
    if (extension.id() == "cel_block" && extension.version().major() == 1) {
      // cel_block v1 is always supported.
      continue;
    }

    // TODO(uncreated-issue/89): Add support for json field names.
    return absl::InvalidArgumentError(absl::StrCat(
        "unsupported CEL extension: ", extension.id(), "@",
        extension.version().major(), ".", extension.version().minor()));
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<FlatExpression> FlatExprBuilder::CreateExpressionImpl(
    std::unique_ptr<Ast> ast, std::vector<RuntimeIssue>* issues) const {
  if (absl::StartsWith(container_, ".") || absl::EndsWith(container_, ".")) {
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid expression container: '", container_, "'"));
  }

  RuntimeIssue::Severity max_severity = options_.fail_on_warnings
                                            ? RuntimeIssue::Severity::kWarning
                                            : RuntimeIssue::Severity::kError;
  IssueCollector issue_collector(max_severity);

  absl::StatusOr<std::vector<cel::ExtensionSpec>> runtime_extensions =
      ExtractAndValidateRuntimeExtensions(*ast);

  if (!runtime_extensions.ok()) {
    CEL_RETURN_IF_ERROR(issue_collector.AddIssue(
        RuntimeIssue::CreateError(runtime_extensions.status())));
  }

  auto status = CheckAstExtensions(*runtime_extensions);
  if (!status.ok()) {
    CEL_RETURN_IF_ERROR(
        issue_collector.AddIssue(RuntimeIssue::CreateError(status)));
  }

  Resolver resolver(container_, function_registry_, type_registry_,
                    GetTypeProvider(),
                    options_.enable_qualified_type_identifiers);

  std::shared_ptr<google::protobuf::Arena> arena;
  ProgramBuilder program_builder;
  PlannerContext extension_context(env_, resolver, options_, GetTypeProvider(),
                                   issue_collector, program_builder, arena);

  for (const std::unique_ptr<AstTransform>& transform : ast_transforms_) {
    CEL_RETURN_IF_ERROR(transform->UpdateAst(extension_context, *ast));
  }

  std::vector<std::unique_ptr<ProgramOptimizer>> optimizers;
  for (const ProgramOptimizerFactory& optimizer_factory : program_optimizers_) {
    CEL_ASSIGN_OR_RETURN(auto optimizer,
                         optimizer_factory(extension_context, *ast));
    if (optimizer != nullptr) {
      optimizers.push_back(std::move(optimizer));
    }
  }

  // These objects are expected to remain scoped to one build call -- references
  // to them shouldn't be persisted in any part of the result expression.
  FlatExprVisitor visitor(resolver, options_, std::move(optimizers),
                          ast->type_map(), GetTypeProvider(), issue_collector,
                          program_builder, extension_context,
                          enable_optional_types_);

  cel::TraversalOptions opts;
  opts.use_comprehension_callbacks = true;
  AstTraverse(ast->root_expr(), visitor, opts);

  if (!visitor.progress_status().ok()) {
    return visitor.progress_status();
  }

  if (visitor.slot_count() > std::numeric_limits<uint32_t>::max() ||
      program_builder.ExtractedSubexpressionCount() >
          std::numeric_limits<uint32_t>::max()) {
    // Impractical to trigger (we'd run out of memory first), but assuming this
    // allows us to pack references to slots and subexpressions.
    return absl::InternalError(
        "Expression too large to be executed, exceeds uint32_t limits.");
  }

  if (issues != nullptr) {
    (*issues) = issue_collector.ExtractIssues();
  }

  ExecutionPath execution_path;
  std::vector<ExecutionPathView> subexpressions =
      FlattenExpressionTable(program_builder, execution_path);
  size_t value_stack_size =
      EstimateMaxStackSize(subexpressions, execution_path.size());

  return FlatExpression(std::move(execution_path), std::move(subexpressions),
                        visitor.slot_count(), GetTypeProvider(), options_,
                        std::move(arena), value_stack_size);
}
const cel::TypeProvider& FlatExprBuilder::GetTypeProvider() const {
  return use_legacy_type_provider_
             ? static_cast<const cel::TypeProvider&>(
                   GetLegacyRuntimeTypeProvider(type_registry_))
             : GetRuntimeTypeProvider(type_registry_);
}

}  // namespace google::api::expr::runtime
