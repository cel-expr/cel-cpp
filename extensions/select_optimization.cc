// Copyright 2023 Google LLC
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

#include "extensions/select_optimization.h"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/functional/overload.h"
#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "absl/types/variant.h"
#include "base/attribute.h"
#include "base/builtins.h"
#include "common/ast.h"
#include "common/ast_rewrite.h"
#include "common/constant.h"
#include "common/expr.h"
#include "common/function_descriptor.h"
#include "common/internal/attribute_trail.h"
#include "common/kind.h"
#include "common/legacy_value.h"
#include "common/memory.h"
#include "common/native_type.h"
#include "common/type.h"
#include "common/value.h"
#include "eval/compiler/flat_expr_builder.h"
#include "eval/compiler/flat_expr_builder_extensions.h"
#include "eval/eval/evaluator_core.h"
#include "eval/eval/expression_step_base.h"
#include "eval/public/cel_value.h"
#include "eval/public/structs/proto_message_type_adapter.h"
#include "internal/casts.h"
#include "internal/number.h"
#include "internal/status_macros.h"
#include "runtime/internal/errors.h"
#include "runtime/internal/runtime_friend_access.h"
#include "runtime/internal/runtime_impl.h"
#include "runtime/runtime_builder.h"
#include "runtime/runtime_options.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"

