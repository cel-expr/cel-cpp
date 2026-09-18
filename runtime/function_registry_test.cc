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

#include <cstdint>
#include <memory>
#include <tuple>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
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

}  // namespace

}  // namespace cel
