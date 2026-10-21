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

#include "runtime/internal/function_matching.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <deque>
#include <iostream>
#include <memory>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/cord.h"
#include "common/value_testing.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor.pb.h"
#include "internal/testing.h"
#include "runtime/internal/function_matching_test_util.h"

namespace cel::runtime_internal {
namespace {

using testing_internal::Arena;
using testing_internal::Box;
using testing_internal::FailingList;
using testing_internal::FailingMap;
using testing_internal::Failure;
using testing_internal::MakeOpaque;
using testing_internal::NativeList;
using testing_internal::Sentinel;

class FunctionMatchingTest : public common_internal::ValueTest<> {
 public:
  Function::InvokeContext context() {
    return {descriptor_pool(), message_factory(), arena()};
  }
  Type list(Type t) { return ListType(arena(), t); }
  Type map(Type k, Type v) { return MapType(arena(), k, v); }
  Value string(const char* s = "s") { return StringValue::From(s, arena()); }
  Value native_list(std::initializer_list<Value> values) {
    return CustomListValue(
        Arena::Create<NativeList>(arena(), std::vector<Value>(values)),
        arena());
  }
  Value list(std::initializer_list<Value> values) {
    auto builder = NewListValueBuilder(arena());
    for (const auto& value : values) EXPECT_OK(builder->Add(value));
    auto built = std::move(*builder).Build();
    return built;
  }
  Value map(std::initializer_list<std::pair<Value, Value>> values) {
    auto builder = NewMapValueBuilder(arena());
    for (const auto& entry : values)
      EXPECT_OK(builder->Put(entry.first, entry.second));
    auto built = std::move(*builder).Build();
    return built;
  }
  Type RenameFree(Type type, const char* prefix) {
    if (type.IsTypeParam()) {
      names_.push_back(std::string(prefix) +
                       std::string(type.GetTypeParam().name()));
      return TypeParamType(names_.back());
    }
    if (type.IsList())
      return list(RenameFree(type.GetList().GetElement(), prefix));
    if (type.IsMap())
      return map(RenameFree(type.GetMap().GetKey(), prefix),
                 RenameFree(type.GetMap().GetValue(), prefix));
    if (type.IsOpaque() && type.GetOpaque() && type.IsOptional())
      return OptionalType(
          arena(), RenameFree(type.GetOptional().GetParameter(), prefix));
    // Full non-Optional opaque metadata is constant data, not free variables.
    return type;
  }
  void Check(std::vector<Type> types, std::vector<Value> values, bool expected,
             bool permute = false) {
    std::vector<size_t> order(types.size());
    std::iota(order.begin(), order.end(), 0);
    do {
      std::vector<Type> params;
      std::vector<Value> args;
      for (size_t i : order) {
        params.push_back(types[i]);
        args.push_back(values[i]);
      }
      FunctionDescriptor descriptor("f", "", false, params);
      EXPECT_THAT(MatchFunctionArguments(descriptor, args, context(), true),
                  absl_testing::IsOkAndHolds(expected));
      FunctionDescriptor implementation("f", "", false, params);
      EXPECT_THAT(MatchFunctionArguments(descriptor, implementation, args,
                                         context(), true),
                  absl_testing::IsOkAndHolds(expected));
    } while (permute && std::next_permutation(order.begin(), order.end()));
  }
  void Probe(std::vector<Type> types, std::vector<Value> values,
             bool expected) {
    Check(types, values, expected, true);
    std::vector<size_t> order(types.size());
    std::iota(order.begin(), order.end(), 0);
    do {
      std::vector<Type> parameters;
      std::vector<Value> arguments;
      for (size_t i : order) {
        parameters.push_back(types[i]);
        arguments.push_back(values[i]);
      }
      FunctionDescriptor descriptor("f", "", false, parameters);
      EXPECT_THAT(
          MatchFunctionArguments(descriptor, arguments, context(), false),
          absl_testing::IsOkAndHolds(true));
      EXPECT_THAT(MatchFunctionArguments(descriptor, descriptor, arguments,
                                         context(), false),
                  absl_testing::IsOkAndHolds(true));
    } while (std::next_permutation(order.begin(), order.end()));
  }
  void JointProbe(std::vector<Type> registration,
                  std::vector<Type> implementation, std::vector<Value> values,
                  bool expected) {
    std::vector<size_t> order(values.size());
    std::iota(order.begin(), order.end(), 0);
    do {
      std::vector<Type> r, i;
      std::vector<Value> args;
      for (size_t j : order) {
        r.push_back(registration[j]);
        i.push_back(implementation[j]);
        args.push_back(values[j]);
      }
      for (bool reverse : {false, true}) {
        for (bool rename : {false, true}) {
          auto left = reverse ? i : r, right = reverse ? r : i;
          if (rename) {
            for (auto& type : left) type = RenameFree(type, "left_");
            for (auto& type : right) type = RenameFree(type, "right_");
          }
          FunctionDescriptor rd("f", "", false, left);
          FunctionDescriptor id("f", "", false, right);
          for (bool type_level : {false, true}) {
            EXPECT_THAT(
                MatchFunctionArguments(rd, id, args, context(), type_level),
                absl_testing::IsOkAndHolds(!type_level || expected));
          }
        }
      }
    } while (std::next_permutation(order.begin(), order.end()));
  }

