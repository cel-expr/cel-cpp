#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cel/expr/syntax.pb.h"
#include "google/protobuf/struct.pb.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "common/function_descriptor.h"
#include "common/kind.h"
#include "common/value.h"
#include "eval/testutil/test_message.pb.h"
#include "extensions/protobuf/runtime_adapter.h"
#include "internal/testing.h"
#include "runtime/activation.h"
#include "runtime/function.h"
#include "runtime/runtime_options.h"
#include "runtime/standard_runtime_builder_factory.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"
#include "google/protobuf/text_format.h"

namespace google {
namespace api {
namespace expr {
namespace runtime {

namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::cel::Activation;
using ::cel::BoolValue;
using ::cel::CreateStandardRuntimeBuilder;
using ::cel::ErrorValue;
using ::cel::Function;
using ::cel::FunctionDescriptor;
using ::cel::IntValue;
using ::cel::Kind;
using ::cel::NullValue;
using ::cel::RuntimeOptions;
using ::cel::Value;
using ::cel::extensions::ProtobufRuntimeAdapter;
using ::cel::expr::Expr;
using ::cel::expr::SourceInfo;
using ::google::protobuf::Arena;
using ::google::protobuf::TextFormat;

// Simple one parameter function that records the message argument it receives.
class RecordArgFunction : public Function {
 public:
  explicit RecordArgFunction(std::vector<Value>* output) : output_(*output) {}

  absl::StatusOr<Value> Invoke(absl::Span<const Value> args,
                               const InvokeContext& context) const override {
    if (args.size() != 1) {
      return absl::Status(absl::StatusCode::kInvalidArgument,
                          "Bad arguments number");
    }
    output_.push_back(args.at(0));
    return BoolValue(true);
  }

  std::vector<Value>& output_;
};

// Simple end-to-end test, which also serves as usage example.
TEST(EndToEndTest, SimpleOnePlusOne) {
  // AST CEL equivalent of "1+var"
  constexpr char kExpr0[] = R"(
    call_expr: <
      function: "_+_"
      args: <
        ident_expr: <
          name: "var"
        >
      >
      args: <
        const_expr: <
          int64_value: 1
        >
      >
    >
  )";

  Expr expr;
  SourceInfo source_info;
  ASSERT_TRUE(TextFormat::ParseFromString(kExpr0, &expr));

  // Obtain CEL Runtime builder.
  ASSERT_OK_AND_ASSIGN(
      auto builder,
      CreateStandardRuntimeBuilder(google::protobuf::DescriptorPool::generated_pool(),
                                   RuntimeOptions{}));
  ASSERT_OK_AND_ASSIGN(auto runtime, std::move(builder).Build());

  // Create Program from AST (Expr object).
  ASSERT_OK_AND_ASSIGN(auto program, ProtobufRuntimeAdapter::CreateProgram(
                                         *runtime, expr, &source_info));
  Activation activation;

  // Bind value to "var" parameter.
  activation.InsertOrAssignValue("var", IntValue(1));

  Arena arena;

  // Run evaluation.
  ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));
  ASSERT_TRUE(result.IsInt());
  EXPECT_EQ(result.GetInt().NativeValue(), 2);
}

// Simple end-to-end test, which also serves as usage example.
TEST(EndToEndTest, EmptyStringCompare) {
  // AST CEL equivalent of "var.string_value == '' && var.int64_value == 0"
  constexpr char kExpr0[] = R"(
    call_expr: <
      function: "_&&_"
      args: <
        call_expr: <
          function: "_==_"
          args: <
            select_expr: <
              operand: <
                ident_expr: <
                  name: "var"
                >
              >
              field: "string_value"
            >
          >
          args: <
            const_expr: <
              string_value: ""
            >
          >
        >
      >
      args: <
        call_expr: <
          function: "_==_"
          args: <
            select_expr: <
              operand: <
                ident_expr: <
                  name: "var"
                >
              >
              field: "int64_value"
            >
          >
          args: <
            const_expr: <
              int64_value: 0
            >
          >
        >
      >
    >
  )";

  Expr expr;
  SourceInfo source_info;
  ASSERT_TRUE(TextFormat::ParseFromString(kExpr0, &expr));

  // Obtain CEL Runtime builder.
  ASSERT_OK_AND_ASSIGN(
      auto builder,
      CreateStandardRuntimeBuilder(google::protobuf::DescriptorPool::generated_pool(),
                                   RuntimeOptions{}));
  ASSERT_OK_AND_ASSIGN(auto runtime, std::move(builder).Build());

  // Create Program from AST (Expr object).
  ASSERT_OK_AND_ASSIGN(auto program, ProtobufRuntimeAdapter::CreateProgram(
                                         *runtime, expr, &source_info));
  Activation activation;

