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

#include <algorithm>
#include <iostream>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/cord.h"
#include "base/attribute.h"
#include "base/attribute_set.h"
#include "checker/type_checker.h"
#include "checker/type_checker_builder_factory.h"
#include "common/ast.h"
#include "common/ast_proto.h"
#include "common/decl.h"
#include "common/value.h"
#include "eval/compiler/cel_expression_builder_flat_impl.h"
#include "eval/compiler/qualified_reference_resolver.h"
#include "eval/eval/cel_expression_flat_impl.h"
#include "eval/internal/adapter_activation_impl.h"
#include "eval/internal/interop.h"
#include "eval/public/activation.h"
#include "eval/public/unknown_set.h"
#include "internal/status_macros.h"
#include "internal/testing.h"
#include "parser/parser.h"
#include "runtime/activation.h"
#include "runtime/internal/function_matching_test_util.h"
#include "runtime/internal/runtime_env_testing.h"

namespace cel::runtime_internal {
namespace {

namespace legacy = ::google::api::expr::runtime;

class Marker final : public Function {
 public:
  Marker(int64_t marker, int* calls) : marker_(marker), calls_(calls) {}
  absl::StatusOr<Value> Invoke(absl::Span<const Value>,
                               const InvokeContext&) const override {
    ++*calls_;
    return IntValue(marker_);
  }

 private:
  int64_t marker_;
  int* calls_;
};

class LegacyMarker final : public legacy::CelFunction {
 public:
  LegacyMarker(FunctionDescriptor descriptor, int64_t marker, int* calls)
      : CelFunction(std::move(descriptor)), marker_(marker), calls_(calls) {}
  absl::Status Evaluate(absl::Span<const legacy::CelValue>,
                        legacy::CelValue* result,
                        google::protobuf::Arena*) const override {
    ++*calls_;
    *result = legacy::CelValue::CreateInt64(marker_);
    return absl::OkStatus();
  }

 private:
  int64_t marker_;
  int* calls_;
};

enum class Route { kParsed, kSingle, kMultiple, kUnknown };

class CountingActivation final : public ActivationInterface {
 public:
  CountingActivation(const ActivationInterface& delegate, int* enumerations,
                     int* ids)
      : delegate_(delegate), enumerations_(enumerations), ids_(ids) {}
  absl::StatusOr<bool> FindVariable(
      absl::string_view name, const google::protobuf::DescriptorPool* pool,
      google::protobuf::MessageFactory* factory, google::protobuf::Arena* arena,
      Value* result) const override {
    return delegate_.FindVariable(name, pool, factory, arena, result);
  }
  std::vector<FunctionOverloadReference> FindFunctionOverloads(
      absl::string_view name) const override {
    ++*enumerations_;
    return delegate_.FindFunctionOverloads(name);
  }
  absl::optional<FunctionOverloadReference> FindFunctionOverloadById(
      absl::string_view name, absl::string_view id) const override {
    ++*ids_;
    return delegate_.FindFunctionOverloadById(name, id);
  }
  absl::Span<const AttributePattern> GetUnknownAttributes() const override {
    return delegate_.GetUnknownAttributes();
  }
  absl::Span<const AttributePattern> GetMissingAttributes() const override {
    return delegate_.GetMissingAttributes();
  }

 private:
  const ActivationInterface& delegate_;
  int *enumerations_, *ids_;
};

// Every invocation goes through the planner and evaluator. Modern and legacy
// activations exercise the same compiled expression and different binding APIs.
class Dispatch {
 public:
  Dispatch(bool lazy, bool type_level) : Dispatch(lazy, type_level, !lazy) {}
  Dispatch(bool lazy, bool type_level, bool modern, bool unknowns = true,
           bool omit_mode = false, bool fail_on_warnings = false)
      : env_(NewTestingRuntimeEnv()), lazy_(lazy), modern_(modern) {
    if (!omit_mode) caller_options_.enable_type_level_overload = type_level;
    caller_options_.fail_on_warnings = fail_on_warnings;
    caller_options_.unknown_processing =
        unknowns ? UnknownProcessingOptions::kAttributeAndFunction
                 : UnknownProcessingOptions::kDisabled;
    builder_ = std::make_unique<legacy::CelExpressionBuilderFlatImpl>(
        env_, caller_options_);
  }
  void MutateCallerOptions(bool mode) {
    caller_options_.enable_type_level_overload = mode;
  }
  void MutateAfterPlanning(bool mode) { mutate_after_planning_ = mode; }
  void SetContainer(std::string container) {
    builder_->set_container(std::move(container));
    builder_->flat_expr_builder().AddAstTransform(
        legacy::NewReferenceResolverExtension(
            legacy::ReferenceResolverOption::kAlways));
  }
  google::protobuf::Arena* arena() { return &arena_; }
  Value message(absl::string_view name) {
    const auto* descriptor =
        env_->descriptor_pool->FindMessageTypeByName(std::string(name));
    EXPECT_NE(descriptor, nullptr);
    return ParsedMessageValue(
        env_->MutableMessageFactory()->GetPrototype(descriptor)->New(arena()),
        arena());
  }
  Type list(Type t) { return ListType(arena(), t); }
  Value list(std::initializer_list<Value> values) {
    auto builder = NewListValueBuilder(arena());
    for (const auto& value : values) EXPECT_OK(builder->Add(value));
    return std::move(*builder).Build();
  }
  Value string() { return StringValue::From("s", arena()); }
  Value unknown() {
    auto* set = google::protobuf::Arena::Create<legacy::UnknownSet>(
        arena(), AttributeSet({Attribute("unknown")}));
    return interop_internal::LegacyValueToModernValueOrDie(
        arena(), legacy::CelValue::CreateUnknownSet(set), true);
  }
  Value exception(bool is_unknown) {
    if (is_unknown) return unknown();
    auto status = absl::CancelledError("distinct CEL input");
    status.SetPayload("cel.test/input", absl::Cord("original"));
    return ErrorValue::From(status, arena());
  }
  absl::Status Add(const FunctionDescriptor& registration,
                   const FunctionDescriptor& implementation, int64_t marker) {
    if (!lazy_)
      return env_->function_registry.Register(
          registration, std::make_unique<Marker>(marker, &calls_[marker]));
    CEL_RETURN_IF_ERROR(
        env_->function_registry.RegisterLazyFunction(registration));
    return AddImplementation(implementation, marker);
  }
  absl::Status AddStatic(const FunctionDescriptor& descriptor, int64_t marker) {
    return env_->function_registry.Register(
        descriptor, std::make_unique<Marker>(marker, &calls_[marker]));
  }
  absl::Status AddImplementation(const FunctionDescriptor& descriptor,
                                 int64_t marker) {
    if (modern_) {
      if (!activation_.InsertFunction(
              descriptor, std::make_unique<Marker>(marker, &calls_[marker])))
        return absl::AlreadyExistsError("test activation insertion");
      return absl::OkStatus();
    }
    return legacy_activation_.InsertFunction(
        std::make_unique<LegacyMarker>(descriptor, marker, &calls_[marker]));
  }
  absl::Status Set(const Value& value, absl::string_view name = "x") {
    activation_.InsertOrAssignValue(name, value);
    if (!modern_) {
      CEL_ASSIGN_OR_RETURN(auto converted, interop_internal::ToLegacyValue(
                                               arena(), value, true));
      legacy_activation_.RemoveValueEntry(name);
      legacy_activation_.InsertValue(name, converted);
    }
    return absl::OkStatus();
  }
  absl::StatusOr<Value> Eval(absl::string_view expression, Route route,
                             std::vector<std::string> ids = {"i", "s"},
                             std::optional<Type> checked_type = std::nullopt) {
    for (auto& call : calls_) call.second = 0;
    CEL_ASSIGN_OR_RETURN(auto parsed,
                         ::google::api::expr::parser::Parse(expression));
    expr::CheckedExpr checked;
    if (checked_type.has_value()) {
      CEL_ASSIGN_OR_RETURN(auto checker_builder,
                           CreateTypeCheckerBuilder(env_->descriptor_pool));
      CEL_ASSIGN_OR_RETURN(
          auto fn,
          MakeFunctionDecl(
              "choose", MakeOverloadDecl("generic", IntType(), *checked_type)));
      CEL_RETURN_IF_ERROR(checker_builder->AddFunction(fn));
      CEL_ASSIGN_OR_RETURN(auto checker, checker_builder->Build());
      CEL_ASSIGN_OR_RETURN(auto ast, CreateAstFromParsedExpr(parsed));
      CEL_ASSIGN_OR_RETURN(auto validated, checker->Check(std::move(ast)));
      if (!validated.IsValid())
        return absl::InvalidArgumentError(validated.FormatError());
      CEL_ASSIGN_OR_RETURN(auto checked_ast, validated.ReleaseAst());
      CEL_RETURN_IF_ERROR(AstToCheckedExpr(*checked_ast, &checked));
    } else {
      *checked.mutable_expr() = parsed.expr();
      *checked.mutable_source_info() = parsed.source_info();
      if (route == Route::kUnknown) ids = {"missing_id"};
      if (route != Route::kParsed) {
        for (size_t i = 0;
             i < ids.size() && (i == 0 || route == Route::kMultiple); ++i)
          (*checked.mutable_reference_map())[checked.expr().id()]
              .add_overload_id(ids[i]);
      }
    }
    auto program =
        route == Route::kParsed && !checked_type.has_value()
            ? builder_->CreateExpression(&parsed.expr(), &parsed.source_info())
            : builder_->CreateExpression(&checked);
    if (!program.ok()) return program.status();
    if (mutate_after_planning_.has_value())
      MutateCallerOptions(*mutate_after_planning_);
    const auto& flat = static_cast<legacy::CelExpressionFlatImpl&>(**program)
                           .flat_expression();
    auto state = flat.MakeEvaluatorState(
        env_->descriptor_pool.get(), env_->MutableMessageFactory(), arena());
    enumerations_ = id_lookups_ = 0;
    if (modern_) {
      CountingActivation counted(activation_, &enumerations_, &id_lookups_);
      return flat.EvaluateWithCallback(counted, nullptr, {}, state);
    }
    interop_internal::AdapterActivationImpl adapted(legacy_activation_);
    CountingActivation counted(adapted, &enumerations_, &id_lookups_);
    return flat.EvaluateWithCallback(counted, nullptr, {}, state);
  }
  int enumerations() const { return enumerations_; }
  int id_lookups() const { return id_lookups_; }
  int calls() const {
    int total = 0;
    for (const auto& call : calls_) total += call.second;
    return total;
  }

