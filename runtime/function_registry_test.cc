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

#include "runtime/function_registry.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <numeric>
#include <set>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "common/ast.h"
#include "common/function_descriptor.h"
#include "common/kind.h"
#include "common/type.h"
#include "common/value.h"
#include "google/protobuf/arena.h"
#include "internal/testing.h"
#include "runtime/activation.h"
#include "runtime/function.h"
#include "runtime/function_adapter.h"
#include "runtime/function_overload_reference.h"
#include "runtime/function_provider.h"

namespace cel {

namespace {

using ::absl_testing::StatusIs;
using ::cel::runtime_internal::FunctionProvider;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::SizeIs;
using ::testing::Truly;

class ConstIntFunction : public cel::Function {
 public:
  static cel::FunctionDescriptor MakeDescriptor() {
    return {"ConstFunction", false, {}};
  }

  absl::StatusOr<Value> Invoke(absl::Span<const Value> args,
                               const InvokeContext& context) const override {
    return IntValue(42);
  }
};

template <typename T, typename = void>
struct HasTripleShapeMatches : std::false_type {};
template <typename T>
struct HasTripleShapeMatches<
    T, std::void_t<decltype(std::declval<const T&>().ShapeMatches(
           std::declval<const T&>(), std::declval<const T&>()))>>
    : std::true_type {};
static_assert(!HasTripleShapeMatches<FunctionDescriptor>::value);
static_assert(std::is_same_v<
              decltype(std::declval<const FunctionDescriptor&>().overload_id()),
              absl::string_view>);

TEST(FunctionRegistryTest,
     TypeSpecFactoriesPreserveMetadataAndOwnTheirStrings) {
  for (bool member : {false, true})
    for (bool with_id : {false, true})
      for (bool strict : {false, true})
        for (bool contextual : {false, true})
          for (bool lazy : {false, true})
            for (bool empty : {false, true}) {
              SCOPED_TRACE(testing::Message() << member << with_id << strict
                                              << contextual << lazy << empty);
              std::string name = "factory";
              std::string id = with_id ? "factory_int_list" : "";
              FunctionTypeSpec spec(
                  std::make_unique<TypeSpec>(PrimitiveType::kBool),
                  empty ? std::vector<TypeSpec>{}
                        : std::vector<TypeSpec>{
                              TypeSpec(PrimitiveType::kInt64),
                              TypeSpec(ListTypeSpec(std::make_unique<TypeSpec>(
                                  PrimitiveType::kString)))});
              FunctionDescriptor original =
                  member ? FunctionDescriptor::Member(name, id, spec,
                                                      {strict, contextual})
                         : FunctionDescriptor::Global(name, id, spec,
                                                      {strict, contextual});
              name.assign("changed");
              id.assign("changed");
              FunctionDescriptor copied(original);
              FunctionDescriptor descriptor(std::move(copied));
              copied = original;
              descriptor = std::move(copied);
              EXPECT_EQ(descriptor.name(), "factory");
              EXPECT_EQ(descriptor.overload_id(),
                        with_id ? "factory_int_list" : "");
              EXPECT_EQ(descriptor.has_overload_id(), with_id);
              EXPECT_EQ(descriptor.receiver_style(), member);
              EXPECT_EQ(descriptor.is_strict(), strict);
              EXPECT_EQ(descriptor.is_contextual(), contextual);
              const std::vector<Kind> kinds =
                  empty ? std::vector<Kind>{}
                        : std::vector<Kind>{Kind::kInt, Kind::kList};
              EXPECT_EQ(descriptor.kinds(), kinds);
              EXPECT_EQ(descriptor.types().size(), kinds.size());
              const FunctionDescriptor legacy("factory", member, kinds);
              EXPECT_EQ(descriptor, legacy);
              EXPECT_FALSE(descriptor < legacy);
              EXPECT_FALSE(legacy < descriptor);
              EXPECT_TRUE(descriptor.ShapeMatches(legacy));
              FunctionRegistry registry;
              if (lazy) {
                ASSERT_OK(registry.RegisterLazyFunction(descriptor));
                EXPECT_THAT(registry.FindLazyOverloadsByArity("factory", member,
                                                              kinds.size()),
                            SizeIs(1));
                if (with_id) {
                  auto found = registry.FindLazyOverloadById(
                      "factory", "factory_int_list");
                  ASSERT_TRUE(found.has_value());
                  EXPECT_EQ(found->descriptor, descriptor);
                }
              } else {
                ASSERT_OK(registry.Register(
                    descriptor, std::make_unique<ConstIntFunction>()));
                EXPECT_THAT(registry.FindStaticOverloadsByArity(
                                "factory", member, kinds.size()),
                            SizeIs(1));
                if (with_id) {
                  auto found = registry.FindStaticOverloadById(
                      "factory", "factory_int_list");
                  ASSERT_TRUE(found.has_value());
                  EXPECT_EQ(found->descriptor, descriptor);
                }
              }
            }
}

TEST(FunctionRegistryTest, EmptyParameterSpellingsHaveSharedNominalIdentity) {
  const std::vector<Type> spellings = {Type(Kind::kTypeParam), TypeParamType(),
                                       TypeParamType("")};
  const FunctionDescriptor same("f", "", false, {IntType(), IntType()});
  const FunctionDescriptor mixed("f", "", false, {IntType(), StringType()});
  const FunctionDescriptor reversed("f", "", false, {StringType(), IntType()});
  size_t pairs = 0;
  for (Type left : spellings)
    for (Type right : spellings) {
      EXPECT_EQ(left, right);
      FunctionDescriptor l("f", "", false, {left, left}),
          r("f", "", false, {right, right});
      EXPECT_EQ(l, r);
      EXPECT_FALSE(l < r);
      EXPECT_FALSE(r < l);
      EXPECT_TRUE(l.ShapeMatches(same));
      EXPECT_FALSE(l.ShapeMatches(mixed));
      EXPECT_FALSE(l.ShapeMatches(reversed));
      EXPECT_TRUE(
          FunctionDescriptor("f", "", false, {left})
              .ShapeMatches(FunctionDescriptor("f", "", false, {right})));
      FunctionDescriptor independent_left("f", "", false, {left, IntType()}),
          independent_right("f", "", false, {StringType(), right});
      EXPECT_TRUE(independent_left.ShapeMatches(independent_right));
      EXPECT_TRUE(independent_right.ShapeMatches(independent_left));
      FunctionDescriptor any("f", "", false, {AnyType(), AnyType()});
      EXPECT_TRUE(any.ShapeMatches(same));
      EXPECT_TRUE(any.ShapeMatches(mixed));
      EXPECT_TRUE(any.ShapeMatches(reversed));
      FunctionRegistry registry;
      EXPECT_OK(registry.Register(l, std::make_unique<ConstIntFunction>()));
      EXPECT_OK(registry.Register(mixed, std::make_unique<ConstIntFunction>()));
      ++pairs;
    }
  EXPECT_EQ(pairs, 9);
}

TEST(FunctionRegistryTest, WildcardOverlapRejectionIsAtomicAcrossStorageKinds) {
  size_t cases = 0;
  for (Type wildcard : {Type(DynType()), Type(TypeParamType("A"))})
    for (bool first_lazy : {false, true})
      for (bool second_lazy : {false, true})
        for (bool wildcard_first : {false, true}) {
          FunctionRegistry registry;
          FunctionDescriptor first(
              "f", "", false, {wildcard_first ? wildcard : Type(IntType())});
          FunctionDescriptor second(
              "f", "", false, {wildcard_first ? Type(IntType()) : wildcard});
          auto add = [&](const FunctionDescriptor& d, bool lazy) {
            return lazy ? registry.RegisterLazyFunction(d)
                        : registry.Register(
                              d, std::make_unique<ConstIntFunction>());
          };
          ASSERT_OK(add(first, first_lazy));
          EXPECT_THAT(add(second, second_lazy),
                      StatusIs(absl::StatusCode::kAlreadyExists));
          const auto statics =
              registry.FindStaticOverloadsByArity("f", false, 1);
          const auto lazies = registry.FindLazyOverloadsByArity("f", false, 1);
          ASSERT_EQ(statics.size(), first_lazy ? 0 : 1);
          ASSERT_EQ(lazies.size(), first_lazy ? 1 : 0);
          if (first_lazy)
            EXPECT_EQ(lazies[0].descriptor, first);
          else
            EXPECT_EQ(statics[0].descriptor, first);
          ++cases;
        }
  EXPECT_EQ(cases, 16);
}

TEST(FunctionRegistryTest, InsertAndRetrieveLazyFunction) {
  cel::FunctionDescriptor lazy_function_desc{"LazyFunction", false, {}};
  FunctionRegistry registry;
  Activation activation;
  ASSERT_OK(registry.RegisterLazyFunction(lazy_function_desc));

  const auto descriptors =
      registry.FindLazyOverloads("LazyFunction", false, {});
  EXPECT_THAT(descriptors, SizeIs(1));
}

// Confirm that lazy and static functions share the same descriptor space:
// i.e. you can't insert both a lazy function and a static function for the same
// descriptors.
TEST(FunctionRegistryTest, LazyAndStaticFunctionShareDescriptorSpace) {
  FunctionRegistry registry;
  cel::FunctionDescriptor desc = ConstIntFunction::MakeDescriptor();
  ASSERT_OK(registry.RegisterLazyFunction(desc));

  absl::Status status = registry.Register(ConstIntFunction::MakeDescriptor(),
                                          std::make_unique<ConstIntFunction>());
  EXPECT_FALSE(status.ok());
}

TEST(FunctionRegistryTest, FindStaticOverloadsReturns) {
  FunctionRegistry registry;
  cel::FunctionDescriptor desc = ConstIntFunction::MakeDescriptor();
  ASSERT_OK(registry.Register(desc, std::make_unique<ConstIntFunction>()));

  std::vector<cel::FunctionOverloadReference> overloads =
      registry.FindStaticOverloads(desc.name(), false, {});

  EXPECT_THAT(overloads,
              ElementsAre(Truly(
                  [](const cel::FunctionOverloadReference& overload) -> bool {
                    return overload.descriptor.name() == "ConstFunction";
                  })))
      << "Expected single ConstFunction()";
}

TEST(FunctionRegistryTest, ListFunctions) {
  cel::FunctionDescriptor lazy_function_desc{"LazyFunction", false, {}};
  FunctionRegistry registry;

  ASSERT_OK(registry.RegisterLazyFunction(lazy_function_desc));
  EXPECT_OK(registry.Register(ConstIntFunction::MakeDescriptor(),
                              std::make_unique<ConstIntFunction>()));

  auto registered_functions = registry.ListFunctions();

  EXPECT_THAT(registered_functions, SizeIs(2));
  EXPECT_THAT(registered_functions["LazyFunction"], SizeIs(1));
  EXPECT_THAT(registered_functions["ConstFunction"], SizeIs(1));
}

TEST(FunctionRegistryTest, DefaultLazyProviderNoOverloadFound) {
  FunctionRegistry registry;
  Activation activation;
  cel::FunctionDescriptor lazy_function_desc{"LazyFunction", false, {}};
  EXPECT_OK(registry.RegisterLazyFunction(lazy_function_desc));

  auto providers = registry.FindLazyOverloads("LazyFunction", false, {});
  ASSERT_THAT(providers, SizeIs(1));
  const FunctionProvider& provider = providers[0].provider;
  ASSERT_OK_AND_ASSIGN(
      std::optional<FunctionOverloadReference> func,
      provider.GetFunction({"LazyFunc", false, {cel::Kind::kInt64}},
                           activation));

  EXPECT_EQ(func, std::nullopt);
}

TEST(FunctionRegistryTest, DefaultLazyProviderReturnsImpl) {
  FunctionRegistry registry;
  Activation activation;
  EXPECT_OK(registry.RegisterLazyFunction(
      FunctionDescriptor("LazyFunction", false, {Kind::kAny})));
  EXPECT_TRUE(activation.InsertFunction(
      FunctionDescriptor("LazyFunction", false, {Kind::kInt}),
      UnaryFunctionAdapter<int64_t, int64_t>::WrapFunction(
          [](int64_t x) { return 2 * x; })));
  EXPECT_TRUE(activation.InsertFunction(
      FunctionDescriptor("LazyFunction", false, {Kind::kDouble}),
      UnaryFunctionAdapter<double, double>::WrapFunction(
          [](double x) { return 2 * x; })));

  auto providers =
      registry.FindLazyOverloads("LazyFunction", false, {Kind::kInt});
  ASSERT_THAT(providers, SizeIs(1));
  const FunctionProvider& provider = providers[0].provider;
  ASSERT_OK_AND_ASSIGN(
      std::optional<FunctionOverloadReference> func,
      provider.GetFunction(
          FunctionDescriptor("LazyFunction", false, {Kind::kInt}), activation));

  ASSERT_TRUE(func.has_value());
  EXPECT_EQ(func->descriptor.name(), "LazyFunction");
  EXPECT_EQ(func->descriptor.kinds(), std::vector{cel::Kind::kInt64});
}

TEST(FunctionRegistryTest, DefaultLazyProviderAmbiguousOverload) {
  FunctionRegistry registry;
  Activation activation;
  EXPECT_OK(registry.RegisterLazyFunction(
      FunctionDescriptor("LazyFunction", false, {Kind::kAny})));
  EXPECT_TRUE(activation.InsertFunction(
      FunctionDescriptor("LazyFunction", false, {Kind::kInt}),
      UnaryFunctionAdapter<int64_t, int64_t>::WrapFunction(
          [](int64_t x) { return 2 * x; })));
  EXPECT_TRUE(activation.InsertFunction(
      FunctionDescriptor("LazyFunction", false, {Kind::kDouble}),
      UnaryFunctionAdapter<double, double>::WrapFunction(
          [](double x) { return 2 * x; })));

  auto providers =
      registry.FindLazyOverloads("LazyFunction", false, {Kind::kInt});
  ASSERT_THAT(providers, SizeIs(1));
  const FunctionProvider& provider = providers[0].provider;

  EXPECT_THAT(
      provider.GetFunction(
          FunctionDescriptor("LazyFunction", false, {Kind::kAny}), activation),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Couldn't resolve function")));
}

TEST(FunctionRegistryTest, CanRegisterNonStrictFunction) {
  {
    FunctionRegistry registry;
    cel::FunctionDescriptor descriptor("NonStrictFunction",
                                       /*receiver_style=*/false, {Kind::kAny},
                                       /*is_strict=*/false);
    ASSERT_OK(
        registry.Register(descriptor, std::make_unique<ConstIntFunction>()));
    EXPECT_THAT(
        registry.FindStaticOverloads("NonStrictFunction", false, {Kind::kAny}),
        SizeIs(1));
  }
  {
    FunctionRegistry registry;
    cel::FunctionDescriptor descriptor("NonStrictLazyFunction",
                                       /*receiver_style=*/false, {Kind::kAny},
                                       /*is_strict=*/false);
    EXPECT_OK(registry.RegisterLazyFunction(descriptor));
    EXPECT_THAT(registry.FindLazyOverloads("NonStrictLazyFunction", false,
                                           {Kind::kAny}),
                SizeIs(1));
  }
}

using NonStrictTestCase = std::tuple<bool, bool>;
using NonStrictRegistrationFailTest = testing::TestWithParam<NonStrictTestCase>;

TEST_P(NonStrictRegistrationFailTest,
       IfOtherOverloadExistsRegisteringNonStrictFails) {
  bool existing_function_is_lazy, new_function_is_lazy;
  std::tie(existing_function_is_lazy, new_function_is_lazy) = GetParam();
  FunctionRegistry registry;
  cel::FunctionDescriptor descriptor("OverloadedFunction",
                                     /*receiver_style=*/false, {Kind::kAny},
                                     /*is_strict=*/true);
  if (existing_function_is_lazy) {
    ASSERT_OK(registry.RegisterLazyFunction(descriptor));
  } else {
    ASSERT_OK(
        registry.Register(descriptor, std::make_unique<ConstIntFunction>()));
  }
  cel::FunctionDescriptor new_descriptor("OverloadedFunction",
                                         /*receiver_style=*/false,
                                         {Kind::kAny, Kind::kAny},
                                         /*is_strict=*/false);
  absl::Status status;
  if (new_function_is_lazy) {
    status = registry.RegisterLazyFunction(new_descriptor);
  } else {
    status =
        registry.Register(new_descriptor, std::make_unique<ConstIntFunction>());
  }
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kAlreadyExists,
                               HasSubstr("Only one overload")));
}

TEST_P(NonStrictRegistrationFailTest,
       IfOtherNonStrictExistsRegisteringStrictFails) {
  bool existing_function_is_lazy, new_function_is_lazy;
  std::tie(existing_function_is_lazy, new_function_is_lazy) = GetParam();
  FunctionRegistry registry;
  cel::FunctionDescriptor descriptor("OverloadedFunction",
                                     /*receiver_style=*/false, {Kind::kAny},
                                     /*is_strict=*/false);
  if (existing_function_is_lazy) {
    ASSERT_OK(registry.RegisterLazyFunction(descriptor));
  } else {
    ASSERT_OK(
        registry.Register(descriptor, std::make_unique<ConstIntFunction>()));
  }
  cel::FunctionDescriptor new_descriptor("OverloadedFunction",
                                         /*receiver_style=*/false,
                                         {Kind::kAny, Kind::kAny},
                                         /*is_strict=*/true);
  absl::Status status;
  if (new_function_is_lazy) {
    status = registry.RegisterLazyFunction(new_descriptor);
  } else {
    status =
        registry.Register(new_descriptor, std::make_unique<ConstIntFunction>());
  }
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kAlreadyExists,
                               HasSubstr("Only one overload")));
}

