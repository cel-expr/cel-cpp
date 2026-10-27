// Copyright 2024 Google LLC
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

#ifndef THIRD_PARTY_CEL_CPP_POLICY_INTERNAL_OPTIMIZER_EXPR_FACTORY_H_
#define THIRD_PARTY_CEL_CPP_POLICY_INTERNAL_OPTIMIZER_EXPR_FACTORY_H_

#include <algorithm>
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "common/ast.h"
#include "common/expr.h"
#include "common/expr_factory.h"
#include "common/source.h"

namespace cel {

class ParserMacroExprFactory;
class TestOptimizerExprFactory;

// `OptimizerExprFactory` is a specialization of `ExprFactory` used for AST
// optimization. It provides utilities for correcting metadata for modified
// ASTs.
class OptimizerExprFactory : protected ExprFactory {
 public:
  struct Issue {
    ExprId location = 0;
    std::string message;
  };

  explicit OptimizerExprFactory(Ast basis);
  OptimizerExprFactory();

 protected:
  using ExprFactory::IsArrayLike;
  using ExprFactory::IsExprLike;
  using ExprFactory::IsStringLike;

  template <typename T, typename U>
  struct IsRValue
      : std::bool_constant<
            std::disjunction_v<std::is_same<T, U>, std::is_same<T, U&&>>> {};

 public:
  // Consume the current set of renumberings.
  absl::flat_hash_map<ExprId, ExprId> ConsumeRenumbers();

  // Starts a new copy context. The current set of renumberings are cleared.
  void StartCopyContext();

  const std::vector<Issue>& issues() const;

  // Record that a node in the working AST was replaced. This is used to correct
  // metadata referencing the old ID.
  void RecordReplacement(ExprId id, const Expr& replacement,
                         bool keep_metadata = false);

  // Prunes metadata for the given ID from source info.
  void PruneMetadata(ExprId id);

  // Makes a copy of source metadata that is remapped to new expr Ids using
  // current renumberings. This is suitable for merging into the main source
  // info.
  SourceInfo RemapSourceInfo(const SourceInfo& info, SourcePosition offset = 0);

  // Merge a remapped SourceInfo into the current one.
  void MergeSourceInfo(const SourceInfo& info);

  const Ast& ast() const;
  Ast& mutable_ast();

  absl::string_view AccuVarName();

  ABSL_MUST_USE_RESULT Expr Copy(const Expr& expr);

  ABSL_MUST_USE_RESULT ListExprElement Copy(const ListExprElement& element);

  ABSL_MUST_USE_RESULT StructExprField Copy(const StructExprField& field);

  ABSL_MUST_USE_RESULT MapExprEntry Copy(const MapExprEntry& entry);

  ABSL_MUST_USE_RESULT Expr NewUnspecified();

  ABSL_MUST_USE_RESULT Expr NewNullConst();

  ABSL_MUST_USE_RESULT Expr NewBoolConst(bool value);

  ABSL_MUST_USE_RESULT Expr NewIntConst(int64_t value);

  ABSL_MUST_USE_RESULT Expr NewUintConst(uint64_t value);

  ABSL_MUST_USE_RESULT Expr NewDoubleConst(double value);

  ABSL_MUST_USE_RESULT Expr NewBytesConst(std::string value);

  ABSL_MUST_USE_RESULT Expr NewBytesConst(absl::string_view value);

  ABSL_MUST_USE_RESULT Expr NewBytesConst(const char* absl_nullable value);

  ABSL_MUST_USE_RESULT Expr NewStringConst(std::string value);

  ABSL_MUST_USE_RESULT Expr NewStringConst(absl::string_view value);

  ABSL_MUST_USE_RESULT Expr NewStringConst(const char* absl_nullable value);

  template <typename Name,
            typename = std::enable_if_t<IsStringLike<Name>::value>>
  ABSL_MUST_USE_RESULT Expr NewIdent(Name name);

  ABSL_MUST_USE_RESULT Expr NewAccuIdent();

  template <typename Operand, typename Field,
            typename = std::enable_if_t<IsExprLike<Operand>::value>,
            typename = std::enable_if_t<IsStringLike<Field>::value>>
  ABSL_MUST_USE_RESULT Expr NewSelect(Operand operand, Field field);