namespace cel::extensions {
namespace {

using ::cel::Ast;
using ::cel::AstRewriterBase;
using ::cel::CallExpr;
using ::cel::ConstantKind;
using ::cel::Expr;
using ::cel::ExprKind;
using ::cel::SelectExpr;
using ::cel::common_internal::AttributeTrail;
using ::google::api::expr::runtime::CelValue;
using ::google::api::expr::runtime::ExecutionFrame;
using ::google::api::expr::runtime::ExecutionFrameBase;
using ::google::api::expr::runtime::ExpressionStep;
using ::google::api::expr::runtime::ExpressionStepBase;
using ::google::api::expr::runtime::GetGenericProtoTypeInfoInstance;
using ::google::api::expr::runtime::PlannerContext;
using ::google::api::expr::runtime::ProgramOptimizer;
using ::google::api::expr::runtime::internal::GetGenericProtoAccessApisInstance;

// Represents a single select operation (field access or indexing).
// For struct-typed field accesses, includes the field name and the field
// number.
struct SelectInstruction {
  int64_t number;
  std::string name;
};

// Represents a single qualifier in a traversal path.
// TODO(uncreated-issue/51): support variable indexes.
using QualifierInstruction =
    std::variant<SelectInstruction, std::string, int64_t, uint64_t, bool>;

struct SelectPath {
  Expr* operand;
  std::vector<QualifierInstruction> select_instructions;
  bool test_only;
  // TODO(uncreated-issue/54): support for optionals.
};

// Generates the AST representation of the qualification path for the optimized
// select branch. I.e., the list-typed second argument of the cel.@attribute
// call.
Expr MakeSelectPathExpr(
    const std::vector<QualifierInstruction>& select_instructions) {
  Expr result;
  auto& ast_list = result.mutable_list_expr().mutable_elements();
  ast_list.reserve(select_instructions.size());
  auto visitor = absl::Overload(
      [&](const SelectInstruction& instruction) {
        Expr ast_instruction;
        Expr field_number;
        field_number.mutable_const_expr().set_int64_value(instruction.number);
        Expr field_name;
        field_name.mutable_const_expr().set_string_value(instruction.name);
        auto& field_specifier =
            ast_instruction.mutable_list_expr().mutable_elements();
        field_specifier.emplace_back().set_expr(std::move(field_number));
        field_specifier.emplace_back().set_expr(std::move(field_name));

        ast_list.emplace_back().set_expr(std::move(ast_instruction));
      },
      [&](absl::string_view instruction) {
        Expr const_expr;
        const_expr.mutable_const_expr().set_string_value(instruction);
        ast_list.emplace_back().set_expr(std::move(const_expr));
      },
      [&](int64_t instruction) {
        Expr const_expr;
        const_expr.mutable_const_expr().set_int64_value(instruction);
        ast_list.emplace_back().set_expr(std::move(const_expr));
      },
      [&](uint64_t instruction) {
        Expr const_expr;
        const_expr.mutable_const_expr().set_uint64_value(instruction);
        ast_list.emplace_back().set_expr(std::move(const_expr));
      },
      [&](bool instruction) {
        Expr const_expr;
        const_expr.mutable_const_expr().set_bool_value(instruction);
        ast_list.emplace_back().set_expr(std::move(const_expr));
      });

  for (const auto& instruction : select_instructions) {
    absl::visit(visitor, instruction);
  }
  return result;
}

// Returns a single select operation based on the inferred type of the operand
// and the field name. If the operand type doesn't define the field, returns
// nullopt.
std::optional<SelectInstruction> GetSelectInstruction(
    const StructType& runtime_type, PlannerContext& planner_context,
    absl::string_view field_name) {
  auto field_or = planner_context.type_reflector()
                      .FindStructTypeFieldByName(runtime_type, field_name)
                      .value_or(std::nullopt);
  if (field_or.has_value()) {
    return SelectInstruction{field_or->number(), std::string(field_or->name())};
  }
  return std::nullopt;
}

absl::StatusOr<SelectQualifier> SelectQualifierFromList(const ListExpr& list) {
  if (list.elements().size() != 2) {
    return absl::InvalidArgumentError("Invalid cel.attribute select list");
  }

  const Expr& field_number = list.elements()[0].expr();
  const Expr& field_name = list.elements()[1].expr();

  if (!field_number.has_const_expr() ||
      !field_number.const_expr().has_int64_value()) {
    return absl::InvalidArgumentError(
        "Invalid cel.attribute field select number");
  }

  if (!field_name.has_const_expr() ||
      !field_name.const_expr().has_string_value()) {
    return absl::InvalidArgumentError(
        "Invalid cel.attribute field select name");
  }

  return FieldSpecifier{field_number.const_expr().int64_value(),
                        field_name.const_expr().string_value()};
}

// Returns a qualifier instruction derived from a unoptimized ast.
absl::StatusOr<QualifierInstruction> SelectInstructionFromConstant(
    const Constant& constant) {
  if (constant.has_int_value()) {
    return QualifierInstruction(constant.int_value());
  } else if (constant.has_uint_value()) {
    return QualifierInstruction(constant.uint_value());
  } else if (constant.has_bool_value()) {
    return QualifierInstruction(constant.bool_value());
  } else if (constant.has_string_value()) {
    return QualifierInstruction(constant.string_value());
  } else if (constant.has_double_value()) {
    cel::internal::Number number(constant.double_value());
    if (number.LosslessConvertibleToInt()) {
      return QualifierInstruction(number.AsInt());
    } else if (number.LosslessConvertibleToUint()) {
      return QualifierInstruction(number.AsUint());
    }
  }

  return absl::InvalidArgumentError("invalid index constant for cel.attribute");
}

absl::StatusOr<SelectQualifier> SelectQualifierFromConstant(
    const Constant& constant) {
  if (constant.has_int_value()) {
    return AttributeQualifier::OfInt(constant.int_value());
  } else if (constant.has_uint_value()) {
    return AttributeQualifier::OfUint(constant.uint_value());
  } else if (constant.has_bool_value()) {
    return AttributeQualifier::OfBool(constant.bool_value());
  } else if (constant.has_string_value()) {
    return AttributeQualifier::OfString(constant.string_value());
  }
  // TODO(uncreated-issue/51): double keys could possibly be valid selectors, but
  // the other stacks don't implement the optimization yet and we normalize the
  // key to a uint or int if we do the late AST rewrite during planning.

  return absl::InvalidArgumentError("invalid cel.attribute constant");
}

absl::StatusOr<size_t> ListIndexFromQualifier(const AttributeQualifier& qual) {
  int64_t value = -1;
  switch (qual.kind()) {
    case Kind::kInt:
      value = *qual.GetInt64Key();
      break;
    default:
      // TODO(uncreated-issue/51): type-checker will reject an unsigned literal, but
      // should be supported as a dyn / variable.
      return runtime_internal::CreateNoMatchingOverloadError(
          cel::builtin::kIndex);
  }

  if (value < 0) {
    return absl::InvalidArgumentError("list index less than 0");
  }

  return static_cast<size_t>(value);
}

absl::StatusOr<Value> MapKeyFromQualifier(const AttributeQualifier& qual,
                                          google::protobuf::Arena* absl_nonnull arena) {
  switch (qual.kind()) {
    case Kind::kInt:
      return cel::IntValue(*qual.GetInt64Key());
    case Kind::kUint:
      return cel::UintValue(*qual.GetUint64Key());
    case Kind::kBool:
      return cel::BoolValue(*qual.GetBoolKey());
    case Kind::kString:
      // Safe as `qual` is valid for the lifetime of the expression/program.
      return StringValue::WrapUnsafe(*qual.GetStringKey());
    default:
      return runtime_internal::CreateNoMatchingOverloadError(
          cel::builtin::kIndex);
  }
}

// // Helper for StructValue::GetFieldByName. Used for opting out of old
// reflection implementation.
absl::StatusOr<Value> WrappedStructGet(
    const Value& target, absl::string_view field,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena,
    bool enable_use_new_field_select_implementation) {
  if (!enable_use_new_field_select_implementation) {
    if (const google::protobuf::Message* message =
            cel::interop_internal::GetLegacyMessage(target);
        message != nullptr) {
      CelValue::MessageWrapper message_wrapper(
          message, &GetGenericProtoTypeInfoInstance());
      CEL_ASSIGN_OR_RETURN(CelValue cel_value,
                           GetGenericProtoAccessApisInstance().GetField(
                               field, message_wrapper,
                               ProtoWrapperTypeOptions::kUnsetProtoDefault,
                               MemoryManagerRef::Pooling(arena)));
      Value result;
      CEL_RETURN_IF_ERROR(cel::ModernValue(arena, cel_value, result));
      return result;
    }
  }
  return target.GetStruct().GetFieldByName(field, descriptor_pool,
                                           message_factory, arena);
}

// Helper for StructValue::Qualify. Used for opting out of old reflection
// implementation.
absl::StatusOr<std::pair<Value, int>> WrappedStructQualify(
    const StructValue& struct_value,
    absl::Span<const SelectQualifier> qualifiers, bool presence_test,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena,
    bool enable_use_new_field_select_implementation) {
  if (!enable_use_new_field_select_implementation) {
    if (const google::protobuf::Message* message =
            cel::interop_internal::GetLegacyMessage(struct_value);
        message != nullptr) {
      CelValue::MessageWrapper message_wrapper(
          message, &GetGenericProtoTypeInfoInstance());
      CEL_ASSIGN_OR_RETURN(auto legacy_result,
                           GetGenericProtoAccessApisInstance().Qualify(
                               qualifiers, message_wrapper, presence_test,
                               MemoryManagerRef::Pooling(arena)));
      Value result;
      CEL_RETURN_IF_ERROR(cel::ModernValue(arena, legacy_result.value, result));
      return std::pair<Value, int>{std::move(result),
                                   legacy_result.qualifier_count};
    }
  }
  return struct_value.Qualify(qualifiers, presence_test, descriptor_pool,
                              message_factory, arena);
}

absl::StatusOr<Value> ApplyQualifier(
    const Value& operand, const SelectQualifier& qualifier,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena,
    bool enable_use_new_field_select_implementation) {
  return absl::visit(
      absl::Overload(
          [&](const FieldSpecifier& field_specifier) -> absl::StatusOr<Value> {
            if (!operand.Is<StructValue>()) {
              return cel::ErrorValue::From(
                  cel::runtime_internal::CreateNoMatchingOverloadError(
                      "<select>"),
                  arena);
            }
            return WrappedStructGet(operand, field_specifier.name,
                                    descriptor_pool, message_factory, arena,
                                    enable_use_new_field_select_implementation);
          },
          [&](const AttributeQualifier& qualifier) -> absl::StatusOr<Value> {
            if (operand.Is<ListValue>()) {
              auto index_or = ListIndexFromQualifier(qualifier);
              if (!index_or.ok()) {
                return cel::ErrorValue::From(index_or.status(), arena);
              }
              return operand.GetList().Get(*index_or, descriptor_pool,
                                           message_factory, arena);
            } else if (operand.Is<MapValue>()) {
              auto key_or = MapKeyFromQualifier(qualifier, arena);
              if (!key_or.ok()) {
                return cel::ErrorValue::From(key_or.status(), arena);
              }
              return operand.GetMap().Get(*key_or, descriptor_pool,
                                          message_factory, arena);
            }
            return cel::ErrorValue::From(
                cel::runtime_internal::CreateNoMatchingOverloadError(
                    cel::builtin::kIndex),
                arena);
          }),
      qualifier);
}

absl::StatusOr<Value> FallbackSelect(
    const Value& root, absl::Span<const SelectQualifier> select_path,
    bool presence_test,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena,
    bool enable_use_new_field_select_implementation) {
  const Value* elem = &root;
  Value result;

  for (const auto& instruction :
       select_path.subspan(0, select_path.size() - 1)) {
    CEL_ASSIGN_OR_RETURN(
        result,
        ApplyQualifier(*elem, instruction, descriptor_pool, message_factory,
                       arena, enable_use_new_field_select_implementation));
    if (result->Is<ErrorValue>()) {
      return result;
    }
    elem = &result;
  }

  const auto& last_instruction = select_path.back();
  if (presence_test) {
    return absl::visit(
        absl::Overload(
            [&](const FieldSpecifier& field_specifier)
                -> absl::StatusOr<Value> {
              if (!elem->Is<StructValue>()) {
                return cel::ErrorValue::From(
                    cel::runtime_internal::CreateNoMatchingOverloadError(
                        "<select>"),
                    arena);
              }
              CEL_ASSIGN_OR_RETURN(
                  bool present,
                  elem->GetStruct().HasFieldByName(field_specifier.name));
              return cel::BoolValue(present);
            },
            [&](const AttributeQualifier& qualifier) -> absl::StatusOr<Value> {
              if (!elem->Is<MapValue>() || qualifier.kind() != Kind::kString) {
                return cel::ErrorValue::From(
                    cel::runtime_internal::CreateNoMatchingOverloadError("has"),
                    arena);
              }

              return elem->GetMap().Has(
                  StringValue::WrapUnsafe(*qualifier.GetStringKey()),
                  descriptor_pool, message_factory, arena);
            }),
        last_instruction);
  }

  return ApplyQualifier(*elem, last_instruction, descriptor_pool,
                        message_factory, arena,
                        enable_use_new_field_select_implementation);
}

absl::StatusOr<std::vector<SelectQualifier>> SelectInstructionsFromCall(
    const CallExpr& call) {
  if (call.args().size() < 2 || !call.args()[1].has_list_expr()) {
    return absl::InvalidArgumentError("Invalid cel.attribute call");
  }
  std::vector<SelectQualifier> instructions;
  const auto& ast_path = call.args()[1].list_expr().elements();
  instructions.reserve(ast_path.size());

  for (const ListExprElement& element : ast_path) {
    // Optimized field select.
    if (element.has_expr()) {
      const auto& element_expr = element.expr();
      if (element_expr.has_list_expr()) {
        CEL_ASSIGN_OR_RETURN(instructions.emplace_back(),
                             SelectQualifierFromList(element_expr.list_expr()));
      } else if (element_expr.has_const_expr()) {
        CEL_ASSIGN_OR_RETURN(
            instructions.emplace_back(),
            SelectQualifierFromConstant(element_expr.const_expr()));
      } else {
        return absl::InvalidArgumentError("Invalid cel.attribute call");
      }
    } else {
      return absl::InvalidArgumentError("Invalid cel.attribute call");
    }
  }

  // TODO(uncreated-issue/54): support for optionals.

  return instructions;
}

class RewriterImpl : public AstRewriterBase {
 public:
  RewriterImpl(const Ast& ast, PlannerContext& planner_context)
      : ast_(ast), planner_context_(planner_context) {}