TEST_P(NonStrictRegistrationFailTest, CanRegisterStrictFunctionsWithoutLimit) {
  bool existing_function_is_lazy, new_function_is_lazy;
  std::tie(existing_function_is_lazy, new_function_is_lazy) = GetParam();
  FunctionRegistry registry;
  cel::FunctionDescriptor descriptor("OverloadedFunction",
                                     /*receiver_style=*/false, {Kind::kAny},
                                     /*is_strict=*/true);
  if (existing_function_is_lazy) {
    ASSERT_OK(registry.RegisterLazyFunction(descriptor));
  } else {
    ASSERT_OK(
        registry.Register(descriptor, std::make_unique<ConstIntFunction>()));
  }
  cel::FunctionDescriptor new_descriptor("OverloadedFunction",
                                         /*receiver_style=*/false,
                                         {Kind::kAny, Kind::kAny},
                                         /*is_strict=*/true);
  absl::Status status;
  if (new_function_is_lazy) {
    status = registry.RegisterLazyFunction(new_descriptor);
  } else {
    status =
        registry.Register(new_descriptor, std::make_unique<ConstIntFunction>());
  }
  EXPECT_OK(status);
}

INSTANTIATE_TEST_SUITE_P(NonStrictRegistrationFailTest,
                         NonStrictRegistrationFailTest,
                         testing::Combine(testing::Bool(), testing::Bool()));

