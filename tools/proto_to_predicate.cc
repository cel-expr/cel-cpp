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

#include "tools/proto_to_predicate.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "google/protobuf/descriptor.pb.h"
#include "absl/log/absl_log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "common/ast.h"
#include "common/expr.h"
#include "common/expr_factory.h"
#include "common/operators.h"
#include "internal/status_macros.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"
#include "google/protobuf/reflection.h"

namespace cel::tools {

using ::google::api::expr::common::CelOperator;
using ::google::protobuf::FieldDescriptor;
using ::google::protobuf::Message;
using ::google::protobuf::Reflection;

class ProtoToPredicateBuilder final : private ExprFactory {
 public:
  ProtoToPredicateBuilder() : id_(1) {}

  absl::StatusOr<Ast> Build(absl::string_view input_name,
                            const Message& message) {
    std::vector<Expr> predicates;
    Expr base_expr = NewIdent(NextId(), input_name);

    CEL_RETURN_IF_ERROR(Walk(message, base_expr, predicates));
    Expr root = LogicalAnd(predicates);
    return Ast(std::move(root), std::move(source_info_));
  }

  absl::StatusOr<Ast> Build(absl::string_view input_name,
                            absl::Span<const Message* const> messages) {
    if (messages.empty()) {
      return Ast(NewBoolConst(NextId(), true), std::move(source_info_));
    }

    std::vector<Expr> message_asts;
    message_asts.reserve(messages.size());
    for (const auto* message : messages) {
      std::vector<Expr> predicates;
      Expr base_expr = NewIdent(NextId(), input_name);

      CEL_RETURN_IF_ERROR(Walk(*message, base_expr, predicates));
      message_asts.push_back(LogicalAnd(predicates));
    }

    return Ast(LogicalOr(message_asts), std::move(source_info_));
  }

 private:
  // Retrieves the "match_path" string option from the field options if
  // defined, returning an empty string otherwise.
  std::string GetMatchPath(const ::google::protobuf::FieldDescriptor* field) {
    const ::google::protobuf::Message& options = field->options();
    const ::google::protobuf::Reflection* refl = options.GetReflection();
    std::vector<const ::google::protobuf::FieldDescriptor*> fields;
    refl->ListFields(options, &fields);
    for (const auto* f : fields) {
      if (f->name() == "match_path") {
        return refl->GetString(options, f);
      }
    }
    return "";
  }

  // Parses a dot-separated string representation of a path (e.g. "dest.region")
  // and builds a corresponding select chain AST.
  Expr ParseAndBuildPath(absl::string_view path_str) {
    std::vector<absl::string_view> parts = absl::StrSplit(path_str, '.');
    Expr e = NewIdent(NextId(), parts[0]);
    for (size_t i = 1; i < parts.size(); ++i) {
      e = NewSelect(NextId(), std::move(e), parts[i]);
    }
    return e;
  }

  // Returns either the path specified by the "match_path" annotation,
  // or the default path derived from the field name.
  Expr GetFieldPath(const Expr& base_expr,
                    const ::google::protobuf::FieldDescriptor* field) {
    std::string match_path_val = GetMatchPath(field);
    if (!match_path_val.empty()) {
      return ParseAndBuildPath(match_path_val);
    }
    return NewSelect(NextId(), base_expr, field->name());
  }
  ExprId NextId() { return id_++; }

  // ---------------------------------------------------------------------------
  // Field value extraction
  // ---------------------------------------------------------------------------

  // Converts a singular field value to a CEL constant expression.
  Expr PrimitiveToExpr(ExprId expr_id, const Message& message,
                       const Reflection* reflection,
                       const FieldDescriptor* field) {
    switch (field->cpp_type()) {
      case FieldDescriptor::CPPTYPE_INT32:
        return NewIntConst(expr_id, reflection->GetInt32(message, field));
      case FieldDescriptor::CPPTYPE_INT64:
        return NewIntConst(expr_id, reflection->GetInt64(message, field));
      case FieldDescriptor::CPPTYPE_UINT32:
        return NewUintConst(expr_id, reflection->GetUInt32(message, field));
      case FieldDescriptor::CPPTYPE_UINT64:
        return NewUintConst(expr_id, reflection->GetUInt64(message, field));
      case FieldDescriptor::CPPTYPE_DOUBLE:
        return NewDoubleConst(expr_id, reflection->GetDouble(message, field));
      case FieldDescriptor::CPPTYPE_FLOAT:
        return NewDoubleConst(expr_id, reflection->GetFloat(message, field));
      case FieldDescriptor::CPPTYPE_BOOL:
        return NewBoolConst(expr_id, reflection->GetBool(message, field));
      case FieldDescriptor::CPPTYPE_ENUM:
        return NewIntConst(expr_id, reflection->GetEnumValue(message, field));
      case FieldDescriptor::CPPTYPE_STRING: {
        std::string str_val = reflection->GetString(message, field);
        if (field->type() == FieldDescriptor::TYPE_BYTES) {
          return NewBytesConst(expr_id, std::move(str_val));
        }
        return NewStringConst(expr_id, std::move(str_val));
      }
      default:
        // Log a warning as message should be handled by Walk.
        ABSL_LOG(WARNING) << "PrimitiveToExpr: Unhandled field type: "
                          << FieldDescriptor::TypeName(field->type());
        break;
    }
    return NewNullConst(expr_id);
  }