 private:
  std::deque<std::string> names_;
};

TEST_F(FunctionMatchingTest, CustomOpaqueEvidencePreservesItsObservedShape) {
  const Type a = TypeParamType("A");
  const auto ci =
      MakeOpaque(arena(), OpaqueType(arena(), "optional_type", {IntType()}));
  const auto ci2 = MakeOpaque(arena(), ci.GetRuntimeType().GetOpaque(), true);
  const auto cs =
      MakeOpaque(arena(), OpaqueType(arena(), "optional_type", {StringType()}));
  const auto cd =
      MakeOpaque(arena(), OpaqueType(arena(), "optional_type", {DynType()}));
  const auto ca =
      MakeOpaque(arena(), OpaqueType(arena(), "optional_type", {a}));
  const auto bi = MakeOpaque(arena(), OpaqueType(arena(), "box", {IntType()}));
  const auto bs =
      MakeOpaque(arena(), OpaqueType(arena(), "box", {StringType()}));
  const Value oi = OptionalValue::Of(IntValue(1), arena());
  const Value os = OptionalValue::Of(string(), arena());
  const Value none = OptionalValue::None();
  EXPECT_FALSE(ci.IsOptional());
  EXPECT_NE(ci.GetOpaque().GetTypeId(), ci2.GetOpaque().GetTypeId());
  Probe({a, a}, {ci, ci}, true);
  Probe({a, a}, {ci, ci2}, true);
  for (auto pair : std::vector<std::vector<Value>>{{ci, cs},
                                                   {cd, ci},
                                                   {ca, ci},
                                                   {bi, bs},
                                                   {ci, oi},
                                                   {cd, oi},
                                                   {cd, none}}) {
    Probe({a, a}, pair, false);
  }
  Probe({a, a, a}, {cd, oi, os}, false);
  Probe({a, a, a}, {cd, none, oi}, false);
  for (auto pair : std::vector<std::vector<Value>>{{ci, cs}, {cd, oi}}) {
    for (bool reverse : {false, true}) {
      if (reverse) std::reverse(pair.begin(), pair.end());
      Probe({list(a), a}, {native_list({pair[0], pair[1]}), BoolValue(true)},
            true);
    }
  }
  for (bool reverse : {false, true}) {
    Probe({list(a), a},
          {reverse ? native_list({os, oi}) : native_list({oi, os}), cd}, false);
  }
  Probe({list(a), a}, {native_list({ci}), cs}, false);
  for (bool real : {false, true}) {
    const Value right = real ? oi : cs;
    Probe({a, a}, {native_list({ci}), native_list({right})}, false);
    Probe({a, a}, {map({{string("k"), ci}}), map({{string("k"), right}})},
          false);
    Probe({a, a},
          {OptionalValue::Of(ci, arena()), OptionalValue::Of(right, arena())},
          false);
  }
  for (Type declared :
       {Type(OptionalType(arena(), IntType())), ci.GetRuntimeType()}) {
    for (const auto& entry : std::vector<std::pair<Value, bool>>{
             {oi, true}, {none, true}, {ci, false}, {cd, false}}) {
      EXPECT_EQ(entry.first.IsOptional(), entry.second);
      Probe({declared}, {entry.first}, entry.second);
    }
  }
  for (Type declared :
       {Type(Kind::kOpaque), Type(AnyType()), Type(DynType())}) {
    for (Value v : {oi, none, ci, cd}) Probe({declared}, {v}, true);
  }
  Probe({a, a}, {none, oi}, true);
  Probe({a, a}, {oi, os}, false);
  const Value zero =
      MakeOpaque(arena(), OpaqueType(arena(), "optional_type", {}));
  const Value two = MakeOpaque(
      arena(), OpaqueType(arena(), "optional_type", {IntType(), StringType()}));
  Probe({a, a}, {zero, zero}, true);
  Probe({a, a}, {two, two}, true);
  Probe({a, a}, {zero, two}, false);

  struct Identity {
    Type left, right;
    bool equal, overlap;
  };
  const std::vector<Identity> identities = {
      {ci.GetRuntimeType(), OptionalType(arena(), IntType()), true, true},
      {ci.GetRuntimeType(), OptionalType(arena(), StringType()), false, false},
      {cd.GetRuntimeType(), OptionalType(arena(), IntType()), false, true},
      {ca.GetRuntimeType(), OptionalType(arena(), IntType()), false, true},
      {ci.GetRuntimeType(), ci2.GetRuntimeType(), true, true}};
  ASSERT_EQ(identities.size(), 5);
  for (const auto& entry : identities) {
    FunctionDescriptor left("f", "", false, {entry.left}),
        right("f", "", false, {entry.right});
    EXPECT_EQ(entry.left == entry.right, entry.equal);
    EXPECT_EQ(left == right, entry.equal);
    EXPECT_EQ(!(left < right) && !(right < left), entry.equal);
    EXPECT_EQ(left.ShapeMatches(right), entry.overlap);
    EXPECT_EQ(right.ShapeMatches(left), entry.overlap);
  }
}

TEST_F(FunctionMatchingTest, JointOptionalPatternsCannotEraseAtomicEvidence) {
  const Type a = TypeParamType("A"), b = TypeParamType("B");
  const Type optional = OptionalType(arena(), b);
  const Value oi = OptionalValue::Of(IntValue(1), arena()),
              os = OptionalValue::Of(string(), arena());
  const Value none = OptionalValue::None();
  const Value ci =
      MakeOpaque(arena(), OpaqueType(arena(), "optional_type", {IntType()}));
  const Value cd =
      MakeOpaque(arena(), OpaqueType(arena(), "optional_type", {DynType()}));
  struct Control {
    std::vector<Value> values;
    bool joint, generic;
  };
  for (const auto& entry : std::vector<Control>{{{oi, os}, false, false},
                                                {{none, oi}, true, true},
                                                {{ci, ci}, false, true}}) {
    JointProbe({a, a}, {optional, optional}, entry.values, entry.joint);
    // Each single declaration retains its own contract.
    Probe({a, a}, entry.values, entry.generic);
    Probe({optional, optional}, entry.values, entry.joint);
  }
  for (const auto& entry :
       std::vector<std::pair<std::vector<Value>, bool>>{{{oi, ci}, false},
                                                        {{none, ci}, false},
                                                        {{oi, cd}, false},
                                                        {{oi, oi}, true}}) {
    JointProbe({a, a}, {OptionalType(arena(), IntType()), TypeParamType("Y")},
               entry.first, entry.second);
  }
}

TEST_F(FunctionMatchingTest, CompletedObservationDoesNotCacheLaterFailure) {
  using testing_internal::ObservationCounts;
  using testing_internal::ReplayThenFailList;
  for (bool type_level : {false, true}) {
    ObservationCounts counts;
    ReplayThenFailList impl({IntValue(1)}, &counts);
    const Value value = CustomListValue(&impl, arena());
    FunctionDescriptor descriptor("f", "", false, {TypeParamType("A")});
    auto result = MatchFunctionArguments(descriptor, descriptor,
                                         std::vector<Value>{value}, context(),
                                         type_level);
    if (type_level) {
      EXPECT_EQ(result.status(), Sentinel());
      EXPECT_EQ(result.status().GetPayload("cel.test/observation"),
                absl::Cord("preserved"));
    } else
      EXPECT_THAT(result, absl_testing::IsOkAndHolds(true));
    EXPECT_EQ(counts.creations, type_level ? 2 : 0);
    EXPECT_EQ(counts.next, type_level ? 2 : 0);
    EXPECT_EQ(counts.exhausted, type_level ? 1 : 0);
  }
}

TEST_F(FunctionMatchingTest,
       ShallowContradictionsAvoidNecessaryContentFailures) {
  for (bool type_level : {false, true})
    for (bool other_is_list : {false, true}) {
      for (bool reverse : {false, true})
        for (bool joint : {false, true}) {
          int observations = 0;
          FailingList impl(Failure::kCreate, &observations);
          std::vector<Value> args = {
              CustomListValue(&impl, arena()),
              other_is_list ? list({IntValue(1)}) : Value(IntValue(1))};
          if (reverse) std::reverse(args.begin(), args.end());
          FunctionDescriptor descriptor(
              "f", "", false, {TypeParamType("A"), TypeParamType("A")});
          auto result =
              joint ? MatchFunctionArguments(descriptor, descriptor, args,
                                             context(), type_level)
                    : MatchFunctionArguments(descriptor, args, context(),
                                             type_level);
          if (type_level && other_is_list) {
            EXPECT_EQ(result.status(), Sentinel());
            EXPECT_EQ(result.status().GetPayload("cel.test/observation"),
                      absl::Cord("preserved"));
          } else
            EXPECT_THAT(result, absl_testing::IsOkAndHolds(!type_level));
          EXPECT_EQ(observations, type_level && other_is_list ? 1 : 0);
        }
    }
}

TEST_F(FunctionMatchingTest, PartialMapPatternsRequireObservation) {
  for (Type pattern :
       {map(StringType(), DynType()), map(DynType(), IntType())}) {
    for (bool type_level : {false, true}) {
      int observations = 0;
      FailingMap impl(Failure::kCreate, &observations);
      FunctionDescriptor descriptor("f", "", false, {pattern});
      auto result = MatchFunctionArguments(
          descriptor, std::vector<Value>{CustomMapValue(&impl, arena())},
          context(), type_level);
      if (type_level) {
        EXPECT_EQ(result.status(), Sentinel());
        EXPECT_EQ(result.status().GetPayload("cel.test/observation"),
                  absl::Cord("preserved"));
      } else
        EXPECT_THAT(result, absl_testing::IsOkAndHolds(true));
      EXPECT_EQ(observations, type_level ? 1 : 0);
    }
  }
  Probe({list(list(DynType()))}, {list({IntValue(1)})}, false);
  Probe({map(StringType(), DynType())}, {map({{IntValue(1), string()}})},
        false);
  Probe({map(DynType(), IntType())}, {map({{string(), string()}})}, false);
  for (Type key : {Type(AnyType()), Type(DynType())})
    for (Type value : {Type(AnyType()), Type(DynType())}) {
      int observations = 0;
      FailingMap impl(Failure::kCreate, &observations);
      Probe({map(key, value)}, {CustomMapValue(&impl, arena())}, true);
      EXPECT_EQ(observations, 0);
    }
}

TEST_F(FunctionMatchingTest, ShallowPreflightPreservesDeclarationProfiles) {
  const Type a = TypeParamType("A");
  size_t atomic = 0, outside = 0, completed = 0, placeholder = 0, kind_only = 0;
  for (Type leaf : {Type(AnyType()), Type(DynType()), Type(ErrorType()), a}) {
    Type left = OpaqueType(arena(), "box", {leaf}),
         right = OpaqueType(arena(), "box", {IntType()});
    for (int wrapper = 0; wrapper < 4; ++wrapper) {
      const auto wrap = [&](Type t) -> Type {
        if (wrapper == 1) return list(t);
        if (wrapper == 2) return map(StringType(), t);
        if (wrapper == 3) return OptionalType(arena(), t);
        return t;
      };
      for (bool reverse : {false, true}) {
        std::vector<Type> r = {wrap(reverse ? right : left)},
                          i = {wrap(reverse ? left : right)};
        std::vector<Type> q = {Type(static_cast<Kind>(r[0].kind()))};
        EXPECT_TRUE(common_internal::FunctionTypesOverlap(r, i));
        EXPECT_FALSE(common_internal::FunctionTypesMatchPreflight(r, i, q));
        EXPECT_TRUE(common_internal::FunctionTypesMatchPreflight(r, q));
        EXPECT_TRUE(common_internal::FunctionTypesMatchPreflight(i, q));
        ++atomic;
      }
    }
  }
  const std::vector<Type> integer = {IntType()};
  for (Type m : {Type(AnyType()), Type(DynType()), Type(ErrorType()),
                 Type(UnknownType())}) {
    const std::vector<Type> t = {m};
    const bool expected = !m.IsUnknown();
    EXPECT_EQ(common_internal::FunctionTypesMatchPreflight(t, integer),
              expected);
    EXPECT_EQ(common_internal::FunctionTypesMatchPreflight(integer, t),
              expected);
    EXPECT_EQ(common_internal::FunctionTypesMatchPreflight(t, integer, integer),
              expected);
    EXPECT_EQ(common_internal::FunctionTypesMatchPreflight(integer, t, integer),
              expected);
    outside += 4;
  }
  const std::vector<Type> one = {StructType(
                              common_internal::MakeBasicStructType("one"))},
                          two = {StructType(
                              common_internal::MakeBasicStructType("two"))};
  const std::vector<Type> struct_kind = {Type(Kind::kStruct)},
                          opaque_kind = {Type(Kind::kOpaque)};
  EXPECT_TRUE(common_internal::FunctionTypesMatchPreflight(one, struct_kind));
  EXPECT_TRUE(
      common_internal::FunctionTypesMatchPreflight(one, one, struct_kind));
  EXPECT_FALSE(
      common_internal::FunctionTypesMatchPreflight(one, two, struct_kind));
  EXPECT_FALSE(
      common_internal::FunctionTypesMatchPreflight(two, one, struct_kind));
  completed += 4;
  for (Type wrapper : {Type(BoolWrapperType()), Type(IntWrapperType()),
                       Type(UintWrapperType()), Type(DoubleWrapperType()),
                       Type(StringWrapperType()), Type(BytesWrapperType())}) {
    const std::vector<Type> t = {wrapper};
    EXPECT_TRUE(common_internal::FunctionTypesMatchPreflight(t, t));
    EXPECT_FALSE(common_internal::FunctionTypesMatchPreflight(t, integer));
    EXPECT_FALSE(common_internal::FunctionTypesMatchPreflight(integer, t));
    completed += 3;
  }
  const std::vector<Type> box = {OpaqueType(arena(), "box", {a})};
  EXPECT_TRUE(common_internal::FunctionTypesMatchPreflight(opaque_kind, box,
                                                           opaque_kind));
  EXPECT_TRUE(common_internal::FunctionTypesMatchPreflight(box, opaque_kind,
                                                           opaque_kind));
  EXPECT_TRUE(common_internal::FunctionTypesMatchPreflight(struct_kind, one,
                                                           struct_kind));
  EXPECT_TRUE(common_internal::FunctionTypesMatchPreflight(one, struct_kind,
                                                           struct_kind));
  placeholder += 4;
  google::protobuf::DescriptorPool pool;
  google::protobuf::FileDescriptorProto file;
  file.set_name("preflight_enum.proto");
  file.set_syntax("proto3");
  file.set_package("preflight");
  for (const char* name : {"One", "Two"}) {
    auto* e = file.add_enum_type();
    e->set_name(name);
    auto* v = e->add_value();
    v->set_name(std::string(name) + "Zero");
    v->set_number(0);
  }
  ASSERT_NE(pool.BuildFile(file), nullptr);
  const Type f1 = FunctionType(arena(), IntType(), {IntType()});
  const Type f2 =
      FunctionType(arena(), StringType(), {StringType(), BoolType()});
  const Type e1 = EnumType(pool.FindEnumTypeByName("preflight.One"));
  const Type e2 = EnumType(pool.FindEnumTypeByName("preflight.Two"));
  for (auto pair : std::vector<std::pair<Type, Type>>{{f1, f2}, {e1, e2}}) {
    EXPECT_NE(common_internal::CompareFunctionTypes(pair.first, pair.second),
              0);
    const std::vector<Type> l = {pair.first}, r = {pair.second},
                            q = {Type(static_cast<Kind>(pair.first.kind()))};
    EXPECT_TRUE(common_internal::FunctionTypesMatchPreflight(l, r));
    EXPECT_TRUE(common_internal::FunctionTypesMatchPreflight(r, l));
    EXPECT_TRUE(common_internal::FunctionTypesMatchPreflight(l, r, q));
    EXPECT_TRUE(common_internal::FunctionTypesMatchPreflight(r, l, q));
    EXPECT_FALSE(common_internal::FunctionTypesMatchPreflight(l, integer));
    EXPECT_FALSE(common_internal::FunctionTypesMatchPreflight(integer, l));
    kind_only += 6;
  }
  EXPECT_FALSE(common_internal::FunctionTypesMatchPreflight(
      std::vector<Type>{f1}, std::vector<Type>{e1}));
  EXPECT_FALSE(common_internal::FunctionTypesMatchPreflight(
      std::vector<Type>{e1}, std::vector<Type>{f1}));
  kind_only += 2;
  EXPECT_EQ(atomic, 32);
  EXPECT_EQ(outside, 16);
  EXPECT_EQ(completed, 22);
  EXPECT_EQ(placeholder, 4);
  EXPECT_EQ(kind_only, 14);
  EXPECT_FALSE(common_internal::FunctionTypesMatchPreflight(
      std::vector<Type>{a, list(a)},
      std::vector<Type>{list(TypeParamType("B")), TypeParamType("B")},
      std::vector<Type>{Type(Kind::kList), Type(Kind::kList)}));
}

TEST_F(FunctionMatchingTest, AtomicMetadataDoesNotCaptureFreeVariables) {
  const Type a = TypeParamType("A"), box = OpaqueType(arena(), "box", {a});
  const Type empty = TypeParamType(""),
             box_empty = OpaqueType(arena(), "box", {empty});
  const Value value = MakeOpaque(arena(), box.GetOpaque()),
              ve = MakeOpaque(arena(), box_empty.GetOpaque());
  JointProbe({box, a, DynType()}, {box, DynType(), a},
             {value, IntValue(1), string()}, true);
  JointProbe({box, TypeParamType("R"), DynType()},
             {box, DynType(), TypeParamType("I")},
             {value, IntValue(1), string()}, true);
  JointProbe({list(box), a, DynType()}, {list(box), DynType(), a},
             {native_list({value}), IntValue(1), string()}, true);
  JointProbe({map(StringType(), box), a, DynType()},
             {map(StringType(), box), DynType(), a},
             {map({{string("k"), value}}), IntValue(1), string()}, true);
  JointProbe({OptionalType(arena(), box), a, DynType()},
             {OptionalType(arena(), box), DynType(), a},
             {OptionalValue::Of(value, arena()), IntValue(1), string()}, true);
  JointProbe({box_empty, empty, DynType()}, {box_empty, DynType(), empty},
             {ve, IntValue(1), string()}, true);
  const std::vector<Type> r = {box, a, DynType()}, i = {box, list(a), a};
  EXPECT_FALSE(common_internal::FunctionTypesOverlap(r, i));
  JointProbe(r, i, {value, list({IntValue(1)}), IntValue(1)}, true);
}

TEST_F(FunctionMatchingTest, ContentAcceptanceImpliesShallowPreflight) {
  const auto started = std::chrono::steady_clock::now();
  const Type a = TypeParamType("A"), b = TypeParamType("B"),
             empty = TypeParamType("");
  const Type box_a = OpaqueType(arena(), "box", {a}),
             box_b = OpaqueType(arena(), "box", {b});
  const Type box_empty = OpaqueType(arena(), "box", {empty});
  const Value ca = MakeOpaque(arena(), box_a.GetOpaque()),
              cb = MakeOpaque(arena(), box_b.GetOpaque());
  const Value ce = MakeOpaque(arena(), box_empty.GetOpaque());
  const Value error = ErrorValue::From(Sentinel(), arena()),
              unknown = UnknownValue();
  const std::vector<std::vector<Type>> patterns = {
      {a, a},
      {a, b},
      {list(a), a},
      {OptionalType(arena(), a), a},
      {box_a, a},
      {list(box_a), a},
      {a, list(box_a)},
      {DynType(), DynType()},
      {list(IntType()), DynType()},
      {OptionalType(arena(), IntType()), AnyType()},
      {box_a, DynType()},
      {ErrorType(), a},
      {UnknownType(), a},
      {empty, empty},
      {map(StringType(), a), a},
      {box_empty, empty},
      {list(box_empty), empty},
      {OptionalType(arena(), box_a), a}};
  const std::vector<Value> values = {IntValue(1),
                                     string(),
                                     BoolValue(true),
                                     list({}),
                                     list({IntValue(1)}),
                                     list({string()}),
                                     list({IntValue(1), string()}),
                                     native_list({ca}),
                                     map({}),
                                     map({{string("k"), IntValue(1)}}),
                                     map({{string("k"), ca}}),
                                     OptionalValue::None(),
                                     OptionalValue::Of(IntValue(1), arena()),
                                     OptionalValue::Of(ca, arena()),
                                     ca,
                                     cb,
                                     error,
                                     unknown,
                                     ce,
                                     native_list({error}),
                                     native_list({unknown}),
                                     native_list({IntValue(1), error}),
                                     native_list({IntValue(1), unknown})};
  ASSERT_EQ(patterns.size(), 18);
  ASSERT_EQ(values.size(), 23);
  std::vector<FunctionDescriptor> descriptors;
  for (const auto& pattern : patterns) {
    descriptors.emplace_back("f", "", false, pattern, false);
    const auto& d = descriptors.back();
    ASSERT_FALSE(d.is_strict());
    ASSERT_FALSE(d.receiver_style());
    ASSERT_EQ(d.name(), "f");
    ASSERT_EQ(d.types().size(), 2);
  }
  std::array<bool, 18> static_covered{}, joint_covered{};
  std::array<bool, 23> value_covered{};
  size_t static_count = 0, joint_count = 0;
  for (size_t x = 0; x < values.size(); ++x)
    for (size_t y = 0; y < values.size(); ++y) {
      const std::vector<Value> args = {values[x], values[y]};
      const std::vector<Type> query = {Type(ValueKindToKind(values[x].kind())),
                                       Type(ValueKindToKind(values[y].kind()))};
      for (size_t p = 0; p < descriptors.size(); ++p) {
        const auto content = MatchFunctionArgumentsContentForTesting(
            descriptors[p], args, context());
        ASSERT_OK(content);
        ++static_count;
        if (*content) {
          EXPECT_TRUE(
              common_internal::FunctionTypesMatchPreflight(patterns[p], query))
              << p << ":" << x << "," << y;
          static_covered[p] = value_covered[x] = value_covered[y] = true;
        }
        for (size_t q = 0; q < descriptors.size(); ++q) {
          const auto joint = MatchFunctionArgumentsContentForTesting(
              descriptors[p], descriptors[q], args, context());
          ASSERT_OK(joint);
          ++joint_count;
          if (*joint) {
            EXPECT_TRUE(common_internal::FunctionTypesMatchPreflight(
                patterns[p], patterns[q], query))
                << p << "/" << q << ":" << x << "," << y;
            if (p == q) joint_covered[p] = true;
          }
        }
      }
    }
  EXPECT_EQ(static_count, 9522);
  EXPECT_EQ(joint_count, 171396);
  EXPECT_TRUE(std::all_of(static_covered.begin(), static_covered.end(),
                          [](bool b) { return b; }));
  EXPECT_TRUE(std::all_of(value_covered.begin(), value_covered.end(),
                          [](bool b) { return b; }));
  EXPECT_TRUE(std::all_of(joint_covered.begin(), joint_covered.end(),
                          [](bool b) { return b; }));
  std::cout << "Content/preflight combinations: " << static_count << " static, "
            << joint_count << " joint; elapsed ms="
            << std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - started)
                   .count()
            << '\n';
}

TEST_F(FunctionMatchingTest, OccurrencesJoinBeforeSharedVariablesMeet) {
  const Type a = TypeParamType("A");
  const auto empty = list({}), ints = list({IntValue(1)}),
             strings = list({string()}), bools = list({BoolValue(true)}),
             mixed = list({IntValue(1), string()});
  Check({list(a)}, {mixed}, true);
  Check({map(StringType(), a)},
        {map({{string("a"), IntValue(1)}, {string("b"), string()}})}, true);
  Check({list(list(a))}, {list({ints, strings})}, true);
  Check({list(IntType())}, {mixed}, false);
  Check({a, a}, {ints, strings}, false, true);
  Check({a, a, a}, {empty, ints, strings}, false, true);
  Check({a, a, a}, {empty, ints, list({IntValue(2)})}, true, true);
  Check({a, a}, {mixed, bools}, true, true);
  Check({a, a, a}, {mixed, ints, bools}, false, true);
  Check({list(a), a}, {list({ints}), strings}, false, true);
  Check({map(a, a)}, {map({{string(), IntValue(1)}})}, false);
  Check({a, a, a},
        {OptionalValue::None(), OptionalValue::Of(ints, arena()),
         OptionalValue::Of(strings, arena())},
        false, true);
  Check({a, a}, {empty, ints}, true, true);
  Check({a, a}, {list({empty}), list({ints})}, true, true);
  Check({a, a}, {empty, map({})}, false, true);
  Check({OptionalType(arena(), list(a))}, {OptionalValue::Of(mixed, arena())},
        true);
}

TEST_F(FunctionMatchingTest,
       JointSignaturesRetainConstraintsThroughEmptyValues) {
  const Type a = TypeParamType("A"), b = TypeParamType("B");
  const auto empty = list({}), ints = list({IntValue(1)}),
             strings = list({string()}), mixed = list({IntValue(1), string()});
  struct Case {
    std::vector<Type> registration, implementation;
    std::vector<Value> values;
    bool expected;
  };
  const std::vector<Case> cases = {
      {{list(a), list(a)},
       {list(DynType()), list(StringType())},
       {ints, empty},
       false},
      {{list(a), list(a)},
       {list(DynType()), list(StringType())},
       {strings, empty},
       true},
      {{list(a), list(a)},
       {list(DynType()), list(StringType())},
       {empty, empty},
       true},
      {{list(a)}, {list(b)}, {mixed}, true},
      {{list(a)}, {list(IntType())}, {mixed}, false},
      {{a, a},
       {list(DynType()), list(BoolType())},
       {mixed, list({BoolValue(true)})},
       true},
      {{a, list(IntType())}, {list(StringType()), a}, {strings, ints}, true}};
  for (const auto& c : cases) {
    std::vector<size_t> order(c.values.size());
    std::iota(order.begin(), order.end(), 0);
    do {
      std::vector<Type> r, i;
      std::vector<Value> args;
      for (size_t j : order) {
        r.push_back(c.registration[j]);
        i.push_back(c.implementation[j]);
        args.push_back(c.values[j]);
      }
      for (bool reverse : {false, true}) {
        for (bool rename : {false, true}) {
          auto left = reverse ? i : r, right = reverse ? r : i;
          if (rename) {
            for (auto& type : left) type = RenameFree(type, "left_");
            for (auto& type : right) type = RenameFree(type, "right_");
          }
          FunctionDescriptor registration("f", "", false, left);
          FunctionDescriptor implementation("f", "", false, right);
          EXPECT_THAT(MatchFunctionArguments(registration, implementation, args,
                                             context(), true),
                      absl_testing::IsOkAndHolds(c.expected));
        }
      }
    } while (std::next_permutation(order.begin(), order.end()));
  }
}

TEST_F(FunctionMatchingTest, NecessaryObservationsPreserveStatusAndExhaustion) {
  for (Failure failure : {Failure::kCreate, Failure::kNext, Failure::kEnd}) {
    int observations = 0;
    FailingList list_impl(failure, &observations);
    FailingMap map_impl(failure, &observations);
    for (bool is_map : {false, true}) {
      const Value value = is_map ? Value(CustomMapValue(&map_impl, arena()))
                                 : Value(CustomListValue(&list_impl, arena()));
      for (Type type :
           {Type(TypeParamType("A")),
            is_map ? map(StringType(), IntType()) : list(IntType())}) {
        FunctionDescriptor descriptor("f", "", false, {type});
        auto result = MatchFunctionArguments(
            descriptor, std::vector<Value>{value}, context(), true);
        if (failure == Failure::kEnd)
          EXPECT_THAT(result, absl_testing::IsOkAndHolds(true));
        else {
          EXPECT_EQ(result.status(), Sentinel());
          EXPECT_EQ(result.status().GetPayload("cel.test/observation"),
                    absl::Cord("preserved"));
        }
      }
    }
    EXPECT_EQ(observations, 4);
  }
}

TEST_F(FunctionMatchingTest,
       RejectedOrUnconstrainedPatternsDoNotObserveContents) {
  int observations = 0;
  FailingList impl(Failure::kCreate, &observations);
  const Value value = CustomListValue(&impl, arena());
  for (bool type_level : {false, true}) {
    for (Type type : {Type(AnyType()), Type(DynType()), list(DynType())}) {
      FunctionDescriptor descriptor("f", "", false, {type});
      EXPECT_THAT(MatchFunctionArguments(descriptor, std::vector<Value>{value},
                                         context(), type_level),
                  absl_testing::IsOkAndHolds(true));
    }
  }
  FunctionDescriptor strict("f", "", false, {AnyType(), list(IntType())});
  EXPECT_THAT(
      MatchFunctionArguments(
          strict,
          std::vector<Value>{ErrorValue::From(Sentinel(), arena()), value},
          context(), true),
      absl_testing::IsOkAndHolds(false));
  FunctionDescriptor wrong_kind("f", "", false, {map(StringType(), IntType())});
  EXPECT_THAT(MatchFunctionArguments(wrong_kind, std::vector<Value>{value},
                                     context(), true),
              absl_testing::IsOkAndHolds(false));
  EXPECT_EQ(observations, 0);
}

TEST_F(FunctionMatchingTest,
       OpaqueMetadataIsAtomicAndContainedExceptionsAreValues) {
  const Type a = TypeParamType("A");
  for (Type actual : {Type(IntType()), Type(DynType()), a}) {
    Box box(OpaqueType(arena(), "box", {actual}));
    const Value value = OpaqueValue(&box, arena());
    for (Type expected : {Type(IntType()), Type(DynType()), a}) {
      Check({OpaqueType(arena(), "box", {expected})}, {value},
            expected == actual);
    }
    Check({Type(Kind::kOpaque)}, {value}, true);
    Check({OptionalType(arena(), IntType())}, {value}, false);
  }
  for (Value exceptional :
       {Value(ErrorValue::From(Sentinel(), arena())), Value(UnknownValue())}) {
    Check({list(a)}, {native_list({exceptional})}, true);
    Check({list(DynType())}, {native_list({exceptional})}, true);
    Check({list(IntType())}, {native_list({exceptional})}, false);
    Check({a, a}, {native_list({exceptional}), list({IntValue(1)})}, false,
          true);
    Probe({list(a), list(a)}, {native_list({exceptional}), list({IntValue(1)})},
          false);
    Probe({list(a), list(a)},
          {native_list({IntValue(1), exceptional}), list({IntValue(2)})}, true);
    Probe({list(a), list(a)},
          {native_list({exceptional, IntValue(1)}), list({IntValue(2)})}, true);
  }
}

TEST_F(FunctionMatchingTest, ScalarKindsMatchWithoutNewConversions) {
  const std::vector<std::pair<Type, Value>> values = {
      {NullType(), NullValue()},
      {BoolType(), BoolValue(true)},
      {IntType(), IntValue(1)},
      {UintType(), UintValue(1)},
      {DoubleType(), DoubleValue(1.5)},
      {StringType(), string()},
      {BytesType(), BytesValue::From("bytes", arena())},
      {DurationType(), DurationValue(absl::Seconds(1))},
      {TimestampType(), TimestampValue(absl::UnixEpoch())},
      {TypeType(), TypeValue(IntType())}};
  for (const auto& [type, value] : values) {
    Check({type}, {value}, true);
    for (const auto& [other_type, other_value] : values) {
      if (type.kind() != other_type.kind()) Check({type}, {other_value}, false);
    }
  }
  Check({IntWrapperType()}, {IntValue(1)}, false);
  Check({Type(Kind::kEnum)}, {IntValue(1)}, false);
}

}  // namespace
}  // namespace cel::runtime_internal