// Test type-level overload resolution: distinguish list<int> vs list<string>
TEST(FunctionRegistryTest, TypeLevelOverloadResolution_ListTypes) {
  google::protobuf::Arena arena;
  FunctionRegistry registry;

  // Register fn(list<int>)
  ListType list_int_type(&arena, IntType{});
  cel::FunctionDescriptor desc_list_int("fn", "fn_int", false,
                                        std::vector<Type>{list_int_type}, true);

  class ListIntFunction : public cel::Function {
   public:
    absl::StatusOr<Value> Invoke(absl::Span<const Value> args,
                                 const InvokeContext& context) const override {
      return IntValue(1);  // Return 1 for list<int>
    }
  };

  ASSERT_OK(
      registry.Register(desc_list_int, std::make_unique<ListIntFunction>()));

  // Register fn(list<string>)
  ListType list_string_type(&arena, StringType{});
  cel::FunctionDescriptor desc_list_string(
      "fn", "fn_string", false, std::vector<Type>{list_string_type}, true);

  class ListStringFunction : public cel::Function {
   public:
    absl::StatusOr<Value> Invoke(absl::Span<const Value> args,
                                 const InvokeContext& context) const override {
      return IntValue(2);  // Return 2 for list<string>
    }
  };

  ASSERT_OK(registry.Register(desc_list_string,
                              std::make_unique<ListStringFunction>()));

  // Verify both overloads are registered using Kind-based lookup
  auto overloads = registry.FindStaticOverloads("fn", false, {Kind::kList});
  EXPECT_THAT(overloads, SizeIs(2));

  // Verify type-level lookup can distinguish them
  auto list_int_overloads =
      registry.FindStaticOverloadsByTypes("fn", false, {list_int_type});
  EXPECT_THAT(list_int_overloads, SizeIs(1));
  EXPECT_EQ(list_int_overloads[0].descriptor.types()[0].DebugString(),
            "list<int>");

  auto list_string_overloads =
      registry.FindStaticOverloadsByTypes("fn", false, {list_string_type});
  EXPECT_THAT(list_string_overloads, SizeIs(1));
  EXPECT_EQ(list_string_overloads[0].descriptor.types()[0].DebugString(),
            "list<string>");
}

// Test type-level overload resolution: map<string, int> vs map<string, string>
TEST(FunctionRegistryTest, TypeLevelOverloadResolution_MapTypes) {
  google::protobuf::Arena arena;
  FunctionRegistry registry;

  // Register fn(map<string, int>)
  MapType map_string_int_type(&arena, StringType{}, IntType{});
  cel::FunctionDescriptor desc_map_int(
      "fn", "fn_map_int", false, std::vector<Type>{map_string_int_type}, true);

  class MapIntFunction : public cel::Function {
   public:
    absl::StatusOr<Value> Invoke(absl::Span<const Value> args,
                                 const InvokeContext& context) const override {
      return IntValue(1);
    }
  };

  ASSERT_OK(
      registry.Register(desc_map_int, std::make_unique<MapIntFunction>()));

  // Register fn(map<string, string>)
  MapType map_string_string_type(&arena, StringType{}, StringType{});
  cel::FunctionDescriptor desc_map_string(
      "fn", "fn_map_string", false, std::vector<Type>{map_string_string_type},
      true);

  class MapStringFunction : public cel::Function {
   public:
    absl::StatusOr<Value> Invoke(absl::Span<const Value> args,
                                 const InvokeContext& context) const override {
      return IntValue(2);
    }
  };

  ASSERT_OK(registry.Register(desc_map_string,
                              std::make_unique<MapStringFunction>()));

  // Verify both are registered
  auto overloads = registry.FindStaticOverloads("fn", false, {Kind::kMap});
  EXPECT_THAT(overloads, SizeIs(2));

  // Verify type-level lookup
  auto map_int_overloads =
      registry.FindStaticOverloadsByTypes("fn", false, {map_string_int_type});
  EXPECT_THAT(map_int_overloads, SizeIs(1));
  EXPECT_EQ(map_int_overloads[0].descriptor.types()[0].DebugString(),
            "map<string, int>");

  auto map_string_overloads = registry.FindStaticOverloadsByTypes(
      "fn", false, {map_string_string_type});
  EXPECT_THAT(map_string_overloads, SizeIs(1));
  EXPECT_EQ(map_string_overloads[0].descriptor.types()[0].DebugString(),
            "map<string, string>");
}

