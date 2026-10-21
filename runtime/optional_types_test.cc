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

#include "runtime/optional_types.h"

#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "cel/expr/syntax.pb.h"
#include "common/function_descriptor.h"
#include "common/kind.h"
#include "common/standard_definitions.h"
#include "common/type.h"
#include "common/value.h"
#include "common/value_testing.h"
#include "extensions/protobuf/runtime_adapter.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/message.h"
#include "internal/testing.h"
#include "internal/testing_descriptor_pool.h"
#include "parser/options.h"
#include "parser/parser.h"
#include "runtime/activation.h"
#include "runtime/function.h"
#include "runtime/function_overload_reference.h"
#include "runtime/function_registry.h"
#include "runtime/internal/function_matching.h"
#include "runtime/internal/function_matching_test_util.h"
#include "runtime/reference_resolver.h"
#include "runtime/runtime.h"
#include "runtime/runtime_builder.h"
#include "runtime/runtime_options.h"
#include "runtime/standard_runtime_builder_factory.h"

namespace cel::extensions {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::cel::expr::ParsedExpr;
using ::cel::extensions::ProtobufRuntimeAdapter;
using ::cel::test::BoolValueIs;
using ::cel::test::IntValueIs;
using ::cel::test::OptionalValueIs;
using ::cel::test::OptionalValueIsEmpty;
using ::google::api::expr::parser::Parse;
using ::google::api::expr::parser::ParserOptions;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::TestWithParam;

MATCHER_P(MatchesOptionalReceiver1, name, "") {
  const FunctionDescriptor& descriptor = arg.descriptor;

  std::vector<Kind> kinds{Kind::kOpaque};
  return descriptor.name() == name && descriptor.receiver_style() == true &&
         descriptor.kinds() == kinds;
}

MATCHER_P2(MatchesOptionalReceiver2, name, kind, "") {
  const FunctionDescriptor& descriptor = arg.descriptor;

  std::vector<Kind> kinds{Kind::kOpaque, kind};
  return descriptor.name() == name && descriptor.receiver_style() == true &&
         descriptor.kinds() == kinds;
}

MATCHER_P2(MatchesOptionalSelect, kind1, kind2, "") {
  const FunctionDescriptor& descriptor = arg.descriptor;

  std::vector<Kind> kinds{kind1, kind2};
  return descriptor.name() == "_?._" && descriptor.receiver_style() == false &&
         descriptor.kinds() == kinds;
}

MATCHER_P2(MatchesOptionalIndex, kind1, kind2, "") {
  const FunctionDescriptor& descriptor = arg.descriptor;

  std::vector<Kind> kinds{kind1, kind2};
  return descriptor.name() == "_[?_]" && descriptor.receiver_style() == false &&
         descriptor.kinds() == kinds;
}

TEST(EnableOptionalTypes, HeterogeneousEqualityRequired) {
  ASSERT_OK_AND_ASSIGN(
      auto builder,
      CreateStandardRuntimeBuilder(
          internal::GetTestingDescriptorPool(),
          RuntimeOptions{.enable_qualified_type_identifiers = true,
                         .enable_heterogeneous_equality = false}));
  EXPECT_THAT(EnableOptionalTypes(builder),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(EnableOptionalTypes, QualifiedTypeIdentifiersRequired) {
  ASSERT_OK_AND_ASSIGN(
      auto builder,
      CreateStandardRuntimeBuilder(
          internal::GetTestingDescriptorPool(),
          RuntimeOptions{.enable_qualified_type_identifiers = false,
                         .enable_heterogeneous_equality = true}));
  EXPECT_THAT(EnableOptionalTypes(builder),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(EnableOptionalTypes, PreconditionsSatisfied) {
  ASSERT_OK_AND_ASSIGN(
      auto builder,
      CreateStandardRuntimeBuilder(
          internal::GetTestingDescriptorPool(),
          RuntimeOptions{.enable_qualified_type_identifiers = true,
                         .enable_heterogeneous_equality = true}));
  EXPECT_THAT(EnableOptionalTypes(builder), IsOk());
}

TEST(EnableOptionalTypes, Functions) {
  ASSERT_OK_AND_ASSIGN(
      auto builder,
      CreateStandardRuntimeBuilder(
          internal::GetTestingDescriptorPool(),
          RuntimeOptions{.enable_qualified_type_identifiers = true,
                         .enable_heterogeneous_equality = true}));
  ASSERT_THAT(EnableOptionalTypes(builder), IsOk());

  EXPECT_THAT(builder.function_registry().FindStaticOverloads("hasValue", true,
                                                              {Kind::kOpaque}),
              ElementsAre(MatchesOptionalReceiver1("hasValue")));

  EXPECT_THAT(builder.function_registry().FindStaticOverloads("value", true,
                                                              {Kind::kOpaque}),
              ElementsAre(MatchesOptionalReceiver1("value")));

  EXPECT_THAT(builder.function_registry().FindStaticOverloads(
                  "_?._", false, {Kind::kStruct, Kind::kString}),
              ElementsAre(MatchesOptionalSelect(Kind::kStruct, Kind::kString)));
  EXPECT_THAT(builder.function_registry().FindStaticOverloads(
                  "_?._", false, {Kind::kMap, Kind::kString}),
              ElementsAre(MatchesOptionalSelect(Kind::kMap, Kind::kString)));
  EXPECT_THAT(builder.function_registry().FindStaticOverloads(
                  "_?._", false, {Kind::kOpaque, Kind::kString}),
              ElementsAre(MatchesOptionalSelect(Kind::kOpaque, Kind::kString)));

  EXPECT_THAT(builder.function_registry().FindStaticOverloads(
                  "_[?_]", false, {Kind::kMap, Kind::kAny}),
              ElementsAre(MatchesOptionalIndex(Kind::kMap, Kind::kAny)));
  EXPECT_THAT(builder.function_registry().FindStaticOverloads(
                  "_[?_]", false, {Kind::kList, Kind::kInt}),
              ElementsAre(MatchesOptionalIndex(Kind::kList, Kind::kInt)));
  EXPECT_THAT(builder.function_registry().FindStaticOverloads(
                  "_[?_]", false, {Kind::kOpaque, Kind::kAny}),
              ElementsAre(MatchesOptionalIndex(Kind::kOpaque, Kind::kAny)));
}

TEST(OptionalTypesTest, OrPreservesPresentReceiver) {
  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(
      auto builder,
      CreateStandardRuntimeBuilder(
          internal::GetTestingDescriptorPool(),
          RuntimeOptions{.enable_qualified_type_identifiers = true}));
  ASSERT_OK(EnableOptionalTypes(builder));
  auto overload = builder.function_registry().FindStaticOverloadById(
      "or", "optional_or_optional");
  ASSERT_TRUE(overload.has_value());
  Function::InvokeContext context(
      internal::GetTestingDescriptorPool(),
      google::protobuf::MessageFactory::generated_factory(), &arena);
  for (const Value& fallback :
       {OptionalValue::None(), OptionalValue::Of(IntValue(2), &arena)}) {
    std::vector<Value> args{OptionalValue::Of(IntValue(1), &arena), fallback};
    ASSERT_OK_AND_ASSIGN(Value result,
                         overload->implementation.Invoke(args, context));
    EXPECT_THAT(result, OptionalValueIs(IntValueIs(1))) << result.DebugString();
  }
}

TEST(OptionalTypesTest, OrPreservesPresentFallback) {
  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(
      auto builder,
      CreateStandardRuntimeBuilder(
          internal::GetTestingDescriptorPool(),
          RuntimeOptions{.enable_qualified_type_identifiers = true}));
  ASSERT_OK(EnableOptionalTypes(builder));
  auto overload = builder.function_registry().FindStaticOverloadById(
      "or", "optional_or_optional");
  ASSERT_TRUE(overload.has_value());
  std::vector<Value> args{OptionalValue::None(),
                          OptionalValue::Of(IntValue(2), &arena)};
  Function::InvokeContext context(
      internal::GetTestingDescriptorPool(),
      google::protobuf::MessageFactory::generated_factory(), &arena);
  ASSERT_OK_AND_ASSIGN(Value result,
                       overload->implementation.Invoke(args, context));
  EXPECT_THAT(result, OptionalValueIs(IntValueIs(2))) << result.DebugString();
}

TEST(OptionalTypesTest, OrRejectsNonOptionalOpaqueFallback) {
  class OtherOpaque final : public OpaqueValueInterface {
   public:
    explicit OtherOpaque(google::protobuf::Arena* arena)
        : type_(arena, "test.other_opaque", {}) {}

    std::string DebugString() const override { return "other_opaque"; }
    absl::string_view GetTypeName() const override { return type_.name(); }
    OpaqueType GetRuntimeType() const override { return type_; }
    absl::Status Equal(const OpaqueValue&,
                       const google::protobuf::DescriptorPool*,
                       google::protobuf::MessageFactory*,
                       google::protobuf::Arena*, Value* result) const override {
      *result = BoolValue(false);
      return absl::OkStatus();
    }
    OpaqueValue Clone(google::protobuf::Arena* arena) const override {
      return OpaqueValue(
          google::protobuf::Arena::Create<OtherOpaque>(arena, arena), arena);
    }
    NativeTypeId GetNativeTypeId() const override {
      return NativeTypeId::For<OtherOpaque>();
    }

   private:
    OpaqueType type_;
  };
  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(
      auto builder,
      CreateStandardRuntimeBuilder(
          internal::GetTestingDescriptorPool(),
          RuntimeOptions{.enable_qualified_type_identifiers = true}));
  ASSERT_OK(EnableOptionalTypes(builder));
  auto overload = builder.function_registry().FindStaticOverloadById(
      "or", "optional_or_optional");
  ASSERT_TRUE(overload.has_value());
  const Value other = OpaqueValue(
      google::protobuf::Arena::Create<OtherOpaque>(&arena, &arena), &arena);
  Function::InvokeContext context(
      internal::GetTestingDescriptorPool(),
      google::protobuf::MessageFactory::generated_factory(), &arena);
  for (const std::vector<Value>& args :
       {std::vector<Value>{OptionalValue::None(), other},
        std::vector<Value>{other, OptionalValue::None()}}) {
    ASSERT_OK_AND_ASSIGN(Value result,
                         overload->implementation.Invoke(args, context));
    ASSERT_TRUE(result.IsError()) << result.DebugString();
    EXPECT_THAT(result.GetError().NativeValue(),
                StatusIs(absl::StatusCode::kUnknown,
                         HasSubstr("No matching overloads")));
  }
  // The fallback is not inspected when the receiver already has a value.
  std::vector<Value> args{OptionalValue::Of(IntValue(1), &arena), other};
  ASSERT_OK_AND_ASSIGN(Value result,
                       overload->implementation.Invoke(args, context));
  EXPECT_THAT(result, OptionalValueIs(IntValueIs(1)));
  args = {OptionalValue::None(), OptionalValue::None()};
  ASSERT_OK_AND_ASSIGN(result, overload->implementation.Invoke(args, context));
  EXPECT_THAT(result, OptionalValueIsEmpty());
  auto or_value = builder.function_registry().FindStaticOverloadById(
      "orValue", "optional_orValue_value");
  ASSERT_TRUE(or_value.has_value());
  args = {other, IntValue(2)};
  ASSERT_OK_AND_ASSIGN(result, or_value->implementation.Invoke(args, context));
  ASSERT_TRUE(result.IsError());
  EXPECT_THAT(
      result.GetError().NativeValue(),
      StatusIs(absl::StatusCode::kUnknown, HasSubstr("No matching overloads")));
}

TEST(OptionalTypesTest, OrValueUnwrapsPresentReceiver) {
  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(
      auto builder,
      CreateStandardRuntimeBuilder(
          internal::GetTestingDescriptorPool(),
          RuntimeOptions{.enable_qualified_type_identifiers = true}));
  ASSERT_OK(EnableOptionalTypes(builder));
  auto overload = builder.function_registry().FindStaticOverloadById(
      "orValue", "optional_orValue_value");
  ASSERT_TRUE(overload.has_value());
  Function::InvokeContext context(
      internal::GetTestingDescriptorPool(),
      google::protobuf::MessageFactory::generated_factory(), &arena);
  for (bool present : {false, true}) {
    std::vector<Value> args{present ? OptionalValue::Of(IntValue(1), &arena)
                                    : OptionalValue::None(),
                            IntValue(2)};
    ASSERT_OK_AND_ASSIGN(Value result,
                         overload->implementation.Invoke(args, context));
    EXPECT_THAT(result, IntValueIs(present ? 1 : 2));
  }
}

TEST(OptionalRuntimeBindingsTest, RetainedConversionAndUnwrapIdsStillInvoke) {
  for (bool type_level : {false, true}) {
    RuntimeOptions options;
    options.enable_qualified_type_identifiers = true;
    options.enable_type_level_overload = type_level;
    ASSERT_OK_AND_ASSIGN(auto builder,
                         CreateStandardRuntimeBuilder(
                             internal::GetTestingDescriptorPool(), options));
    ASSERT_OK(EnableOptionalTypes(builder));
    ASSERT_OK(
        EnableReferenceResolver(builder, ReferenceResolverEnabled::kAlways));
    google::protobuf::Arena arena;
    Function::InvokeContext context(
        internal::GetTestingDescriptorPool(),
        google::protobuf::MessageFactory::generated_factory(), &arena);
    auto list = NewListValueBuilder(&arena);
    ASSERT_OK(list->Add(OptionalValue::Of(IntValue(1), &arena)));
    ASSERT_OK(list->Add(OptionalValue::None()));
    const Value input = std::move(*list).Build();
    const auto check_result = [&](const Value& result, bool scalar) {
      if (scalar) {
        EXPECT_THAT(result, IntValueIs(1));
        return;
      }
      ASSERT_TRUE(result.IsList());
      EXPECT_THAT(result.GetList().Size(), absl_testing::IsOkAndHolds(1));
      Value element;
      ASSERT_OK(result.GetList().Get(0, context.descriptor_pool(),
                                     context.message_factory(), &arena,
                                     &element));
      EXPECT_THAT(element, IntValueIs(1));
    };
    struct Entry {
      absl::string_view name, id;
      bool member, scalar;
      Kind kind;
    };
    const std::vector<Entry> entries = {
        {"int", StandardOverloadIds::kBoolToInt, false, true, Kind::kBool},
        {"optional.unwrap", "optional_unwrap_list", false, false, Kind::kList},
        {"unwrapOpt", "optional_unwrapOpt_list", true, false, Kind::kList}};
    for (const auto& entry : entries) {
      auto found = builder.function_registry().FindStaticOverloadById(
          entry.name, entry.id);
      ASSERT_TRUE(found.has_value());
      EXPECT_EQ(found->descriptor.name(), entry.name);
      EXPECT_EQ(found->descriptor.overload_id(), entry.id);
      EXPECT_FALSE(found->descriptor.overload_id().empty());
      EXPECT_EQ(found->descriptor.receiver_style(), entry.member);
      EXPECT_EQ(found->descriptor.types().size(), 1);
      EXPECT_EQ(found->descriptor.kinds(), std::vector<Kind>{entry.kind});
      ASSERT_OK_AND_ASSIGN(
          auto result,
          found->implementation.Invoke(
              std::vector<Value>{entry.scalar ? Value(BoolValue(true)) : input},
              context));
      check_result(result, entry.scalar);
    }
    ASSERT_OK_AND_ASSIGN(auto runtime, std::move(builder).Build());
    int index = 0;
    for (const char* expression :
         {"int(true)", "optional.unwrap([optional.of(1),optional.none()])",
          "[optional.of(1),optional.none()].unwrapOpt()"}) {
      ASSERT_OK_AND_ASSIGN(
          auto parsed, Parse(expression, "<input>",
                             ParserOptions{.enable_optional_syntax = true}));
      ASSERT_OK_AND_ASSIGN(auto program, ProtobufRuntimeAdapter::CreateProgram(
                                             *runtime, parsed));
      Activation activation;
      ASSERT_OK_AND_ASSIGN(auto result, program->Evaluate(&arena, activation));
      check_result(result, index++ == 0);
    }
  }
}

TEST(OptionalRuntimeBindingsTest,
     OpaqueMatchingDoesNotGrantOptionalPayloadAccess) {
  for (bool type_level : {false, true})
    for (bool dynamic_metadata : {false, true}) {
      RuntimeOptions options;
      options.enable_qualified_type_identifiers = true;
      options.enable_type_level_overload = type_level;
      ASSERT_OK_AND_ASSIGN(auto builder,
                           CreateStandardRuntimeBuilder(
                               internal::GetTestingDescriptorPool(), options));
      ASSERT_OK(EnableOptionalTypes(builder));
      ASSERT_OK(
          EnableReferenceResolver(builder, ReferenceResolverEnabled::kAlways));
      google::protobuf::Arena arena;
      const Value custom = runtime_internal::testing_internal::MakeOpaque(
          &arena,
          OpaqueType(&arena, "optional_type",
                     {dynamic_metadata ? Type(DynType()) : Type(IntType())}));
      const Value fallback = OptionalValue::Of(IntValue(2), &arena);
      Function::InvokeContext context(
          internal::GetTestingDescriptorPool(),
          google::protobuf::MessageFactory::generated_factory(), &arena);
      for (bool unwrap : {false, true}) {
        const absl::string_view name = unwrap ? "orValue" : "or",
                                id = unwrap ? "optional_orValue_value"
                                            : "optional_or_optional";
        auto found =
            builder.function_registry().FindStaticOverloadById(name, id);
        ASSERT_TRUE(found.has_value());
        const std::vector<Value> arguments = {custom, fallback};
        EXPECT_THAT(runtime_internal::MatchFunctionArguments(
                        found->descriptor, arguments, context, type_level),
                    absl_testing::IsOkAndHolds(true));
        ASSERT_OK_AND_ASSIGN(auto result,
                             found->implementation.Invoke(arguments, context));
        ASSERT_TRUE(result.IsError());
        EXPECT_THAT(result.GetError().NativeValue(),
                    StatusIs(absl::StatusCode::kUnknown,
                             HasSubstr("No matching overload")));
      }
      ASSERT_OK_AND_ASSIGN(auto runtime, std::move(builder).Build());
      for (const char* expression : {"x.or(y)", "x.orValue(y)"}) {
        ASSERT_OK_AND_ASSIGN(
            auto parsed, Parse(expression, "<input>",
                               ParserOptions{.enable_optional_syntax = true}));
        ASSERT_OK_AND_ASSIGN(
            auto program,
            ProtobufRuntimeAdapter::CreateProgram(*runtime, parsed));
        Activation activation;
        activation.InsertOrAssignValue("x", custom);
        activation.InsertOrAssignValue("y", fallback);
        ASSERT_OK_AND_ASSIGN(auto result,
                             program->Evaluate(&arena, activation));
        ASSERT_TRUE(result.IsError());
        EXPECT_THAT(result.GetError().NativeValue(),
                    StatusIs(absl::StatusCode::kUnknown,
                             HasSubstr("No matching overload")));
      }
    }
}

struct EvaluateResultTestCase {
  std::string name;
  std::string expression;
  test::ValueMatcher value_matcher;

  template <typename S>
  friend void AbslStringify(S& sink, const EvaluateResultTestCase& tc) {
    sink.Append(tc.name);
  }
};

class OptionalTypesTest
    : public TestWithParam<std::tuple<EvaluateResultTestCase, bool>> {
 public:
  const EvaluateResultTestCase& GetTestCase() {
    return std::get<0>(GetParam());
  }

  bool EnableShortCircuiting() { return std::get<1>(GetParam()); }
};

TEST_P(OptionalTypesTest, Defaults) {
  RuntimeOptions opts;
  opts.enable_qualified_type_identifiers = true;
  opts.short_circuiting = EnableShortCircuiting();
  const EvaluateResultTestCase& test_case = GetTestCase();

  ASSERT_OK_AND_ASSIGN(
      auto builder,
      CreateStandardRuntimeBuilder(internal::GetTestingDescriptorPool(), opts));

  ASSERT_OK(EnableOptionalTypes(builder));
  ASSERT_OK(
      EnableReferenceResolver(builder, ReferenceResolverEnabled::kAlways));

  ASSERT_OK_AND_ASSIGN(auto runtime, std::move(builder).Build());

  ASSERT_OK_AND_ASSIGN(ParsedExpr expr,
                       Parse(test_case.expression, "<input>",
                             ParserOptions{.enable_optional_syntax = true}));

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Program> program,
                       ProtobufRuntimeAdapter::CreateProgram(*runtime, expr));

  google::protobuf::Arena arena;
  Activation activation;

  ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));

  EXPECT_THAT(result, test_case.value_matcher) << test_case.expression;
}

INSTANTIATE_TEST_SUITE_P(
    Basic, OptionalTypesTest,
    testing::Combine(
        testing::ValuesIn(std::vector<EvaluateResultTestCase>{
            {"optional_none_hasValue", "optional.none().hasValue()",
             BoolValueIs(false)},
            {"optional_of_hasValue", "optional.of(0).hasValue()",
             BoolValueIs(true)},
            {"optional_ofNonZeroValue_hasValue",
             "optional.ofNonZeroValue(0).hasValue()", BoolValueIs(false)},
            {"optional_or_absent",
             "optional.ofNonZeroValue(0).or(optional.ofNonZeroValue(0))",
             OptionalValueIsEmpty()},
            {"optional_or_present", "optional.of(1).or(optional.none())",
             OptionalValueIs(IntValueIs(1))},
            {"optional_orValue_absent", "optional.ofNonZeroValue(0).orValue(1)",
             IntValueIs(1)},
            {"optional_orValue_present", "optional.of(1).orValue(2)",
             IntValueIs(1)},
            {"list_of_optional", "[optional.of(1)][0].orValue(1)",
             IntValueIs(1)},
            {"list_unwrap_empty", "optional.unwrap([]) == []",
             BoolValueIs(true)},
            {"list_unwrap_empty_optional_none",
             "optional.unwrap([optional.none(), optional.none()]) == []",
             BoolValueIs(true)},
            {"list_unwrap_three_elements",
             "optional.unwrap([optional.of(42), optional.none(), "
             "optional.of(\"a\")]) == [42, \"a\"]",
             BoolValueIs(true)},
            {"list_unwrap_no_none",
             "optional.unwrap([optional.of(42), optional.of(\"a\")]) == [42, "
             "\"a\"]",
             BoolValueIs(true)},
            {"list_unwrapOpt_empty", "[].unwrapOpt() == []", BoolValueIs(true)},
            {"list_unwrapOpt_empty_optional_none",
             "[optional.none(), optional.none()].unwrapOpt() == []",
             BoolValueIs(true)},
            {"list_unwrapOpt_three_elements",
             "[optional.of(42), optional.none(), "
             "optional.of(\"a\")].unwrapOpt() == [42, \"a\"]",
             BoolValueIs(true)},
            {"list_unwrapOpt_no_none",
             "[optional.of(42), optional.of(\"a\")].unwrapOpt() == [42, \"a\"]",
             BoolValueIs(true)},
            {"list_first", "[1, 2, 3].first()", OptionalValueIs(IntValueIs(1))},
            {"list_first_empty", "[].first()", OptionalValueIsEmpty()},
            {"list_last", "[1, 2, 3].last()", OptionalValueIs(IntValueIs(3))},
            {"list_last_empty", "[].last()", OptionalValueIsEmpty()},
        }),
        /*enable_short_circuiting*/ testing::Bool()));

class UnreachableFunction final : public cel::Function {
 public:
  explicit UnreachableFunction(int64_t* count) : count_(count) {}

  absl::StatusOr<Value> Invoke(absl::Span<const Value> args,
                               const InvokeContext& context) const override {
    ++(*count_);
    return ErrorValue::From(absl::CancelledError(), context.arena());
  }

 private:
  int64_t* const count_;
};

TEST(OptionalTypesTest, ErrorShortCircuiting) {
  RuntimeOptions opts{.enable_qualified_type_identifiers = true};
  google::protobuf::Arena arena;

  ASSERT_OK_AND_ASSIGN(
      auto builder,
      CreateStandardRuntimeBuilder(internal::GetTestingDescriptorPool(), opts));

  int64_t unreachable_count = 0;

  ASSERT_OK(EnableOptionalTypes(builder));
  ASSERT_OK(
      EnableReferenceResolver(builder, ReferenceResolverEnabled::kAlways));
  ASSERT_OK(builder.function_registry().Register(
      cel::FunctionDescriptor("unreachable", false, {}),
      std::make_unique<UnreachableFunction>(&unreachable_count)));

  ASSERT_OK_AND_ASSIGN(auto runtime, std::move(builder).Build());

  ASSERT_OK_AND_ASSIGN(
      ParsedExpr expr,
      Parse("optional.of(1 / 0).orValue(unreachable())", "<input>",
            ParserOptions{.enable_optional_syntax = true}));

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Program> program,
                       ProtobufRuntimeAdapter::CreateProgram(*runtime, expr));

  Activation activation;

  ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));

  EXPECT_EQ(unreachable_count, 0);
  ASSERT_TRUE(result->Is<ErrorValue>()) << result->DebugString();
  EXPECT_THAT(result.GetError().NativeValue(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("divide by zero")));
}