  void PreVisitExpr(const Expr& expr) override { path_.push_back(&expr); }

  void PreVisitSelect(const Expr& expr, const SelectExpr& select) override {
    const Expr& operand = select.operand();
    const std::string& field_name = select.field();
    // Select optimization can generalize to lists and maps, but for now only
    // support message traversal.
    const TypeSpec checker_type = ast_.GetTypeOrDyn(operand.id());

    std::optional<Type> rt_type =
        (checker_type.has_message_type())
            ? GetRuntimeType(checker_type.message_type().type())
            : std::nullopt;
    if (rt_type.has_value() && (*rt_type).Is<StructType>()) {
      const StructType& runtime_type = rt_type->GetStruct();
      std::optional<SelectInstruction> field_or =
          GetSelectInstruction(runtime_type, planner_context_, field_name);
      if (field_or.has_value()) {
        candidates_[&expr] = std::move(field_or).value();
      }
    } else if (checker_type.has_map_type()) {
      candidates_[&expr] = QualifierInstruction(field_name);
    }
    // else
    // TODO(uncreated-issue/54): add support for either dyn or any. Excluded to
    // simplify program plan.
  }

  void PreVisitCall(const Expr& expr, const CallExpr& call) override {
    if (call.args().size() != 2 || call.function() != ::cel::builtin::kIndex) {
      return;
    }

    const auto& qualifier_expr = call.args()[1];
    if (qualifier_expr.has_const_expr()) {
      auto qualifier_or =
          SelectInstructionFromConstant(qualifier_expr.const_expr());
      if (!qualifier_or.ok()) {
        // TODO(uncreated-issue/54): should warn, but by default warnings fail overall
        // program planning.
        return;
      }
      candidates_[&expr] = std::move(qualifier_or).value();
    }
    // TODO(uncreated-issue/54): support variable indexes
  }

