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

#include "eval/eval/bitset_steps.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "base/attribute.h"
#include "common/value.h"
#include "common/value_kind.h"
#include "common/value_testing.h"
#include "eval/eval/attribute_trail.h"
#include "eval/eval/evaluator_core.h"
#include "eval/eval/expression_step_logic.h"
#include "internal/testing.h"
#include "internal/testing_descriptor_pool.h"
#include "internal/testing_message_factory.h"
#include "runtime/activation.h"
#include "runtime/internal/runtime_type_provider.h"
#include "runtime/runtime_options.h"
#include "google/protobuf/arena.h"

namespace google::api::expr::runtime {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::cel::Attribute;
using ::cel::DoubleValue;
using ::cel::ErrorValue;
using ::cel::IntValue;
using ::cel::ListValueBuilderPtr;
using ::cel::StringValue;
using ::cel::UintValue;
using ::cel::UnknownValue;
using ::cel::Value;
using ::cel::ValueKind;
using ::cel::test::BoolValueIs;
using ::cel::test::ErrorValueIs;
using ::cel::test::ValueKindIs;
using ::testing::HasSubstr;

class ValueStep : public ExpressionStepLogic {
 public:
  ValueStep(Value value, Attribute attr)
      : ExpressionStepLogic(),
        value_(std::move(value)),
        attr_(std::move(attr)) {}
  explicit ValueStep(Value value)
      : ExpressionStepLogic(), value_(std::move(value)), attr_() {}

  void Evaluate(ExecutionFrame* frame) const override {
    frame->value_stack().Push(value_, attr_);
  }