TEST(OptionalTypesTest, PresentReceiverSkipsErrorFallback) {
  for (bool type_level : {false, true})
    for (bool present : {false, true})
      for (absl::string_view method : {"or", "orValue"}) {
        RuntimeOptions opts{.enable_qualified_type_identifiers = true};
        opts.enable_type_level_overload = type_level;
        ASSERT_OK_AND_ASSIGN(auto builder,
                             CreateStandardRuntimeBuilder(
                                 internal::GetTestingDescriptorPool(), opts));
        ASSERT_OK(EnableOptionalTypes(builder));
        ASSERT_OK(EnableReferenceResolver(builder,
                                          ReferenceResolverEnabled::kAlways));
        int64_t fallback_count = 0;
        ASSERT_OK(builder.function_registry().Register(
            FunctionDescriptor("unreachable", false, {}),
            std::make_unique<UnreachableFunction>(&fallback_count)));
        ASSERT_OK_AND_ASSIGN(auto runtime, std::move(builder).Build());
        const std::string expression =
            std::string(present ? "optional.of(1)." : "optional.none().") +
            std::string(method) + "(unreachable())";
        ASSERT_OK_AND_ASSIGN(
            ParsedExpr expr,
            Parse(expression, "<input>",
                  ParserOptions{.enable_optional_syntax = true}));
        ASSERT_OK_AND_ASSIGN(
            auto program,
            ProtobufRuntimeAdapter::CreateProgram(*runtime, expr));
        google::protobuf::Arena arena;
        Activation activation;
        ASSERT_OK_AND_ASSIGN(Value result,
                             program->Evaluate(&arena, activation));
        EXPECT_EQ(fallback_count, present ? 0 : 1);
        if (!present) {
          ASSERT_TRUE(result.IsError());
          EXPECT_EQ(result.GetError().NativeValue(), absl::CancelledError());
        } else if (method == "or") {
          EXPECT_THAT(result, OptionalValueIs(IntValueIs(1)));
        } else {
          EXPECT_THAT(result, IntValueIs(1));
        }
      }
}