  bool PostVisitRewrite(Expr& expr) override {
    if (!progress_status_.ok()) {
      return false;
    }
    path_.pop_back();
    auto candidate_iter = candidates_.find(&expr);
    if (candidate_iter == candidates_.end()) {
      return false;
    }

    // On post visit, filter candidates that aren't rooted on a message or a
    // select chain.
    const QualifierInstruction& candidate = candidate_iter->second;
    if (!HasOptimizeableRoot(&expr, candidate)) {
      candidates_.erase(candidate_iter);
      return false;
    }

    if (!path_.empty() && candidates_.find(path_.back()) != candidates_.end()) {
      // parent is optimizeable, defer rewriting until we consider the parent.
      return false;
    }

    SelectPath path = GetSelectPath(&expr);

    // generate the new cel.attribute call.
    absl::string_view fn = path.test_only ? kCelHasField : kCelAttribute;

    Expr operand(std::move(*path.operand));
    Expr call;
    call.set_id(expr.id());
    call.mutable_call_expr().set_function(std::string(fn));
    call.mutable_call_expr().mutable_args().reserve(2);

    call.mutable_call_expr().mutable_args().push_back(std::move(operand));
    call.mutable_call_expr().mutable_args().push_back(
        MakeSelectPathExpr(path.select_instructions));

    // TODO(uncreated-issue/54): support for optionals.
    expr = std::move(call);

    return true;
  }