 private:
  google::protobuf::Arena arena_;
  std::shared_ptr<RuntimeEnv> env_;
  bool lazy_, modern_;
  RuntimeOptions caller_options_;
  std::optional<bool> mutate_after_planning_;
  int enumerations_ = 0, id_lookups_ = 0;
  std::unique_ptr<legacy::CelExpressionBuilderFlatImpl> builder_;
  Activation activation_;
  legacy::Activation legacy_activation_;
  std::map<int64_t, int> calls_;
};

const Route kRoutes[] = {Route::kParsed, Route::kSingle, Route::kMultiple,
                         Route::kUnknown};

void ExpectMarker(const absl::StatusOr<Value>& result, int64_t marker,
                  const Dispatch& h) {
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->IsInt()) << result->DebugString();
  EXPECT_EQ(result->GetInt().NativeValue(), marker);
  EXPECT_EQ(h.calls(), 1);
}
void ExpectNoOverload(const absl::StatusOr<Value>& result, const Dispatch& h) {
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->IsError()) << result->DebugString();
  EXPECT_THAT(result->GetError().NativeValue().message(),
              testing::HasSubstr("No matching overload"));
  EXPECT_EQ(h.calls(), 0);
}

TEST(FunctionDispatchTest, TypeSpecFactoriesUseCheckedIdsAndLegacyFallback) {
  const FunctionTypeSpec spec(std::make_unique<TypeSpec>(PrimitiveType::kInt64),
                              {TypeSpec(PrimitiveType::kInt64)});
  for (bool lazy : {false, true})
    for (bool modern : {false, true})
      for (bool type_level : {false, true})
        for (bool member : {false, true}) {
          SCOPED_TRACE(testing::Message()
                       << lazy << modern << type_level << member);
          Dispatch h(lazy, type_level, modern);
          auto descriptor =
              member ? FunctionDescriptor::Member("choose", "i", spec)
                     : FunctionDescriptor::Global("choose", "i", spec);
          ASSERT_OK(h.Add(descriptor, descriptor, 11));
          const absl::string_view expression =
              member ? "x.choose()" : "choose(x)";
          for (Route route :
               {Route::kParsed, Route::kSingle, Route::kUnknown}) {
            SCOPED_TRACE(static_cast<int>(route));
            ASSERT_OK(h.Set(IntValue(1)));
            ExpectMarker(h.Eval(expression, route), 11, h);
            ASSERT_OK(h.Set(h.string()));
            ExpectNoOverload(h.Eval(expression, route), h);
          }
        }
}

void ExpectException(const Value& result, const Value& input,
                     const Dispatch& h) {
  if (input.IsUnknown()) {
    ASSERT_TRUE(result.IsUnknown()) << result.DebugString();
    EXPECT_EQ(result.GetUnknown().ToAttributeSet(),
              input.GetUnknown().ToAttributeSet());
    EXPECT_EQ(common_internal::GetUnknownFunctionResultSet(result.GetUnknown()),
              common_internal::GetUnknownFunctionResultSet(input.GetUnknown()));
  } else {
    ASSERT_TRUE(result.IsError()) << result.DebugString();
    EXPECT_EQ(result.GetError().NativeValue(), input.GetError().NativeValue());
    EXPECT_EQ(result.GetError().NativeValue().GetPayload("cel.test/input"),
              absl::Cord("original"));
  }
  EXPECT_EQ(h.calls(), 0);
}

enum class MatchOutcome { kAccept, kReject, kObservationFailure };

void CheckRoute(const std::vector<Type>& registration,
                const std::vector<Type>& implementation,
                const std::vector<Value>& arguments, bool lazy, bool type_level,
                Route route, bool with_id, MatchOutcome expected,
                bool modern = true) {
  SCOPED_TRACE(lazy);
  SCOPED_TRACE(type_level);
  SCOPED_TRACE(static_cast<int>(route));
  Dispatch h(lazy, type_level, modern);
  FunctionDescriptor r("choose", with_id ? "i" : "", false, registration);
  FunctionDescriptor i("choose", with_id ? "i" : "", false, implementation);
  ASSERT_OK(h.Add(r, i, 11));
  std::string expression = "choose(";
  for (size_t n = 0; n < arguments.size(); ++n) {
    const std::string name = "arg" + std::to_string(n);
    ASSERT_OK(h.Set(arguments[n], name));
    if (n) expression += ",";
    expression += name;
  }
  expression += ")";
  auto result = h.Eval(expression, route, {"i"});
  if (expected == MatchOutcome::kAccept)
    ExpectMarker(result, 11, h);
  else if (expected == MatchOutcome::kReject)
    ExpectNoOverload(result, h);
  else {
    EXPECT_EQ(result.status(), testing_internal::Sentinel());
    EXPECT_EQ(result.status().GetPayload("cel.test/observation"),
              absl::Cord("preserved"));
    EXPECT_EQ(h.calls(), 0);
  }
  EXPECT_EQ(h.enumerations(), lazy && !with_id ? 1 : 0);
  EXPECT_EQ(h.id_lookups(), lazy && with_id ? 1 : 0);
}

TEST(FunctionDispatchTest, SharedShallowPreflightUsesEveryFeasibleRoute) {
  google::protobuf::Arena arena;
  const auto list = [&](std::initializer_list<Value> values) -> Value {
    auto builder = NewListValueBuilder(&arena);
    for (const auto& value : values) EXPECT_OK(builder->Add(value));
    return std::move(*builder).Build();
  };
  const Value integer = IntValue(1), text = StringValue::From("s", &arena);
  const std::vector<std::vector<Value>> normal = {
      {integer, text},
      {list({integer}), integer},
      {list({integer}), list({text})}};
  struct Path {
    bool lazy, id;
    Route route;
  };
  const std::vector<Path> paths = {
      {false, true, Route::kParsed}, {false, true, Route::kSingle},
      {true, true, Route::kParsed},  {true, true, Route::kSingle},
      {true, false, Route::kParsed}, {true, false, Route::kUnknown}};
  const std::vector<Type> signature = {TypeParamType("X"), TypeParamType("X")};
  size_t normal_cells = 0, fault_cells = 0;
  for (const auto& path : paths)
    for (bool type : {false, true}) {
      for (bool modern : {false, true}) {
        if (!path.lazy && !modern) continue;
        for (const auto& args : normal) {
          CheckRoute(
              signature, signature, args, path.lazy, type, path.route, path.id,
              type ? MatchOutcome::kReject : MatchOutcome::kAccept, modern);
          ++normal_cells;
        }
      }
      for (bool other_list : {false, true})
        for (bool reverse : {false, true}) {
          int observations = 0;
          testing_internal::FailingList impl(testing_internal::Failure::kCreate,
                                             &observations);
          std::vector<Value> args = {CustomListValue(&impl, &arena),
                                     other_list ? list({integer}) : integer};
          if (reverse) std::reverse(args.begin(), args.end());
          CheckRoute(signature, signature, args, path.lazy, type, path.route,
                     path.id,
                     !type        ? MatchOutcome::kAccept
                     : other_list ? MatchOutcome::kObservationFailure
                                  : MatchOutcome::kReject);
          EXPECT_EQ(observations, type && other_list ? 1 : 0);
          ++fault_cells;
        }
    }
  EXPECT_EQ(normal_cells, 60);
  EXPECT_EQ(fault_cells, 48);
  std::cout << "Shallow routing: " << normal_cells << " normal, " << fault_cells
            << " fault cells\n";
}

