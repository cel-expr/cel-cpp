#include "eval/public/activation_bind_helper.h"

#include <memory>
#include <string>

#include "cel/expr/syntax.pb.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "eval/public/activation.h"
#include "eval/public/builtin_func_registrar.h"
#include "eval/public/cel_expr_builder_factory.h"
#include "eval/public/cel_expression.h"
#include "eval/public/cel_function.h"
#include "eval/public/cel_options.h"
#include "eval/public/cel_value.h"
#include "eval/testutil/test_message.pb.h"
#include "internal/status_macros.h"
#include "internal/testing.h"
#include "parser/parser.h"
#include "testutil/util.h"
#include "google/protobuf/arena.h"

namespace google {
namespace api {
namespace expr {
namespace runtime {

namespace {

using ::absl_testing::IsOk;
using testutil::EqualsProto;

TEST(ActivationBindHelperTest, TestSingleBoolBind) {
  TestMessage message;
  message.set_bool_value(true);

  google::protobuf::Arena arena;

  Activation activation;

  ASSERT_OK(BindProtoToActivation(&message, &arena, &activation));

  auto result = activation.FindValue("bool_value", &arena);

  ASSERT_TRUE(result.has_value());

  CelValue value = result.value();

  ASSERT_TRUE(value.IsBool());
  EXPECT_EQ(value.BoolOrDie(), true);
}

TEST(ActivationBindHelperTest, TestSingleInt32Bind) {
  TestMessage message;
  message.set_int32_value(42);

  google::protobuf::Arena arena;

  Activation activation;

  ASSERT_OK(BindProtoToActivation(&message, &arena, &activation));

  auto result = activation.FindValue("int32_value", &arena);

  ASSERT_TRUE(result.has_value());

  CelValue value = result.value();

  ASSERT_TRUE(value.IsInt64());
  EXPECT_EQ(value.Int64OrDie(), 42);
}

TEST(ActivationBindHelperTest, TestUnsetRepeatedIsEmptyList) {
  TestMessage message;

  google::protobuf::Arena arena;

  Activation activation;

  ASSERT_OK(BindProtoToActivation(&message, &arena, &activation));

  auto result = activation.FindValue("int32_list", &arena);

  ASSERT_TRUE(result.has_value());

  CelValue value = result.value();

  ASSERT_TRUE(value.IsList());
  EXPECT_TRUE(value.ListOrDie()->empty());
}

TEST(ActivationBindHelperTest, TestSkipUnsetFields) {
  TestMessage message;
  message.set_int32_value(42);

  google::protobuf::Arena arena;

  Activation activation;

  ASSERT_OK(BindProtoToActivation(&message, &arena, &activation,
                                  ProtoUnsetFieldOptions::kSkip));

  // Explicitly set field is unaffected.
  auto result = activation.FindValue("int32_value", &arena);

  ASSERT_TRUE(result.has_value());

  CelValue value = result.value();

  ASSERT_TRUE(value.IsInt64());
  EXPECT_EQ(value.Int64OrDie(), 42);

  result = activation.FindValue("message_value", &arena);
  ASSERT_FALSE(result.has_value());
}

TEST(ActivationBindHelperTest, TestBindDefaultFields) {
  TestMessage message;
  message.set_int32_value(42);

  google::protobuf::Arena arena;

  Activation activation;

  ASSERT_OK(BindProtoToActivation(&message, &arena, &activation,
                                  ProtoUnsetFieldOptions::kBindDefault));

  auto result = activation.FindValue("int32_value", &arena);

  ASSERT_TRUE(result.has_value());

  CelValue value = result.value();

  ASSERT_TRUE(value.IsInt64());
  EXPECT_EQ(value.Int64OrDie(), 42);

  result = activation.FindValue("message_value", &arena);
  ASSERT_TRUE(result.has_value());
  EXPECT_NE(nullptr, result->MessageOrDie());
  EXPECT_THAT(TestMessage::default_instance(),
              EqualsProto(*result->MessageOrDie()));
}

TEST(ActivationBindHelperTest, RejectsNullArena) {
  TestMessage message;
  message.set_bool_value(true);

  Activation activation;

  ASSERT_EQ(BindProtoToActivation(&message, /*arena=*/nullptr, &activation),
            absl::InvalidArgumentError(
                "arena must not be null for BindProtoToActivation."));
}

class RecordListPtrFunction final : public CelFunction {
 public:
  explicit RecordListPtrFunction(const CelList** recorded)
      : CelFunction(CelFunctionDescriptor("record_list_ptr",
                                          /*receiver_style=*/false,
                                          {CelValue::Type::kList},
                                          /*is_strict=*/true)),
        recorded_(recorded) {}

  absl::Status Evaluate(absl::Span<const CelValue> arguments, CelValue* result,
                        google::protobuf::Arena*) const override {
    *recorded_ = arguments[0].ListOrDie();
    *result = CelValue::CreateInt64(arguments[0].ListOrDie()->size());
    return absl::OkStatus();
  }