  absl::Status GetProgressStatus() const { return progress_status_; }

 private:
  SelectPath GetSelectPath(Expr* expr) {
    SelectPath result;
    result.test_only = false;
    Expr* operand = expr;
    auto candidate_iter = candidates_.find(operand);
    while (candidate_iter != candidates_.end()) {
      result.select_instructions.push_back(candidate_iter->second);
      if (operand->has_select_expr()) {
        if (operand->select_expr().test_only()) {
          result.test_only = true;
        }
        operand = &(operand->mutable_select_expr().mutable_operand());
      } else {
        ABSL_DCHECK(operand->has_call_expr());
        operand = &(operand->mutable_call_expr().mutable_args()[0]);
      }
      candidate_iter = candidates_.find(operand);
    }
    absl::c_reverse(result.select_instructions);
    result.operand = operand;
    return result;
  }

  // Check whether the candidate has a message type as a root (the operand for
  // the batched select operation).
  // Called on post visit.
  bool HasOptimizeableRoot(const Expr* expr,
                           const QualifierInstruction& candidate) {
    if (absl::holds_alternative<SelectInstruction>(candidate)) {
      return true;
    }
    const Expr* operand = nullptr;
    if (expr->has_call_expr() && expr->call_expr().args().size() == 2 &&
        expr->call_expr().function() == ::cel::builtin::kIndex) {
      operand = &expr->call_expr().args()[0];
    } else if (expr->has_select_expr()) {
      operand = &expr->select_expr().operand();
    }

    if (operand == nullptr) {
      return false;
    }

    return candidates_.find(operand) != candidates_.end();
  }

