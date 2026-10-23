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

#include "extensions/set_membership_optimization.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/container/flat_hash_set.h"
#include "absl/hash/hash.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "base/builtins.h"
#include "common/ast.h"
#include "common/constant.h"
#include "common/expr.h"
#include "common/native_type.h"
#include "common/standard_definitions.h"
#include "common/type.h"
#include "common/value.h"
#include "eval/compiler/flat_expr_builder.h"
#include "eval/compiler/flat_expr_builder_extensions.h"
#include "eval/eval/attribute_utility.h"
#include "eval/eval/bitset_steps.h"
#include "eval/eval/evaluator_core.h"
#include "eval/eval/expression_step_base.h"
#include "internal/casts.h"
#include "internal/status_macros.h"
#include "runtime/internal/errors.h"
#include "runtime/internal/runtime_friend_access.h"
#include "runtime/internal/runtime_impl.h"
#include "runtime/runtime.h"
#include "runtime/runtime_builder.h"

namespace cel::extensions {
namespace {

using ::google::api::expr::runtime::AttributeTrail;
using ::google::api::expr::runtime::AttributeUtility;
using ::google::api::expr::runtime::ExecutionFrame;
using ::google::api::expr::runtime::ExecutionFrameBase;
using ::google::api::expr::runtime::ExecutionPath;
using ::google::api::expr::runtime::ExecutionPathView;
using ::google::api::expr::runtime::ExpressionStep;
using ::google::api::expr::runtime::ExpressionStepBase;
using ::google::api::expr::runtime::ExtractBit;
using ::google::api::expr::runtime::FlatExprBuilder;
using ::google::api::expr::runtime::GetIfConstant;
using ::google::api::expr::runtime::PlannerContext;
using ::google::api::expr::runtime::ProgramOptimizer;
using ::google::api::expr::runtime::ProgramOptimizerFactory;

// Repeated here to support @in without adding dependency on the sets
// extension.
constexpr absl::string_view kSetsContains = "sets.contains";
constexpr absl::string_view kSetsIntersects = "sets.intersects";
constexpr absl::string_view kSetsEquivalent = "sets.equivalent";

constexpr absl::string_view kSetsContainsOverloadId = "list_sets_contains_list";
constexpr absl::string_view kSetsIntersectsOverloadId =
    "list_sets_intersects_list";
constexpr absl::string_view kSetsEquivalentOverloadId =
    "list_sets_equivalent_list";

struct StringSetHash {
  using is_transparent = void;
  size_t operator()(absl::string_view v) const { return absl::HashOf(v); }
  size_t operator()(const StringValue& v) const { return absl::HashOf(v); }
};

struct StringSetEq {
  using is_transparent = void;
  bool operator()(absl::string_view lhs, absl::string_view rhs) const {
    return lhs == rhs;
  }
  bool operator()(absl::string_view lhs, const StringValue& rhs) const {
    return lhs == rhs;
  }
  bool operator()(const StringValue& lhs, absl::string_view rhs) const {
    return lhs == rhs;
  }
  bool operator()(const StringValue& lhs, const StringValue& rhs) const {
    return lhs == rhs;
  }
};

using StringSet = absl::flat_hash_set<std::string, StringSetHash, StringSetEq>;

enum class StringSetListOp {
  kIntersects,
  kContains,
  kEquivalent,
};

absl::string_view StringSetListOpName(StringSetListOp op) {
  switch (op) {
    case StringSetListOp::kIntersects:
      return kSetsIntersects;
    case StringSetListOp::kContains:
      return kSetsContains;
    case StringSetListOp::kEquivalent:
      return kSetsEquivalent;
  }
  return "";
}

class StringSetInStep final : public ExpressionStepBase {
 public:
  StringSetInStep(StringSet set, int64_t expr_id)
      : ExpressionStepBase(expr_id), set_(std::move(set)) {}

  void Evaluate(ExecutionFrame* frame) const override {
    if (!frame->value_stack().HasEnough(1)) {
      frame->Abort(absl::InternalError("StringSetInStep: stack underflow"));
      return;
    }
    const Value& arg = frame->value_stack().Peek();
    if (arg.IsError()) {
      return;
    }
    if (frame->unknown_processing_enabled()) {
      AttributeUtility::Accumulator accu =
          frame->attribute_utility().CreateAccumulator();
      accu.MaybeAdd(arg, frame->value_stack().PeekAttribute());
      if (!accu.IsEmpty()) {
        frame->value_stack().PopAndPush(std::move(accu).Build(frame->arena()));
        return;
      }
    }
    bool found = arg.IsString() && set_.contains(arg.GetString());
    frame->value_stack().PopAndPush(BoolValue(found));
  }