  template <typename Operand, typename Field,
            typename = std::enable_if_t<IsExprLike<Operand>::value>,
            typename = std::enable_if_t<IsStringLike<Field>::value>>
  ABSL_MUST_USE_RESULT Expr NewPresenceTest(Operand operand, Field field);

  template <
      typename Function, typename... Args,
      typename = std::enable_if_t<IsStringLike<Function>::value>,
      typename = std::enable_if_t<std::conjunction_v<IsRValue<Expr, Args>...>>>
  ABSL_MUST_USE_RESULT Expr NewCall(Function function, Args&&... args);

  template <typename Function, typename Args,
            typename = std::enable_if_t<IsStringLike<Function>::value>,
            typename = std::enable_if_t<IsArrayLike<Expr, Args>::value>>
  ABSL_MUST_USE_RESULT Expr NewCall(Function function, Args args);

  template <
      typename Function, typename Target, typename... Args,
      typename = std::enable_if_t<IsStringLike<Function>::value>,
      typename = std::enable_if_t<IsExprLike<Target>::value>,
      typename = std::enable_if_t<std::conjunction_v<IsRValue<Expr, Args>...>>>
  ABSL_MUST_USE_RESULT Expr NewMemberCall(Function function, Target target,
                                          Args&&... args);

  template <typename Function, typename Target, typename Args,
            typename = std::enable_if_t<IsStringLike<Function>::value>,
            typename = std::enable_if_t<IsExprLike<Target>::value>,
            typename = std::enable_if_t<IsArrayLike<Expr, Args>::value>>
  ABSL_MUST_USE_RESULT Expr NewMemberCall(Function function, Target target,
                                          Args args);

  using ExprFactory::NewListElement;

  template <typename... Elements,
            typename = std::enable_if_t<
                std::conjunction_v<IsRValue<ListExprElement, Elements>...>>>
  ABSL_MUST_USE_RESULT Expr NewList(Elements&&... elements);

  template <typename Elements,
            typename =
                std::enable_if_t<IsArrayLike<ListExprElement, Elements>::value>>
  ABSL_MUST_USE_RESULT Expr NewList(Elements elements);

  template <typename Name, typename Value,
            typename = std::enable_if_t<IsStringLike<Name>::value>,
            typename = std::enable_if_t<IsExprLike<Value>::value>>
  ABSL_MUST_USE_RESULT StructExprField NewStructField(Name name, Value value,
                                                      bool optional = false);

  template <typename Name, typename... Fields,
            typename = std::enable_if_t<IsStringLike<Name>::value>,
            typename = std::enable_if_t<
                std::conjunction_v<IsRValue<StructExprField, Fields>...>>>
  ABSL_MUST_USE_RESULT Expr NewStruct(Name name, Fields&&... fields);

  template <
      typename Name, typename Fields,
      typename = std::enable_if_t<IsStringLike<Name>::value>,
      typename = std::enable_if_t<IsArrayLike<StructExprField, Fields>::value>>
  ABSL_MUST_USE_RESULT Expr NewStruct(Name name, Fields fields);

  template <typename Key, typename Value,
            typename = std::enable_if_t<IsExprLike<Key>::value>,
            typename = std::enable_if_t<IsExprLike<Value>::value>>
  ABSL_MUST_USE_RESULT MapExprEntry NewMapEntry(Key key, Value value,
                                                bool optional = false);

  template <typename... Entries, typename = std::enable_if_t<std::conjunction_v<
                                     IsRValue<MapExprEntry, Entries>...>>>
  ABSL_MUST_USE_RESULT Expr NewMap(Entries&&... entries);

  template <typename Entries, typename = std::enable_if_t<
                                  IsArrayLike<MapExprEntry, Entries>::value>>
  ABSL_MUST_USE_RESULT Expr NewMap(Entries entries);

  template <typename IterVar, typename IterRange, typename AccuVar,
            typename AccuInit, typename LoopCondition, typename LoopStep,
            typename Result,
            typename = std::enable_if_t<IsStringLike<IterVar>::value>,
            typename = std::enable_if_t<IsExprLike<IterRange>::value>,
            typename = std::enable_if_t<IsStringLike<AccuVar>::value>,
            typename = std::enable_if_t<IsExprLike<AccuInit>::value>,
            typename = std::enable_if_t<IsExprLike<LoopStep>::value>,
            typename = std::enable_if_t<IsExprLike<LoopCondition>::value>,
            typename = std::enable_if_t<IsExprLike<Result>::value>>
  ABSL_MUST_USE_RESULT Expr NewComprehension(IterVar iter_var,
                                             IterRange iter_range,
                                             AccuVar accu_var,
                                             AccuInit accu_init,
                                             LoopCondition loop_condition,
                                             LoopStep loop_step, Result result);