// Test wildcard types: dyn, any, error
TEST(FunctionRegistryTest, TypeLevelOverloadResolution_WildcardTypes) {
  google::protobuf::Arena arena;
  FunctionRegistry registry;

  // Register fn(dyn) - should match anything
  cel::FunctionDescriptor desc_dyn("fn", "fn_dyn", false,
                                   std::vector<Type>{DynType{}}, true);

  class DynFunction : public cel::Function {
   public:
    absl::StatusOr<Value> Invoke(absl::Span<const Value> args,
                                 const InvokeContext& context) const override {
      return IntValue(1);
    }
  };

  ASSERT_OK(registry.Register(desc_dyn, std::make_unique<DynFunction>()));

  // Verify it matches various types
  auto int_overloads =
      registry.FindStaticOverloadsByTypes("fn", false, {IntType{}});
  EXPECT_THAT(int_overloads, SizeIs(1));

  auto string_overloads =
      registry.FindStaticOverloadsByTypes("fn", false, {StringType{}});
  EXPECT_THAT(string_overloads, SizeIs(1));

  ListType list_type(&arena, IntType{});
  auto list_overloads =
      registry.FindStaticOverloadsByTypes("fn", false, {list_type});
  EXPECT_THAT(list_overloads, SizeIs(1));
}

// Test that list<dyn> acts as wildcard for any list
TEST(FunctionRegistryTest, TypeLevelOverloadResolution_ListDynWildcard) {
  google::protobuf::Arena arena;
  FunctionRegistry registry;

  // Register fn(list<dyn>)
  ListType list_dyn_type(&arena, DynType{});
  cel::FunctionDescriptor desc("fn", "fn_list_dyn", false,
                               std::vector<Type>{list_dyn_type}, true);

  class ListDynFunction : public cel::Function {
   public:
    absl::StatusOr<Value> Invoke(absl::Span<const Value> args,
                                 const InvokeContext& context) const override {
      return IntValue(1);
    }
  };

  ASSERT_OK(registry.Register(desc, std::make_unique<ListDynFunction>()));

  // Should match list<int>, list<string>, etc.
  ListType list_int(&arena, IntType{});
  auto list_int_overloads =
      registry.FindStaticOverloadsByTypes("fn", false, {list_int});
  EXPECT_THAT(list_int_overloads, SizeIs(1));

  ListType list_string(&arena, StringType{});
  auto list_string_overloads =
      registry.FindStaticOverloadsByTypes("fn", false, {list_string});
  EXPECT_THAT(list_string_overloads, SizeIs(1));
}

// Test empty struct type acts as wildcard
TEST(FunctionRegistryTest, TypeLevelOverloadResolution_StructWildcard) {
  FunctionRegistry registry;

  // Register fn(struct) - empty MessageType acts as wildcard
  cel::FunctionDescriptor desc("fn", "fn_struct", false,
                               std::vector<Type>{MessageType{}}, true);

  class StructFunction : public cel::Function {
   public:
    absl::StatusOr<Value> Invoke(absl::Span<const Value> args,
                                 const InvokeContext& context) const override {
      return IntValue(1);
    }
  };

  ASSERT_OK(registry.Register(desc, std::make_unique<StructFunction>()));

  // Should match any struct type
  auto overloads = registry.FindStaticOverloads("fn", false, {Kind::kStruct});
  EXPECT_THAT(overloads, SizeIs(1));
}

TEST(FunctionRegistryTest, RepeatedTypeParamDuplicateRegistration) {
  for (absl::string_view parameter_name : {"A", "B"}) {
    SCOPED_TRACE(parameter_name);
    FunctionRegistry registry;
    FunctionDescriptor first("f", "first", false,
                             {TypeParamType("A"), TypeParamType("A")});
    FunctionDescriptor duplicate(
        "f", "duplicate", false,
        {TypeParamType(parameter_name), TypeParamType(parameter_name)});
    EXPECT_TRUE(first.ShapeMatches(duplicate));
    EXPECT_TRUE(duplicate.ShapeMatches(first));
    ASSERT_OK(registry.RegisterLazyFunction(first));
    EXPECT_THAT(registry.RegisterLazyFunction(duplicate),
                StatusIs(absl::StatusCode::kAlreadyExists));
  }
}

TEST(FunctionRegistryTest, TypeParamMatchesLegacyFunctionKind) {
  FunctionRegistry registry;
  FunctionDescriptor generic("f", "generic", false, {TypeParamType("A")});
  FunctionDescriptor legacy("f", false, {Kind::kFunction});
  EXPECT_TRUE(generic.ShapeMatches(legacy));
  EXPECT_TRUE(legacy.ShapeMatches(generic));
  ASSERT_OK(registry.RegisterLazyFunction(generic));
  EXPECT_THAT(registry.RegisterLazyFunction(legacy),
              StatusIs(absl::StatusCode::kAlreadyExists));
}

TEST(FunctionRegistryTest, CyclicGenericSignaturesAreDisjoint) {
  google::protobuf::Arena arena;
  FunctionRegistry registry;
  FunctionDescriptor left(
      "f", "left", false,
      {TypeParamType("A"), ListType(&arena, TypeParamType("A"))});
  FunctionDescriptor right(
      "f", "right", false,
      {ListType(&arena, TypeParamType("B")), TypeParamType("B")});
  EXPECT_FALSE(left.ShapeMatches(right));
  EXPECT_FALSE(right.ShapeMatches(left));
  ASSERT_OK(registry.RegisterLazyFunction(left));
  EXPECT_OK(registry.RegisterLazyFunction(right));
}

// Each descriptor's type parameters have their own scope. These signatures
// overlap at f(string, int), even though both authors named their parameter A.
TEST(FunctionRegistryTest, DistinctTypeParamScopesRejectOverlappingSignatures) {
  FunctionRegistry registry;
  FunctionDescriptor left("f", "left", false, {TypeParamType("A"), IntType()});
  FunctionDescriptor right("f", "right", false,
                           {StringType(), TypeParamType("A")});
  EXPECT_TRUE(left.ShapeMatches(right));
  EXPECT_TRUE(right.ShapeMatches(left));
  ASSERT_OK(registry.RegisterLazyFunction(left));
  EXPECT_THAT(registry.RegisterLazyFunction(right),
              StatusIs(absl::StatusCode::kAlreadyExists));
}

// Reject duplicate IDs rather than silently redirecting existing ID lookups
// to an overload that accepts a different kind.
TEST(FunctionRegistryTest, ActivationRejectsOverloadIdCollision) {
  Activation activation;
  FunctionDescriptor first("f", "shared_id", false, {IntType()});
  FunctionDescriptor second("f", "shared_id", false, {DoubleType()});
  ASSERT_TRUE(
      activation.InsertFunction(first, std::make_unique<ConstIntFunction>()));
  EXPECT_FALSE(
      activation.InsertFunction(second, std::make_unique<ConstIntFunction>()));
  EXPECT_THAT(activation.FindFunctionOverloads("f"), SizeIs(1));
  auto selected = activation.FindFunctionOverloadById("f", "shared_id");
  ASSERT_TRUE(selected.has_value());
  EXPECT_EQ(selected->descriptor.types()[0], IntType());
}