TEST(FunctionDispatchTest,
     AtomicMetadataAndNominalGatesKeepTheirOwnBoundaries) {
  google::protobuf::Arena arena;
  const Type a = TypeParamType("A"), empty = TypeParamType("");
  const Type box = OpaqueType(&arena, "box", {a}),
             box_empty = OpaqueType(&arena, "box", {empty});
  const Value custom = testing_internal::MakeOpaque(&arena, box.GetOpaque());
  const Value ce = testing_internal::MakeOpaque(&arena, box_empty.GetOpaque());
  const Value integer = IntValue(1), text = StringValue::From("s", &arena);
  const Value singleton = CustomListValue(
      google::protobuf::Arena::Create<testing_internal::NativeList>(
          &arena, std::vector<Value>{custom}),
      &arena);
  const Value ints = CustomListValue(
      google::protobuf::Arena::Create<testing_internal::NativeList>(
          &arena, std::vector<Value>{integer}),
      &arena);
  auto map_builder = NewMapValueBuilder(&arena);
  ASSERT_OK(map_builder->Put(StringValue::From("k", &arena), custom));
  const Value mapped = std::move(*map_builder).Build();
  struct Family {
    const char* name;
    std::vector<Type> r, i;
    std::vector<Value> args;
    bool permute;
  };
  const std::vector<Family> families = {
      {"metadata",
       {box, a, DynType()},
       {box, DynType(), a},
       {custom, integer, text},
       true},
      {"free names",
       {box, TypeParamType("R"), DynType()},
       {box, DynType(), TypeParamType("I")},
       {custom, integer, text},
       false},
      {"list",
       {ListType(&arena, box), a, DynType()},
       {ListType(&arena, box), DynType(), a},
       {singleton, integer, text},
       false},
      {"map",
       {MapType(&arena, StringType(), box), a, DynType()},
       {MapType(&arena, StringType(), box), DynType(), a},
       {mapped, integer, text},
       false},
      {"optional",
       {OptionalType(&arena, box), a, DynType()},
       {OptionalType(&arena, box), DynType(), a},
       {OptionalValue::Of(custom, &arena), integer, text},
       false},
      {"empty name",
       {box_empty, empty, DynType()},
       {box_empty, DynType(), empty},
       {ce, integer, text},
       false}};
  size_t joint_cells = 0, static_cells = 0, boundary_joint = 0,
         boundary_static = 0;
  for (const auto& family : families) {
    SCOPED_TRACE(family.name);
    std::vector<size_t> order = {0, 1, 2};
    do {
      std::vector<Type> r, i;
      std::vector<Value> args;
      for (size_t p : order) {
        r.push_back(family.r[p]);
        i.push_back(family.i[p]);
        args.push_back(family.args[p]);
      }
      for (bool type : {false, true})
        for (bool known : {false, true}) {
          const Route route = known ? Route::kSingle : Route::kParsed;
          for (bool reverse : {false, true}) {
            CheckRoute(reverse ? i : r, reverse ? r : i, args, true, type,
                       route, known, MatchOutcome::kAccept);
            ++joint_cells;
          }
          for (const auto& signature : {r, i}) {
            CheckRoute(signature, signature, args, false, type, route, true,
                       MatchOutcome::kAccept);
            ++static_cells;
          }
        }
    } while (family.permute &&
             std::next_permutation(order.begin(), order.end()));
  }
  const std::vector<Type> r = {box, a, DynType()},
                          i = {box, ListType(&arena, a), a};
  const std::vector<Value> args = {custom, ints, integer};
  for (bool type : {false, true})
    for (bool known : {false, true}) {
      const Route route = known ? Route::kSingle : Route::kParsed;
      for (bool reverse : {false, true}) {
        CheckRoute(reverse ? i : r, reverse ? r : i, args, true, type, route,
                   known,
                   known ? MatchOutcome::kAccept : MatchOutcome::kReject);
        ++boundary_joint;
      }
      for (const auto& signature : {r, i}) {
        CheckRoute(signature, signature, args, false, type, route, true,
                   MatchOutcome::kAccept);
        ++boundary_static;
      }
    }
  EXPECT_EQ(joint_cells, 88);
  EXPECT_EQ(static_cells, 88);
  EXPECT_EQ(boundary_joint, 8);
  EXPECT_EQ(boundary_static, 8);
  std::cout << "Opaque routing: "
            << joint_cells + static_cells + boundary_joint + boundary_static
            << " cells\n";
}

TEST(FunctionDispatchTest,
     NativeOpaqueInferencePreservesCapabilityAndOccurrences) {
  google::protobuf::Arena arena;
  const Type a = TypeParamType("A"), b = TypeParamType("B");
  const auto custom = [&](Type type, bool distinct = false) {
    return testing_internal::MakeOpaque(
        &arena, OpaqueType(&arena, "optional_type", {type}), distinct);
  };
  const auto list = [&](std::initializer_list<Value> values) -> Value {
    return CustomListValue(
        google::protobuf::Arena::Create<testing_internal::NativeList>(
            &arena, std::vector<Value>(values)),
        &arena);
  };
  const Value ci = custom(IntType()), ci2 = custom(IntType(), true),
              cs = custom(StringType()), cd = custom(DynType());
  const Value oi = OptionalValue::Of(IntValue(1), &arena),
              os = OptionalValue::Of(StringValue::From("s", &arena), &arena),
              none = OptionalValue::None();
  struct Plain {
    std::vector<Type> signature;
    std::vector<Value> args;
    bool accepts;
  };
  const std::vector<Plain> plain = {
      {{a, a}, {ci, ci2}, true},
      {{a, a}, {ci, cs}, false},
      {{a, a}, {cd, none}, false},
      {{a, a, a}, {cd, oi, os}, false},
      {{a, a, a}, {cd, none, oi}, false},
      {{ListType(&arena, a), a}, {list({ci, cs}), BoolValue(true)}, true},
      {{ListType(&arena, a), a}, {list({cs, ci}), BoolValue(true)}, true},
      {{ListType(&arena, a), a}, {list({cd, oi}), BoolValue(true)}, true},
      {{ListType(&arena, a), a}, {list({oi, cd}), BoolValue(true)}, true}};
  for (const auto& fixture : plain) {
    std::vector<size_t> order(fixture.args.size());
    std::iota(order.begin(), order.end(), 0);
    do {
      std::vector<Type> signature;
      std::vector<Value> args;
      for (size_t p : order) {
        signature.push_back(fixture.signature[p]);
        args.push_back(fixture.args[p]);
      }
      for (bool lazy : {false, true})
        for (bool type : {false, true})
          for (bool known : {false, true})
            CheckRoute(signature, signature, args, lazy, type,
                       known ? Route::kSingle : Route::kParsed, !lazy || known,
                       (!type || fixture.accepts) ? MatchOutcome::kAccept
                                                  : MatchOutcome::kReject);
    } while (std::next_permutation(order.begin(), order.end()));
  }
  const Type optional = OptionalType(&arena, b);
  struct Joint {
    std::vector<Type> r, i;
    std::vector<Value> args;
    bool joint, static_reg;
    bool static_controls;
  };
  const std::vector<Joint> joint = {
      {{a, a}, {optional, optional}, {oi, os}, false, false, true},
      {{a, a}, {optional, optional}, {none, oi}, true, true, true},
      {{a, a}, {optional, optional}, {ci, ci}, false, true, true},
      {{a, a},
       {OptionalType(&arena, IntType()), TypeParamType("Y")},
       {oi, ci},
       false,
       false,
       false},
      {{a, a},
       {OptionalType(&arena, IntType()), TypeParamType("Y")},
       {none, ci},
       false,
       false,
       false},
      {{a, a},
       {OptionalType(&arena, IntType()), TypeParamType("Y")},
       {oi, cd},
       false,
       false,
       false},
      {{a, a},
       {OptionalType(&arena, IntType()), TypeParamType("Y")},
       {oi, oi},
       true,
       false,
       false}};
  for (const auto& fixture : joint)
    for (bool swap : {false, true}) {
      auto r = fixture.r, i = fixture.i;
      auto args = fixture.args;
      if (swap) {
        std::reverse(r.begin(), r.end());
        std::reverse(i.begin(), i.end());
        std::reverse(args.begin(), args.end());
      }
      for (bool type : {false, true})
        for (bool known : {false, true}) {
          const Route route = known ? Route::kSingle : Route::kParsed;
          for (bool reverse : {false, true}) {
            CheckRoute(reverse ? i : r, reverse ? r : i, args, true, type,
                       route, known,
                       (!type || fixture.joint) ? MatchOutcome::kAccept
                                                : MatchOutcome::kReject);
          }
          if (fixture.static_controls) {
            CheckRoute(r, r, args, false, type, route, true,
                       (!type || fixture.static_reg) ? MatchOutcome::kAccept
                                                     : MatchOutcome::kReject);
            CheckRoute(i, i, args, false, type, route, true,
                       (!type || fixture.joint) ? MatchOutcome::kAccept
                                                : MatchOutcome::kReject);
          }
        }
    }
}