 private:
  const CelList** recorded_;
};

absl::StatusOr<std::unique_ptr<CelExpression>> CompileExpr(
    CelExpressionBuilder& builder, absl::string_view expr) {
  CEL_ASSIGN_OR_RETURN(auto parsed, parser::Parse(expr));
  return builder.CreateExpression(&parsed.expr(), &parsed.source_info());
}

TEST(ActivationBindHelperTest, RepeatedFieldsBorrowElementsAndEvaluateCleanly) {
  TestMessage heap_message;
  heap_message.add_int32_list(3);
  heap_message.add_int32_list(7);
  heap_message.add_string_list(std::string(64, 'a'));
  heap_message.add_string_list(std::string(64, 'b'));
  TestMessage* nested0 = heap_message.add_message_list();
  nested0->set_int32_value(1);
  nested0->set_string_value(std::string(64, 'x'));
  TestMessage* nested1 = heap_message.add_message_list();
  nested1->set_int32_value(7);
  nested1->set_string_value(std::string(64, 'y'));

  google::protobuf::Arena source_arena;
  auto* arena_message = google::protobuf::Arena::Create<TestMessage>(&source_arena);
  arena_message->CopyFrom(heap_message);

  const CelList* recorded_list = nullptr;
  InterpreterOptions options;
  std::unique_ptr<CelExpressionBuilder> builder =
      CreateCelExpressionBuilder(options);
  ASSERT_THAT(RegisterBuiltinFunctions(builder->GetRegistry(), options),
              IsOk());
  ASSERT_THAT(builder->GetRegistry()->Register(
                  std::make_unique<RecordListPtrFunction>(&recorded_list)),
              IsOk());

  ASSERT_OK_AND_ASSIGN(
      auto exists_expr,
      CompileExpr(*builder,
                  "message_list.exists(m, m.int32_value == 7 && "
                  "m.string_value == '" +
                      std::string(64, 'y') +
                      "') && string_list.exists(s, s == '" +
                      std::string(64, 'b') + "')"));
  ASSERT_OK_AND_ASSIGN(auto index_expr,
                       CompileExpr(*builder, "message_list[1]"));
  ASSERT_OK_AND_ASSIGN(
      auto contains_expr,
      CompileExpr(*builder, "7 in int32_list && '" + std::string(64, 'a') +
                                "' in string_list"));
  ASSERT_OK_AND_ASSIGN(auto legacy_fn_expr,
                       CompileExpr(*builder, "record_list_ptr(message_list)"));

  for (const TestMessage* msg : {&heap_message, arena_message}) {
    google::protobuf::Arena eval_arena;
    Activation activation;
    ASSERT_THAT(BindProtoToActivation(msg, &eval_arena, &activation), IsOk());

    auto bound_msg_list = activation.FindValue("message_list", &eval_arena);
    ASSERT_TRUE(bound_msg_list.has_value());
    ASSERT_TRUE(bound_msg_list->IsList());

    // First evaluation may initialize steady-state frame scratch; subsequent
    // evaluations over borrowed repeated messages/strings allocate zero arena
    // memory for elements.
    ASSERT_OK_AND_ASSIGN(CelValue exists_val,
                         exists_expr->Evaluate(activation, &eval_arena));
    ASSERT_TRUE(exists_val.IsBool());
    EXPECT_TRUE(exists_val.BoolOrDie());

    google::protobuf::Arena iter_arena;
    ASSERT_OK_AND_ASSIGN(exists_val,
                         exists_expr->Evaluate(activation, &iter_arena));
    EXPECT_TRUE(exists_val.BoolOrDie());
    EXPECT_EQ(iter_arena.SpaceUsed(), 0);

    // Indexing returns a legacy MessageWrapper pointing directly to the
    // backing repeated submessage.
    ASSERT_OK_AND_ASSIGN(CelValue indexed_val,
                         index_expr->Evaluate(activation, &eval_arena));
    ASSERT_TRUE(indexed_val.IsMessage());
    EXPECT_EQ(indexed_val.MessageOrDie(), &msg->message_list(1));

    // Membership checks succeed without copying onto a fresh evaluation arena.
    google::protobuf::Arena contains_arena;
    ASSERT_OK_AND_ASSIGN(CelValue contains_val,
                         contains_expr->Evaluate(activation, &contains_arena));
    ASSERT_TRUE(contains_val.IsBool());
    EXPECT_TRUE(contains_val.BoolOrDie());
    EXPECT_EQ(contains_arena.SpaceUsed(), 0);

    // Passing a bound repeated field to a legacy CelFunction preserves the
    // exact CelList pointer with no re-wrapping.
    recorded_list = nullptr;
    ASSERT_OK_AND_ASSIGN(CelValue fn_val,
                         legacy_fn_expr->Evaluate(activation, &eval_arena));
    ASSERT_TRUE(fn_val.IsInt64());
    EXPECT_EQ(fn_val.Int64OrDie(), 2);
    EXPECT_EQ(recorded_list, bound_msg_list->ListOrDie());
  }
}

}  // namespace

}  // namespace runtime
}  // namespace expr
}  // namespace api
}  // namespace google