TEST(FunctionRegistryTest, DescriptorComparisonHandlesLegacyPlaceholders) {
  google::protobuf::Arena arena;
  for (Kind kind :
       {Kind::kFunction, Kind::kOpaque, Kind::kStruct, Kind::kEnum}) {
    SCOPED_TRACE(static_cast<int>(kind));
    const Type empty(kind);
    for (const Type& type : {empty, Type(ListType(&arena, empty)),
                             Type(MapType(&arena, StringType(), empty)),
                             Type(OpaqueType(&arena, "box", {empty})),
                             Type(FunctionType(&arena, empty, {empty}))}) {
      FunctionDescriptor left("f", "left", false, {type});
      FunctionDescriptor right("f", "right", false, {type});
      EXPECT_TRUE(left == right);
      EXPECT_FALSE(left < right);
      EXPECT_FALSE(right < left);
    }
  }
}

TEST(FunctionRegistryTest, DescriptorComparisonIgnoresTypeTypeParameters) {
  google::protobuf::Arena arena;
  for (const Type& first :
       {Type(TypeType()), Type(TypeType(&arena, IntType()))}) {
    const Type second = TypeType(&arena, StringType());
    FunctionDescriptor a("f", "a", false, {first, IntType()});
    FunctionDescriptor b("f", "b", false, {second, StringType()});
    EXPECT_TRUE(a < b);
    EXPECT_FALSE(b < a);
    FunctionDescriptor equal("f", "other", false, {second, IntType()});
    EXPECT_TRUE(a == equal);
    EXPECT_FALSE(a < equal);
    EXPECT_FALSE(equal < a);
  }
}

TEST(FunctionRegistryTest, DescriptorOrderingDistinguishesNestedTypes) {
  google::protobuf::Arena arena;
  const Type ints = OpaqueType(&arena, "box", {ListType(&arena, IntType())});
  const Type strings =
      OpaqueType(&arena, "box", {ListType(&arena, StringType())});
  ASSERT_EQ(ints.DebugString(), strings.DebugString());
  FunctionDescriptor a("f", "a", false, {ints, IntType()});
  FunctionDescriptor b("f", "b", false, {strings, IntType()});
  FunctionDescriptor c("f", "c", false, {ints, StringType()});
  EXPECT_FALSE(a == b);
  EXPECT_NE(a < b, b < a);
  EXPECT_NE(b < c, c < b);
  EXPECT_TRUE(a < c);
  EXPECT_THAT(std::set<FunctionDescriptor>({a, b, c}), SizeIs(3));
}

TEST(FunctionRegistryTest, DescriptorIdentityIgnoresAllocationAndOptions) {
  google::protobuf::Arena first_arena, second_arena;
  auto make_type = [](google::protobuf::Arena* arena) -> Type {
    return ListType(
        arena,
        OpaqueType(arena, "box",
                   {MapType(arena, StringType(), ListType(arena, IntType()))}));
  };
  FunctionDescriptor first("f", "first", false, {make_type(&first_arena)},
                           FunctionDescriptorOptions{true, false});
  FunctionDescriptor second("f", "second", false, {make_type(&second_arena)},
                            FunctionDescriptorOptions{false, true});
  EXPECT_TRUE(first == second);
  EXPECT_FALSE(first < second);
  EXPECT_FALSE(second < first);
  EXPECT_THAT(std::set<FunctionDescriptor>({first, second}), SizeIs(1));
}

TEST(FunctionRegistryTest, DescriptorOrderingIsStrictWeakAndConsistent) {
  google::protobuf::Arena arena;
  std::vector<Type> types;
  for (Kind kind :
       {Kind::kNull,          Kind::kBool,          Kind::kInt,
        Kind::kUint,          Kind::kDouble,        Kind::kString,
        Kind::kBytes,         Kind::kStruct,        Kind::kDuration,
        Kind::kTimestamp,     Kind::kList,          Kind::kMap,
        Kind::kUnknown,       Kind::kType,          Kind::kError,
        Kind::kAny,           Kind::kDyn,           Kind::kOpaque,
        Kind::kBoolWrapper,   Kind::kIntWrapper,    Kind::kUintWrapper,
        Kind::kDoubleWrapper, Kind::kStringWrapper, Kind::kBytesWrapper,
        Kind::kTypeParam,     Kind::kFunction,      Kind::kEnum}) {
    types.emplace_back(kind);
  }
  types.insert(types.end(),
               {TypeType(&arena, IntType()), TypeType(&arena, StringType()),
                ListType(&arena, TypeType(&arena, IntType())),
                ListType(&arena, TypeType(&arena, StringType())),
                OpaqueType(&arena, "box", {ListType(&arena, IntType())}),
                OpaqueType(&arena, "box", {ListType(&arena, StringType())}),
                FunctionType(&arena, IntType(), {StringType()}),
                FunctionType(&arena, StringType(), {IntType()}),
                TypeParamType("A"), TypeParamType("B")});
  std::vector<FunctionDescriptor> descriptors;
  for (const auto& type : types) {
    descriptors.emplace_back("f", "", false, std::vector<Type>{type});
  }
  descriptors.emplace_back("g", "", false, std::vector<Type>{IntType()});
  descriptors.emplace_back("f", "", true, std::vector<Type>{IntType()});
  descriptors.emplace_back("f", "", false,
                           std::vector<Type>{IntType(), StringType()});
  for (size_t i = 0; i < descriptors.size(); ++i) {
    SCOPED_TRACE(i);
    const auto& a = descriptors[i];
    EXPECT_FALSE(a < a);
    for (size_t j = 0; j < descriptors.size(); ++j) {
      SCOPED_TRACE(j);
      const auto& b = descriptors[j];
      EXPECT_EQ(a == b, !(a < b) && !(b < a));
      if (a < b) {
        EXPECT_FALSE(b < a);
      }
      for (const auto& c : descriptors) {
        if (a < b && b < c) {
          EXPECT_TRUE(a < c);
        }
        if (!(a < b) && !(b < a) && !(b < c) && !(c < b)) {
          EXPECT_FALSE(a < c);
          EXPECT_FALSE(c < a);
        }
      }
    }
  }
}

TEST(FunctionRegistryTest,
     LegacyPairwiseAndFilteredMetadataHaveSeparateContracts) {
  // Each pair overlaps, but no single binding can satisfy all three.
  FunctionDescriptor registration("f", "", false, {DynType(), StringType()});
  FunctionDescriptor query("f", "", false, {IntType(), DynType()});
  FunctionDescriptor implementation("f", "", false,
                                    {TypeParamType("A"), TypeParamType("A")});
  ASSERT_TRUE(registration.ShapeMatches(query));
  ASSERT_TRUE(registration.ShapeMatches(implementation));
  ASSERT_TRUE(query.ShapeMatches(implementation));
  EXPECT_FALSE(common_internal::FunctionTypesOverlap(
      implementation.types(), query.types(), registration.types()));
  EXPECT_FALSE(common_internal::FunctionTypesOverlap(
      registration.types(), implementation.types(), query.types()));
  EXPECT_FALSE(common_internal::FunctionTypesOverlap(
      query.types(), registration.types(), implementation.types()));
  FunctionRegistry registry;
  Activation activation;
  ASSERT_OK(registry.RegisterLazyFunction(registration));
  ASSERT_TRUE(activation.InsertFunction(implementation,
                                        std::make_unique<ConstIntFunction>()));
  auto providers = registry.FindLazyOverloadsByArity("f", false, 2);
  ASSERT_THAT(providers, SizeIs(1));
  ASSERT_OK_AND_ASSIGN(auto found,
                       providers[0].provider.GetFunction(query, activation));
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(&found->implementation,
            &activation.FindFunctionOverloads("f")[0].implementation);
  int callbacks = 0;
  ASSERT_OK_AND_ASSIGN(
      auto filtered,
      providers[0].provider.GetFunctionMatching(
          query, activation,
          [&](const FunctionOverloadReference&) -> absl::StatusOr<bool> {
            ++callbacks;
            return true;
          }));
  EXPECT_FALSE(filtered.has_value());
  EXPECT_EQ(callbacks, 0);
}