 private:
  StringSet set_;
};

class StringSetListStep final : public ExpressionStepBase {
 public:
  StringSetListStep(StringSetListOp op, StringSet set, int64_t expr_id)
      : ExpressionStepBase(expr_id), op_(op), set_(std::move(set)) {}

  void Evaluate(ExecutionFrame* frame) const override {
    if (!frame->value_stack().HasEnough(1)) {
      frame->Abort(absl::InternalError("StringSetListStep: stack underflow"));
      return;
    }
    const Value& arg = frame->value_stack().Peek();
    if (arg.IsError()) {
      return;
    }
    if (frame->unknown_processing_enabled()) {
      AttributeUtility::Accumulator accu =
          frame->attribute_utility().CreateAccumulator();
      accu.MaybeAdd(arg, frame->value_stack().PeekAttribute());
      if (!accu.IsEmpty()) {
        frame->value_stack().PopAndPush(std::move(accu).Build(frame->arena()));
        return;
      }
    }

    if (!arg.IsList()) {
      frame->value_stack().PopAndPush(cel::ErrorValue::From(
          cel::runtime_internal::CreateNoMatchingOverloadError(
              StringSetListOpName(op_)),
          frame->arena()));
      return;
    }

    const ListValue& list = arg.GetList();
    absl::StatusOr<bool> result = EvaluateList(*frame, list);
    if (!result.ok()) {
      frame->Abort(std::move(result).status());
      return;
    }
    frame->value_stack().PopAndPush(BoolValue(*result));
  }

 private:
  absl::StatusOr<bool> EvaluateList(ExecutionFrameBase& frame,
                                    const ListValue& list) const {
    switch (op_) {
      case StringSetListOp::kIntersects: {
        bool intersects = false;
        CEL_RETURN_IF_ERROR(list.ForEach(
            [&](size_t /*index*/,
                const Value& element) -> absl::StatusOr<bool> {
              if (element.IsString() && set_.contains(element.GetString())) {
                intersects = true;
                return false;
              }
              return true;
            },
            frame.descriptor_pool(), frame.message_factory(), frame.arena()));
        return intersects;
      }
      case StringSetListOp::kContains: {
        CEL_ASSIGN_OR_RETURN(size_t list_size, list.Size());
        if (list_size < set_.size()) {
          return false;
        }
        absl::flat_hash_set<absl::string_view> matched;
        CEL_RETURN_IF_ERROR(list.ForEach(
            [&](size_t index, const Value& element) -> absl::StatusOr<bool> {
              if (element.IsString()) {
                auto it = set_.find(element.GetString());
                if (it != set_.end()) {
                  matched.insert(*it);
                  if (matched.size() == set_.size()) {
                    return false;
                  }
                }
              }
              size_t remaining = index < list_size ? list_size - 1 - index : 0;
              if (remaining < set_.size() - matched.size()) {
                return false;
              }
              return true;
            },
            frame.descriptor_pool(), frame.message_factory(), frame.arena()));
        return matched.size() == set_.size();
      }
      case StringSetListOp::kEquivalent: {
        CEL_ASSIGN_OR_RETURN(size_t list_size, list.Size());
        if (list_size < set_.size()) {
          return false;
        }
        bool all_in_set = true;
        absl::flat_hash_set<absl::string_view> matched;
        CEL_RETURN_IF_ERROR(list.ForEach(
            [&](size_t /*index*/,
                const Value& element) -> absl::StatusOr<bool> {
              if (!element.IsString()) {
                all_in_set = false;
                return false;
              }
              auto it = set_.find(element.GetString());
              if (it == set_.end()) {
                all_in_set = false;
                return false;
              }
              matched.insert(*it);
              return true;
            },
            frame.descriptor_pool(), frame.message_factory(), frame.arena()));
        return all_in_set && matched.size() == set_.size();
      }
    }
    return false;
  }