TEST(FunctionDispatchTest, GenericKindCollisionsRetainAmbiguityAndStaticOrder) {
  size_t ambiguity = 0, ordered = 0;
  for (bool container : {false, true})
    for (bool empty_name : {false, true})
      for (bool reverse : {false, true})
        for (bool type : {false, true}) {
          for (bool modern : {false, true}) {
            Dispatch h(true, type, modern);
            const Type v = TypeParamType(empty_name ? "" : "A");
            const std::vector<Type> generic = {v, container ? h.list(v) : v};
            const std::vector<Type> concrete = {
                IntType(),
                container ? h.list(StringType()) : Type(StringType())};
            FunctionDescriptor reg("choose", "", false, {AnyType(), AnyType()});
            FunctionDescriptor g("choose", "", false, generic),
                c("choose", "", false, concrete);
            ASSERT_OK(h.Add(reg, reverse ? c : g, reverse ? 22 : 11));
            ASSERT_OK(h.AddImplementation(reverse ? g : c, reverse ? 11 : 22));
            ASSERT_OK(h.Set(IntValue(1)));
            ASSERT_OK(
                h.Set(container ? h.list({h.string()}) : h.string(), "y"));
            auto result = h.Eval("choose(x,y)", Route::kParsed);
            if (type)
              ExpectMarker(result, 22, h);
            else {
              EXPECT_EQ(result.status().code(),
                        absl::StatusCode::kInvalidArgument);
              EXPECT_EQ(h.calls(), 0);
            }
            EXPECT_EQ(h.enumerations(), 1);
            EXPECT_EQ(h.id_lookups(), 0);
            ++ambiguity;
          }
          Dispatch h(false, type, true);
          const Type v = TypeParamType(empty_name ? "" : "A");
          FunctionDescriptor g("choose", "g", false,
                               {v, container ? h.list(v) : v});
          FunctionDescriptor c("choose", "c", false,
                               {IntType(), container ? h.list(StringType())
                                                     : Type(StringType())});
          ASSERT_OK(h.Add(reverse ? c : g, reverse ? c : g, reverse ? 22 : 11));
          ASSERT_OK(h.Add(reverse ? g : c, reverse ? g : c, reverse ? 11 : 22));
          ASSERT_OK(h.Set(IntValue(1)));
          ASSERT_OK(h.Set(container ? h.list({h.string()}) : h.string(), "y"));
          ExpectMarker(h.Eval("choose(x,y)", Route::kParsed),
                       type || reverse ? 22 : 11, h);
          ++ordered;
        }
  EXPECT_EQ(ambiguity, 32);
  EXPECT_EQ(ordered, 16);
}

TEST(FunctionDispatchTest, EmptyParameterSpellingsKeepEveryRuntimeContract) {
  google::protobuf::Arena arena;
  for (Type tp : {Type(Kind::kTypeParam), Type(TypeParamType()),
                  Type(TypeParamType(""))}) {
    const Value integer = IntValue(1), two = IntValue(2),
                text = StringValue::From("s", &arena);
    for (bool type : {false, true}) {
      const std::vector<Type> repeated = {tp, tp};
      CheckRoute(repeated, repeated, {integer, text}, false, type,
                 Route::kParsed, true,
                 type ? MatchOutcome::kReject : MatchOutcome::kAccept);
      struct Fixture {
        std::vector<Type> r, i;
        std::vector<Value> values;
        bool own_reg, static_accept, no_id, id_type;
      };
      const std::vector<Fixture> fixtures = {{repeated,
                                              {IntType(), StringType()},
                                              {integer, text},
                                              false,
                                              true,
                                              false,
                                              false},
                                             {repeated,
                                              {IntType(), StringType()},
                                              {integer, two},
                                              false,
                                              false,
                                              false,
                                              false},
                                             {repeated,
                                              {IntType(), IntType()},
                                              {integer, two},
                                              true,
                                              true,
                                              true,
                                              true},
                                             {{tp, IntType()},
                                              {StringType(), tp},
                                              {text, integer},
                                              true,
                                              true,
                                              true,
                                              true}};
      for (const auto& f : fixtures) {
        CheckRoute(
            f.i, f.i, f.values, false, type, Route::kParsed, true,
            f.static_accept ? MatchOutcome::kAccept : MatchOutcome::kReject);
        if (f.own_reg)
          CheckRoute(f.r, f.r, f.values, false, type, Route::kParsed, true,
                     MatchOutcome::kAccept);
        for (bool known : {false, true}) {
          const bool accepts =
              known ? (f.static_accept && (!type || f.id_type)) : f.no_id;
          CheckRoute(f.r, f.i, f.values, true, type,
                     known ? Route::kSingle : Route::kParsed, known,
                     accepts ? MatchOutcome::kAccept : MatchOutcome::kReject,
                     false);
        }
      }
    }
  }
}

TEST(FunctionDispatchTest, RuntimeOptionCaptureSurvivesCallerMutation) {
  for (bool lazy : {false, true})
    for (int setting : {0, 1, 2})
      for (bool after_planning : {false, true}) {
        const bool type = setting == 2;
        Dispatch h(lazy, type, true, true, setting == 0);
        FunctionDescriptor descriptor("choose", "i", false,
                                      {h.list(IntType())});
        ASSERT_OK(h.Add(descriptor, descriptor, 11));
        ASSERT_OK(h.Set(h.list({IntValue(1), h.string()})));
        if (after_planning)
          h.MutateAfterPlanning(!type);
        else
          h.MutateCallerOptions(!type);
        auto result = h.Eval("choose(x)", Route::kParsed);
        if (type)
          ExpectNoOverload(result, h);
        else
          ExpectMarker(result, 11, h);
      }
  EXPECT_FALSE(legacy::ConvertToRuntimeOptions(legacy::InterpreterOptions())
                   .enable_type_level_overload);
}

TEST(FunctionDispatchTest,
     KindQueriesAndParsedIdsUseObservableProviderBranches) {
  for (bool type : {false, true}) {
    Dispatch h(true, type);
    auto first = h.message("cel.expr.conformance.proto2.TestAllTypes"),
         second = h.message("cel.expr.conformance.proto3.TestAllTypes");
    FunctionDescriptor reg("choose", "", false, {AnyType()}),
        impl("choose", "", false, {first.GetRuntimeType()});
    ASSERT_OK(h.Add(reg, impl, 11));
    ASSERT_OK(h.Set(second));
    auto result = h.Eval("choose(x)", Route::kParsed);
    if (type)
      ExpectNoOverload(result, h);
    else
      ExpectMarker(result, 11, h);
    EXPECT_EQ(h.enumerations(), 1);
    EXPECT_EQ(h.id_lookups(), 0);
    for (bool with_id : {false, true})
      for (Route route : {Route::kParsed, Route::kSingle}) {
        if (!with_id && route == Route::kSingle) continue;
        CheckRoute(
            {h.list(IntType())}, {h.list(StringType())},
            {h.list({IntValue(1)})}, true, type, route, with_id,
            with_id && !type ? MatchOutcome::kAccept : MatchOutcome::kReject,
            false);
      }
  }
}

TEST(FunctionDispatchTest,
     ProviderUniquenessRequiresLaterNecessaryObservation) {
  for (bool type : {false, true})
    for (bool same_provider : {false, true}) {
      Dispatch h(true, type, true);
      testing_internal::ObservationCounts counts;
      testing_internal::ReplayThenFailList impl(
          same_provider ? std::vector<Value>{}
                        : std::vector<Value>{IntValue(1)},
          &counts);
      ASSERT_OK(h.Set(CustomListValue(&impl, h.arena())));
      if (same_provider) {
        FunctionDescriptor reg("choose", "", false, {AnyType()});
        FunctionDescriptor first("choose", "", false, {h.list(IntType())}),
            second("choose", "", false, {h.list(StringType())});
        ASSERT_OK(h.Add(reg, first, 11));
        ASSERT_OK(h.AddImplementation(second, 22));
      } else {
        FunctionDescriptor generic("choose", "i", false, {TypeParamType("A")});
        ASSERT_OK(h.Add(generic, generic, 11));
      }
      auto result = h.Eval("choose(x)", Route::kParsed);
      if (type) {
        EXPECT_EQ(result.status(), testing_internal::Sentinel());
        EXPECT_EQ(result.status().GetPayload("cel.test/observation"),
                  absl::Cord("preserved"));
        EXPECT_EQ(h.calls(), 0);
      } else if (same_provider) {
        EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
        EXPECT_EQ(h.calls(), 0);
      } else
        ExpectMarker(result, 11, h);
      EXPECT_EQ(counts.creations, type ? 2 : 0);
      EXPECT_EQ(counts.next, type ? (same_provider ? 1 : 2) : 0);
      EXPECT_EQ(counts.exhausted, type ? 1 : 0);
      EXPECT_EQ(h.enumerations(), same_provider ? 1 : 0);
      EXPECT_EQ(h.id_lookups(), same_provider ? 0 : 1);
    }
}