TEST(FunctionRegistryTest, ThreeWayMatchingRefinesWildcards) {
  google::protobuf::Arena arena;
  const Type a = TypeParamType("A");
  const Type list_a = ListType(&arena, a);
  const Type list_dyn = ListType(&arena, DynType());
  const Type list_int = ListType(&arena, IntType());
  const Type list_string = ListType(&arena, StringType());
  using Signature = std::vector<Type>;
  const std::vector<std::array<Signature, 3>> cases{
      {{{DynType(), IntType()}, {StringType(), DynType()}, {a, a}}},
      {{{a, a}, {list_dyn, list_string}, {list_int, list_dyn}}},
      {{{a, a},
        {OpaqueType(), OpaqueType(&arena, "right", {})},
        {OpaqueType(&arena, "left", {}), OpaqueType()}}},
      {{{a, DynType(), a}, {list_a, a, DynType()}, {DynType(), list_a, a}}}};
  for (size_t i = 0; i < cases.size(); ++i) {
    SCOPED_TRACE(i);
    for (bool rename : {false, true}) {
      std::vector<FunctionDescriptor> signatures;
      static constexpr const char* kParameters[] = {"scope0", "scope1",
                                                    "scope2"};
      for (size_t scope = 0; scope < 3; ++scope) {
        Signature types;
        const Type parameter = TypeParamType(kParameters[scope]);
        for (const Type& type : cases[i][scope]) {
          if (rename && type.IsTypeParam())
            types.push_back(parameter);
          else if (rename && type.IsList() &&
                   type.GetList().GetElement().IsTypeParam())
            types.push_back(ListType(&arena, parameter));
          else
            types.push_back(type);
        }
        signatures.emplace_back("f", "", false, std::move(types));
      }
      for (size_t first = 0; first < 3; ++first) {
        for (size_t second = 0; second < 3; ++second) {
          if (first == second) continue;
          const size_t third = 3 - first - second;
          ASSERT_TRUE(signatures[first].ShapeMatches(signatures[second]));
          EXPECT_FALSE(common_internal::FunctionTypesOverlap(
              signatures[first].types(), signatures[second].types(),
              signatures[third].types()));
        }
      }
    }
  }
}

TEST(FunctionRegistryTest, NestedWildcardBindingsAreRefined) {
  google::protobuf::Arena arena;
  FunctionDescriptor generic(
      "f", "", false,
      {TypeParamType("A"), TypeParamType("A"), TypeParamType("A")});
  for (const Type& wildcard :
       {Type(DynType()), Type(AnyType()), Type(ErrorType())}) {
    FunctionDescriptor disjoint(
        "f", "", false,
        {ListType(&arena, wildcard), ListType(&arena, IntType()),
         ListType(&arena, StringType())});
    EXPECT_FALSE(generic.ShapeMatches(disjoint));
    EXPECT_FALSE(disjoint.ShapeMatches(generic));
  }
}

class ListedActivation final : public ActivationInterface {
 public:
  explicit ListedActivation(std::vector<FunctionOverloadReference> overloads)
      : overloads_(std::move(overloads)) {}
  absl::StatusOr<bool> FindVariable(absl::string_view,
                                    const google::protobuf::DescriptorPool*,
                                    google::protobuf::MessageFactory*,
                                    google::protobuf::Arena*,
                                    Value*) const override {
    return false;
  }
  std::vector<FunctionOverloadReference> FindFunctionOverloads(
      absl::string_view) const override {
    return overloads_;
  }
  absl::Span<const AttributePattern> GetUnknownAttributes() const override {
    return {};
  }
  absl::Span<const AttributePattern> GetMissingAttributes() const override {
    return {};
  }

 private:
  std::vector<FunctionOverloadReference> overloads_;
};

class SingularProvider final : public FunctionProvider {
 public:
  explicit SingularProvider(std::optional<FunctionOverloadReference> result,
                            absl::Status status = absl::OkStatus())
      : result_(std::move(result)), status_(std::move(status)) {}
  absl::StatusOr<std::optional<FunctionOverloadReference>> GetFunction(
      const FunctionDescriptor&, const ActivationInterface&) const override {
    if (!status_.ok()) return status_;
    return result_;
  }

 private:
  std::optional<FunctionOverloadReference> result_;
  absl::Status status_;
};

TEST(FunctionRegistryTest,
     SingularProviderDefaultFiltersWithoutSwallowingStatus) {
  ConstIntFunction implementation;
  FunctionDescriptor descriptor("f", "id", false, {IntType()});
  FunctionOverloadReference reference{descriptor, implementation};
  Activation activation;
  SingularProvider provider(reference);
  int callbacks = 0;
  for (bool accept : {false, true}) {
    ASSERT_OK_AND_ASSIGN(
        auto found,
        provider.GetFunctionMatching(descriptor, activation,
                                     [&](const auto&) -> absl::StatusOr<bool> {
                                       ++callbacks;
                                       return accept;
                                     }));
    EXPECT_EQ(found.has_value(), accept);
    if (found) {
      EXPECT_EQ(&found->implementation, &implementation);
    }
  }
  EXPECT_EQ(callbacks, 2);
  auto sentinel = absl::DataLossError("provider failure");
  sentinel.SetPayload("cel.test/provider", absl::Cord("preserved"));
  EXPECT_EQ(provider
                .GetFunctionMatching(descriptor, activation,
                                     [&](const auto&) -> absl::StatusOr<bool> {
                                       return sentinel;
                                     })
                .status(),
            sentinel);
  SingularProvider failure(std::nullopt, sentinel), empty(std::nullopt);
  EXPECT_EQ(failure
                .GetFunctionMatching(descriptor, activation,
                                     [&](const auto&) -> absl::StatusOr<bool> {
                                       ++callbacks;
                                       return true;
                                     })
                .status(),
            sentinel);
  ASSERT_OK_AND_ASSIGN(
      auto absent,
      empty.GetFunctionMatching(descriptor, activation,
                                [&](const auto&) -> absl::StatusOr<bool> {
                                  ++callbacks;
                                  return true;
                                }));
  EXPECT_FALSE(absent);
  EXPECT_EQ(callbacks, 2);
}

TEST(FunctionRegistryTest, DefaultIdScanRejectsDuplicateAndForeignNames) {
  ConstIntFunction implementation;
  FunctionDescriptor a("f", "id", false, {IntType()}), b("f", "id", true, {}),
      foreign("other", "id", false, {});
  ListedActivation duplicate({{a, implementation}, {b, implementation}});
  EXPECT_FALSE(duplicate.FindFunctionOverloadById("f", "id"));
  EXPECT_FALSE(duplicate.FindFunctionOverloadById("f", ""));
  ListedActivation unique({{a, implementation}, {foreign, implementation}});
  ASSERT_TRUE(unique.FindFunctionOverloadById("f", "id"));
  EXPECT_FALSE(unique.FindFunctionOverloadById("missing", "id"));
}