  std::optional<Type> GetRuntimeType(absl::string_view type_name) {
    return planner_context_.type_reflector().FindType(type_name).value_or(
        std::nullopt);
  }

  void SetProgressStatus(const absl::Status& status) {
    if (progress_status_.ok() && !status.ok()) {
      progress_status_ = status;
    }
  }

  const Ast& ast_;
  PlannerContext& planner_context_;
  // ids of potentially optimizeable expr nodes.
  absl::flat_hash_map<const Expr*, QualifierInstruction> candidates_;
  std::vector<const Expr*> path_;
  absl::Status progress_status_;
};

class OptimizedSelectImpl {
 public:
  OptimizedSelectImpl(std::vector<SelectQualifier> select_path,
                      std::vector<AttributeQualifier> qualifiers,
                      bool presence_test, SelectOptimizationOptions options)
      : select_path_(std::move(select_path)),
        qualifiers_(std::move(qualifiers)),
        presence_test_(presence_test),
        options_(options)

  {
    ABSL_DCHECK(!select_path_.empty());
  }

  // Move constructible.
  OptimizedSelectImpl(const OptimizedSelectImpl&) = delete;
  OptimizedSelectImpl& operator=(const OptimizedSelectImpl&) = delete;
  OptimizedSelectImpl(OptimizedSelectImpl&&) = default;
  OptimizedSelectImpl& operator=(OptimizedSelectImpl&&) = delete;

  absl::StatusOr<Value> ApplySelect(ExecutionFrameBase& frame,
                                    const StructValue& struct_value) const;

  std::optional<Attribute> attribute() const { return attribute_; }

  const std::vector<AttributeQualifier>& qualifiers() const {
    return qualifiers_;
  }