  Expr PrimitiveToExpr(const Message& message, const Reflection* reflection,
                       const FieldDescriptor* field) {
    return PrimitiveToExpr(NextId(), message, reflection, field);
  }

  // Converts a repeated field element to a CEL constant expression.
  Expr RepeatedPrimitiveToExpr(const Message& message,
                               const Reflection* reflection,
                               const FieldDescriptor* field, int index) {
    const ExprId id = NextId();
    switch (field->cpp_type()) {
      case FieldDescriptor::CPPTYPE_INT32:
        return NewIntConst(id,
                           reflection->GetRepeatedInt32(message, field, index));
      case FieldDescriptor::CPPTYPE_INT64:
        return NewIntConst(id,
                           reflection->GetRepeatedInt64(message, field, index));
      case FieldDescriptor::CPPTYPE_UINT32:
        return NewUintConst(
            id, reflection->GetRepeatedUInt32(message, field, index));
      case FieldDescriptor::CPPTYPE_UINT64:
        return NewUintConst(
            id, reflection->GetRepeatedUInt64(message, field, index));
      case FieldDescriptor::CPPTYPE_DOUBLE:
        return NewDoubleConst(
            id, reflection->GetRepeatedDouble(message, field, index));
      case FieldDescriptor::CPPTYPE_FLOAT:
        return NewDoubleConst(
            id, reflection->GetRepeatedFloat(message, field, index));
      case FieldDescriptor::CPPTYPE_BOOL:
        return NewBoolConst(id,
                            reflection->GetRepeatedBool(message, field, index));
      case FieldDescriptor::CPPTYPE_ENUM:
        return NewIntConst(
            id, reflection->GetRepeatedEnumValue(message, field, index));
      case FieldDescriptor::CPPTYPE_STRING: {
        std::string str_val =
            reflection->GetRepeatedString(message, field, index);
        if (field->type() == FieldDescriptor::TYPE_BYTES) {
          return NewBytesConst(id, std::move(str_val));
        }
        return NewStringConst(id, std::move(str_val));
      }
      default:
        break;
    }
    return NewNullConst(id);
  }

  // ---------------------------------------------------------------------------
  // Expression construction helpers
  // ---------------------------------------------------------------------------

  // Creates a binary operator call: `lhs <op> rhs`.
  Expr ConstructBinaryOp(absl::string_view op, Expr lhs, Expr rhs) {
    std::vector<Expr> args = {std::move(lhs), std::move(rhs)};
    return NewCall(NextId(), op, std::move(args));
  }

  Expr ConstructEquality(Expr lhs, Expr rhs) {
    return ConstructBinaryOp(CelOperator::EQUALS, std::move(lhs),
                             std::move(rhs));
  }

  Expr LogicalOr(std::vector<Expr>& exprs) {
    return LogicalOp(CelOperator::LOGICAL_OR, exprs);
  }

  Expr LogicalAnd(std::vector<Expr>& exprs) {
    return LogicalOp(CelOperator::LOGICAL_AND, exprs);
  }

  // Left-folds a vector of expressions with a binary operator.
  // Requires: `exprs` is non-empty.
  Expr LogicalOp(absl::string_view op, std::vector<Expr>& exprs) {
    if (exprs.empty()) {
      return NewBoolConst(NextId(), true);
    }
    if (exprs.size() == 1) {
      return std::move(exprs[0]);
    }
    return NewCall(NextId(), op, std::move(exprs));
  }

  // ---------------------------------------------------------------------------
  // Map field predicate (extracted from Walk for readability)
  // ---------------------------------------------------------------------------