TEST(FunctionDispatchTest, NestedAndPartialWildcardsKeepLocalObservationRules) {
  using testing_internal::FailingList;
  using testing_internal::FailingMap;
  using testing_internal::Failure;
  for (bool lazy : {false, true})
    for (bool type : {false, true}) {
      for (bool partial_value : {false, true}) {
        Dispatch h(lazy, type, true);
        int creations = 0;
        FailingMap map(Failure::kCreate, &creations);
        Type pattern = partial_value
                           ? Type(MapType(h.arena(), DynType(), IntType()))
                           : Type(MapType(h.arena(), StringType(), DynType()));
        FunctionDescriptor d("choose", "i", false, {pattern});
        ASSERT_OK(h.Add(d, d, 11));
        ASSERT_OK(h.Set(CustomMapValue(&map, h.arena())));
        auto result = h.Eval("choose(x)", Route::kParsed);
        if (type) {
          EXPECT_EQ(result.status(), testing_internal::Sentinel());
          EXPECT_EQ(result.status().GetPayload("cel.test/observation"),
                    absl::Cord("preserved"));
          EXPECT_EQ(h.calls(), 0);
        } else
          ExpectMarker(result, 11, h);
        EXPECT_EQ(creations, type ? 1 : 0);
      }
      for (Type key : {Type(AnyType()), Type(DynType())})
        for (Type value : {Type(AnyType()), Type(DynType())}) {
          Dispatch h(lazy, type, true);
          int creations = 0;
          FailingMap map(Failure::kCreate, &creations);
          FunctionDescriptor d("choose", "i", false,
                               {MapType(h.arena(), key, value)});
          ASSERT_OK(h.Add(d, d, 11));
          ASSERT_OK(h.Set(CustomMapValue(&map, h.arena())));
          ExpectMarker(h.Eval("choose(x)", Route::kParsed), 11, h);
          EXPECT_EQ(creations, 0);
        }
      for (Type child : {Type(AnyType()), Type(DynType())}) {
        Dispatch h(lazy, type, true);
        int creations = 0;
        FailingList list(Failure::kCreate, &creations);
        FunctionDescriptor d("choose", "i", false, {h.list(child)});
        ASSERT_OK(h.Add(d, d, 11));
        ASSERT_OK(h.Set(CustomListValue(&list, h.arena())));
        ExpectMarker(h.Eval("choose(x)", Route::kParsed), 11, h);
        EXPECT_EQ(creations, 0);
      }
      {
        Dispatch h(lazy, type, true);
        int inner_creations = 0, outer_reads = 0;
        FailingList inner(Failure::kCreate, &inner_creations);
        testing_internal::NativeList outer({CustomListValue(&inner, h.arena())},
                                           &outer_reads);
        FunctionDescriptor d("choose", "i", false, {h.list(h.list(DynType()))});
        ASSERT_OK(h.Add(d, d, 11));
        ASSERT_OK(h.Set(CustomListValue(&outer, h.arena())));
        ExpectMarker(h.Eval("choose(x)", Route::kParsed), 11, h);
        EXPECT_EQ(inner_creations, 0);
        if (type)
          EXPECT_GT(outer_reads, 0);
        else
          EXPECT_EQ(outer_reads, 0);
      }
      for (int shape : {0, 1, 2}) {
        Dispatch h(lazy, type, true);
        Type pattern;
        if (shape == 0) {
          pattern = h.list(h.list(DynType()));
          ASSERT_OK(h.Set(h.list({IntValue(1)})));
        } else {
          pattern = shape == 1
                        ? Type(MapType(h.arena(), StringType(), DynType()))
                        : Type(MapType(h.arena(), DynType(), IntType()));
          auto map = NewMapValueBuilder(h.arena());
          ASSERT_OK(map->Put(shape == 1 ? Value(IntValue(1)) : h.string(),
                             h.string()));
          ASSERT_OK(h.Set(std::move(*map).Build()));
        }
        FunctionDescriptor d("choose", "i", false, {pattern});
        ASSERT_OK(h.Add(d, d, 11));
        auto result = h.Eval("choose(x)", Route::kParsed);
        if (type)
          ExpectNoOverload(result, h);
        else
          ExpectMarker(result, 11, h);
      }
    }
}

TEST(FunctionDispatchTest,
     ContainedMixedExceptionsJoinBeforeDistinctOccurrencesMeet) {
  google::protobuf::Arena arena;
  const auto list = [&](std::vector<Value> values) -> Value {
    return CustomListValue(
        google::protobuf::Arena::Create<testing_internal::NativeList>(
            &arena, std::move(values)),
        &arena);
  };
  const Type a = TypeParamType("A");
  const std::vector<Type> pattern = {ListType(&arena, a), ListType(&arena, a)};
  for (Value exceptional :
       {Value(ErrorValue::From(testing_internal::Sentinel(), &arena)),
        Value(UnknownValue())})
    for (bool mixed : {false, true})
      for (bool elements_reverse : {false, true})
        for (bool swap : {false, true}) {
          std::vector<Value> elements =
              mixed ? std::vector<Value>{IntValue(1), exceptional}
                    : std::vector<Value>{exceptional};
          if (elements_reverse) std::reverse(elements.begin(), elements.end());
          std::vector<Value> args = {list(elements),
                                     list({IntValue(mixed ? 2 : 1)})};
          if (swap) std::reverse(args.begin(), args.end());
          for (bool lazy : {false, true})
            for (bool type : {false, true}) {
              CheckRoute(pattern, pattern, args, lazy, type, Route::kParsed,
                         true,
                         !type || mixed ? MatchOutcome::kAccept
                                        : MatchOutcome::kReject);
            }
        }
}

TEST(FunctionDispatchTest, CheckedCandidateGroupsKeepTheirOriginalPriority) {
  for (bool type : {false, true})
    for (bool present : {false, true})
      for (bool integer : {false, true})
        for (Route route : kRoutes) {
          Dispatch h(true, type);
          FunctionDescriptor s("choose", "s", false, {IntType()}),
              l("choose", "l", false, {StringType()});
          FunctionDescriptor impl("choose", present ? "l" : "other", false,
                                  {StringType()});
          ASSERT_OK(h.Add(l, impl, 22));
          ASSERT_OK(h.AddStatic(s, 11));
          ASSERT_OK(h.Set(integer ? Value(IntValue(1)) : h.string()));
          auto result = h.Eval("choose(x)", route, {"s", "l"});
          if (route == Route::kSingle && integer)
            ExpectMarker(result, 11, h);
          else if (route != Route::kSingle && !integer && present)
            ExpectMarker(result, 22, h);
          else
            ExpectNoOverload(result, h);
        }
}

TEST(FunctionDispatchTest, WrongMetadataKeepsPlanningAndEvaluationDiagnostics) {
  for (bool lazy : {false, true})
    for (bool type : {false, true})
      for (bool wrong_style : {false, true})
        for (Route route : {Route::kParsed, Route::kSingle}) {
          Dispatch h(lazy, type, !lazy, true, false, true);
          FunctionDescriptor d(
              "choose", "i", wrong_style,
              std::vector<Type>(wrong_style ? 1 : 2, IntType()));
          ASSERT_OK(h.Add(d, d, 11));
          ASSERT_OK(h.Set(IntValue(1)));
          auto result = h.Eval("choose(x)", route);
          if (route == Route::kParsed) {
            EXPECT_EQ(result.status().code(),
                      absl::StatusCode::kInvalidArgument);
            EXPECT_EQ(h.calls(), 0);
          } else
            ExpectNoOverload(result, h);
        }
}

TEST(FunctionDispatchTest, PrimitiveRoutesAndRuntimeIdFallback) {
  for (bool lazy : {false, true})
    for (bool type : {false, true})
      for (Route route : kRoutes) {
        SCOPED_TRACE(lazy);
        SCOPED_TRACE(type);
        SCOPED_TRACE(static_cast<int>(route));
        Dispatch h(lazy, type);
        FunctionDescriptor i("choose", "i", false, {IntType()}),
            s("choose", "s", false, {StringType()});
        ASSERT_OK(h.Add(i, i, 11));
        ASSERT_OK(h.Add(s, s, 22));
        ASSERT_OK(h.Set(IntValue(1)));
        ExpectMarker(h.Eval("choose(x)", route, {"s", "i"}), 11, h);
        ASSERT_OK(h.Set(h.string()));
        ExpectMarker(h.Eval("choose(x)", route, {"i", "s"}), 22, h);
      }
}