TEST(FunctionRegistryTest,
     IdConflictsAreAtomicAcrossStylesAritiesAndBindingKinds) {
  for (bool first_lazy : {false, true})
    for (bool next_lazy : {false, true}) {
      FunctionRegistry registry;
      FunctionDescriptor original("f", "id", false, {IntType()});
      if (first_lazy)
        ASSERT_OK(registry.RegisterLazyFunction(original));
      else
        ASSERT_OK(
            registry.Register(original, std::make_unique<ConstIntFunction>()));
      for (const auto& conflict :
           {FunctionDescriptor("f", "id", false, {DoubleType()}),
            FunctionDescriptor("f", "id", true, {IntType()}),
            FunctionDescriptor("f", "id", false, {})}) {
        auto status = next_lazy
                          ? registry.RegisterLazyFunction(conflict)
                          : registry.Register(
                                conflict, std::make_unique<ConstIntFunction>());
        EXPECT_THAT(status, StatusIs(absl::StatusCode::kAlreadyExists));
        ASSERT_THAT(registry.ListFunctions().at("f"), SizeIs(1));
        if (first_lazy) {
          auto found = registry.FindLazyOverloadById("f", "id");
          ASSERT_TRUE(found);
          EXPECT_EQ(found->descriptor, original);
        } else {
          auto found = registry.FindStaticOverloadById("f", "id");
          ASSERT_TRUE(found);
          EXPECT_EQ(found->descriptor, original);
        }
      }
      EXPECT_OK(registry.RegisterLazyFunction(
          FunctionDescriptor("other", "id", false, {})));
    }
  Activation activation;
  FunctionDescriptor original("f", "id", false, {IntType()});
  ASSERT_TRUE(activation.InsertFunction(original,
                                        std::make_unique<ConstIntFunction>()));
  auto* stable = &activation.FindFunctionOverloadById("f", "id")->descriptor;
  for (const auto& conflict :
       {FunctionDescriptor("f", "id", false, {DoubleType()}),
        FunctionDescriptor("f", "id", true, {IntType()}),
        FunctionDescriptor("f", "id", false, {})}) {
    EXPECT_FALSE(activation.InsertFunction(
        conflict, std::make_unique<ConstIntFunction>()));
    ASSERT_THAT(activation.FindFunctionOverloads("f"), SizeIs(1));
    EXPECT_EQ(&activation.FindFunctionOverloadById("f", "id")->descriptor,
              stable);
  }
  for (int i = 0; i < 200; ++i) {
    EXPECT_TRUE(activation.InsertFunction(
        FunctionDescriptor("g" + std::to_string(i), "id", false, {}),
        std::make_unique<ConstIntFunction>()));
  }
  Activation moved(std::move(activation));
  Activation assigned;
  assigned = std::move(moved);
  ASSERT_TRUE(assigned.FindFunctionOverloadById("f", "id"));
  EXPECT_EQ(&assigned.FindFunctionOverloadById("f", "id")->descriptor, stable);
  EXPECT_FALSE(assigned.FindFunctionOverloadById("f", ""));
  EXPECT_TRUE(assigned.InsertFunction(
      FunctionDescriptor("empty", "", false, {IntType()}),
      std::make_unique<ConstIntFunction>()));
  EXPECT_TRUE(assigned.InsertFunction(
      FunctionDescriptor("empty", "", false, {StringType()}),
      std::make_unique<ConstIntFunction>()));
}

TEST(FunctionRegistryTest, IdReferencesSurviveRegistryRehashAndMoves) {
  for (bool lazy : {false, true}) {
    FunctionRegistry registry;
    FunctionDescriptor original("f", "id", false, {IntType()});
    const FunctionDescriptor* descriptor;
    const void* implementation;
    if (lazy) {
      ASSERT_OK(registry.RegisterLazyFunction(original));
      auto found = registry.FindLazyOverloadById("f", "id");
      ASSERT_TRUE(found);
      descriptor = &found->descriptor;
      implementation = &found->provider;
    } else {
      ASSERT_OK(
          registry.Register(original, std::make_unique<ConstIntFunction>()));
      auto found = registry.FindStaticOverloadById("f", "id");
      ASSERT_TRUE(found);
      descriptor = &found->descriptor;
      implementation = &found->implementation;
    }
    for (int i = 0; i < 200; ++i) {
      ASSERT_OK(registry.RegisterLazyFunction(
          FunctionDescriptor("g" + std::to_string(i), "id", false, {})));
    }
    FunctionRegistry moved(std::move(registry));
    FunctionRegistry assigned;
    assigned = std::move(moved);
    EXPECT_FALSE(assigned.FindLazyOverloadById("f", ""));
    EXPECT_FALSE(assigned.FindStaticOverloadById("f", ""));
    if (lazy) {
      auto found = assigned.FindLazyOverloadById("f", "id");
      ASSERT_TRUE(found);
      EXPECT_EQ(&found->descriptor, descriptor);
      EXPECT_EQ(&found->provider, implementation);
    } else {
      auto found = assigned.FindStaticOverloadById("f", "id");
      ASSERT_TRUE(found);
      EXPECT_EQ(&found->descriptor, descriptor);
      EXPECT_EQ(&found->implementation, implementation);
    }
  }
}

}  // namespace

}  // namespace cel

namespace cel::common_internal {
namespace {

struct Term {
  std::string head;
  std::vector<Term> children;
  bool operator==(const Term& other) const {
    return head == other.head && children == other.children;
  }
};
using Signature = std::vector<Term>;

class Corpus {
 public:
  Term Next(int depth) {
    static const char* const leaves[] = {
        "int",      "string",     "bool",       "uint",       "double",
        "bytes",    "null",       "duration",   "timestamp",  "type",
        "dyn",      "any",        "error",      "unknown",    "A",
        "B",        "C",          "struct:one", "struct:two", "struct:*",
        "opaque:*", "function:*", "enum:*"};
    const uint32_t choice = depth == 0 ? 0 : Random() % 6;
    if (choice == 0) return {leaves[Random() % 23], {}};
    static const char* const constructors[] = {
        "", "list", "map", "optional", "box:one", "box:two"};
    Term term{constructors[choice], {Next(depth - 1)}};
    if (choice == 2) term.children.push_back(Next(depth - 1));
    return term;
  }

 private:
  uint32_t Random() {
    state_ = 1664525u * state_ + 1013904223u;
    return state_;
  }
  uint32_t state_ = 0x43454c31;
};

bool Variable(const Term& t) { return t.head[0] == '$'; }
bool Contains(const std::string& name, const Term& t) {
  if (t.head == name) return true;
  for (const auto& child : t.children)
    if (Contains(name, child)) return true;
  return false;
}
void Substitute(const std::string& variable, const Term& value, Term& target) {
  if (target.head == variable)
    target = value;
  else
    for (auto& child : target.children) Substitute(variable, value, child);
}
std::string KindOf(const Term& t) {
  if (t.head.rfind("struct:", 0) == 0) return "struct";
  if (t.head.rfind("box:", 0) == 0 || t.head == "optional") return "opaque";
  return t.head;
}

// Independent Robinson substitution oracle: no production graph or matcher
// helper is used to compute an expected result.
class Reference {
 public:
  bool Match(const std::vector<Signature>& signatures) {
    std::vector<Signature> scoped;
    for (size_t i = 0; i < signatures.size(); ++i) {
      scoped.emplace_back();
      for (const auto& term : signatures[i])
        scoped.back().push_back(Build(term, i));
    }
    std::vector<std::pair<Term, Term>> equations;
    for (size_t i = 1; i < scoped.size(); ++i) {
      for (size_t j = 0; j < scoped[0].size(); ++j) {
        equations.emplace_back(scoped[0][j], scoped[i][j]);
      }
    }
    while (!equations.empty()) {
      auto [left, right] = std::move(equations.back());
      equations.pop_back();
      if (left == right) continue;
      if (Variable(right) && !Variable(left)) std::swap(left, right);
      if (Variable(left)) {
        const auto domain = domains_.find(left.head);
        if (domain != domains_.end()) {
          if (Variable(right)) {
            auto other = domains_.find(right.head);
            if (other != domains_.end() && other->second != domain->second)
              return false;
            const std::string kind = domain->second;
            domains_[right.head] = kind;
          } else if (KindOf(right) != domain->second)
            return false;
        }
        if (Contains(left.head, right)) return false;
        for (auto& equation : equations) {
          Substitute(left.head, right, equation.first);
          Substitute(left.head, right, equation.second);
        }
      } else {
        if (left.head != right.head ||
            left.children.size() != right.children.size())
          return false;
        for (size_t i = 0; i < left.children.size(); ++i) {
          equations.emplace_back(left.children[i], right.children[i]);
        }
      }
    }
    return true;
  }