  // Builds the predicate for a map field to assert that all key-value pairs
  // specified in the policy are present in the input map field:
  //   "key" in input.map && input.map["key"] == value
  absl::Status WalkMapField(const Reflection* reflection,
                            const Message& message,
                            const FieldDescriptor* field, const Expr& base_expr,
                            int size, std::vector<Expr>& predicates) {
    const FieldDescriptor* const key_field =
        field->message_type()->FindFieldByName("key");
    const FieldDescriptor* const value_field =
        field->message_type()->FindFieldByName("value");

    Expr map_path = GetFieldPath(base_expr, field);

    struct MapEntry {
      const Message* message;
    };
    std::vector<MapEntry> entries;
    entries.reserve(size);
    for (int i = 0; i < size; ++i) {
      entries.push_back({&reflection->GetRepeatedMessage(message, field, i)});
    }

    if (!entries.empty()) {
      const Reflection* const entry_ref = entries[0].message->GetReflection();
      std::sort(entries.begin(), entries.end(),
                [entry_ref, key_field](const MapEntry& a, const MapEntry& b) {
                  switch (key_field->cpp_type()) {
                    case FieldDescriptor::CPPTYPE_INT32:
                      return entry_ref->GetInt32(*a.message, key_field) <
                             entry_ref->GetInt32(*b.message, key_field);
                    case FieldDescriptor::CPPTYPE_INT64:
                      return entry_ref->GetInt64(*a.message, key_field) <
                             entry_ref->GetInt64(*b.message, key_field);
                    case FieldDescriptor::CPPTYPE_UINT32:
                      return entry_ref->GetUInt32(*a.message, key_field) <
                             entry_ref->GetUInt32(*b.message, key_field);
                    case FieldDescriptor::CPPTYPE_UINT64:
                      return entry_ref->GetUInt64(*a.message, key_field) <
                             entry_ref->GetUInt64(*b.message, key_field);
                    case FieldDescriptor::CPPTYPE_BOOL:
                      return !entry_ref->GetBool(*a.message, key_field) &&
                             entry_ref->GetBool(*b.message, key_field);
                    case FieldDescriptor::CPPTYPE_STRING:
                      return entry_ref->GetString(*a.message, key_field) <
                             entry_ref->GetString(*b.message, key_field);
                    default:
                      return false;
                  }
                });
    }

    std::vector<Expr> map_checks;
    map_checks.reserve(size);
    for (const auto& entry : entries) {
      const Message& entry_msg = *entry.message;
      const Reflection* const entry_ref = entry_msg.GetReflection();

      Expr key_expr = PrimitiveToExpr(entry_msg, entry_ref, key_field);

      // Represents `"key" in input.map` to assert the key exists.
      Expr in_check = NewCall(NextId(), CelOperator::IN,
                              std::vector<Expr>{key_expr, map_path});
      // Represents `input.map["key"]` to lookup the value.
      Expr lookup_path = NewCall(NextId(), CelOperator::INDEX,
                                 std::vector<Expr>{map_path, key_expr});

      if (value_field->cpp_type() == FieldDescriptor::CPPTYPE_MESSAGE) {
        const Message& value_msg =
            entry_ref->GetMessage(entry_msg, value_field);
        std::vector<Expr> val_predicates;
        CEL_RETURN_IF_ERROR(Walk(value_msg, lookup_path, val_predicates));

        if (!val_predicates.empty()) {
          // Represents `"key" in input.map && (nested message fields check...)`
          map_checks.push_back(std::move(in_check));
          map_checks.insert(map_checks.end(),
                            std::make_move_iterator(val_predicates.begin()),
                            std::make_move_iterator(val_predicates.end()));
        } else {
          // Represents `"key" in input.map` if nested message is empty.
          map_checks.push_back(std::move(in_check));
        }
      } else {
        Expr value_expr = PrimitiveToExpr(entry_msg, entry_ref, value_field);
        // Represents `input.map["key"] == value`
        Expr eq_check =
            ConstructEquality(std::move(lookup_path), std::move(value_expr));

        // Represents `"key" in input.map && input.map["key"] == value`
        map_checks.push_back(std::move(in_check));
        map_checks.push_back(std::move(eq_check));
      }
    }

    predicates.push_back(LogicalAnd(map_checks));
    return absl::OkStatus();
  }

  // ---------------------------------------------------------------------------
  // Repeated field predicate (extracted from Walk for readability)
  // ---------------------------------------------------------------------------