  // Bind value to "var" parameter.
  constexpr char kData[] = R"(
    string_value: ""
    int64_value: 0
  )";
  TestMessage data;
  ASSERT_TRUE(TextFormat::ParseFromString(kData, &data));
  Arena arena;
  activation.InsertOrAssignValue(
      "var",
      Value::WrapMessage(&data, google::protobuf::DescriptorPool::generated_pool(),
                         google::protobuf::MessageFactory::generated_factory(), &arena));

  // Run evaluation.
  ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));
  ASSERT_TRUE(result.IsBool());
  EXPECT_TRUE(result.GetBool().NativeValue());
}

TEST(EndToEndTest, NullLiteral) {
  // AST CEL equivalent of "Value{null_value: NullValue.NULL_VALUE}"
  constexpr char kExpr0[] = R"(
    struct_expr: <
      message_name: "Value"
      entries: <
        field_key: "null_value"
        value: <
          select_expr: <
            operand: <
              ident_expr: <
                name: "NullValue"
              >
            >
            field: "NULL_VALUE"
          >
        >
      >
    >
  )";

  Expr expr;
  SourceInfo source_info;
  ASSERT_TRUE(TextFormat::ParseFromString(kExpr0, &expr));

  // Obtain CEL Runtime builder.
  RuntimeOptions options;
  options.container = "google.protobuf";
  ASSERT_OK_AND_ASSIGN(auto builder,
                       CreateStandardRuntimeBuilder(
                           google::protobuf::DescriptorPool::generated_pool(), options));
  ASSERT_OK_AND_ASSIGN(auto runtime, std::move(builder).Build());

  // Create Program from AST (Expr object).
  ASSERT_OK_AND_ASSIGN(auto program, ProtobufRuntimeAdapter::CreateProgram(
                                         *runtime, expr, &source_info));
  Activation activation;
  Arena arena;
  // Run evaluation.
  ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));
  ASSERT_TRUE(result.IsNull());
}

// Equivalent to 'RecordArg(test_message)'
constexpr char kNullMessageHandlingExpr[] = R"pb(
  id: 1
  call_expr: <
    function: "RecordArg"
    args: <
      ident_expr: < name: "test_message" >
      id: 2
    >
  >
)pb";

TEST(EndToEndTest, StrictNullHandling) {
  RuntimeOptions options;

  Expr expr;
  ASSERT_TRUE(
      google::protobuf::TextFormat::ParseFromString(kNullMessageHandlingExpr, &expr));
  SourceInfo info;

  ASSERT_OK_AND_ASSIGN(auto builder,
                       CreateStandardRuntimeBuilder(
                           google::protobuf::DescriptorPool::generated_pool(), options));
  std::vector<Value> extension_calls;
  ASSERT_THAT(builder.function_registry().Register(
                  FunctionDescriptor("RecordArg", false, {Kind::kStruct}),
                  std::make_unique<RecordArgFunction>(&extension_calls)),
              IsOk());
  ASSERT_OK_AND_ASSIGN(auto runtime, std::move(builder).Build());

  ASSERT_OK_AND_ASSIGN(auto program, ProtobufRuntimeAdapter::CreateProgram(
                                         *runtime, expr, &info));

  Activation activation;
  google::protobuf::Arena arena;
  activation.InsertOrAssignValue("test_message", NullValue());

  ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));
  ASSERT_TRUE(result.Is<ErrorValue>()) << result.DebugString();
  EXPECT_THAT(result.Get<ErrorValue>().NativeValue(),
              StatusIs(absl::StatusCode::kUnknown,
                       testing::HasSubstr("No matching overloads")));
}

TEST(EndToEndTest, OutOfRangeDurationConstant) {
  RuntimeOptions options;
  options.enable_timestamp_duration_overflow_errors = true;

  Expr expr;
  // Duration representable in absl::Duration, but out of range for CelValue
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"(
          call_expr {
          function: "type"
          args {
            const_expr {
              duration_value {
                seconds: 28552639587287040
              }
            }
          }
        })",
      &expr));
  SourceInfo info;

  ASSERT_OK_AND_ASSIGN(auto builder,
                       CreateStandardRuntimeBuilder(
                           google::protobuf::DescriptorPool::generated_pool(), options));
  ASSERT_OK_AND_ASSIGN(auto runtime, std::move(builder).Build());

  ASSERT_OK_AND_ASSIGN(auto program, ProtobufRuntimeAdapter::CreateProgram(
                                         *runtime, expr, &info));

  Activation activation;
  google::protobuf::Arena arena;

  ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));
  ASSERT_TRUE(result.Is<ErrorValue>()) << result.DebugString();
  EXPECT_THAT(result.Get<ErrorValue>().NativeValue(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       testing::HasSubstr("Duration is out of range")));
}

}  // namespace

}  // namespace runtime
}  // namespace expr
}  // namespace api
}  // namespace google