  StringSetListOp op_;
  StringSet set_;
};

std::optional<uint64_t> TryExtractBitmaskFromListExpr(
    const ListExpr& list_expr) {
  uint64_t bitmask = 0;

  for (const ListExprElement& element : list_expr.elements()) {
    if (element.optional() || !element.has_expr() ||
        !element.expr().has_const_expr()) {
      return std::nullopt;
    }
    const Constant& constant = element.expr().const_expr();
    ConstantKindCase kind = constant.kind_case();
    if (kind != ConstantKindCase::kInt && kind != ConstantKindCase::kUint) {
      return std::nullopt;
    }

    if (kind == ConstantKindCase::kInt) {
      int64_t v = constant.int_value();
      if (v < 0 || v >= 64) {
        return std::nullopt;
      }
      bitmask |= (uint64_t{1} << v);
    } else {
      uint64_t v = constant.uint_value();
      if (v >= 64) {
        return std::nullopt;
      }
      bitmask |= (uint64_t{1} << v);
    }
  }

  return bitmask;
}

std::optional<uint64_t> TryExtractBitmaskFromFoldedList(
    PlannerContext& context, const ListValue& list_value) {
  uint64_t bitmask = 0;
  bool valid = true;

  absl::Status status = list_value.ForEach(
      [&](size_t /*index*/, const Value& element) -> absl::StatusOr<bool> {
        uint64_t bit = ExtractBit(element);
        if (bit == 0) {
          valid = false;
          return false;
        }
        bitmask |= bit;
        return true;
      },
      context.descriptor_pool(), context.MutableMessageFactory(),
      context.MutableArena());

  if (!status.ok() || !valid) {
    return std::nullopt;
  }
  return bitmask;
}

std::optional<uint64_t> TryExtractBitmask(PlannerContext& context,
                                          const Expr& list_arg) {
  ExecutionPathView subplan = context.GetSubplan(list_arg);
  if (subplan.empty()) {
    return std::nullopt;
  }
  if (subplan.size() == 1) {
    Value constant_value;
    if (GetIfConstant(subplan[0], constant_value) && constant_value.IsList()) {
      return TryExtractBitmaskFromFoldedList(context, constant_value.GetList());
    }
  }

  if (list_arg.has_list_expr()) {
    return TryExtractBitmaskFromListExpr(list_arg.list_expr());
  }

  return std::nullopt;
}

bool IsStringListType(const PlannerContext& context, int64_t expr_id) {
  std::optional<Type> type = context.GetResolvedType(expr_id);
  if (!type.has_value() || !type->IsList()) {
    return false;
  }
  return type->GetList().GetElement().IsString();
}

std::optional<StringSet> TryExtractStringSetFromListExpr(
    const ListExpr& list_expr, size_t min_set_size) {
  if (list_expr.elements().empty() ||
      list_expr.elements().size() < min_set_size) {
    return std::nullopt;
  }
  StringSet set;
  set.reserve(list_expr.elements().size());
  for (const ListExprElement& element : list_expr.elements()) {
    if (element.optional() || !element.has_expr() ||
        !element.expr().has_const_expr()) {
      return std::nullopt;
    }
    const Constant& constant = element.expr().const_expr();
    if (constant.kind_case() != ConstantKindCase::kString) {
      return std::nullopt;
    }
    set.insert(constant.string_value());
  }
  return set;
}

std::optional<StringSet> TryExtractStringSetFromFoldedList(
    PlannerContext& context, const ListValue& list_value, size_t min_set_size) {
  absl::StatusOr<size_t> size = list_value.Size();
  if (!size.ok() || *size == 0 || *size < min_set_size) {
    return std::nullopt;
  }
  StringSet set;
  set.reserve(*size);
  bool valid = true;

  absl::Status status = list_value.ForEach(
      [&](size_t /*index*/, const Value& element) -> absl::StatusOr<bool> {
        if (!element.IsString()) {
          valid = false;
          return false;
        }
        set.insert(element.GetString().ToString());
        return true;
      },
      context.descriptor_pool(), context.MutableMessageFactory(),
      context.MutableArena());

  if (!status.ok() || !valid) {
    return std::nullopt;
  }
  return set;
}

std::optional<StringSet> TryExtractStringSet(PlannerContext& context,
                                             const Expr& list_arg,
                                             size_t min_set_size) {
  if (!IsStringListType(context, list_arg.id())) {
    return std::nullopt;
  }
  ExecutionPathView subplan = context.GetSubplan(list_arg);
  if (subplan.empty()) {
    return std::nullopt;
  }
  if (subplan.size() == 1) {
    Value constant_value;
    if (GetIfConstant(subplan[0], constant_value) && constant_value.IsList()) {
      return TryExtractStringSetFromFoldedList(
          context, constant_value.GetList(), min_set_size);
    }
  }

  if (list_arg.has_list_expr()) {
    return TryExtractStringSetFromListExpr(list_arg.list_expr(), min_set_size);
  }

  return std::nullopt;
}

bool IsInFunction(absl::string_view fn) {
  return fn == cel::builtin::kIn || fn == cel::builtin::kInDeprecated ||
         fn == cel::builtin::kInFunction;
}

bool IsTargetFunction(absl::string_view fn) {
  return IsInFunction(fn) || fn == kSetsContains || fn == kSetsIntersects ||
         fn == kSetsEquivalent;
}

bool HasExpectedOverloadOrUnchecked(const Ast& ast, int64_t node_id,
                                    absl::string_view expected_overload_id) {
  const Reference* reference = ast.GetReference(node_id);
  if (reference == nullptr || reference->overload_id().empty()) {
    return true;
  }
  return reference->overload_id().size() == 1 &&
         reference->overload_id()[0] == expected_overload_id;
}

class SetMembershipOptimizer : public ProgramOptimizer {
 public:
  explicit SetMembershipOptimizer(
      const Ast& ast, const SetMembershipOptimizationOptions& options)
      : ast_(ast), options_(options) {}