 private:
  Value value_;
  AttributeTrail attr_;
};

Value MakeList(google::protobuf::Arena* absl_nonnull arena,
               const std::vector<Value>& elements) {
  ListValueBuilderPtr builder = cel::NewListValueBuilder(arena);
  for (const Value& elem : elements) {
    ABSL_CHECK_OK(builder->Add(elem));
  }
  return std::move(*builder).Build();
}

enum class OutputType { kBoolTrue, kBoolFalse, kError, kUnknown };

struct BitIsSetTestCase {
  Value input;
  uint64_t bitmask;
  OutputType expected_result;
};

class BitIsSetTest : public ::testing::TestWithParam<BitIsSetTestCase> {};

TEST_P(BitIsSetTest, Evaluate) {
  const BitIsSetTestCase& test_case = GetParam();
  cel::Activation activation;
  google::protobuf::Arena arena;
  cel::RuntimeOptions opts;
  opts.unknown_processing = cel::UnknownProcessingOptions::kAttributeOnly;
  cel::runtime_internal::RuntimeTypeProvider type_provider(
      cel::internal::GetTestingDescriptorPool());

  FlatExpressionEvaluatorState state(
      /*value_stack_size=*/5,
      /*comprehension_slot_count=*/0, /*iterator_stack_size=*/0, type_provider,
      cel::internal::GetTestingDescriptorPool(),
      cel::internal::GetTestingMessageFactory(), &arena);

  ExecutionPath steps;
  steps.push_back(ExpressionStep::MakeGenericStep(
      std::make_unique<ValueStep>(test_case.input)));
  steps.push_back(ExpressionStep::MakeBitIsSetStep(test_case.bitmask));

  ExecutionFrame frame(steps, activation, opts, state);

  ASSERT_OK_AND_ASSIGN(Value result, frame.Evaluate());

  switch (test_case.expected_result) {
    case OutputType::kBoolTrue:
      EXPECT_THAT(result, BoolValueIs(true));
      break;
    case OutputType::kBoolFalse:
      EXPECT_THAT(result, BoolValueIs(false));
      break;
    case OutputType::kError:
      EXPECT_THAT(result, ValueKindIs(ValueKind::kError));
      break;
    case OutputType::kUnknown:
      EXPECT_THAT(result, ValueKindIs(ValueKind::kUnknown));
      break;
  }
}

INSTANTIATE_TEST_SUITE_P(
    BitIsSetTest, BitIsSetTest,
    testing::Values(
        // IntValue boundaries and basics
        BitIsSetTestCase{IntValue(0), 0b101, OutputType::kBoolTrue},
        BitIsSetTestCase{IntValue(1), 0b101, OutputType::kBoolFalse},
        BitIsSetTestCase{IntValue(2), 0b101, OutputType::kBoolTrue},
        BitIsSetTestCase{IntValue(63), uint64_t{1} << 63,
                         OutputType::kBoolTrue},
        BitIsSetTestCase{IntValue(62), uint64_t{1} << 63,
                         OutputType::kBoolFalse},
        BitIsSetTestCase{IntValue(-1), ~uint64_t{0}, OutputType::kBoolFalse},
        BitIsSetTestCase{IntValue(std::numeric_limits<int64_t>::min()),
                         ~uint64_t{0}, OutputType::kBoolFalse},
        BitIsSetTestCase{IntValue(64), ~uint64_t{0}, OutputType::kBoolFalse},
        BitIsSetTestCase{IntValue(std::numeric_limits<int64_t>::max()),
                         ~uint64_t{0}, OutputType::kBoolFalse},
        BitIsSetTestCase{IntValue(0), 0, OutputType::kBoolFalse},
        // UintValue boundaries and basics
        BitIsSetTestCase{UintValue(0), 0b101, OutputType::kBoolTrue},
        BitIsSetTestCase{UintValue(1), 0b101, OutputType::kBoolFalse},
        BitIsSetTestCase{UintValue(63), uint64_t{1} << 63,
                         OutputType::kBoolTrue},
        BitIsSetTestCase{UintValue(64), ~uint64_t{0}, OutputType::kBoolFalse},
        BitIsSetTestCase{UintValue(std::numeric_limits<uint64_t>::max()),
                         ~uint64_t{0}, OutputType::kBoolFalse},
        // DoubleValue (heterogeneous equality is always enabled for bitset
        // steps)
        BitIsSetTestCase{DoubleValue(0.0), 0b101, OutputType::kBoolTrue},
        BitIsSetTestCase{DoubleValue(2.0), 0b101, OutputType::kBoolTrue},
        BitIsSetTestCase{DoubleValue(1.0), 0b101, OutputType::kBoolFalse},
        BitIsSetTestCase{DoubleValue(63.0), uint64_t{1} << 63,
                         OutputType::kBoolTrue},
        BitIsSetTestCase{DoubleValue(64.0), ~uint64_t{0},
                         OutputType::kBoolFalse},
        BitIsSetTestCase{DoubleValue(-1.0), ~uint64_t{0},
                         OutputType::kBoolFalse},
        BitIsSetTestCase{DoubleValue(2.5), ~uint64_t{0},
                         OutputType::kBoolFalse},
        BitIsSetTestCase{DoubleValue(NAN), ~uint64_t{0},
                         OutputType::kBoolFalse},
        BitIsSetTestCase{DoubleValue(INFINITY), ~uint64_t{0},
                         OutputType::kBoolFalse},
        // Non-numeric and Unknown
        BitIsSetTestCase{StringValue(), 0b101, OutputType::kBoolFalse},
        BitIsSetTestCase{UnknownValue(), 0b101, OutputType::kUnknown}));

TEST(BitIsSetStandaloneTest, PropagatesErrorAndPartialUnknown) {
  cel::Activation activation;
  google::protobuf::Arena arena;
  cel::RuntimeOptions opts;
  opts.unknown_processing = cel::UnknownProcessingOptions::kAttributeOnly;
  cel::runtime_internal::RuntimeTypeProvider type_provider(
      cel::internal::GetTestingDescriptorPool());

  FlatExpressionEvaluatorState state(
      /*value_stack_size=*/5,
      /*comprehension_slot_count=*/0, /*iterator_stack_size=*/0, type_provider,
      cel::internal::GetTestingDescriptorPool(),
      cel::internal::GetTestingMessageFactory(), &arena);

  ASSERT_THAT(activation.SetUnknownPatterns({cel::AttributePattern(
                  "foo", {cel::AttributeQualifierPattern::OfString("bar")})}),
              IsOk());

  {
    ExecutionPath steps;
    steps.push_back(ExpressionStep::MakeGenericStep(
        std::make_unique<ValueStep>(IntValue(0), cel::Attribute("foo"))));
    steps.push_back(ExpressionStep::MakeBitIsSetStep(0b1));
    ExecutionFrame frame(steps, activation, opts, state);
    ASSERT_OK_AND_ASSIGN(Value result, frame.Evaluate());
    EXPECT_THAT(result, ValueKindIs(ValueKind::kUnknown));
  }

  {
    ExecutionPath steps;
    steps.push_back(ExpressionStep::MakeGenericStep(std::make_unique<ValueStep>(
        ErrorValue::From(absl::CancelledError("custom_err"), &arena),
        cel::Attribute("foo"))));
    steps.push_back(ExpressionStep::MakeBitIsSetStep(0b1));
    ExecutionFrame frame(steps, activation, opts, state);
    ASSERT_OK_AND_ASSIGN(Value result, frame.Evaluate());
    EXPECT_THAT(result, ErrorValueIs(StatusIs(absl::StatusCode::kCancelled,
                                              "custom_err")));
  }
}

TEST(BitIsSetStandaloneTest, StackUnderflow) {
  cel::Activation activation;
  google::protobuf::Arena arena;
  cel::RuntimeOptions opts;
  cel::runtime_internal::RuntimeTypeProvider type_provider(
      cel::internal::GetTestingDescriptorPool());

  FlatExpressionEvaluatorState state(
      /*value_stack_size=*/5,
      /*comprehension_slot_count=*/0, /*iterator_stack_size=*/0, type_provider,
      cel::internal::GetTestingDescriptorPool(),
      cel::internal::GetTestingMessageFactory(), &arena);

  ExecutionPath steps;
  steps.push_back(ExpressionStep::MakeBitIsSetStep(0b1));
  ExecutionFrame frame(steps, activation, opts, state);
  EXPECT_THAT(frame.Evaluate(),
              StatusIs(absl::StatusCode::kInternal, HasSubstr("underflow")));
}

enum class BitSetStepKind { kAndIsNonZero, kAndEqual, kEqual };

ExpressionStep MakeBitSetStepForKind(BitSetStepKind kind, uint64_t bitmask) {
  switch (kind) {
    case BitSetStepKind::kAndIsNonZero:
      return ExpressionStep::MakeBitAndIsNonZeroStep(bitmask);
    case BitSetStepKind::kAndEqual:
      return ExpressionStep::MakeBitAndEqualStep(bitmask);
    case BitSetStepKind::kEqual:
      return ExpressionStep::MakeBitEqualStep(bitmask);
  }
}

struct BitSetTestCase {
  BitSetStepKind kind;
  std::vector<Value> list_elements;
  uint64_t bitmask;
  OutputType expected_result;
};

class BitSetTest : public ::testing::TestWithParam<BitSetTestCase> {};

TEST_P(BitSetTest, Evaluate) {
  const BitSetTestCase& test_case = GetParam();
  cel::Activation activation;
  google::protobuf::Arena arena;
  cel::RuntimeOptions opts;
  opts.unknown_processing = cel::UnknownProcessingOptions::kAttributeOnly;
  cel::runtime_internal::RuntimeTypeProvider type_provider(
      cel::internal::GetTestingDescriptorPool());

  FlatExpressionEvaluatorState state(
      /*value_stack_size=*/5,
      /*comprehension_slot_count=*/0, /*iterator_stack_size=*/0, type_provider,
      cel::internal::GetTestingDescriptorPool(),
      cel::internal::GetTestingMessageFactory(), &arena);

  ExecutionPath steps;
  steps.push_back(ExpressionStep::MakeGenericStep(
      std::make_unique<ValueStep>(MakeList(&arena, test_case.list_elements))));
  steps.push_back(MakeBitSetStepForKind(test_case.kind, test_case.bitmask));

  ExecutionFrame frame(steps, activation, opts, state);

  ASSERT_OK_AND_ASSIGN(Value result, frame.Evaluate());

  switch (test_case.expected_result) {
    case OutputType::kBoolTrue:
      EXPECT_THAT(result, BoolValueIs(true));
      break;
    case OutputType::kBoolFalse:
      EXPECT_THAT(result, BoolValueIs(false));
      break;
    case OutputType::kError:
      EXPECT_THAT(result, ValueKindIs(ValueKind::kError));
      break;
    case OutputType::kUnknown:
      EXPECT_THAT(result, ValueKindIs(ValueKind::kUnknown));
      break;
  }
}

INSTANTIATE_TEST_SUITE_P(
    BitSetTest, BitSetTest,
    testing::Values(
        // kBitAndIsNonZero (Any / overlap)
        BitSetTestCase{
            BitSetStepKind::kAndIsNonZero, {}, 0b101, OutputType::kBoolFalse},
        BitSetTestCase{BitSetStepKind::kAndIsNonZero,
                       {IntValue(1), IntValue(3)},
                       0b101,
                       OutputType::kBoolFalse},
        BitSetTestCase{BitSetStepKind::kAndIsNonZero,
                       {IntValue(1), UintValue(2)},
                       0b101,
                       OutputType::kBoolTrue},
        BitSetTestCase{BitSetStepKind::kAndIsNonZero,
                       {UintValue(63)},
                       uint64_t{1} << 63,
                       OutputType::kBoolTrue},
        BitSetTestCase{BitSetStepKind::kAndIsNonZero,
                       {IntValue(-5), IntValue(100), DoubleValue(0.0)},
                       0b101,
                       OutputType::kBoolTrue},
        // kBitAndEqual (All / contains all bits of mask)
        BitSetTestCase{BitSetStepKind::kAndEqual, {}, 0, OutputType::kBoolTrue},
        BitSetTestCase{
            BitSetStepKind::kAndEqual, {}, 0b101, OutputType::kBoolFalse},
        BitSetTestCase{BitSetStepKind::kAndEqual,
                       {IntValue(0)},
                       0b101,
                       OutputType::kBoolFalse},
        BitSetTestCase{BitSetStepKind::kAndEqual,
                       {IntValue(0), UintValue(2)},
                       0b101,
                       OutputType::kBoolTrue},
        BitSetTestCase{BitSetStepKind::kAndEqual,
                       {IntValue(0), DoubleValue(2.0), IntValue(5),
                        IntValue(100), IntValue(-1)},
                       0b101,
                       OutputType::kBoolTrue},
        // kBitEqual (Equivalent / exact set match)
        BitSetTestCase{BitSetStepKind::kEqual, {}, 0, OutputType::kBoolTrue},
        BitSetTestCase{
            BitSetStepKind::kEqual, {}, 0b101, OutputType::kBoolFalse},
        BitSetTestCase{BitSetStepKind::kEqual,
                       {IntValue(0)},
                       0b101,
                       OutputType::kBoolFalse},
        BitSetTestCase{BitSetStepKind::kEqual,
                       {IntValue(0), UintValue(2)},
                       0b101,
                       OutputType::kBoolTrue},
        BitSetTestCase{BitSetStepKind::kEqual,
                       {IntValue(0), UintValue(2), DoubleValue(0.0)},
                       0b101,
                       OutputType::kBoolTrue},
        BitSetTestCase{BitSetStepKind::kEqual,
                       {IntValue(0), IntValue(1), UintValue(2)},
                       0b101,
                       OutputType::kBoolFalse},
        BitSetTestCase{BitSetStepKind::kEqual,
                       {IntValue(0), UintValue(2), IntValue(-1)},
                       0b101,
                       OutputType::kBoolFalse},
        BitSetTestCase{BitSetStepKind::kEqual,
                       {IntValue(0), UintValue(2), IntValue(64)},
                       0b101,
                       OutputType::kBoolFalse},
        BitSetTestCase{BitSetStepKind::kEqual,
                       {IntValue(0), UintValue(2), UintValue(64)},
                       0b101,
                       OutputType::kBoolFalse},
        BitSetTestCase{BitSetStepKind::kEqual,
                       {IntValue(0), UintValue(2), DoubleValue(1.5)},
                       0b101,
                       OutputType::kBoolFalse},
        BitSetTestCase{BitSetStepKind::kEqual,
                       {IntValue(0), UintValue(2), DoubleValue(NAN)},
                       0b101,
                       OutputType::kBoolFalse},
        // Non-numeric element inside list (heterogeneous equality treats as not
        // equal to any integer in [0, 63])
        BitSetTestCase{BitSetStepKind::kAndIsNonZero,
                       {StringValue()},
                       0b101,
                       OutputType::kBoolFalse},
        BitSetTestCase{BitSetStepKind::kAndIsNonZero,
                       {StringValue(), IntValue(2)},
                       0b101,
                       OutputType::kBoolTrue},
        BitSetTestCase{BitSetStepKind::kAndEqual,
                       {IntValue(0), IntValue(2), StringValue()},
                       0b101,
                       OutputType::kBoolTrue},
        BitSetTestCase{BitSetStepKind::kAndEqual,
                       {IntValue(0), StringValue()},
                       0b101,
                       OutputType::kBoolFalse},
        BitSetTestCase{BitSetStepKind::kEqual,
                       {IntValue(0), IntValue(2), StringValue()},
                       0b101,
                       OutputType::kBoolFalse}));

TEST(BitSetStandaloneTest, HandlesNonListAndErrorsAndUnknowns) {
  cel::Activation activation;
  google::protobuf::Arena arena;
  cel::RuntimeOptions opts;
  opts.unknown_processing = cel::UnknownProcessingOptions::kAttributeOnly;
  cel::runtime_internal::RuntimeTypeProvider type_provider(
      cel::internal::GetTestingDescriptorPool());

  FlatExpressionEvaluatorState state(
      /*value_stack_size=*/5,
      /*comprehension_slot_count=*/0, /*iterator_stack_size=*/0, type_provider,
      cel::internal::GetTestingDescriptorPool(),
      cel::internal::GetTestingMessageFactory(), &arena);

  ASSERT_THAT(activation.SetUnknownPatterns({cel::AttributePattern(
                  "foo", {cel::AttributeQualifierPattern::OfString("bar")})}),
              IsOk());

  for (BitSetStepKind kind :
       {BitSetStepKind::kAndIsNonZero, BitSetStepKind::kAndEqual,
        BitSetStepKind::kEqual}) {
    // Non-list top-of-stack operand returns NoMatchingOverloadError.
    {
      ExecutionPath steps;
      steps.push_back(ExpressionStep::MakeGenericStep(
          std::make_unique<ValueStep>(IntValue(1))));
      steps.push_back(MakeBitSetStepForKind(kind, 0b1));
      ExecutionFrame frame(steps, activation, opts, state);
      ASSERT_OK_AND_ASSIGN(Value result, frame.Evaluate());
      EXPECT_THAT(result, ErrorValueIs(absl_testing::StatusIs(
                              absl::StatusCode::kUnknown,
                              HasSubstr("No matching overload"))));
    }

    // Partial unknown attribute on list returns UnknownValue.
    {
      ExecutionPath steps;
      steps.push_back(
          ExpressionStep::MakeGenericStep(std::make_unique<ValueStep>(
              MakeList(&arena, {IntValue(0)}), cel::Attribute("foo"))));
      steps.push_back(MakeBitSetStepForKind(kind, 0b1));
      ExecutionFrame frame(steps, activation, opts, state);
      ASSERT_OK_AND_ASSIGN(Value result, frame.Evaluate());
      EXPECT_THAT(result, ValueKindIs(ValueKind::kUnknown));
    }

    // Top-level ErrorValue on stack is propagated.
    {
      ExecutionPath steps;
      steps.push_back(
          ExpressionStep::MakeGenericStep(std::make_unique<ValueStep>(
              ErrorValue::From(absl::AbortedError("top_err"), &arena))));
      steps.push_back(MakeBitSetStepForKind(kind, 0b1));
      ExecutionFrame frame(steps, activation, opts, state);
      ASSERT_OK_AND_ASSIGN(Value result, frame.Evaluate());
      EXPECT_THAT(result, ErrorValueIs(
                              StatusIs(absl::StatusCode::kAborted, "top_err")));
    }
  }
}

TEST(BitSetStandaloneTest, StackUnderflow) {
  cel::Activation activation;
  google::protobuf::Arena arena;
  cel::RuntimeOptions opts;
  cel::runtime_internal::RuntimeTypeProvider type_provider(
      cel::internal::GetTestingDescriptorPool());

  FlatExpressionEvaluatorState state(
      /*value_stack_size=*/5,
      /*comprehension_slot_count=*/0, /*iterator_stack_size=*/0, type_provider,
      cel::internal::GetTestingDescriptorPool(),
      cel::internal::GetTestingMessageFactory(), &arena);

  for (BitSetStepKind kind :
       {BitSetStepKind::kAndIsNonZero, BitSetStepKind::kAndEqual,
        BitSetStepKind::kEqual}) {
    ExecutionPath steps;
    steps.push_back(MakeBitSetStepForKind(kind, 0b1));
    ExecutionFrame frame(steps, activation, opts, state);
    EXPECT_THAT(frame.Evaluate(),
                StatusIs(absl::StatusCode::kInternal, HasSubstr("underflow")));
  }
}

}  // namespace
}  // namespace google::api::expr::runtime