TEST(FunctionDispatchTest, StrictExceptionsBeforeProviderAmbiguity) {
  for (bool type : {false, true})
    for (bool modern : {false, true})
      for (Route route : kRoutes)
        for (bool unknown : {false, true}) {
          SCOPED_TRACE(type);
          SCOPED_TRACE(modern);
          SCOPED_TRACE(static_cast<int>(route));
          Dispatch h(true, type, modern);
          FunctionDescriptor i("choose", "i", false, {IntType()}),
              s("choose", "s", false, {StringType()});
          if (route == Route::kParsed || route == Route::kUnknown) {
            FunctionDescriptor any("choose", false, {Kind::kAny});
            ASSERT_OK(h.Add(any, i, 11));
            ASSERT_OK(h.AddImplementation(s, 22));
          } else {
            ASSERT_OK(h.Add(i, i, 11));
            ASSERT_OK(h.Add(s, s, 22));
          }
          const Value input = h.exception(unknown);
          ASSERT_OK(h.Set(input));
          ASSERT_OK_AND_ASSIGN(auto result, h.Eval("choose(x)", route));
          ExpectException(result, input, h);
        }
  for (bool lazy : {false, true})
    for (bool type : {false, true})
      for (Route route : {Route::kSingle, Route::kMultiple}) {
        Dispatch h(lazy, type);
        FunctionDescriptor i("choose", "i", false, {IntType()}),
            s("choose", "s", false, {StringType()});
        ASSERT_OK(h.Add(i, i, 11));
        ASSERT_OK(h.Add(s, s, 22));
        for (bool unknown : {false, true}) {
          const Value input = h.exception(unknown);
          ASSERT_OK(h.Set(input));
          ASSERT_OK_AND_ASSIGN(auto result, h.Eval("choose(x)", route));
          ExpectException(result, input, h);
        }
      }
  for (bool type : {false, true}) {
    Dispatch h(true, type, false, false);
    FunctionDescriptor any("choose", false, {Kind::kAny}),
        i("choose", "i", false, {IntType()}),
        s("choose", "s", false, {StringType()});
    ASSERT_OK(h.Add(any, i, 11));
    ASSERT_OK(h.AddImplementation(s, 22));
    ASSERT_OK(h.Set(h.unknown()));
    ExpectNoOverload(h.Eval("choose(x)", Route::kParsed), h);
  }
}

TEST(FunctionDispatchTest, OccurrenceAndJointTablesKeepTheirEvaluatorResults) {
  google::protobuf::Arena arena;
  const Type a = TypeParamType("A");
  const auto lt = [&](Type t) -> Type { return ListType(&arena, t); };
  const auto mt = [&](Type k, Type v) -> Type { return MapType(&arena, k, v); };
  const auto list = [&](std::initializer_list<Value> values) -> Value {
    auto builder = NewListValueBuilder(&arena);
    for (const auto& value : values) EXPECT_OK(builder->Add(value));
    return std::move(*builder).Build();
  };
  const Value text = StringValue::From("s", &arena), integer = IntValue(1);
  const Value empty = list({}), ints = list({integer}), strings = list({text}),
              bools = list({BoolValue(true)}), mixed = list({integer, text});
  auto map_builder = NewMapValueBuilder(&arena);
  ASSERT_OK(map_builder->Put(StringValue::From("a", &arena), integer));
  ASSERT_OK(map_builder->Put(StringValue::From("b", &arena), text));
  const Value mixed_map = std::move(*map_builder).Build();
  auto single_map = NewMapValueBuilder(&arena);
  ASSERT_OK(single_map->Put(text, integer));
  const Value mapped = std::move(*single_map).Build();
  auto empty_map = NewMapValueBuilder(&arena);
  const Value no_entries = std::move(*empty_map).Build();
  struct Occurrence {
    std::vector<Type> signature;
    std::vector<Value> args;
    bool expected, native;
  };
  const std::vector<Occurrence> occurrences = {
      {{lt(a)}, {mixed}, true, false},
      {{mt(StringType(), a)}, {mixed_map}, true, false},
      {{lt(lt(a))}, {list({ints, strings})}, true, false},
      {{lt(IntType())}, {mixed}, false, false},
      {{a, a}, {ints, strings}, false, false},
      {{a, a, a}, {empty, ints, strings}, false, false},
      {{a, a, a}, {empty, ints, list({IntValue(2)})}, true, false},
      {{a, a}, {mixed, bools}, true, false},
      {{a, a, a}, {mixed, ints, bools}, false, false},
      {{lt(a), a}, {list({ints}), strings}, false, false},
      {{mt(a, a)}, {mapped}, false, false},
      {{a, a, a},
       {OptionalValue::None(), OptionalValue::Of(ints, &arena),
        OptionalValue::Of(strings, &arena)},
       false,
       true},
      {{a, a}, {empty, ints}, true, false},
      {{a, a}, {list({empty}), list({ints})}, true, false},
      {{a, a}, {empty, no_entries}, false, false},
      {{OptionalType(&arena, lt(a))},
       {OptionalValue::Of(mixed, &arena)},
       true,
       true}};
  ASSERT_EQ(occurrences.size(), 16);
  for (const auto& fixture : occurrences) {
    std::vector<size_t> order(fixture.args.size());
    std::iota(order.begin(), order.end(), 0);
    do {
      std::vector<Type> signature;
      std::vector<Value> args;
      for (size_t index : order) {
        signature.push_back(fixture.signature[index]);
        args.push_back(fixture.args[index]);
      }
      for (bool lazy : {false, true})
        for (bool type : {false, true}) {
          CheckRoute(signature, signature, args, lazy, type, Route::kParsed,
                     true,
                     !type || fixture.expected ? MatchOutcome::kAccept
                                               : MatchOutcome::kReject,
                     !lazy || fixture.native);
        }
    } while (std::next_permutation(order.begin(), order.end()));
  }
  for (bool rename : {false, true}) {
    const Type r = TypeParamType(rename ? "R" : "A"),
               i = TypeParamType(rename ? "I" : "A"),
               b = TypeParamType(rename ? "J" : "B");
    struct Joint {
      std::vector<Type> r, i;
      std::vector<Value> args;
      bool expected;
    };
    const std::vector<Joint> joints = {
        {{lt(r), lt(r)},
         {lt(DynType()), lt(StringType())},
         {ints, empty},
         false},
        {{lt(r), lt(r)},
         {lt(DynType()), lt(StringType())},
         {strings, empty},
         true},
        {{lt(r), lt(r)},
         {lt(DynType()), lt(StringType())},
         {empty, empty},
         true},
        {{lt(r)}, {lt(b)}, {mixed}, true},
        {{lt(r)}, {lt(IntType())}, {mixed}, false},
        {{r, r}, {lt(DynType()), lt(BoolType())}, {mixed, bools}, true},
        {{r, lt(IntType())}, {lt(StringType()), i}, {strings, ints}, true}};
    ASSERT_EQ(joints.size(), 7);
    for (const auto& fixture : joints) {
      std::vector<size_t> order(fixture.args.size());
      std::iota(order.begin(), order.end(), 0);
      do {
        std::vector<Type> reg, impl;
        std::vector<Value> args;
        for (size_t index : order) {
          reg.push_back(fixture.r[index]);
          impl.push_back(fixture.i[index]);
          args.push_back(fixture.args[index]);
        }
        for (bool reverse : {false, true})
          for (bool type : {false, true})
            for (Route route : {Route::kParsed, Route::kSingle}) {
              CheckRoute(reverse ? impl : reg, reverse ? reg : impl, args, true,
                         type, route, true,
                         !type || fixture.expected ? MatchOutcome::kAccept
                                                   : MatchOutcome::kReject,
                         false);
            }
      } while (std::next_permutation(order.begin(), order.end()));
    }
  }
}

TEST(FunctionDispatchTest, EffectiveStrictnessBelongsToImplementation) {
  for (bool registration_strict : {false, true})
    for (bool implementation_strict : {false, true})
      for (bool type : {false, true})
        for (Route route : {Route::kParsed, Route::kSingle})
          for (bool unknown : {false, true}) {
            Dispatch h(true, type, false);
            FunctionDescriptor reg("choose", "i", false, {AnyType()},
                                   registration_strict);
            FunctionDescriptor impl("choose", "i", false, {AnyType()},
                                    implementation_strict);
            ASSERT_OK(h.Add(reg, impl, 11));
            const Value input = h.exception(unknown);
            ASSERT_OK(h.Set(input));
            auto result = h.Eval("choose(x)", route);
            if (!implementation_strict)
              ExpectMarker(result, 11, h);
            else {
              ASSERT_OK(result.status());
              ExpectException(*result, input, h);
            }
          }
}