TEST(OptionalTypesTest, CreateList_TypeConversionError) {
  RuntimeOptions opts{.enable_qualified_type_identifiers = true};
  google::protobuf::Arena arena;

  ASSERT_OK_AND_ASSIGN(
      auto builder,
      CreateStandardRuntimeBuilder(internal::GetTestingDescriptorPool(), opts));

  ASSERT_THAT(EnableOptionalTypes(builder), IsOk());
  ASSERT_THAT(
      EnableReferenceResolver(builder, ReferenceResolverEnabled::kAlways),
      IsOk());

  ASSERT_OK_AND_ASSIGN(auto runtime, std::move(builder).Build());

  ASSERT_OK_AND_ASSIGN(ParsedExpr expr,
                       Parse("[?foo]", "<input>",
                             ParserOptions{.enable_optional_syntax = true}));

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Program> program,
                       ProtobufRuntimeAdapter::CreateProgram(*runtime, expr));

  Activation activation;
  activation.InsertOrAssignValue("foo", IntValue(1));

  ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));

  ASSERT_TRUE(result.IsError()) << result.DebugString();
  EXPECT_THAT(result.GetError().ToStatus(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("type conversion error")));
}

TEST(OptionalTypesTest, CreateMap_TypeConversionError) {
  RuntimeOptions opts{.enable_qualified_type_identifiers = true};
  google::protobuf::Arena arena;

  ASSERT_OK_AND_ASSIGN(
      auto builder,
      CreateStandardRuntimeBuilder(internal::GetTestingDescriptorPool(), opts));

  ASSERT_THAT(EnableOptionalTypes(builder), IsOk());
  ASSERT_THAT(
      EnableReferenceResolver(builder, ReferenceResolverEnabled::kAlways),
      IsOk());

  ASSERT_OK_AND_ASSIGN(auto runtime, std::move(builder).Build());

  ASSERT_OK_AND_ASSIGN(ParsedExpr expr,
                       Parse("{?1: foo}", "<input>",
                             ParserOptions{.enable_optional_syntax = true}));

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Program> program,
                       ProtobufRuntimeAdapter::CreateProgram(*runtime, expr));

  Activation activation;
  activation.InsertOrAssignValue("foo", IntValue(1));

  ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));

  ASSERT_TRUE(result.IsError()) << result.DebugString();
  EXPECT_THAT(result.GetError().ToStatus(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("type conversion error")));
}