 private:
  Term Build(const Term& term, size_t scope) {
    if (term.head == "A" || term.head == "B" || term.head == "C") {
      return {"$" + std::to_string(scope) + term.head, {}};
    }
    if (term.head == "dyn" || term.head == "any" || term.head == "error" ||
        term.head == "struct:*" || term.head == "opaque:*") {
      Term fresh{"$u" + std::to_string(serial_++), {}};
      if (term.head == "struct:*") domains_[fresh.head] = "struct";
      if (term.head == "opaque:*") domains_[fresh.head] = "opaque";
      return fresh;
    }
    Term result{term.head, {}};
    for (const auto& child : term.children)
      result.children.push_back(Build(child, scope));
    return result;
  }
  size_t serial_ = 0;
  std::unordered_map<std::string, std::string> domains_;
};

Type ActualType(const Term& t, google::protobuf::Arena& arena, int scope,
                bool rename) {
  const auto& h = t.head;
  if (h == "A" || h == "B" || h == "C") {
    static const char* const original[] = {"A", "B", "C"};
    static const char* const renamed[][3] = {
        {"R0A", "R0B", "R0C"}, {"R1A", "R1B", "R1C"}, {"R2A", "R2B", "R2C"}};
    return TypeParamType(rename ? renamed[scope][h[0] - 'A']
                                : original[h[0] - 'A']);
  }
  const auto child = [&](size_t i) {
    return ActualType(t.children[i], arena, scope, rename);
  };
  if (h == "list") return ListType(&arena, child(0));
  if (h == "map") return MapType(&arena, child(0), child(1));
  if (h == "optional") return OptionalType(&arena, child(0));
  if (h == "box:one" || h == "box:two")
    return OpaqueType(&arena, h, {child(0)});
  if (h == "struct:one" || h == "struct:two")
    return StructType(MakeBasicStructType(h));
  if (h == "struct:*") return Type(Kind::kStruct);
  if (h == "opaque:*") return OpaqueType();
  if (h == "function:*") return FunctionType();
  if (h == "enum:*") return EnumType();
  if (h == "int") return IntType();
  if (h == "string") return StringType();
  if (h == "bool") return BoolType();
  if (h == "uint") return UintType();
  if (h == "double") return DoubleType();
  if (h == "bytes") return BytesType();
  if (h == "null") return NullType();
  if (h == "duration") return DurationType();
  if (h == "timestamp") return TimestampType();
  if (h == "type") return TypeType();
  if (h == "dyn") return DynType();
  if (h == "any") return AnyType();
  if (h == "error") return ErrorType();
  if (h == "unknown") return UnknownType();
  ADD_FAILURE() << h;
  return DynType();
}

std::string Json(const Term& t) {
  std::string out = "[\"" + t.head + "\"";
  for (const auto& c : t.children) out += "," + Json(c);
  return out + "]";
}

TEST(FunctionTypesTest, ScopedBindingsRetainOccursAndPlaceholderRefinement) {
  google::protobuf::Arena arena;
  const Type a = TypeParamType("A"), b = TypeParamType("B");
  EXPECT_TRUE(
      FunctionTypesOverlap(std::vector<Type>{a, a}, std::vector<Type>{a, a}));
  EXPECT_FALSE(FunctionTypesOverlap(std::vector<Type>{a, ListType(&arena, a)},
                                    std::vector<Type>{ListType(&arena, b), b}));
  const Type left = OpaqueType(&arena, "left", {}),
             right = OpaqueType(&arena, "right", {});
  const std::vector<Type> generic = {a, a}, first = {OpaqueType(), right},
                          second = {left, OpaqueType()};
  EXPECT_TRUE(FunctionTypesOverlap(generic, first));
  EXPECT_TRUE(FunctionTypesOverlap(generic, second));
  EXPECT_TRUE(FunctionTypesOverlap(first, second));
  EXPECT_FALSE(FunctionTypesOverlap(generic, first, second));
  EXPECT_TRUE(FunctionTypesOverlap(generic, first,
                                   std::vector<Type>{right, OpaqueType()}));
}

TEST(FunctionTypesTest, BoundedSignaturesAgreeWithIndependentReference) {
  Corpus corpus;
  size_t checks = 0, positive = 0;
  std::string serialized = "[";
  for (int count : {2, 3}) {
    for (int index = 0; index < 2048; ++index) {
      SCOPED_TRACE(count);
      SCOPED_TRACE(index);
      std::vector<Signature> signatures(count);
      for (auto& signature : signatures) {
        for (int j = 0; j < index % 4; ++j) signature.push_back(corpus.Next(3));
      }
      const bool expected = Reference().Match(signatures);
      positive += expected;
      if (serialized.size() != 1) serialized += ",";
      serialized += std::string("{\"expected\":") +
                    (expected ? "true" : "false") + ",\"signatures\":[";
      for (int i = 0; i < count; ++i) {
        if (i) serialized += ",";
        serialized += "[";
        for (size_t j = 0; j < signatures[i].size(); ++j) {
          if (j) serialized += ",";
          serialized += Json(signatures[i][j]);
        }
        serialized += "]";
      }
      serialized += "]}";
      for (bool rename : {false, true}) {
        google::protobuf::Arena arena;
        std::vector<FunctionDescriptor> descriptors;
        for (int i = 0; i < count; ++i) {
          std::vector<Type> types;
          for (const auto& term : signatures[i])
            types.push_back(ActualType(term, arena, i, rename));
          descriptors.emplace_back("f", "", false, std::move(types));
        }
        std::vector<int> order(count);
        std::iota(order.begin(), order.end(), 0);
        do {
          const bool actual =
              count == 2
                  ? descriptors[order[0]].ShapeMatches(descriptors[order[1]])
                  : FunctionTypesOverlap(descriptors[order[0]].types(),
                                         descriptors[order[1]].types(),
                                         descriptors[order[2]].types());
          ASSERT_EQ(actual, expected) << "alpha rename=" << rename;
          ++checks;
        } while (std::next_permutation(order.begin(), order.end()));
      }
    }
  }
  serialized += "]";
  EXPECT_EQ(positive, 1112);
  EXPECT_EQ(checks, 32768);
  EXPECT_EQ(serialized.size(), 816926);
  if (const char* outputs = std::getenv("TEST_UNDECLARED_OUTPUTS_DIR")) {
    std::ofstream(std::string(outputs) + "/signature_corpus.json")
        << serialized;
  }
}

}  // namespace
}  // namespace cel::common_internal
