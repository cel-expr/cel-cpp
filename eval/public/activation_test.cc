#include "eval/public/activation.h"

#include <memory>
#include <string>
#include <utility>

#include "eval/eval/attribute_trail.h"
#include "eval/eval/ident_step.h"
#include "eval/internal/adapter_activation_impl.h"
#include "eval/public/cel_attribute.h"
#include "eval/public/cel_function.h"
#include "extensions/protobuf/memory_manager.h"
#include "internal/status_macros.h"
#include "internal/testing.h"
#include "parser/parser.h"

namespace google {
namespace api {
namespace expr {
namespace runtime {

namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::cel::expr::Expr;
using ::cel::extensions::ProtoMemoryManager;
using ::google::protobuf::Arena;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Property;
using ::testing::Return;

class MockValueProducer : public CelValueProducer {
 public:
  MOCK_METHOD(CelValue, Produce, (Arena*), (override));
};

// Simple function that takes no args and returns an int64.
class ConstCelFunction : public CelFunction {
 public:
  explicit ConstCelFunction(absl::string_view name)
      : CelFunction({std::string(name), false, {}}) {}
  explicit ConstCelFunction(const CelFunctionDescriptor& desc)
      : CelFunction(desc) {}

  absl::Status Evaluate(absl::Span<const CelValue> args, CelValue* output,
                        google::protobuf::Arena* arena) const override {
    *output = CelValue::CreateInt64(42);
    return absl::OkStatus();
  }
};

TEST(ActivationTest, DuplicateIdInsertionPreservesOriginal) {
  Activation activation;
  cel::FunctionDescriptor first("choose", "shared", false, {cel::IntType()});
  ASSERT_OK(
      activation.InsertFunction(std::make_unique<ConstCelFunction>(first)));
  cel::interop_internal::AdapterActivationImpl adapted(activation);
  auto original = adapted.FindFunctionOverloadById("choose", "shared");
  ASSERT_TRUE(original);
  const auto* original_descriptor = &original->descriptor;
  const auto* implementation = &original->implementation;
  for (const auto& descriptor :
       {cel::FunctionDescriptor("choose", "shared", false, {cel::DoubleType()}),
        cel::FunctionDescriptor("choose", "shared", true, {cel::DoubleType()}),
        cel::FunctionDescriptor("choose", "shared", false, {})}) {
    EXPECT_FALSE(
        activation
            .InsertFunction(std::make_unique<ConstCelFunction>(descriptor))
            .ok());
    EXPECT_THAT(activation.FindFunctionOverloads("choose"),
                ::testing::SizeIs(1));
    auto found = adapted.FindFunctionOverloadById("choose", "shared");
    ASSERT_TRUE(found);
    EXPECT_EQ(&found->descriptor, original_descriptor);
    EXPECT_EQ(&found->implementation, implementation);
  }
  ASSERT_OK(activation.InsertFunction(std::make_unique<ConstCelFunction>(
      cel::FunctionDescriptor("other", "shared", false, {}))));
  for (int i = 0; i < 200; ++i) {
    ASSERT_OK(activation.InsertFunction(std::make_unique<ConstCelFunction>(
        cel::FunctionDescriptor("g" + std::to_string(i), "id", false, {}))));
  }
  Activation moved(std::move(activation));
  Activation assigned;
  assigned = std::move(moved);
  cel::interop_internal::AdapterActivationImpl moved_adapter(assigned);
  auto found = moved_adapter.FindFunctionOverloadById("choose", "shared");
  ASSERT_TRUE(found);
  EXPECT_EQ(&found->descriptor, original_descriptor);
  EXPECT_EQ(&found->implementation, implementation);
  ASSERT_OK(assigned.InsertFunction(std::make_unique<ConstCelFunction>(
      cel::FunctionDescriptor("empty", false, {cel::Kind::kInt}))));
  ASSERT_OK(assigned.InsertFunction(std::make_unique<ConstCelFunction>(
      cel::FunctionDescriptor("empty", false, {cel::Kind::kString}))));
  EXPECT_FALSE(moved_adapter.FindFunctionOverloadById("empty", ""));
  EXPECT_THAT(assigned.FindFunctionOverloads("empty"), ::testing::SizeIs(2));
  EXPECT_TRUE(assigned.RemoveFunctionEntries(first));
  EXPECT_FALSE(moved_adapter.FindFunctionOverloadById("choose", "shared"));
  EXPECT_THAT(assigned.FindFunctionOverloads("choose"), IsEmpty());
}

TEST(ActivationTest, PublicKindGuardsAcceptDynamicParameters) {
  ConstCelFunction nullary(cel::FunctionDescriptor("f", false, {}));
  EXPECT_TRUE(nullary.MatchArguments(absl::Span<const CelValue>()));
  EXPECT_TRUE(nullary.MatchArguments(absl::Span<const cel::Value>()));
  EXPECT_FALSE(
      nullary.MatchArguments(std::vector<CelValue>{CelValue::CreateInt64(1)}));
  EXPECT_FALSE(
      nullary.MatchArguments(std::vector<cel::Value>{cel::IntValue(1)}));
  for (cel::Kind expected :
       {cel::Kind::kAny, cel::Kind::kDyn, cel::Kind::kTypeParam,
        cel::Kind::kInt, cel::Kind::kString}) {
    ConstCelFunction function(cel::FunctionDescriptor("f", false, {expected}));
    const std::vector<CelValue> legacy{CelValue::CreateInt64(1)};
    const std::vector<cel::Value> modern{cel::IntValue(1)};
    EXPECT_EQ(function.MatchArguments(legacy), expected != cel::Kind::kString);
    EXPECT_EQ(function.MatchArguments(modern), expected != cel::Kind::kString);
    EXPECT_FALSE(function.MatchArguments(absl::Span<const CelValue>()));
    EXPECT_FALSE(function.MatchArguments(absl::Span<const cel::Value>()));
    ConstCelFunction multiple(cel::FunctionDescriptor(
        "f", false, {expected, cel::Kind::kInt, cel::Kind::kBool}));
    std::vector<CelValue> legacy_many{CelValue::CreateInt64(1),
                                      CelValue::CreateInt64(2),
                                      CelValue::CreateBool(true)};
    std::vector<cel::Value> modern_many{cel::IntValue(1), cel::IntValue(2),
                                        cel::BoolValue(true)};
    EXPECT_EQ(multiple.MatchArguments(legacy_many),
              expected != cel::Kind::kString);
    EXPECT_EQ(multiple.MatchArguments(modern_many),
              expected != cel::Kind::kString);
    legacy_many.back() = CelValue::CreateInt64(3);
    modern_many.back() = cel::IntValue(3);
    EXPECT_FALSE(multiple.MatchArguments(legacy_many));
    EXPECT_FALSE(multiple.MatchArguments(modern_many));
    legacy_many.pop_back();
    modern_many.pop_back();
    EXPECT_FALSE(multiple.MatchArguments(legacy_many));
    EXPECT_FALSE(multiple.MatchArguments(modern_many));
    legacy_many.push_back(CelValue::CreateBool(true));
    legacy_many.push_back(CelValue::CreateInt64(4));
    modern_many.push_back(cel::BoolValue(true));
    modern_many.push_back(cel::IntValue(4));
    EXPECT_FALSE(multiple.MatchArguments(legacy_many));
    EXPECT_FALSE(multiple.MatchArguments(modern_many));
  }
}

TEST(ActivationTest, CheckValueInsertFindAndRemove) {
  Activation activation;

  Arena arena;

  activation.InsertValue("value42", CelValue::CreateInt64(42));

  // Test getting unbound value
  EXPECT_FALSE(activation.FindValue("value43", &arena));

  // Test getting bound value
  EXPECT_TRUE(activation.FindValue("value42", &arena));

  CelValue value = activation.FindValue("value42", &arena).value();

  // Test value is correct.
  EXPECT_THAT(value.Int64OrDie(), Eq(42));

  // Test removing unbound value
  EXPECT_FALSE(activation.RemoveValueEntry("value43"));

  // Test removing bound value
  EXPECT_TRUE(activation.RemoveValueEntry("value42"));

  // Now the value is unbound
  EXPECT_FALSE(activation.FindValue("value42", &arena));
}

TEST(ActivationTest, CheckValueProducerInsertFindAndRemove) {
  const std::string kValue = "42";

  auto producer = std::make_unique<MockValueProducer>();

  google::protobuf::Arena arena;

  ON_CALL(*producer, Produce(&arena))
      .WillByDefault(Return(CelValue::CreateString(&kValue)));

  // ValueProducer is expected to be invoked only once.
  EXPECT_CALL(*producer, Produce(&arena)).Times(1);

  Activation activation;

  activation.InsertValueProducer("value42", std::move(producer));

  // Test getting unbound value
  EXPECT_FALSE(activation.FindValue("value43", &arena));

  // Test getting bound value - 1st pass

  // Access attempt is repeated twice.
  // ValueProducer is expected to be invoked only once.
  for (int i = 0; i < 2; i++) {
    auto opt_value = activation.FindValue("value42", &arena);
    EXPECT_TRUE(opt_value.has_value()) << " for pass " << i;
    CelValue value = opt_value.value();
    EXPECT_THAT(value.StringOrDie().value(), Eq(kValue)) << " for pass " << i;
  }

  // Test removing bound value
  EXPECT_TRUE(activation.RemoveValueEntry("value42"));

  // Now the value is unbound
  EXPECT_FALSE(activation.FindValue("value42", &arena));
}

TEST(ActivationTest, CheckInsertFunction) {
  Activation activation;
  ASSERT_OK(activation.InsertFunction(
      std::make_unique<ConstCelFunction>("ConstFunc")));

  auto overloads = activation.FindFunctionOverloads("ConstFunc");
  EXPECT_THAT(overloads,
              ElementsAre(Property(
                  &CelFunction::descriptor,
                  Property(&CelFunctionDescriptor::name, Eq("ConstFunc")))));

  EXPECT_THAT(activation.InsertFunction(
                  std::make_unique<ConstCelFunction>("ConstFunc")),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Function with same shape")));

  EXPECT_THAT(activation.FindFunctionOverloads("ConstFunc0"), IsEmpty());
}

TEST(ActivationTest, CheckRemoveFunction) {
  Activation activation;
  ASSERT_OK(activation.InsertFunction(std::make_unique<ConstCelFunction>(
      CelFunctionDescriptor{"ConstFunc", false, {CelValue::Type::kInt64}})));
  EXPECT_OK(activation.InsertFunction(std::make_unique<ConstCelFunction>(
      CelFunctionDescriptor{"ConstFunc", false, {CelValue::Type::kUint64}})));

  auto overloads = activation.FindFunctionOverloads("ConstFunc");
  EXPECT_THAT(
      overloads,
      ElementsAre(
          Property(&CelFunction::descriptor,
                   Property(&CelFunctionDescriptor::name, Eq("ConstFunc"))),
          Property(&CelFunction::descriptor,
                   Property(&CelFunctionDescriptor::name, Eq("ConstFunc")))));

  EXPECT_TRUE(activation.RemoveFunctionEntries(
      {"ConstFunc", false, {CelValue::Type::kAny}}));

  EXPECT_THAT(activation.FindFunctionOverloads("ConstFunc"), IsEmpty());
}

TEST(ActivationTest, CheckValueProducerClear) {
  const std::string kValue1 = "42";
  const std::string kValue2 = "43";

  auto producer1 = std::make_unique<MockValueProducer>();
  auto producer2 = std::make_unique<MockValueProducer>();

  google::protobuf::Arena arena;

  ON_CALL(*producer1, Produce(&arena))
      .WillByDefault(Return(CelValue::CreateString(&kValue1)));
  ON_CALL(*producer2, Produce(&arena))
      .WillByDefault(Return(CelValue::CreateString(&kValue2)));
  EXPECT_CALL(*producer1, Produce(&arena)).Times(2);
  EXPECT_CALL(*producer2, Produce(&arena)).Times(1);

  Activation activation;
  activation.InsertValueProducer("value42", std::move(producer1));
  activation.InsertValueProducer("value43", std::move(producer2));

  // Produce first value
  auto opt_value = activation.FindValue("value42", &arena);
  EXPECT_TRUE(opt_value.has_value());
  EXPECT_THAT(opt_value->StringOrDie().value(), Eq(kValue1));

  // Test clearing bound value
  EXPECT_TRUE(activation.ClearValueEntry("value42"));
  EXPECT_FALSE(activation.ClearValueEntry("value43"));

  // Produce second value
  auto opt_value2 = activation.FindValue("value43", &arena);
  EXPECT_TRUE(opt_value2.has_value());
  EXPECT_THAT(opt_value2->StringOrDie().value(), Eq(kValue2));

  // Clear all values
  EXPECT_EQ(1, activation.ClearCachedValues());
  EXPECT_FALSE(activation.ClearValueEntry("value42"));
  EXPECT_FALSE(activation.ClearValueEntry("value43"));

  // Produce first value again
  auto opt_value3 = activation.FindValue("value42", &arena);
  EXPECT_TRUE(opt_value3.has_value());
  EXPECT_THAT(opt_value3->StringOrDie().value(), Eq(kValue1));
  EXPECT_EQ(1, activation.ClearCachedValues());
}

TEST(ActivationTest, ErrorPathTest) {
  Activation activation;

  Expr expr;
  auto* select_expr = expr.mutable_select_expr();
  select_expr->set_field("ip");

  Expr* ident_expr = select_expr->mutable_operand();
  ident_expr->mutable_ident_expr()->set_name("destination");

  const CelAttributePattern destination_ip_pattern(
      "destination",
      {CreateCelAttributeQualifierPattern(CelValue::CreateStringView("ip"))});

  AttributeTrail trail("destination");
  trail =
      trail.Step(CreateCelAttributeQualifier(CelValue::CreateStringView("ip")));

  ASSERT_EQ(destination_ip_pattern.IsMatch(trail.attribute()),
            CelAttributePattern::MatchType::FULL);
  EXPECT_TRUE(activation.missing_attribute_patterns().empty());

  ASSERT_THAT(activation.SetMissingAttributePatterns({destination_ip_pattern}),
              IsOk());
  EXPECT_EQ(
      activation.missing_attribute_patterns()[0].IsMatch(trail.attribute()),
      CelAttributePattern::MatchType::FULL);
}

}  // namespace

}  // namespace runtime
}  // namespace expr
}  // namespace api
}  // namespace google