  // Builds predicates for a repeated field:
  // - Repeated Messages are mapped to a logical OR (||) of the generated
  //   predicates for each message.
  // - Repeated Primitives are mapped either to:
  //   - `lhs in [values]` if a "match_path" option is specified.
  //   - `value in input.field` conjoined with && for each value otherwise.
  absl::Status WalkRepeatedField(const Reflection* reflection,
                                 const Message& message,
                                 const FieldDescriptor* field,
                                 const Expr& base_expr, int size,
                                 std::vector<Expr>& predicates) {
    if (field->cpp_type() == FieldDescriptor::CPPTYPE_MESSAGE) {
      std::vector<Expr> message_asts;
      message_asts.reserve(size);
      for (int i = 0; i < size; ++i) {
        const Message& sub_message =
            reflection->GetRepeatedMessage(message, field, i);
        std::vector<Expr> sub_predicates;
        Expr sub_base = GetFieldPath(base_expr, field);
        CEL_RETURN_IF_ERROR(Walk(sub_message, sub_base, sub_predicates));
        message_asts.push_back(LogicalAnd(sub_predicates));
      }
      // Represents alternate message predicates conjoined with OR: `msg_1 ||
      // msg_2 || ...`
      predicates.push_back(LogicalOr(message_asts));
      return absl::OkStatus();
    }

    std::vector<ListExprElement> elements;
    elements.reserve(size);
    for (int i = 0; i < size; ++i) {
      elements.push_back(NewListElement(
          RepeatedPrimitiveToExpr(message, reflection, field, i)));
    }
    Expr literal_list = NewList(NextId(), std::move(elements));

    std::string match_path_val = GetMatchPath(field);
    if (!match_path_val.empty()) {
      Expr lhs = ParseAndBuildPath(match_path_val);
      // Represents `lhs in [values]` check (e.g. `dest.region in ["us-east",
      // "us-west"]`).
      predicates.push_back(
          NewCall(NextId(), CelOperator::IN,
                  std::vector<Expr>{std::move(lhs), std::move(literal_list)}));
      return absl::OkStatus();
    }

    Expr map_path = NewSelect(NextId(), base_expr, field->name());
    std::vector<Expr> element_checks;
    element_checks.reserve(size);
    for (int i = 0; i < size; ++i) {
      Expr elem_expr = RepeatedPrimitiveToExpr(message, reflection, field, i);
      // Represents `value in input.field` check.
      Expr in_check =
          NewCall(NextId(), CelOperator::IN,
                  std::vector<Expr>{std::move(elem_expr), map_path});
      element_checks.push_back(std::move(in_check));
    }
    // Represents `"val1" in input.list && "val2" in input.list && ...`
    predicates.push_back(LogicalAnd(element_checks));

    return absl::OkStatus();
  }

  // ---------------------------------------------------------------------------
  // Recursive message walk
  // ---------------------------------------------------------------------------

  absl::Status Walk(const Message& message, const Expr& base_expr,
                    std::vector<Expr>& predicates) {
    const Reflection* const reflection = message.GetReflection();
    std::vector<const FieldDescriptor*> fields;
    reflection->ListFields(message, &fields);

    for (const auto* field : fields) {
      if (field->is_map()) {
        const int size = reflection->FieldSize(message, field);
        if (size > 0) {
          CEL_RETURN_IF_ERROR(WalkMapField(reflection, message, field,
                                           base_expr, size, predicates));
        }
      } else if (field->is_repeated()) {
        const int size = reflection->FieldSize(message, field);
        if (size > 0) {
          CEL_RETURN_IF_ERROR(WalkRepeatedField(reflection, message, field,
                                                base_expr, size, predicates));
        }
      } else if (field->cpp_type() == FieldDescriptor::CPPTYPE_MESSAGE) {
        const Message& sub_message = reflection->GetMessage(message, field);
        Expr field_path = GetFieldPath(base_expr, field);
        CEL_RETURN_IF_ERROR(Walk(sub_message, field_path, predicates));
      } else {
        // Primitive field: base_expr.field == <value>
        Expr field_path = GetFieldPath(base_expr, field);
        predicates.push_back(
            ConstructEquality(std::move(field_path),
                              PrimitiveToExpr(message, reflection, field)));
      }
    }
    return absl::OkStatus();
  }

  ExprId id_;
  SourceInfo source_info_;
};

absl::StatusOr<Ast> ProtoToPredicateAst(absl::string_view input_name,
                                        const ::google::protobuf::Message& message) {
  ProtoToPredicateBuilder builder;
  return builder.Build(input_name, message);
}

absl::StatusOr<Ast> ProtoToPredicateAst(
    absl::string_view input_name,
    absl::Span<const ::google::protobuf::Message* const> messages) {
  ProtoToPredicateBuilder builder;
  return builder.Build(input_name, messages);
}

}  // namespace cel::tools