  template <typename IterVar, typename IterVar2, typename IterRange,
            typename AccuVar, typename AccuInit, typename LoopCondition,
            typename LoopStep, typename Result,
            typename = std::enable_if_t<IsStringLike<IterVar>::value>,
            typename = std::enable_if_t<IsStringLike<IterVar2>::value>,
            typename = std::enable_if_t<IsExprLike<IterRange>::value>,
            typename = std::enable_if_t<IsStringLike<AccuVar>::value>,
            typename = std::enable_if_t<IsExprLike<AccuInit>::value>,
            typename = std::enable_if_t<IsExprLike<LoopStep>::value>,
            typename = std::enable_if_t<IsExprLike<LoopCondition>::value>,
            typename = std::enable_if_t<IsExprLike<Result>::value>>
  ABSL_MUST_USE_RESULT Expr NewComprehension(
      IterVar iter_var, IterVar2 iter_var2, IterRange iter_range,
      AccuVar accu_var, AccuInit accu_init, LoopCondition loop_condition,
      LoopStep loop_step, Result result);

  ABSL_MUST_USE_RESULT Expr ReportError(absl::string_view message);

  // Reports an error at the id in the optimized AST.
  ABSL_MUST_USE_RESULT Expr ReportErrorAt(const Expr& expr,
                                          absl::string_view message);
  // Reports an error at the mapped id of the copy of expr in the optimized AST.
  ABSL_MUST_USE_RESULT Expr ReportErrorAtCopy(const Expr& expr,
                                              absl::string_view message);

 protected:
  ABSL_MUST_USE_RESULT ExprId NextId();

  ABSL_MUST_USE_RESULT ExprId CopyId(ExprId id);

  ABSL_MUST_USE_RESULT ExprId CopyId(const Expr& expr);

  using ExprFactory::AccuVarName;
  using ExprFactory::NewAccuIdent;
  using ExprFactory::NewBoolConst;
  using ExprFactory::NewBytesConst;
  using ExprFactory::NewCall;
  using ExprFactory::NewComprehension;
  using ExprFactory::NewConst;
  using ExprFactory::NewDoubleConst;
  using ExprFactory::NewIdent;
  using ExprFactory::NewIntConst;
  using ExprFactory::NewList;
  using ExprFactory::NewMap;
  using ExprFactory::NewMapEntry;
  using ExprFactory::NewMemberCall;
  using ExprFactory::NewNullConst;
  using ExprFactory::NewPresenceTest;
  using ExprFactory::NewSelect;
  using ExprFactory::NewStringConst;
  using ExprFactory::NewStruct;
  using ExprFactory::NewStructField;
  using ExprFactory::NewUintConst;
  using ExprFactory::NewUnspecified;

 private:
  Ast ast_;
  absl::flat_hash_map<ExprId, ExprId> renumbers_;
  std::vector<Issue> issues_;

