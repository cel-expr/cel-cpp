#include "eval/eval/evaluator_stack.h"

#include <cstddef>

#include "base/attribute.h"
#include "common/value.h"
#include "internal/testing.h"

namespace google::api::expr::runtime {

namespace {

// Test Value Stack Push/Pop operation
TEST(EvaluatorStackTest, StackPushPop) {
  cel::Attribute attribute("name", {});
  EvaluatorStack stack(10);
  stack.Push(cel::IntValue(1));
  stack.Push(cel::IntValue(2), AttributeTrail());
  stack.Push(cel::IntValue(3), AttributeTrail("name"));

  ASSERT_EQ(stack.Peek().GetInt().NativeValue(), 3);
  ASSERT_FALSE(stack.PeekAttribute().empty());
  ASSERT_EQ(stack.PeekAttribute().attribute(), attribute);

  stack.Pop(1);

  ASSERT_EQ(stack.Peek().GetInt().NativeValue(), 2);
  ASSERT_TRUE(stack.PeekAttribute().empty());

  stack.Pop(1);

  ASSERT_EQ(stack.Peek().GetInt().NativeValue(), 1);
  ASSERT_TRUE(stack.PeekAttribute().empty());
}

// Test that inner stacks within value stack retain the equality of their sizes.
TEST(EvaluatorStackTest, StackBalanced) {
  EvaluatorStack stack(10);
  ASSERT_EQ(stack.size(), stack.attribute_size());

  stack.Push(cel::IntValue(1));
  ASSERT_EQ(stack.size(), stack.attribute_size());
  stack.Push(cel::IntValue(2), AttributeTrail());
  stack.Push(cel::IntValue(3), AttributeTrail());
  ASSERT_EQ(stack.size(), stack.attribute_size());

  stack.PopAndPush(cel::IntValue(4), AttributeTrail());
  ASSERT_EQ(stack.size(), stack.attribute_size());
  stack.PopAndPush(cel::IntValue(5));
  ASSERT_EQ(stack.size(), stack.attribute_size());

  stack.Pop(3);
  ASSERT_EQ(stack.size(), stack.attribute_size());
}

TEST(EvaluatorStackTest, Clear) {
  EvaluatorStack stack(10);
  ASSERT_EQ(stack.size(), stack.attribute_size());

  stack.Push(cel::IntValue(1));
  stack.Push(cel::IntValue(2), AttributeTrail());
  stack.Push(cel::IntValue(3), AttributeTrail());
  ASSERT_EQ(stack.size(), 3);

  stack.Clear();
  ASSERT_EQ(stack.size(), 0);
  ASSERT_TRUE(stack.empty());
}

TEST(EvaluatorStackTest, SizeBytesIsConstexpr) {
  constexpr size_t kSize = EvaluatorStack::SizeBytes(9);
  static_assert(kSize >= 9 * (sizeof(cel::Value) + sizeof(AttributeTrail)));
  EXPECT_GE(kSize, 9 * (sizeof(cel::Value) + sizeof(AttributeTrail)));
}

TEST(EvaluatorStackTest, UnownedBuffer) {
  alignas(__STDCPP_DEFAULT_NEW_ALIGNMENT__)
      uint8_t buffer[EvaluatorStack::SizeBytes(2)];
  EvaluatorStack stack(buffer, sizeof(buffer), 2);
  EXPECT_EQ(stack.max_size(), 2);
  EXPECT_TRUE(stack.empty());

  stack.Push(cel::IntValue(1));
  stack.Push(cel::IntValue(2), AttributeTrail("name"));
  EXPECT_TRUE(stack.full());
  EXPECT_EQ(stack.Peek().GetInt().NativeValue(), 2);

  // Resize from unowned buffer to heap buffer.
  stack.SetMaxSize(4);
  stack.Push(cel::IntValue(3));
  EXPECT_EQ(stack.size(), 3);
  EXPECT_EQ(stack.max_size(), 4);
  EXPECT_EQ(stack.Peek().GetInt().NativeValue(), 3);
}

}  // namespace

}  // namespace google::api::expr::runtime
