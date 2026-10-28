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
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/container/flat_hash_map.h"
#include "absl/functional/overload.h"
#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
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
#include "common/native_type.h"
#include "common/type.h"
#include "common/value.h"
#include "eval/compiler/flat_expr_builder.h"
#include "eval/compiler/flat_expr_builder_extensions.h"
#include "eval/eval/evaluator_core.h"
#include "eval/eval/expression_step_base.h"
#include "extensions/protobuf/internal/map_reflection.h"
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
#include "google/protobuf/map_field.h"
#include "google/protobuf/message.h"
#include "google/protobuf/reflection.h"

// undef for Windows
#undef GetMessage

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
using ::google::api::expr::runtime::ExecutionFrame;
using ::google::api::expr::runtime::ExecutionFrameBase;
using ::google::api::expr::runtime::ExpressionStep;
using ::google::api::expr::runtime::ExpressionStepBase;
using ::google::api::expr::runtime::PlannerContext;
using ::google::api::expr::runtime::ProgramOptimizer;

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
std::optional<SelectInstruction> MakeFieldStepAst(
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

// Returns a qualifier instruction derived from a unoptimized ast.
absl::StatusOr<QualifierInstruction> MakeIndexStepAst(
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

// Derives select step info from the optimized AST.
absl::StatusOr<SelectQualifier> SelectQualifierFromList(const ListExpr& list) {
  // Type codes may follow [number, name]; they're unused here.
  if (list.elements().size() < 2 || list.elements().size() > 4) {
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

// Derives index step info from the optimized AST.
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
      value = *qual.AsInt();
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
      return cel::IntValue(*qual.AsInt());
    case Kind::kUint:
      return cel::UintValue(*qual.AsUint());
    case Kind::kBool:
      return cel::BoolValue(*qual.AsBool());
    case Kind::kString:
      // Safe as `qual` is valid for the lifetime of the expression/program.
      return StringValue::WrapUnsafe(*qual.GetStringKey());
    default:
      return runtime_internal::CreateNoMatchingOverloadError(
          cel::builtin::kIndex);
  }
}

absl::StatusOr<Value> ApplyQualifier(
    const Value& operand, const SelectQualifier& qualifier,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena) {
  return absl::visit(
      absl::Overload(
          [&](const FieldSpecifier& field_specifier) -> absl::StatusOr<Value> {
            if (!operand.Is<StructValue>()) {
              return cel::ErrorValue::From(
                  cel::runtime_internal::CreateNoMatchingOverloadError(
                      "<select>"),
                  arena);
            }
            return operand.GetStruct().GetFieldByName(
                field_specifier.name, descriptor_pool, message_factory, arena);
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
    google::protobuf::Arena* absl_nonnull arena) {
  const Value* elem = &root;
  Value result;

  for (const auto& instruction :
       select_path.subspan(0, select_path.size() - 1)) {
    CEL_ASSIGN_OR_RETURN(result,
                         ApplyQualifier(*elem, instruction, descriptor_pool,
                                        message_factory, arena));
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
                        message_factory, arena);
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
    if (element.optional()) {
      return absl::InvalidArgumentError("Invalid cel.attribute call");
    }
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
  RewriterImpl(const Ast& ast, PlannerContext& planner_context,
               const SelectOptimizationOptions& options)
      : ast_(ast), planner_context_(planner_context), options_(options) {}

  void PreVisitExpr(const Expr& expr) override { path_.push_back(&expr); }

  void PreVisitSelect(const Expr& expr, const SelectExpr& select) override {
    const Expr& operand = select.operand();
    const std::string& field_name = select.field();
    // Select optimization can generalize to lists and maps, but for now only
    // support message traversal.
    const TypeSpec ast_type = ast_.GetTypeOrDyn(operand.id());
    std::optional<Type> rt_type =
        planner_context_.TryResolveType(operand.id(), ast_type);
    if (!rt_type.has_value()) {
      return;
    }
    if (rt_type->IsStruct()) {
      const StructType& runtime_type = rt_type->GetStruct();
      std::optional<SelectInstruction> field_or =
          MakeFieldStepAst(runtime_type, planner_context_, field_name);
      if (field_or.has_value()) {
        candidates_[&expr] = std::move(field_or).value();
      }
    } else if (rt_type->IsMap()) {
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
      auto qualifier_or = MakeIndexStepAst(qualifier_expr.const_expr());
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
    if (path.select_instructions.size() <
        options_.min_field_depth_for_optimization) {
      return false;
    }

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

  void SetProgressStatus(const absl::Status& status) {
    if (progress_status_.ok() && !status.ok()) {
      progress_status_ = status;
    }
  }

  const Ast& ast_;
  PlannerContext& planner_context_;
  SelectOptimizationOptions options_;
  // ids of potentially optimizeable expr nodes.
  absl::flat_hash_map<const Expr*, QualifierInstruction> candidates_;
  std::vector<const Expr*> path_;
  absl::Status progress_status_;
};

struct FieldSelectStep {
  const google::protobuf::Descriptor* absl_nonnull containing_type;
  const google::protobuf::FieldDescriptor* absl_nonnull field;
};

struct MapSelectStep {
  const google::protobuf::FieldDescriptor* absl_nonnull map_field;
  const google::protobuf::FieldDescriptor* absl_nonnull key_field;
  const google::protobuf::FieldDescriptor* absl_nonnull value_field;
  // Store the error to propagate on access at runtime.
  // The map key might alias string data owned by the SelectOptimizationImpl
  // object.
  absl::StatusOr<google::protobuf::MapKey> key;
};

struct ListSelectStep {
  const google::protobuf::FieldDescriptor* absl_nonnull repeated_field;
};

using TypedSelectStep =
    std::variant<FieldSelectStep, MapSelectStep, ListSelectStep>;

absl::StatusOr<google::protobuf::MapKey> CreateProtoMapKey(
    const google::protobuf::FieldDescriptor* absl_nonnull key_desc,
    const cel::AttributeQualifier& key) {
  google::protobuf::MapKey proto_key;
  switch (key_desc->cpp_type()) {
    case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
      if (key.kind() != cel::Kind::kBool) {
        return runtime_internal::CreateInvalidMapKeyTypeError(
            key_desc->cpp_type_name());
      }
      proto_key.SetBoolValue(*key.AsBool());
      return proto_key;
    case google::protobuf::FieldDescriptor::CPPTYPE_INT32: {
      if (key.kind() != cel::Kind::kInt64) {
        return runtime_internal::CreateInvalidMapKeyTypeError(
            key_desc->cpp_type_name());
      }
      int64_t key_value = *key.AsInt();
      if (key_value > std::numeric_limits<int32_t>::max() ||
          key_value < std::numeric_limits<int32_t>::lowest()) {
        return absl::OutOfRangeError("integer overflow");
      }
      proto_key.SetInt32Value(static_cast<int32_t>(key_value));
      return proto_key;
    }
    case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
      if (key.kind() != cel::Kind::kInt64) {
        return runtime_internal::CreateInvalidMapKeyTypeError(
            key_desc->cpp_type_name());
      }
      proto_key.SetInt64Value(*key.AsInt());
      return proto_key;
    case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
      if (key.kind() != cel::Kind::kString) {
        return runtime_internal::CreateInvalidMapKeyTypeError(
            key_desc->cpp_type_name());
      }
      // This is an alias. The underlying string should be stable.
      proto_key.SetStringValue(*key.AsString());
      return proto_key;
    case google::protobuf::FieldDescriptor::CPPTYPE_UINT32: {
      if (key.kind() != cel::Kind::kUint64) {
        return runtime_internal::CreateInvalidMapKeyTypeError(
            key_desc->cpp_type_name());
      }
      uint64_t key_value = *key.AsUint();
      if (key_value > std::numeric_limits<uint32_t>::max()) {
        return absl::OutOfRangeError("unsigned integer overflow");
      }
      proto_key.SetUInt32Value(static_cast<uint32_t>(key_value));
      return proto_key;
    }
    case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
      if (key.kind() != cel::Kind::kUint64) {
        return runtime_internal::CreateInvalidMapKeyTypeError(
            key_desc->cpp_type_name());
      }
      proto_key.SetUInt64Value(*key.AsUint());
      return proto_key;
    default:
      return runtime_internal::CreateInvalidMapKeyTypeError(
          key_desc->cpp_type_name());
  }
}

class OptimizedSelectImpl {
 public:
  OptimizedSelectImpl(std::vector<SelectQualifier> select_path,
                      std::vector<TypedSelectStep> typed_select_path,
                      std::vector<AttributeQualifier> qualifiers,
                      bool presence_test, SelectOptimizationOptions options)
      : select_path_(std::move(select_path)),
        typed_select_path_(std::move(typed_select_path)),
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

  absl::Status ApplySelect(ExecutionFrameBase& frame,
                           Value& result_in_out) const;

  std::optional<Attribute> attribute() const { return attribute_; }

  const std::vector<AttributeQualifier>& qualifiers() const {
    return qualifiers_;
  }

 private:
  absl::Status ContinueFromValue(ExecutionFrameBase& frame, Value step_result,
                                 size_t next_index, Value& result_in_out) const;

  std::optional<Attribute> attribute_;
  std::vector<SelectQualifier> select_path_;
  std::vector<TypedSelectStep> typed_select_path_;
  std::vector<AttributeQualifier> qualifiers_;
  bool presence_test_;
  SelectOptimizationOptions options_;
};

bool SupportsCachedFieldDescriptor(const google::protobuf::Message* absl_nonnull message,
                                   const TypedSelectStep& step) {
  if (const FieldSelectStep* field_step = absl::get_if<FieldSelectStep>(&step);
      field_step != nullptr) {
    ABSL_DCHECK_EQ(field_step->field->containing_type(),
                   field_step->containing_type);
    ABSL_DCHECK_EQ(field_step->containing_type->file()->pool(),
                   field_step->field->file()->pool());
    return message->GetDescriptor() == field_step->containing_type;
  }
  return false;
}

Value WrapMessageField(
    const google::protobuf::Message* absl_nonnull message,
    const google::protobuf::FieldDescriptor* absl_nonnull field_descriptor,
    bool is_unsafe, const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena) {
  if (is_unsafe) {
    return Value::WrapFieldUnsafe(ProtoWrapperTypeOptions::kUnsetNull, message,
                                  field_descriptor, descriptor_pool,
                                  message_factory, arena);
  }
  return Value::WrapField(ProtoWrapperTypeOptions::kUnsetNull, message,
                          field_descriptor, descriptor_pool, message_factory,
                          arena);
}

Value WrapMapElement(
    const google::protobuf::MapValueConstRef& value_ref,
    const google::protobuf::Message* absl_nonnull message,
    const google::protobuf::FieldDescriptor* absl_nonnull value_descriptor,
    bool is_unsafe, const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena) {
  if (is_unsafe) {
    return Value::WrapMapFieldValueUnsafe(value_ref, message, value_descriptor,
                                          descriptor_pool, message_factory,
                                          arena);
  }
  return Value::WrapMapFieldValue(value_ref, message, value_descriptor,
                                  descriptor_pool, message_factory, arena);
}

Value WrapRepeatedElement(
    int index, const google::protobuf::Message* absl_nonnull message,
    const google::protobuf::FieldDescriptor* absl_nonnull repeated_descriptor,
    bool is_unsafe, const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena) {
  if (is_unsafe) {
    return Value::WrapRepeatedFieldUnsafe(index, message, repeated_descriptor,
                                          descriptor_pool, message_factory,
                                          arena);
  }
  return Value::WrapRepeatedField(index, message, repeated_descriptor,
                                  descriptor_pool, message_factory, arena);
}

absl::StatusOr<Value> QualifyOrFallback(
    ExecutionFrameBase& frame, const StructValue& struct_value,
    absl::Span<const SelectQualifier> select_path, bool presence_test,
    bool force_fallback_implementation) {
  absl::StatusOr<std::pair<Value, int>> value_and_count =
      (force_fallback_implementation)
          ? absl::UnimplementedError("Forced fallback impl")
          : struct_value.Qualify(select_path, presence_test,
                                 frame.descriptor_pool(),
                                 frame.message_factory(), frame.arena());

  if (!value_and_count.ok()) {
    if (value_and_count.status().code() == absl::StatusCode::kUnimplemented) {
      return FallbackSelect(struct_value, select_path, presence_test,
                            frame.descriptor_pool(), frame.message_factory(),
                            frame.arena());
    }

    return value_and_count.status();
  }

  if (value_and_count->second < 0 ||
      value_and_count->second >= select_path.size()) {
    return std::move(value_and_count->first);
  }

  return FallbackSelect(value_and_count->first,
                        select_path.subspan(value_and_count->second),
                        presence_test, frame.descriptor_pool(),
                        frame.message_factory(), frame.arena());
}

absl::Status OptimizedSelectImpl::ContinueFromValue(
    ExecutionFrameBase& frame, Value step_result, size_t next_index,
    Value& result_in_out) const {
  if (step_result.IsError() || next_index >= select_path_.size()) {
    result_in_out = std::move(step_result);
    return absl::OkStatus();
  }
  absl::Span<const SelectQualifier> remaining =
      absl::MakeConstSpan(select_path_).subspan(next_index);
  if (step_result.IsStruct()) {
    CEL_ASSIGN_OR_RETURN(
        result_in_out,
        QualifyOrFallback(frame, step_result.GetStruct(), remaining,
                          presence_test_,
                          options_.force_fallback_implementation));
    return absl::OkStatus();
  }
  CEL_ASSIGN_OR_RETURN(result_in_out,
                       FallbackSelect(step_result, remaining, presence_test_,
                                      frame.descriptor_pool(),
                                      frame.message_factory(), frame.arena()));
  return absl::OkStatus();
}

absl::Status OptimizedSelectImpl::ApplySelect(ExecutionFrameBase& frame,
                                              Value& result_in_out) const {
  if (options_.force_fallback_implementation || typed_select_path_.empty()) {
    CEL_ASSIGN_OR_RETURN(
        result_in_out,
        QualifyOrFallback(frame, result_in_out.GetStruct(), select_path_,
                          presence_test_,
                          options_.force_fallback_implementation));
    return absl::OkStatus();
  }
  const google::protobuf::Message* current_message = nullptr;
  bool is_unsafe = false;
  if (result_in_out.IsParsedMessage()) {
    ParsedMessageValue parsed_message = result_in_out.GetParsedMessage();
    current_message = &(*parsed_message);
    is_unsafe =
        cel::interop_internal::IsUnsafeParsedMessageValue(parsed_message);
  } else if (const google::protobuf::Message* legacy_message =
                 cel::interop_internal::GetLegacyMessage(result_in_out);
             legacy_message != nullptr) {
    current_message = legacy_message;
    is_unsafe = true;
  } else {
    CEL_ASSIGN_OR_RETURN(
        result_in_out,
        QualifyOrFallback(frame, result_in_out.GetStruct(), select_path_,
                          presence_test_,
                          options_.force_fallback_implementation));
    return absl::OkStatus();
  }

  if (!SupportsCachedFieldDescriptor(current_message, typed_select_path_[0])) {
    CEL_ASSIGN_OR_RETURN(
        result_in_out,
        QualifyOrFallback(frame, result_in_out.GetStruct(), select_path_,
                          presence_test_,
                          options_.force_fallback_implementation));
    return absl::OkStatus();
  }

  for (size_t i = 0; i < typed_select_path_.size(); ++i) {
    const TypedSelectStep& step = typed_select_path_[i];

    // handle singular
    if (const FieldSelectStep* field_step =
            absl::get_if<FieldSelectStep>(&step);
        field_step != nullptr) {
      const google::protobuf::FieldDescriptor* field_descriptor = field_step->field;
      // We consumed the whole select path. Materialize the leaf.
      if (i + 1 == select_path_.size()) {
        if (presence_test_) {
          result_in_out =
              BoolValue{cel::UnsafeParsedMessageValue(current_message)
                            .HasField(field_descriptor)};
          return absl::OkStatus();
        }
        result_in_out = WrapMessageField(
            current_message, field_descriptor, is_unsafe,
            frame.descriptor_pool(), frame.message_factory(), frame.arena());
        return absl::OkStatus();
      }

      if (i + 1 >= typed_select_path_.size()) {
        Value step_result = WrapMessageField(
            current_message, field_descriptor, is_unsafe,
            frame.descriptor_pool(), frame.message_factory(), frame.arena());
        return ContinueFromValue(frame, std::move(step_result), i + 1,
                                 result_in_out);
      }

      if (field_descriptor->is_repeated()) {
        // The next step in `typed_select_path_` is a `MapSelectStep` or
        // `ListSelectStep` that operates on `current_message`.
        continue;
      }

      const google::protobuf::Message* next_message =
          &current_message->GetReflection()->GetMessage(*current_message,
                                                        field_descriptor);
      // Check for messages extended by overlays. This is redundant if all
      // messages are from hermetic pools, which we cannot guarantee.
      if (!SupportsCachedFieldDescriptor(next_message,
                                         typed_select_path_[i + 1])) {
        Value step_result = WrapMessageField(
            current_message, field_descriptor, is_unsafe,
            frame.descriptor_pool(), frame.message_factory(), frame.arena());
        return ContinueFromValue(frame, std::move(step_result), i + 1,
                                 result_in_out);
      }
      current_message = next_message;
      continue;
    }

    // Handle map
    if (const MapSelectStep* map_step = absl::get_if<MapSelectStep>(&step);
        map_step != nullptr) {
      if (i + 1 == select_path_.size() && presence_test_) {
        const AttributeQualifier& qualifier =
            absl::get<AttributeQualifier>(select_path_[i]);
        if (qualifier.kind() != cel::Kind::kString) {
          result_in_out = ErrorValue::From(
              runtime_internal::CreateNoMatchingOverloadError("has"),
              frame.arena());
          return absl::OkStatus();
        }
        if (!map_step->key.ok()) {
          result_in_out =
              ErrorValue::From(map_step->key.status(), frame.arena());
          return absl::OkStatus();
        }
        bool found = protobuf_internal::ContainsMapKey(
            *current_message->GetReflection(), *current_message,
            *map_step->map_field, *map_step->key);
        result_in_out = BoolValue{found};
        return absl::OkStatus();
      }

      if (!map_step->key.ok()) {
        result_in_out = ErrorValue::From(map_step->key.status(), frame.arena());
        return absl::OkStatus();
      }
      google::protobuf::MapValueConstRef value_ref;
      bool found = protobuf_internal::LookupMapValue(
          *current_message->GetReflection(), *current_message,
          *map_step->map_field, *map_step->key, &value_ref);
      if (!found) {
        const AttributeQualifier& qualifier =
            absl::get<AttributeQualifier>(select_path_[i]);
        result_in_out = ErrorValue::From(
            runtime_internal::CreateNoSuchKeyError(qualifier.ToString()),
            frame.arena());
        return absl::OkStatus();
      }

      if (i + 1 == select_path_.size()) {
        result_in_out = WrapMapElement(
            value_ref, current_message, map_step->value_field, is_unsafe,
            frame.descriptor_pool(), frame.message_factory(), frame.arena());
        return absl::OkStatus();
      }

      // We don't have a typed select step for the next step, so we fall back.
      if (i + 1 >= typed_select_path_.size()) {
        Value step_result = WrapMapElement(
            value_ref, current_message, map_step->value_field, is_unsafe,
            frame.descriptor_pool(), frame.message_factory(), frame.arena());
        return ContinueFromValue(frame, std::move(step_result), i + 1,
                                 result_in_out);
      }

      const google::protobuf::Message* next_message = &value_ref.GetMessageValue();
      if (!SupportsCachedFieldDescriptor(next_message,
                                         typed_select_path_[i + 1])) {
        Value step_result = WrapMapElement(
            value_ref, current_message, map_step->value_field, is_unsafe,
            frame.descriptor_pool(), frame.message_factory(), frame.arena());
        return ContinueFromValue(frame, std::move(step_result), i + 1,
                                 result_in_out);
      }
      current_message = next_message;
      continue;
    }

    // handle list
    if (const ListSelectStep* list_step = absl::get_if<ListSelectStep>(&step);
        list_step != nullptr) {
      if (i + 1 == select_path_.size() && presence_test_) {
        result_in_out = ErrorValue::From(
            runtime_internal::CreateNoMatchingOverloadError("has"),
            frame.arena());
        return absl::OkStatus();
      }

      const AttributeQualifier& qualifier =
          absl::get<AttributeQualifier>(select_path_[i]);
      if (qualifier.kind() != cel::Kind::kInt64) {
        result_in_out =
            ErrorValue::From(runtime_internal::CreateNoMatchingOverloadError(
                                 cel::builtin::kIndex),
                             frame.arena());
        return absl::OkStatus();
      }
      int64_t index = *qualifier.AsInt();
      const google::protobuf::Reflection* reflection = current_message->GetReflection();
      int size =
          reflection->FieldSize(*current_message, list_step->repeated_field);
      if (index < 0 || index >= size) {
        result_in_out = ErrorValue::From(
            absl::InvalidArgumentError(absl::StrCat(
                "index out of bounds: index=", index, " size=", size)),
            frame.arena());
        return absl::OkStatus();
      }
      int int_index = static_cast<int>(index);

      if (i + 1 == select_path_.size()) {
        result_in_out = WrapRepeatedElement(
            int_index, current_message, list_step->repeated_field, is_unsafe,
            frame.descriptor_pool(), frame.message_factory(), frame.arena());
        return absl::OkStatus();
      }

      // We don't have a typed select step for the next step, so we fall back.
      if (i + 1 >= typed_select_path_.size()) {
        Value step_result = WrapRepeatedElement(
            int_index, current_message, list_step->repeated_field, is_unsafe,
            frame.descriptor_pool(), frame.message_factory(), frame.arena());
        return ContinueFromValue(frame, std::move(step_result), i + 1,
                                 result_in_out);
      }

      const google::protobuf::Message* next_message = &reflection->GetRepeatedMessage(
          *current_message, list_step->repeated_field, int_index);
      if (!SupportsCachedFieldDescriptor(next_message,
                                         typed_select_path_[i + 1])) {
        Value step_result = WrapRepeatedElement(
            int_index, current_message, list_step->repeated_field, is_unsafe,
            frame.descriptor_pool(), frame.message_factory(), frame.arena());
        return ContinueFromValue(frame, std::move(step_result), i + 1,
                                 result_in_out);
      }
      current_message = next_message;
      continue;
    }

    ABSL_UNREACHABLE();
    return absl::InternalError("Unknown select step type");
  }

  return absl::OkStatus();
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

  if (!operand.Is<StructValue>()) {
    frame->Abort(absl::InvalidArgumentError(
        "Expected struct type for select optimization."));
    return;
  }

  absl::Status status = impl_.ApplySelect(*frame, operand);
  if (!status.ok()) {
    frame->Abort(std::move(status));
  }
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

std::vector<TypedSelectStep> BuildTypedSelectPath(
    PlannerContext& context, const Expr& operand,
    absl::Span<const SelectQualifier> instructions) {
  std::vector<TypedSelectStep> typed_select_path;
  std::optional<cel::Type> current_type = context.GetResolvedType(operand.id());
  for (const SelectQualifier& instruction : instructions) {
    if (!current_type.has_value()) {
      break;
    }
    if (const FieldSpecifier* field_specifier =
            absl::get_if<FieldSpecifier>(&instruction);
        field_specifier != nullptr) {
      if (!current_type->IsMessage()) {
        break;
      }
      cel::MessageType message_type = current_type->GetMessage();
      absl::StatusOr<std::optional<cel::StructTypeField>> field =
          context.type_reflector().FindStructTypeFieldByName(
              message_type, field_specifier->name);
      if (!field.ok() || !field->has_value() || !(*field)->IsMessage()) {
        break;
      }
      const google::protobuf::Descriptor* containing_descriptor =
          message_type.descriptor();
      const google::protobuf::FieldDescriptor* field_descriptor =
          (*field)->GetMessage().descriptor();
      if (containing_descriptor->file()->pool() !=
          field_descriptor->file()->pool()) {
        break;
      }
      typed_select_path.push_back(
          FieldSelectStep{containing_descriptor, field_descriptor});
      current_type = (*field)->GetType();
    } else if (const AttributeQualifier* attr_qualifier =
                   absl::get_if<AttributeQualifier>(&instruction);
               attr_qualifier != nullptr) {
      if (typed_select_path.empty() ||
          !absl::holds_alternative<FieldSelectStep>(typed_select_path.back())) {
        break;
      }
      const google::protobuf::FieldDescriptor* prev_field =
          absl::get<FieldSelectStep>(typed_select_path.back()).field;
      if (prev_field->is_map() && current_type->IsMap()) {
        const google::protobuf::Descriptor* entry_descriptor = prev_field->message_type();
        const google::protobuf::FieldDescriptor* key_field = entry_descriptor->map_key();
        const google::protobuf::FieldDescriptor* value_field =
            entry_descriptor->map_value();
        if (key_field == nullptr || value_field == nullptr) {
          break;
        }
        typed_select_path.push_back(
            MapSelectStep{prev_field, key_field, value_field,
                          CreateProtoMapKey(key_field, *attr_qualifier)});
        current_type = current_type->GetMap().GetValue();
      } else if (prev_field->is_repeated() && !prev_field->is_map() &&
                 current_type->IsList()) {
        typed_select_path.push_back(ListSelectStep{prev_field});
        current_type = current_type->GetList().GetElement();
      } else {
        break;
      }
    } else {
      break;
    }
  }
  return typed_select_path;
}

absl::Status SelectOptimizer::OnPostVisit(PlannerContext& context,
                                          const Expr& node) {
  if (!node.has_call_expr()) {
    return absl::OkStatus();
  }

  absl::string_view fn = node.call_expr().function();
  if (fn != kCelHasField && fn != kCelAttribute) {
    return absl::OkStatus();
  }
  const bool presence_test = fn == kCelHasField;

  // cel.@attribute may take a 3rd arg that only types the result; it's unused.
  const size_t max_args = presence_test ? 2 : 3;
  const size_t num_args = node.call_expr().args().size();
  if (num_args < 2 || num_args > max_args) {
    return absl::InvalidArgumentError("Invalid cel.attribute call");
  }

  CEL_ASSIGN_OR_RETURN(std::vector<SelectQualifier> instructions,
                       SelectInstructionsFromCall(node.call_expr()));

  if (instructions.empty()) {
    return absl::InvalidArgumentError("Invalid cel.attribute no select steps.");
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

  std::vector<TypedSelectStep> typed_select_path;
  if (options_.enable_field_descriptor_caching &&
      context.options().enable_typed_field_access) {
    typed_select_path = BuildTypedSelectPath(context, operand, instructions);
  }

  OptimizedSelectImpl impl(std::move(instructions),
                           std::move(typed_select_path), std::move(qualifiers),
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
      std::make_unique<StackMachineImpl>(node.id(), std::move(impl)), node.id(),
      /*stack_delta=*/0));

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
  RewriterImpl rewriter(ast, context, options_);
  AstRewrite(ast.mutable_root_expr(), rewriter);
  return rewriter.GetProgressStatus();
}

google::api::expr::runtime::ProgramOptimizerFactory
CreateSelectOptimizationProgramOptimizer(
    const SelectOptimizationOptions& options) {
  return [=](PlannerContext& context, const Ast& ast) {
    if (options.enable_field_descriptor_caching) {
      context.set_resolve_types();
    }
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
      std::make_unique<SelectOptimizationAstUpdater>(options));
  // Add overloads for select optimization signature.
  // These are never bound, only used to prevent the builder from failing on
  // the overloads check.
  CEL_RETURN_IF_ERROR(builder.function_registry().RegisterLazyFunction(
      FunctionDescriptor(kCelAttribute, false, {Kind::kAny, Kind::kList})));
  CEL_RETURN_IF_ERROR(
      builder.function_registry().RegisterLazyFunction(FunctionDescriptor(
          kCelAttribute, false, {Kind::kAny, Kind::kList, Kind::kAny})));

  CEL_RETURN_IF_ERROR(builder.function_registry().RegisterLazyFunction(
      FunctionDescriptor(kCelHasField, false, {Kind::kAny, Kind::kList})));
  flat_expr_builder->enable_select_optimization();
  // Add runtime implementation.
  flat_expr_builder->AddProgramOptimizer(
      CreateSelectOptimizationProgramOptimizer(options));
  return absl::OkStatus();
}

}  // namespace cel::extensions