  absl::Status OnPreVisit(PlannerContext& context, const Expr& node) override {
    return absl::OkStatus();
  }

  absl::Status OnPostVisit(PlannerContext& context, const Expr& node) override {
    if (!node.has_call_expr()) {
      return absl::OkStatus();
    }

    const CallExpr& call = node.call_expr();
    if (call.has_target() || call.args().size() != 2) {
      return absl::OkStatus();
    }

    absl::string_view fn = call.function();
    if (!IsTargetFunction(fn)) {
      return absl::OkStatus();
    }

    const Expr& arg0 = call.args()[0];
    const Expr& arg1 = call.args()[1];
    if (!context.NodeHasInspectablePlan(arg0) ||
        !context.NodeHasInspectablePlan(arg1)) {
      return absl::OkStatus();
    }

    if (IsInFunction(fn)) {
      if (!HasExpectedOverloadOrUnchecked(ast_, node.id(),
                                          StandardOverloadIds::kInList)) {
        return absl::OkStatus();
      }
      std::optional<uint64_t> bitmask = TryExtractBitmask(context, arg1);
      if (bitmask.has_value()) {
        return ReplaceWithOptimizedStep(
            context, node, arg0,
            ExpressionStep::MakeBitIsSetStep(*bitmask, node.id()));
      }
      std::optional<StringSet> str_set =
          TryExtractStringSet(context, arg1, options_.min_set_size);
      if (str_set.has_value()) {
        return ReplaceWithOptimizedStep(
            context, node, arg0,
            ExpressionStep::MakeGenericStep(std::make_unique<StringSetInStep>(
                                                std::move(*str_set), node.id()),
                                            node.id(), /*stack_delta=*/0));
      }
      return absl::OkStatus();
    }

    if (fn == kSetsContains) {
      if (!HasExpectedOverloadOrUnchecked(ast_, node.id(),
                                          kSetsContainsOverloadId)) {
        return absl::OkStatus();
      }
      std::optional<uint64_t> bitmask = TryExtractBitmask(context, arg1);
      if (bitmask.has_value()) {
        return ReplaceWithOptimizedStep(
            context, node, arg0,
            ExpressionStep::MakeBitAndEqualStep(*bitmask, node.id()));
      }
      std::optional<StringSet> str_set =
          TryExtractStringSet(context, arg1, options_.min_set_size);
      if (str_set.has_value()) {
        return ReplaceWithOptimizedStep(
            context, node, arg0,
            ExpressionStep::MakeGenericStep(
                std::make_unique<StringSetListStep>(
                    StringSetListOp::kContains, std::move(*str_set), node.id()),
                node.id(), /*stack_delta=*/0));
      }
      return absl::OkStatus();
    }

    if (fn == kSetsIntersects) {
      if (!HasExpectedOverloadOrUnchecked(ast_, node.id(),
                                          kSetsIntersectsOverloadId)) {
        return absl::OkStatus();
      }
      std::optional<uint64_t> bitmask = TryExtractBitmask(context, arg1);
      if (bitmask.has_value()) {
        return ReplaceWithOptimizedStep(
            context, node, arg0,
            ExpressionStep::MakeBitAndIsNonZeroStep(*bitmask, node.id()));
      }
      bitmask = TryExtractBitmask(context, arg0);
      if (bitmask.has_value()) {
        return ReplaceWithOptimizedStep(
            context, node, arg1,
            ExpressionStep::MakeBitAndIsNonZeroStep(*bitmask, node.id()));
      }
      std::optional<StringSet> str_set =
          TryExtractStringSet(context, arg1, options_.min_set_size);
      if (str_set.has_value()) {
        return ReplaceWithOptimizedStep(
            context, node, arg0,
            ExpressionStep::MakeGenericStep(std::make_unique<StringSetListStep>(
                                                StringSetListOp::kIntersects,
                                                std::move(*str_set), node.id()),
                                            node.id(), /*stack_delta=*/0));
      }
      str_set = TryExtractStringSet(context, arg0, options_.min_set_size);
      if (str_set.has_value()) {
        return ReplaceWithOptimizedStep(
            context, node, arg1,
            ExpressionStep::MakeGenericStep(std::make_unique<StringSetListStep>(
                                                StringSetListOp::kIntersects,
                                                std::move(*str_set), node.id()),
                                            node.id(), /*stack_delta=*/0));
      }
      return absl::OkStatus();
    }

    if (fn == kSetsEquivalent) {
      if (!HasExpectedOverloadOrUnchecked(ast_, node.id(),
                                          kSetsEquivalentOverloadId)) {
        return absl::OkStatus();
      }
      std::optional<uint64_t> bitmask = TryExtractBitmask(context, arg1);
      if (bitmask.has_value()) {
        return ReplaceWithOptimizedStep(
            context, node, arg0,
            ExpressionStep::MakeBitEqualStep(*bitmask, node.id()));
      }
      bitmask = TryExtractBitmask(context, arg0);
      if (bitmask.has_value()) {
        return ReplaceWithOptimizedStep(
            context, node, arg1,
            ExpressionStep::MakeBitEqualStep(*bitmask, node.id()));
      }
      std::optional<StringSet> str_set =
          TryExtractStringSet(context, arg1, options_.min_set_size);
      if (str_set.has_value()) {
        return ReplaceWithOptimizedStep(
            context, node, arg0,
            ExpressionStep::MakeGenericStep(std::make_unique<StringSetListStep>(
                                                StringSetListOp::kEquivalent,
                                                std::move(*str_set), node.id()),
                                            node.id(), /*stack_delta=*/0));
      }
      str_set = TryExtractStringSet(context, arg0, options_.min_set_size);
      if (str_set.has_value()) {
        return ReplaceWithOptimizedStep(
            context, node, arg1,
            ExpressionStep::MakeGenericStep(std::make_unique<StringSetListStep>(
                                                StringSetListOp::kEquivalent,
                                                std::move(*str_set), node.id()),
                                            node.id(), /*stack_delta=*/0));
      }
      return absl::OkStatus();
    }

    // We try to fail early to minimize overhead. This is effectively
    // unreachable.
    return absl::OkStatus();
  }