TEST(OptionalTypesTest, CreateStruct_KeyTypeConversionError) {
  RuntimeOptions opts{.enable_qualified_type_identifiers = true};
  google::protobuf::Arena arena;

  ASSERT_OK_AND_ASSIGN(
      auto builder,
      CreateStandardRuntimeBuilder(internal::GetTestingDescriptorPool(), opts));

  ASSERT_THAT(EnableOptionalTypes(builder), IsOk());
  ASSERT_THAT(
      EnableReferenceResolver(builder, ReferenceResolverEnabled::kAlways),
      IsOk());

  ASSERT_OK_AND_ASSIGN(auto runtime, std::move(builder).Build());

  ASSERT_OK_AND_ASSIGN(
      ParsedExpr expr,
      Parse("cel.expr.conformance.proto2.TestAllTypes{?single_int32: foo}",
            "<input>", ParserOptions{.enable_optional_syntax = true}));

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Program> program,
                       ProtobufRuntimeAdapter::CreateProgram(*runtime, expr));

  Activation activation;
  activation.InsertOrAssignValue("foo", IntValue(1));

  ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));

  ASSERT_TRUE(result.IsError()) << result.DebugString();
  EXPECT_THAT(result.GetError().ToStatus(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("type conversion error")));
}

TEST(OptionalTypesTest, GenericOptionalBindingUsesContainedTypes) {
  class SameTypeFunction final : public Function {
   public:
    absl::StatusOr<Value> Invoke(absl::Span<const Value>,
                                 const InvokeContext&) const override {
      return BoolValue(true);
    }
  };
  RuntimeOptions options;
  options.enable_qualified_type_identifiers = true;
  options.enable_type_level_overload = true;
  ASSERT_OK_AND_ASSIGN(auto builder,
                       CreateStandardRuntimeBuilder(
                           internal::GetTestingDescriptorPool(), options));
  ASSERT_OK(EnableOptionalTypes(builder));
  ASSERT_OK(
      EnableReferenceResolver(builder, ReferenceResolverEnabled::kAlways));
  ASSERT_OK(builder.function_registry().Register(
      FunctionDescriptor(
          "sameType", "same_A_A_A", false,
          {TypeParamType("A"), TypeParamType("A"), TypeParamType("A")}),
      std::make_unique<SameTypeFunction>()));
  ASSERT_OK_AND_ASSIGN(auto runtime, std::move(builder).Build());
  for (const auto& [expression, matches] :
       std::vector<std::pair<absl::string_view, bool>>{
           {"sameType(optional.none(), optional.of([1]), optional.of(['s']))",
            false},
           {"sameType(optional.none(), optional.of([1]), optional.of([2]))",
            true},
           {"sameType(optional.of(1), optional.of(2), optional.none())",
            true}}) {
    SCOPED_TRACE(expression);
    ASSERT_OK_AND_ASSIGN(ParsedExpr expr,
                         Parse(expression, "<input>",
                               ParserOptions{.enable_optional_syntax = true}));
    ASSERT_OK_AND_ASSIGN(std::unique_ptr<Program> program,
                         ProtobufRuntimeAdapter::CreateProgram(*runtime, expr));
    google::protobuf::Arena arena;
    Activation activation;
    ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));
    if (matches) {
      EXPECT_THAT(result, BoolValueIs(true));
    } else {
      EXPECT_TRUE(result.IsError()) << result.DebugString();
    }
  }
}

TEST(OptionalTypesTest, TypeLevelOpaqueDoesNotAbort) {
  RuntimeOptions opts;
  opts.enable_qualified_type_identifiers = true;
  opts.enable_type_level_overload = true;
  ASSERT_OK_AND_ASSIGN(
      auto builder,
      CreateStandardRuntimeBuilder(internal::GetTestingDescriptorPool(), opts));
  ASSERT_OK(EnableOptionalTypes(builder));
  ASSERT_OK(
      EnableReferenceResolver(builder, ReferenceResolverEnabled::kAlways));
  ASSERT_OK_AND_ASSIGN(auto runtime, std::move(builder).Build());
  ASSERT_OK_AND_ASSIGN(ParsedExpr expr,
                       Parse("optional.none().hasValue()", "<input>",
                             ParserOptions{.enable_optional_syntax = true}));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Program> program,
                       ProtobufRuntimeAdapter::CreateProgram(*runtime, expr));
  google::protobuf::Arena arena;
  Activation activation;
  ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));
  EXPECT_THAT(result, BoolValueIs(false));
}

}  // namespace
}  // namespace cel::extensions