TEST(FunctionDispatchTest, RegistrationGuardsAlsoApplyToExceptionalIdCalls) {
  for (bool type : {false, true})
    for (Route route : {Route::kParsed, Route::kSingle, Route::kUnknown})
      for (bool unknown : {false, true}) {
        Dispatch h(true, type);
        FunctionDescriptor reg("choose", "i", false, {IntType()});
        FunctionDescriptor impl("choose", "i", false, {AnyType()}, false);
        ASSERT_OK(h.Add(reg, impl, 11));
        const Value input = h.exception(unknown);
        ASSERT_OK(h.Set(input));
        ASSERT_OK_AND_ASSIGN(auto result, h.Eval("choose(x)", route));
        ExpectException(result, input, h);
      }
}

TEST(FunctionDispatchTest, IdBindingUsesTheConfiguredRuntimeGuard) {
  for (bool type : {false, true})
    for (bool modern : {false, true})
      for (bool with_id : {false, true})
        for (Route route : {Route::kParsed, Route::kSingle, Route::kUnknown}) {
          Dispatch h(true, type, modern);
          FunctionDescriptor reg("choose", with_id ? "i" : "", false,
                                 {h.list(IntType())});
          FunctionDescriptor impl("choose", with_id ? "i" : "", false,
                                  {h.list(StringType())});
          ASSERT_OK(h.Add(reg, impl, 11));
          for (absl::string_view expression :
               {"choose([1])", "choose(['s'])", "choose([])"}) {
            auto result = h.Eval(expression, route);
            if (with_id && !type)
              ExpectMarker(result, 11, h);
            else
              ExpectNoOverload(result, h);
          }
        }
}

TEST(FunctionDispatchTest,
     GenericContainersAgreeWithRealCheckerForHeterogeneousValues) {
  for (bool lazy : {false, true})
    for (bool type : {false, true})
      for (bool checked : {false, true})
        for (int shape : {0, 1, 2, 3}) {
          Dispatch h(lazy, type);
          Type a = TypeParamType("A"), expected;
          absl::string_view expression;
          if (shape == 0) {
            expected = a;
            expression = "choose([1,'s'])";
          }
          if (shape == 1) {
            expected = h.list(a);
            expression = "choose([1,'s'])";
          }
          if (shape == 2) {
            expected = MapType(h.arena(), StringType(), a);
            expression = "choose({'a':1,'b':'s'})";
          }
          if (shape == 3) {
            expected = h.list(h.list(a));
            expression = "choose([[1],['s']])";
          }
          FunctionDescriptor d("choose", "generic", false, {expected});
          ASSERT_OK(h.Add(d, d, 11));
          ExpectMarker(
              h.Eval(expression, checked ? Route::kSingle : Route::kParsed,
                     {"generic"},
                     checked ? std::optional<Type>(expected) : std::nullopt),
              11, h);
        }
}

TEST(FunctionDispatchTest, ConcreteContainersAndJointConstraints) {
  for (bool lazy : {false, true})
    for (bool type : {false, true})
      for (Route route : {Route::kParsed, Route::kSingle}) {
        Dispatch h(lazy, type);
        FunctionDescriptor d("choose", "i", false, {h.list(IntType())});
        ASSERT_OK(h.Add(d, d, 11));
        auto result = h.Eval("choose([1,'s'])", route);
        if (type)
          ExpectNoOverload(result, h);
        else
          ExpectMarker(result, 11, h);
      }
  for (bool type : {false, true})
    for (Route route : {Route::kParsed, Route::kSingle}) {
      Dispatch h(true, type);
      FunctionDescriptor reg(
          "choose", "i", false,
          {h.list(TypeParamType("A")), h.list(TypeParamType("A"))});
      FunctionDescriptor impl("choose", "i", false,
                              {h.list(DynType()), h.list(StringType())});
      ASSERT_OK(h.Add(reg, impl, 11));
      for (absl::string_view expression :
           {"choose([1],[])", "choose([],[1])", "choose(['s'],[])",
            "choose([],[])"}) {
        auto result = h.Eval(expression, route);
        if (type &&
            (expression == "choose([1],[])" || expression == "choose([],[1])"))
          ExpectNoOverload(result, h);
        else
          ExpectMarker(result, 11, h);
      }
    }
}

TEST(FunctionDispatchTest, ProviderAmbiguityCountsOnlyAcceptedImplementations) {
  for (bool type : {false, true})
    for (bool modern : {false, true})
      for (bool reverse : {false, true}) {
        Dispatch h(true, type, modern);
        FunctionDescriptor any("choose", false, {Kind::kAny});
        FunctionDescriptor i("choose", "", false, {h.list(IntType())}),
            s("choose", "", false, {h.list(StringType())});
        ASSERT_OK(h.Add(any, reverse ? s : i, reverse ? 22 : 11));
        ASSERT_OK(h.AddImplementation(reverse ? i : s, reverse ? 11 : 22));
        for (absl::string_view expression :
             {"choose([1,'s'])", "choose([])", "choose([1])"}) {
          auto result = h.Eval(expression, Route::kParsed);
          if (!type || expression == "choose([])") {
            EXPECT_THAT(
                result.status(),
                absl_testing::StatusIs(absl::StatusCode::kInvalidArgument));
            EXPECT_EQ(h.calls(), 0);
          } else if (expression == "choose([1,'s'])")
            ExpectNoOverload(result, h);
          else
            ExpectMarker(result, 11, h);
        }
      }
}

TEST(FunctionDispatchTest, OrderedCandidatesAndMetadata) {
  for (bool lazy : {false, true})
    for (bool type : {false, true})
      for (bool reverse : {false, true})
        for (Route route : kRoutes) {
          Dispatch h(lazy, type);
          FunctionDescriptor i("choose", "i", false, {h.list(IntType())}),
              s("choose", "s", false, {h.list(StringType())});
          ASSERT_OK(h.Add(reverse ? s : i, reverse ? s : i, reverse ? 22 : 11));
          ASSERT_OK(h.Add(reverse ? i : s, reverse ? i : s, reverse ? 11 : 22));
          const auto ids = reverse ? std::vector<std::string>{"i", "s"}
                                   : std::vector<std::string>{"s", "i"};
          const int first =
              (route == Route::kParsed || route == Route::kUnknown)
                  ? (reverse ? 22 : 11)
                  : (reverse ? 11 : 22);
          ExpectMarker(h.Eval("choose([])", route, ids), first, h);
        }
  for (bool lazy : {false, true})
    for (bool type : {false, true})
      for (bool receiver : {false, true})
        for (int arity : {0, 1, 2, 3, 5}) {
          if (receiver && arity == 0) continue;
          Dispatch h(lazy, type);
          FunctionDescriptor d("choose", "i", receiver,
                               std::vector<Type>(arity, IntType()));
          ASSERT_OK(h.Add(d, d, 11));
          ASSERT_OK(h.Set(IntValue(1)));
          std::string expression = receiver ? "x.choose(" : "choose(";
          for (int i = receiver ? 1 : 0; i < arity; ++i) {
            if (i > (receiver ? 1 : 0)) expression += ",";
            expression += "x";
          }
          expression += ")";
          ExpectMarker(h.Eval(expression, Route::kSingle), 11, h);
        }
  for (bool lazy : {false, true})
    for (bool type : {false, true}) {
      Dispatch h(lazy, type);
      FunctionDescriptor d("choose", "i", false, {IntType()});
      ASSERT_OK(h.Add(d, d, 11));
      ASSERT_OK(h.Set(IntValue(1)));
      ExpectNoOverload(h.Eval("x.choose()", Route::kSingle), h);
      ExpectNoOverload(h.Eval("choose()", Route::kSingle), h);
    }
  for (bool lazy : {false, true})
    for (bool type : {false, true}) {
      Dispatch h(lazy, type);
      h.SetContainer("ns");
      FunctionDescriptor d("ns.choose", "i", false, {IntType()});
      ASSERT_OK(h.Add(d, d, 11));
      ASSERT_OK(h.Set(IntValue(1)));
      ExpectMarker(h.Eval("choose(x)", Route::kSingle), 11, h);
      ExpectMarker(h.Eval(".ns.choose(x)", Route::kSingle), 11, h);
    }
}