 private:
  std::optional<Attribute> attribute_;
  std::vector<SelectQualifier> select_path_;
  std::vector<AttributeQualifier> qualifiers_;
  bool presence_test_;
  SelectOptimizationOptions options_;
};

absl::StatusOr<Value> OptimizedSelectImpl::ApplySelect(
    ExecutionFrameBase& frame, const StructValue& struct_value) const {
  auto value_or =
      (options_.force_fallback_implementation)
          ? absl::UnimplementedError("Forced fallback impl")
          : WrappedStructQualify(
                struct_value, select_path_, presence_test_,
                frame.descriptor_pool(), frame.message_factory(), frame.arena(),
                frame.options().enable_use_new_field_select_implementation);

  if (!value_or.ok()) {
    if (value_or.status().code() == absl::StatusCode::kUnimplemented) {
      return FallbackSelect(
          struct_value, select_path_, presence_test_, frame.descriptor_pool(),
          frame.message_factory(), frame.arena(),
          frame.options().enable_use_new_field_select_implementation);
    }

    return value_or.status();
  }

  if (value_or->second < 0 || value_or->second >= select_path_.size()) {
    return std::move(value_or->first);
  }

  return FallbackSelect(
      value_or->first,
      absl::MakeConstSpan(select_path_).subspan(value_or->second),
      presence_test_, frame.descriptor_pool(), frame.message_factory(),
      frame.arena(),
      frame.options().enable_use_new_field_select_implementation);
}

class StackMachineImpl : public ExpressionStepBase {
 public:
  StackMachineImpl(int expr_id, OptimizedSelectImpl impl)
      : ExpressionStepBase(expr_id), impl_(std::move(impl)) {}

  void Evaluate(ExecutionFrame* frame) const override;

 private:
  OptimizedSelectImpl impl_;
};

void StackMachineImpl::Evaluate(ExecutionFrame* frame) const {
  // TODO(uncreated-issue/51): add support for variable qualifiers and string literal
  // variable names.

  // For now, we expect the operand to be top of stack.
  Value& operand = frame->value_stack().Peek();
  AttributeTrail& trail = frame->value_stack().PeekAttribute();

  if (operand.Is<ErrorValue>() || operand.Is<UnknownValue>()) {
    // Just forward the error which is already top of stack.
    return;
  }

  if (frame->enable_attribute_tracking()) {
    // Compute the attribute trail then check for any marked values.
    // When possible, this is computed at plan time based on the optimized
    // select arguments.
    // TODO(uncreated-issue/51): add support variable qualifiers
    for (const auto& qualifier : impl_.qualifiers()) {
      if (trail.Match<AttributeTrail::kFull>(qualifier, operand,
                                             frame->unknown_tree())) {
        return;
      }
    }
  }

  if (!operand->Is<StructValue>()) {
    frame->Abort(absl::InvalidArgumentError(
        "Expected struct type for select optimization."));
    return;
  }

  absl::StatusOr<Value> result = impl_.ApplySelect(*frame, operand.GetStruct());
  if (!result.ok()) {
    frame->Abort(std::move(result).status());
    return;
  }

  operand = *result;
}

class SelectOptimizer : public ProgramOptimizer {
 public:
  explicit SelectOptimizer(const SelectOptimizationOptions& options)
      : options_(options) {}

  absl::Status OnPreVisit(PlannerContext& context, const Expr& node) override {
    return absl::OkStatus();
  }

  absl::Status OnPostVisit(PlannerContext& context, const Expr& node) override;