 private:
  static absl::Status ReplaceWithOptimizedStep(PlannerContext& context,
                                               const Expr& call_node,
                                               const Expr& var_arg,
                                               ExpressionStep step) {
    CEL_ASSIGN_OR_RETURN(ExecutionPath path, context.ExtractSubplan(var_arg));
    path.push_back(std::move(step));
    return context.ReplaceSubplan(call_node, std::move(path));
  }

  // TODO(b/572083203): expose this in the planner context instead of capturing
  // a reference.
  const Ast& ast_;
  SetMembershipOptimizationOptions options_;
};

FlatExprBuilder* GetFlatExprBuilder(RuntimeBuilder& builder) {
  Runtime& runtime =
      runtime_internal::RuntimeFriendAccess::GetMutableRuntime(builder);
  if (runtime_internal::RuntimeFriendAccess::RuntimeTypeId(runtime) ==
      NativeTypeId::For<runtime_internal::RuntimeImpl>()) {
    runtime_internal::RuntimeImpl& runtime_impl =
        cel::internal::down_cast<runtime_internal::RuntimeImpl&>(runtime);
    return &runtime_impl.expr_builder();
  }
  return nullptr;
}

}  // namespace

ProgramOptimizerFactory CreateSetMembershipOptimizationProgramOptimizer(
    const SetMembershipOptimizationOptions& options) {
  return [options](PlannerContext& context, const Ast& ast)
             -> absl::StatusOr<std::unique_ptr<ProgramOptimizer>> {
    if (!context.options().enable_heterogeneous_equality) {
      return nullptr;
    }
    context.set_resolve_types();
    return std::make_unique<SetMembershipOptimizer>(ast, options);
  };
}

absl::Status EnableSetMembershipOptimization(
    cel::RuntimeBuilder& builder,
    const SetMembershipOptimizationOptions& options) {
  FlatExprBuilder* flat_expr_builder = GetFlatExprBuilder(builder);
  if (flat_expr_builder == nullptr) {
    return absl::InvalidArgumentError(
        "SetMembershipOptimization requires default runtime implementation");
  }
  flat_expr_builder->AddProgramOptimizer(
      CreateSetMembershipOptimizationProgramOptimizer(options));
  return absl::OkStatus();
}

}  // namespace cel::extensions