TEST(FunctionDispatchTest, LegacyStructsAndNominalNames) {
  for (bool lazy : {false, true})
    for (bool type : {false, true})
      for (bool legacy_kind : {false, true}) {
        Dispatch h(lazy, type);
        const Value first =
            h.message("cel.expr.conformance.proto2.TestAllTypes");
        const Value second =
            h.message("cel.expr.conformance.proto3.TestAllTypes");
        FunctionDescriptor d(
            "choose", "i", false,
            {legacy_kind ? Type(Kind::kStruct) : first.GetRuntimeType()});
        ASSERT_OK(h.Add(d, d, 11));
        ASSERT_OK(h.Set(first));
        ExpectMarker(h.Eval("choose(x)", Route::kParsed), 11, h);
        ASSERT_OK(h.Set(second));
        auto result = h.Eval("choose(x)", Route::kParsed);
        if (type && !legacy_kind)
          ExpectNoOverload(result, h);
        else
          ExpectMarker(result, 11, h);
      }
}

TEST(FunctionDispatchTest, NativeOpaqueAndOptionalBoundaries) {
  using testing_internal::Box;
  // CelValue cannot represent opaque values. Use native bindings for these
  // values in both static and lazy calls.
  for (bool lazy : {false, true})
    for (bool type : {false, true})
      for (Type actual :
           {Type(IntType()), Type(DynType()), Type(TypeParamType("A"))})
        for (int shape : {0, 1, 2, 3, 4}) {
          Dispatch h(lazy, type, true);
          Box box(OpaqueType(h.arena(), "box", {actual}));
          const Value value = OpaqueValue(&box, h.arena());
          const std::vector<Type> expected = {
              Type(Kind::kOpaque), OpaqueType(h.arena(), "box", {IntType()}),
              OpaqueType(h.arena(), "box", {DynType()}),
              OpaqueType(h.arena(), "box", {TypeParamType("A")}),
              OptionalType(h.arena(), IntType())};
          FunctionDescriptor d("choose", "i", false, {expected[shape]});
          ASSERT_OK(h.Add(d, d, 11));
          ASSERT_OK(h.Set(value));
          auto result = h.Eval("choose(x)", Route::kParsed);
          if (!type || shape == 0 || expected[shape] == value.GetRuntimeType())
            ExpectMarker(result, 11, h);
          else
            ExpectNoOverload(result, h);
        }
  for (bool lazy : {false, true})
    for (bool type : {false, true})
      for (bool legacy_kind : {false, true}) {
        Dispatch h(lazy, type, true);
        FunctionDescriptor d(
            "choose", "i", false,
            {legacy_kind ? Type(Kind::kOpaque)
                         : Type(OptionalType(h.arena(), IntType()))});
        ASSERT_OK(h.Add(d, d, 11));
        for (const Value& value :
             {Value(OptionalValue::None()),
              Value(OptionalValue::Of(IntValue(1), h.arena())),
              Value(OptionalValue::Of(h.string(), h.arena()))}) {
          ASSERT_OK(h.Set(value));
          auto result = h.Eval("choose(x)", Route::kParsed);
          if (type && !legacy_kind && value.GetOptional().HasValue() &&
              value.GetOptional().Value().IsString())
            ExpectNoOverload(result, h);
          else
            ExpectMarker(result, 11, h);
        }
      }
}

TEST(FunctionDispatchTest, ContainedExceptionsRemainContainerElements) {
  using testing_internal::NativeList;
  for (bool lazy : {false, true})
    for (bool type : {false, true})
      for (bool unknown : {false, true})
        for (int shape : {0, 1, 2, 3}) {
          Dispatch h(lazy, type, true);
          const Value exceptional =
              unknown ? h.unknown()
                      : Value(ErrorValue::From(absl::CancelledError("element"),
                                               h.arena()));
          NativeList impl({exceptional});
          ASSERT_OK(h.Set(CustomListValue(&impl, h.arena())));
          std::vector<Type> parameters;
          if (shape == 3) {
            parameters = {TypeParamType("A"), TypeParamType("A")};
            ASSERT_OK(h.Set(h.list({IntValue(1)}), "y"));
          } else {
            const Type element = shape == 0   ? Type(TypeParamType("A"))
                                 : shape == 1 ? Type(DynType())
                                              : Type(IntType());
            parameters = {h.list(element)};
          }
          FunctionDescriptor d("choose", "i", false, parameters);
          ASSERT_OK(h.Add(d, d, 11));
          auto result =
              h.Eval(shape == 3 ? "choose(x,y)" : "choose(x)", Route::kParsed);
          if (type && shape >= 2)
            ExpectNoOverload(result, h);
          else
            ExpectMarker(result, 11, h);
        }
}

TEST(FunctionDispatchTest, ObservationStatusesAndShortCircuitOrder) {
  using testing_internal::FailingList;
  using testing_internal::FailingMap;
  using testing_internal::Failure;
  using testing_internal::Sentinel;
  for (bool lazy : {false, true})
    for (bool is_map : {false, true})
      for (Failure failure : {Failure::kCreate, Failure::kNext, Failure::kEnd})
        for (bool generic : {false, true}) {
          Dispatch h(lazy, true, true);
          int observations = 0;
          FailingList list_impl(failure, &observations);
          FailingMap map_impl(failure, &observations);
          const Value value =
              is_map ? Value(CustomMapValue(&map_impl, h.arena()))
                     : Value(CustomListValue(&list_impl, h.arena()));
          const Type type =
              generic
                  ? Type(TypeParamType("A"))
                  : (is_map ? Type(MapType(h.arena(), StringType(), IntType()))
                            : h.list(IntType()));
          FunctionDescriptor d("choose", "i", false, {type});
          ASSERT_OK(h.Add(d, d, 11));
          ASSERT_OK(h.Set(value));
          auto result = h.Eval("choose(x)", Route::kSingle);
          if (failure == Failure::kEnd)
            ExpectMarker(result, 11, h);
          else {
            EXPECT_EQ(result.status(), Sentinel());
            EXPECT_EQ(result.status().GetPayload("cel.test/observation"),
                      absl::Cord("preserved"));
            EXPECT_EQ(h.calls(), 0);
          }
          EXPECT_GT(observations, 0);
        }
  for (bool lazy : {false, true})
    for (bool type : {false, true}) {
      Dispatch h(lazy, type, true);
      int observations = 0;
      FailingList impl(Failure::kCreate, &observations);
      ASSERT_OK(h.Set(CustomListValue(&impl, h.arena())));
      FunctionDescriptor first("choose", "i", false,
                               {AnyType(), h.list(IntType())});
      FunctionDescriptor later("choose", "s", false,
                               {TypeParamType("A"), h.list(StringType())});
      ASSERT_OK(h.Add(first, first, 11));
      ASSERT_OK(h.Add(later, later, 22));
      ASSERT_OK(h.Set(h.list({}), "y"));
      ExpectMarker(h.Eval("choose(x,y)", Route::kParsed), 11, h);
      EXPECT_EQ(observations, 0);
    }
  for (bool lazy : {false, true})
    for (bool type : {false, true})
      for (int shape : {0, 1, 2}) {
        if (type && shape == 0) continue;
        Dispatch h(lazy, type, true);
        int observations = 0;
        FailingList impl(Failure::kCreate, &observations);
        ASSERT_OK(h.Set(CustomListValue(&impl, h.arena())));
        const Type parameter = shape == 0   ? h.list(IntType())
                               : shape == 1 ? Type(AnyType())
                                            : Type(DynType());
        FunctionDescriptor d("choose", "i", false, {parameter});
        ASSERT_OK(h.Add(d, d, 11));
        ExpectMarker(h.Eval("choose(x)", Route::kParsed), 11, h);
        EXPECT_EQ(observations, 0);
      }
  for (bool lazy : {false, true}) {
    Dispatch h(lazy, true, true);
    int observations = 0;
    FailingList impl(Failure::kCreate, &observations);
    ASSERT_OK(h.Set(CustomListValue(&impl, h.arena())));
    const auto input = absl::CancelledError("strict original error");
    ASSERT_OK(h.Set(ErrorValue::From(input, h.arena()), "e"));
    FunctionDescriptor d("choose", "i", false, {AnyType(), h.list(IntType())});
    ASSERT_OK(h.Add(d, d, 11));
    ASSERT_OK_AND_ASSIGN(auto result, h.Eval("choose(e,x)", Route::kParsed));
    ASSERT_TRUE(result.IsError());
    EXPECT_EQ(result.GetError().NativeValue(), input);
    EXPECT_EQ(observations, 0);
    EXPECT_EQ(h.calls(), 0);
  }
  for (bool lazy : {false, true}) {
    Dispatch h(lazy, true, true);
    int observations = 0;
    FailingMap impl(Failure::kCreate, &observations);
    ASSERT_OK(h.Set(CustomMapValue(&impl, h.arena())));
    FunctionDescriptor d("choose", "i", false, {h.list(IntType())});
    ASSERT_OK(h.Add(d, d, 11));
    ExpectNoOverload(h.Eval("choose(x)", Route::kParsed), h);
    EXPECT_EQ(observations, 0);
  }
}

}  // namespace
}  // namespace cel::runtime_internal