 private:
  SelectOptimizationOptions options_;
};

absl::Status SelectOptimizer::OnPostVisit(PlannerContext& context,
                                          const Expr& node) {
  if (!node.has_call_expr()) {
    return absl::OkStatus();
  }

  absl::string_view fn = node.call_expr().function();
  if (fn != kCelHasField && fn != kCelAttribute) {
    return absl::OkStatus();
  }

  if (node.call_expr().args().size() < 2 ||
      node.call_expr().args().size() > 3) {
    return absl::InvalidArgumentError("Invalid cel.attribute call");
  }

  if (node.call_expr().args().size() == 3) {
    return absl::UnimplementedError("Optionals not yet supported");
  }

  CEL_ASSIGN_OR_RETURN(std::vector<SelectQualifier> instructions,
                       SelectInstructionsFromCall(node.call_expr()));

  if (instructions.empty()) {
    return absl::InvalidArgumentError("Invalid cel.attribute no select steps.");
  }

  bool presence_test = false;

  if (fn == kCelHasField) {
    presence_test = true;
  }

  const Expr& operand = node.call_expr().args()[0];
  absl::string_view identifier;
  if (operand.has_ident_expr()) {
    identifier = operand.ident_expr().name();
  }

  if (absl::StrContains(identifier, ".")) {
    return absl::UnimplementedError("qualified identifiers not supported.");
  }

  std::vector<AttributeQualifier> qualifiers;
  qualifiers.reserve(instructions.size());
  for (const auto& instruction : instructions) {
    qualifiers.push_back(
        absl::visit(absl::Overload(
                        [](const FieldSpecifier& field) {
                          return AttributeQualifier::OfString(field.name);
                        },
                        [](const AttributeQualifier& q) { return q; }),
                    instruction));
  }

  // TODO(uncreated-issue/51): If the first argument is a string literal, the custom
  // step needs to handle variable lookup.
  auto* subexpression = context.program_builder().GetSubexpression(&node);
  if (subexpression == nullptr || subexpression->IsFlattened()) {
    // No information on the subprogram, can't optimize.
    return absl::OkStatus();
  }

  OptimizedSelectImpl impl(std::move(instructions), std::move(qualifiers),
                           presence_test, options_);

  google::api::expr::runtime::ExecutionPath path;

  // else, we need to preserve the original plan for the first argument.
  if (context.GetSubplan(operand).empty()) {
    // Indicates another extension modified the step. Nothing to do here.
    return absl::OkStatus();
  }
  CEL_ASSIGN_OR_RETURN(auto operand_subplan, context.ExtractSubplan(operand));
  absl::c_move(operand_subplan, std::back_inserter(path));

  path.push_back(ExpressionStep::MakeGenericStep(
      std::make_unique<StackMachineImpl>(node.id(), std::move(impl)),
      node.id()));

  return context.ReplaceSubplan(node, std::move(path));
}

google::api::expr::runtime::FlatExprBuilder* GetFlatExprBuilder(
    RuntimeBuilder& builder) {
  auto& runtime =
      runtime_internal::RuntimeFriendAccess::GetMutableRuntime(builder);
  if (runtime_internal::RuntimeFriendAccess::RuntimeTypeId(runtime) ==
      NativeTypeId::For<runtime_internal::RuntimeImpl>()) {
    auto& runtime_impl =
        cel::internal::down_cast<runtime_internal::RuntimeImpl&>(runtime);
    return &runtime_impl.expr_builder();
  }
  return nullptr;
}

}  // namespace

absl::Status SelectOptimizationAstUpdater::UpdateAst(PlannerContext& context,
                                                     Ast& ast) const {
  RewriterImpl rewriter(ast, context);
  AstRewrite(ast.mutable_root_expr(), rewriter);
  return rewriter.GetProgressStatus();
}

google::api::expr::runtime::ProgramOptimizerFactory
CreateSelectOptimizationProgramOptimizer(
    const SelectOptimizationOptions& options) {
  return [=](PlannerContext& context, const Ast& ast) {
    return std::make_unique<SelectOptimizer>(options);
  };
}

absl::Status EnableSelectOptimization(
    cel::RuntimeBuilder& builder, const SelectOptimizationOptions& options) {
  auto* flat_expr_builder = GetFlatExprBuilder(builder);
  if (flat_expr_builder == nullptr) {
    return absl::InvalidArgumentError(
        "SelectOptimization requires default runtime implementation");
  }

  flat_expr_builder->AddAstTransform(
      std::make_unique<SelectOptimizationAstUpdater>());
  // Add overloads for select optimization signature.
  // These are never bound, only used to prevent the builder from failing on
  // the overloads check.
  CEL_RETURN_IF_ERROR(builder.function_registry().RegisterLazyFunction(
      FunctionDescriptor(kCelAttribute, false, {Kind::kAny, Kind::kList})));

  CEL_RETURN_IF_ERROR(builder.function_registry().RegisterLazyFunction(
      FunctionDescriptor(kCelHasField, false, {Kind::kAny, Kind::kList})));
  // Add runtime implementation.
  flat_expr_builder->AddProgramOptimizer(
      CreateSelectOptimizationProgramOptimizer(options));
  return absl::OkStatus();
}

}  // namespace cel::extensions