  ExprId next_id_ = 1;
};

// Implementation details.

template <typename Name, typename>
Expr OptimizerExprFactory::NewIdent(Name name) {
  return NewIdent(NextId(), std::move(name));
}

template <typename Operand, typename Field, typename, typename>
Expr OptimizerExprFactory::NewSelect(Operand operand, Field field) {
  return NewSelect(NextId(), std::move(operand), std::move(field));
}

template <typename Operand, typename Field, typename, typename>
Expr OptimizerExprFactory::NewPresenceTest(Operand operand, Field field) {
  return NewPresenceTest(NextId(), std::move(operand), std::move(field));
}

template <typename Function, typename... Args, typename, typename>
Expr OptimizerExprFactory::NewCall(Function function, Args&&... args) {
  std::vector<Expr> array;
  array.reserve(sizeof...(Args));
  (array.push_back(std::forward<Args>(args)), ...);
  return NewCall(NextId(), std::move(function), std::move(array));
}

template <typename Function, typename Args, typename, typename>
Expr OptimizerExprFactory::NewCall(Function function, Args args) {
  return NewCall(NextId(), std::move(function), std::move(args));
}

template <typename Function, typename Target, typename... Args, typename,
          typename, typename>
Expr OptimizerExprFactory::NewMemberCall(Function function, Target target,
                                         Args&&... args) {
  std::vector<Expr> array;
  array.reserve(sizeof...(Args));
  (array.push_back(std::forward<Args>(args)), ...);
  return NewMemberCall(NextId(), std::move(function), std::move(target),
                       std::move(array));
}

template <typename Function, typename Target, typename Args, typename, typename,
          typename>
Expr OptimizerExprFactory::NewMemberCall(Function function, Target target,
                                         Args args) {
  return NewMemberCall(NextId(), std::move(function), std::move(target),
                       std::move(args));
}

template <typename... Elements, typename>
Expr OptimizerExprFactory::NewList(Elements&&... elements) {
  std::vector<ListExprElement> array;
  array.reserve(sizeof...(Elements));
  (array.push_back(std::forward<Elements>(elements)), ...);
  return NewList(NextId(), std::move(array));
}

template <typename Elements, typename>
Expr OptimizerExprFactory::NewList(Elements elements) {
  return NewList(NextId(), std::move(elements));
}

template <typename Name, typename Value, typename, typename>
StructExprField OptimizerExprFactory::NewStructField(Name name, Value value,
                                                     bool optional) {
  return NewStructField(NextId(), std::move(name), std::move(value), optional);
}

template <typename Name, typename... Fields, typename, typename>
Expr OptimizerExprFactory::NewStruct(Name name, Fields&&... fields) {
  std::vector<StructExprField> array;
  array.reserve(sizeof...(Fields));
  (array.push_back(std::forward<Fields>(fields)), ...);
  return NewStruct(NextId(), std::move(name), std::move(array));
}

template <typename Name, typename Fields, typename, typename>
Expr OptimizerExprFactory::NewStruct(Name name, Fields fields) {
  return NewStruct(NextId(), std::move(name), std::move(fields));
}

template <typename Key, typename Value, typename, typename>
MapExprEntry OptimizerExprFactory::NewMapEntry(Key key, Value value,
                                               bool optional) {
  return NewMapEntry(NextId(), std::move(key), std::move(value), optional);
}

template <typename... Entries, typename>
Expr OptimizerExprFactory::NewMap(Entries&&... entries) {
  std::vector<MapExprEntry> array;
  array.reserve(sizeof...(Entries));
  (array.push_back(std::forward<Entries>(entries)), ...);
  return NewMap(NextId(), std::move(array));
}

template <typename Entries, typename>
Expr OptimizerExprFactory::NewMap(Entries entries) {
  return NewMap(NextId(), std::move(entries));
}

template <typename IterVar, typename IterRange, typename AccuVar,
          typename AccuInit, typename LoopCondition, typename LoopStep,
          typename Result, typename, typename, typename, typename, typename,
          typename, typename>
Expr OptimizerExprFactory::NewComprehension(IterVar iter_var,
                                            IterRange iter_range,
                                            AccuVar accu_var,
                                            AccuInit accu_init,
                                            LoopCondition loop_condition,
                                            LoopStep loop_step, Result result) {
  return NewComprehension(NextId(), std::move(iter_var), std::move(iter_range),
                          std::move(accu_var), std::move(accu_init),
                          std::move(loop_condition), std::move(loop_step),
                          std::move(result));
}

template <typename IterVar, typename IterVar2, typename IterRange,
          typename AccuVar, typename AccuInit, typename LoopCondition,
          typename LoopStep, typename Result, typename, typename, typename,
          typename, typename, typename, typename, typename>
Expr OptimizerExprFactory::NewComprehension(
    IterVar iter_var, IterVar2 iter_var2, IterRange iter_range,
    AccuVar accu_var, AccuInit accu_init, LoopCondition loop_condition,
    LoopStep loop_step, Result result) {
  return NewComprehension(NextId(), std::move(iter_var), std::move(iter_var2),
                          std::move(iter_range), std::move(accu_var),
                          std::move(accu_init), std::move(loop_condition),
                          std::move(loop_step), std::move(result));
}

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_POLICY_INTERNAL_OPTIMIZER_EXPR_FACTORY_H_
