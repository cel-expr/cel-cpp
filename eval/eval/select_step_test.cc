#include "eval/eval/select_step.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cel/expr/syntax.pb.h"
#include "google/protobuf/wrappers.pb.h"
#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "base/attribute.h"
#include "base/attribute_set.h"
#include "base/type_provider.h"
#include "common/expr.h"
#include "common/legacy_value.h"
#include "common/type.h"
#include "eval/eval/cel_expression_flat_impl.h"
#include "eval/eval/evaluator_core.h"
#include "eval/public/activation.h"
#include "eval/public/cel_attribute.h"
#include "eval/public/cel_value.h"
#include "eval/public/containers/container_backed_map_impl.h"
#include "eval/public/structs/cel_proto_wrapper.h"
#include "eval/public/structs/trivial_legacy_type_info.h"
#include "eval/public/unknown_attribute_set.h"
#include "eval/public/unknown_set.h"
#include "eval/testutil/test_extensions.pb.h"
#include "eval/testutil/test_message.pb.h"
#include "internal/proto_matchers.h"
#include "internal/status_macros.h"
#include "internal/testing.h"
#include "runtime/internal/runtime_env.h"
#include "runtime/internal/runtime_env_testing.h"
#include "runtime/runtime_options.h"
#include "cel/expr/conformance/proto3/test_all_types.pb.h"
#include "google/protobuf/descriptor.h"

namespace google::api::expr::runtime {

namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::cel::Attribute;
using ::cel::AttributeSet;
using ::cel::Expr;
using ::cel::RuntimeOptions;
using ::cel::TypeProvider;
using ::cel::expr::conformance::proto3::TestAllTypes;
using ::cel::internal::test::EqualsProto;
using ::cel::runtime_internal::NewTestingRuntimeEnv;
using ::cel::runtime_internal::RuntimeEnv;
using ::testing::Eq;
using ::testing::HasSubstr;

struct RunExpressionOptions {
  bool enable_unknowns = false;
  bool enable_wrapper_type_null_unboxing = false;
};

class SelectStepTest : public testing::Test {
 public:
  SelectStepTest() : env_(NewTestingRuntimeEnv()) {}
  // Helper method. Creates simple pipeline containing Select step and runs it.
  absl::StatusOr<CelValue> RunExpression(const CelValue target,
                                         absl::string_view field, bool test,
                                         RunExpressionOptions options) {
    ExecutionPath path;

    Expr expr;
    auto& select = expr.mutable_select_expr();
    select.set_field(std::string(field));
    select.set_test_only(test);
    Expr& expr0 = select.mutable_operand();

    auto& ident = expr0.mutable_ident_expr();
    ident.set_name("target");
    CEL_ASSIGN_OR_RETURN(
        auto step1,
        CreateSelectStep(select.field(), select.test_only(),
                         options.enable_wrapper_type_null_unboxing));

    path.push_back(
        ExpressionStep::MakeIdentifierStep(ident.name(), expr0.id()));
    path.push_back(
        ExpressionStep::MakeGenericStep(std::move(step1), expr.id()));

    cel::RuntimeOptions runtime_options;
    if (options.enable_unknowns) {
      runtime_options.unknown_processing =
          cel::UnknownProcessingOptions::kAttributeOnly;
    }
    // Force the creation of a message factory at the env level.
    static_cast<void>(env_->MutableMessageFactory());
    CelExpressionFlatImpl cel_expr(
        env_, FlatExpression(std::move(path), /*comprehension_slot_count=*/0,
                             env_->type_registry.GetComposedTypeProvider(),
                             runtime_options));
    Activation activation;
    activation.InsertValue("target", target);

    return cel_expr.Evaluate(activation, &arena_);
  }

  absl::StatusOr<CelValue> RunExpression(const TestExtensions* message,
                                         absl::string_view field, bool test,
                                         RunExpressionOptions options) {
    return RunExpression(CelProtoWrapper::CreateMessage(message, &arena_),
                         field, test, options);
  }

  absl::StatusOr<CelValue> RunExpression(const TestMessage* message,
                                         absl::string_view field, bool test,
                                         RunExpressionOptions options) {
    return RunExpression(CelProtoWrapper::CreateMessage(message, &arena_),
                         field, test, options);
  }

  absl::StatusOr<CelValue> RunExpression(const CelMap* map_value,
                                         absl::string_view field, bool test,
                                         RunExpressionOptions options) {
    return RunExpression(CelValue::CreateMap(map_value), field, test, options);
  }

 protected:
  absl_nonnull std::shared_ptr<const RuntimeEnv> env_;
  google::protobuf::Arena arena_;
};

class SelectStepConformanceTest : public SelectStepTest,
                                  public testing::WithParamInterface<bool> {};

TEST_P(SelectStepConformanceTest, SelectMessageIsNull) {
  RunExpressionOptions options;
  options.enable_unknowns = GetParam();

  ASSERT_OK_AND_ASSIGN(CelValue result,
                       RunExpression(static_cast<const TestMessage*>(nullptr),
                                     "bool_value", true, options));

  ASSERT_TRUE(result.IsError());
}

TEST_P(SelectStepConformanceTest, SelectTargetNotStructOrMap) {
  RunExpressionOptions options;
  options.enable_unknowns = GetParam();

  ASSERT_OK_AND_ASSIGN(
      CelValue result,
      RunExpression(CelValue::CreateStringView("some_value"), "some_field",
                    /*test=*/false, options));

  ASSERT_TRUE(result.IsError());
  EXPECT_THAT(*result.ErrorOrDie(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Applying SELECT to non-message type")));
}

TEST_P(SelectStepConformanceTest, PresenseIsFalseTest) {
  TestMessage message;
  RunExpressionOptions options;
  options.enable_unknowns = GetParam();

  ASSERT_OK_AND_ASSIGN(CelValue result,
                       RunExpression(&message, "bool_value", true, options));

  ASSERT_TRUE(result.IsBool());
  EXPECT_EQ(result.BoolOrDie(), false);
}

TEST_P(SelectStepConformanceTest, PresenseIsTrueTest) {
  RunExpressionOptions options;
  options.enable_unknowns = GetParam();
  TestMessage message;
  message.set_bool_value(true);

  ASSERT_OK_AND_ASSIGN(CelValue result,
                       RunExpression(&message, "bool_value", true, options));
  ASSERT_TRUE(result.IsBool());
  EXPECT_EQ(result.BoolOrDie(), true);
}

TEST_P(SelectStepConformanceTest, ExtensionsPresenceIsTrueTest) {
  TestExtensions exts;
  TestExtensions* nested = exts.MutableExtension(nested_ext);
  nested->set_name("nested");
  RunExpressionOptions options;
  options.enable_unknowns = GetParam();

  ASSERT_OK_AND_ASSIGN(
      CelValue result,
      RunExpression(&exts, "google.api.expr.runtime.nested_ext", true,
                    options));

  ASSERT_TRUE(result.IsBool());
  EXPECT_TRUE(result.BoolOrDie());
}

TEST_P(SelectStepConformanceTest, ExtensionsPresenceIsFalseTest) {
  TestExtensions exts;
  RunExpressionOptions options;
  options.enable_unknowns = GetParam();

  ASSERT_OK_AND_ASSIGN(
      CelValue result,
      RunExpression(&exts, "google.api.expr.runtime.nested_ext", true,
                    options));

  ASSERT_TRUE(result.IsBool());
  EXPECT_FALSE(result.BoolOrDie());
}

TEST_P(SelectStepConformanceTest, MapPresenseIsFalseTest) {
  RunExpressionOptions options;
  options.enable_unknowns = GetParam();
  std::string key1 = "key1";
  std::vector<std::pair<CelValue, CelValue>> key_values{
      {CelValue::CreateString(&key1), CelValue::CreateInt64(1)}};

  auto map_value = CreateContainerBackedMap(
                       absl::Span<std::pair<CelValue, CelValue>>(key_values))
                       .value();

  ASSERT_OK_AND_ASSIGN(CelValue result,
                       RunExpression(map_value.get(), "key2", true, options));
  ASSERT_TRUE(result.IsBool());
  EXPECT_EQ(result.BoolOrDie(), false);
}

TEST_P(SelectStepConformanceTest, MapPresenseIsTrueTest) {
  RunExpressionOptions options;
  options.enable_unknowns = GetParam();
  std::string key1 = "key1";
  std::vector<std::pair<CelValue, CelValue>> key_values{
      {CelValue::CreateString(&key1), CelValue::CreateInt64(1)}};

  auto map_value = CreateContainerBackedMap(
                       absl::Span<std::pair<CelValue, CelValue>>(key_values))
                       .value();

  ASSERT_OK_AND_ASSIGN(CelValue result,
                       RunExpression(map_value.get(), "key1", true, options));

  ASSERT_TRUE(result.IsBool());
  EXPECT_EQ(result.BoolOrDie(), true);
}

TEST_F(SelectStepTest, MapPresenseIsErrorTest) {
  TestMessage message;

  Expr select_expr;
  auto& select = select_expr.mutable_select_expr();
  select.set_field("1");
  select.set_test_only(true);
  Expr& expr1 = select.mutable_operand();
  auto& select_map = expr1.mutable_select_expr();
  select_map.set_field("int32_int32_map");
  Expr& expr0 = select_map.mutable_operand();
  auto& ident = expr0.mutable_ident_expr();
  ident.set_name("target");
  ASSERT_OK_AND_ASSIGN(
      auto step1,
      CreateSelectStep(select_map.field(), select_map.test_only(),
                       /*enable_wrapper_type_null_unboxing=*/false));
  ASSERT_OK_AND_ASSIGN(
      auto step2,
      CreateSelectStep(select.field(), select.test_only(),
                       /*enable_wrapper_type_null_unboxing=*/false));

  ExecutionPath path;
  path.push_back(ExpressionStep::MakeIdentifierStep(ident.name(), expr0.id()));
  path.push_back(ExpressionStep::MakeGenericStep(std::move(step1), expr1.id()));
  path.push_back(
      ExpressionStep::MakeGenericStep(std::move(step2), select_expr.id()));
  CelExpressionFlatImpl cel_expr(
      env_, FlatExpression(std::move(path), /*comprehension_slot_count=*/0,
                           env_->type_registry.GetComposedTypeProvider(),
                           cel::RuntimeOptions{}));
  Activation activation;
  activation.InsertValue("target",
                         CelProtoWrapper::CreateMessage(&message, &arena_));

  ASSERT_OK_AND_ASSIGN(CelValue result, cel_expr.Evaluate(activation, &arena_));
  ASSERT_TRUE(result.IsError());
  EXPECT_EQ(result.ErrorOrDie()->code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(SelectStepTest, MapPresenseIsTrueWithUnknownTest) {
  UnknownSet unknown_set;
  std::string key1 = "key1";
  std::vector<std::pair<CelValue, CelValue>> key_values{
      {CelValue::CreateString(&key1),
       CelValue::CreateUnknownSet(&unknown_set)}};

  auto map_value = CreateContainerBackedMap(
                       absl::Span<std::pair<CelValue, CelValue>>(key_values))
                       .value();

  RunExpressionOptions options;
  options.enable_unknowns = true;

  ASSERT_OK_AND_ASSIGN(CelValue result,
                       RunExpression(map_value.get(), "key1", true, options));
  ASSERT_TRUE(result.IsBool());
  EXPECT_EQ(result.BoolOrDie(), true);
}

TEST_P(SelectStepConformanceTest, FieldIsNotPresentInProtoTest) {
  TestMessage message;

  RunExpressionOptions options;
  options.enable_unknowns = GetParam();
  ASSERT_OK_AND_ASSIGN(CelValue result,
                       RunExpression(&message, "fake_field", false, options));
  ASSERT_TRUE(result.IsError());
  EXPECT_THAT(result.ErrorOrDie()->code(), Eq(absl::StatusCode::kNotFound));
}

TEST_P(SelectStepConformanceTest, FieldIsNotSetTest) {
  TestMessage message;
  RunExpressionOptions options;
  options.enable_unknowns = GetParam();

  ASSERT_OK_AND_ASSIGN(CelValue result,
                       RunExpression(&message, "bool_value", false, options));

  ASSERT_TRUE(result.IsBool());
  EXPECT_EQ(result.BoolOrDie(), false);
}

TEST_P(SelectStepConformanceTest, SimpleBoolTest) {
  TestMessage message;
  message.set_bool_value(true);
  RunExpressionOptions options;
  options.enable_unknowns = GetParam();

  ASSERT_OK_AND_ASSIGN(CelValue result,
                       RunExpression(&message, "bool_value", false, options));

  ASSERT_TRUE(result.IsBool());
  EXPECT_EQ(result.BoolOrDie(), true);
}

TEST_P(SelectStepConformanceTest, SimpleInt32Test) {
  TestMessage message;
  message.set_int32_value(1);
  RunExpressionOptions options;
  options.enable_unknowns = GetParam();

  ASSERT_OK_AND_ASSIGN(CelValue result,
                       RunExpression(&message, "int32_value", false, options));

  ASSERT_TRUE(result.IsInt64());
  EXPECT_EQ(result.Int64OrDie(), 1);
}

TEST_P(SelectStepConformanceTest, SimpleInt64Test) {
  TestMessage message;
  message.set_int64_value(1);
  RunExpressionOptions options;
  options.enable_unknowns = GetParam();

  ASSERT_OK_AND_ASSIGN(CelValue result,
                       RunExpression(&message, "int64_value", false, options));
  ASSERT_TRUE(result.IsInt64());
  EXPECT_EQ(result.Int64OrDie(), 1);
}

TEST_P(SelectStepConformanceTest, SimpleUInt32Test) {
  TestMessage message;
  message.set_uint32_value(1);
  RunExpressionOptions options;
  options.enable_unknowns = GetParam();

  ASSERT_OK_AND_ASSIGN(CelValue result,
                       RunExpression(&message, "uint32_value", false, options));

  ASSERT_TRUE(result.IsUint64());
  EXPECT_EQ(result.Uint64OrDie(), 1);
}

TEST_P(SelectStepConformanceTest, SimpleUint64Test) {
  TestMessage message;
  message.set_uint64_value(1);
  RunExpressionOptions options;
  options.enable_unknowns = GetParam();

  ASSERT_OK_AND_ASSIGN(CelValue result,
                       RunExpression(&message, "uint64_value", false, options));

  ASSERT_TRUE(result.IsUint64());
  EXPECT_EQ(result.Uint64OrDie(), 1);
}

TEST_P(SelectStepConformanceTest, SimpleStringTest) {
  TestMessage message;
  std::string value = "test";
  message.set_string_value(value);
  RunExpressionOptions options;
  options.enable_unknowns = GetParam();

  ASSERT_OK_AND_ASSIGN(CelValue result,
                       RunExpression(&message, "string_value", false, options));

  ASSERT_TRUE(result.IsString());
  EXPECT_EQ(result.StringOrDie().value(), "test");
}

TEST_P(SelectStepConformanceTest, WrapperTypeNullUnboxingEnabledTest) {
  TestMessage message;
  message.mutable_string_wrapper_value()->set_value("test");
  RunExpressionOptions options;
  options.enable_unknowns = GetParam();
  options.enable_wrapper_type_null_unboxing = true;

  ASSERT_OK_AND_ASSIGN(
      CelValue result,
      RunExpression(&message, "string_wrapper_value", false, options));

  ASSERT_TRUE(result.IsString());
  EXPECT_EQ(result.StringOrDie().value(), "test");
  ASSERT_OK_AND_ASSIGN(
      result, RunExpression(&message, "int32_wrapper_value", false, options));
  EXPECT_TRUE(result.IsNull());
}

TEST_P(SelectStepConformanceTest, WrapperTypeNullUnboxingDisabledTest) {
  TestMessage message;
  message.mutable_string_wrapper_value()->set_value("test");
  RunExpressionOptions options;
  options.enable_unknowns = GetParam();
  options.enable_wrapper_type_null_unboxing = false;

  ASSERT_OK_AND_ASSIGN(
      CelValue result,
      RunExpression(&message, "string_wrapper_value", false, options));

  ASSERT_TRUE(result.IsString());
  EXPECT_EQ(result.StringOrDie().value(), "test");
  ASSERT_OK_AND_ASSIGN(
      result, RunExpression(&message, "int32_wrapper_value", false, options));
  EXPECT_TRUE(result.IsInt64());
}

TEST_P(SelectStepConformanceTest, SimpleBytesTest) {
  TestMessage message;
  std::string value = "test";
  message.set_bytes_value(value);
  RunExpressionOptions options;
  options.enable_unknowns = GetParam();

  ASSERT_OK_AND_ASSIGN(CelValue result,
                       RunExpression(&message, "bytes_value", false, options));

  ASSERT_TRUE(result.IsBytes());
  EXPECT_EQ(result.BytesOrDie().value(), "test");
}

TEST_P(SelectStepConformanceTest, SimpleMessageTest) {
  TestMessage message;
  TestMessage* message2 = message.mutable_message_value();
  message2->set_int32_value(1);
  message2->set_string_value("test");
  RunExpressionOptions options;
  options.enable_unknowns = GetParam();

  ASSERT_OK_AND_ASSIGN(CelValue result, RunExpression(&message, "message_value",
                                                      false, options));

  ASSERT_TRUE(result.IsMessage());
  EXPECT_THAT(*message2, EqualsProto(*result.MessageOrDie()));
}

TEST_P(SelectStepConformanceTest, GlobalExtensionsIntTest) {
  TestExtensions exts;
  exts.SetExtension(int32_ext, 42);
  RunExpressionOptions options;
  options.enable_unknowns = GetParam();

  ASSERT_OK_AND_ASSIGN(CelValue result,
                       RunExpression(&exts, "google.api.expr.runtime.int32_ext",
                                     false, options));

  ASSERT_TRUE(result.IsInt64());
  EXPECT_EQ(result.Int64OrDie(), 42L);
}

TEST_P(SelectStepConformanceTest, GlobalExtensionsMessageTest) {
  TestExtensions exts;
  TestExtensions* nested = exts.MutableExtension(nested_ext);
  nested->set_name("nested");
  RunExpressionOptions options;
  options.enable_unknowns = GetParam();

  ASSERT_OK_AND_ASSIGN(
      CelValue result,
      RunExpression(&exts, "google.api.expr.runtime.nested_ext", false,
                    options));

  ASSERT_TRUE(result.IsMessage());
  EXPECT_THAT(result.MessageOrDie(), Eq(nested));
}

TEST_P(SelectStepConformanceTest, GlobalExtensionsMessageUnsetTest) {
  // Implementation details:
  // The test environment is a dynamic descriptor pool with the same definition
  // as the linked proto.
  // Use a dynamic message with the expected factory and pool so we can expect
  // the identity of the default message.
  const google::protobuf::Descriptor* descriptor =
      env_->descriptor_pool->FindMessageTypeByName(
          TestExtensions::descriptor()->full_name());
  ASSERT_NE(descriptor, nullptr);
  const google::protobuf::FieldDescriptor* field =
      env_->descriptor_pool->FindExtensionByPrintableName(
          descriptor, "google.api.expr.runtime.nested_ext");
  ASSERT_NE(field, nullptr);
  ASSERT_TRUE(field->containing_type() == descriptor);
  const auto* prototype =
      env_->MutableMessageFactory()->GetPrototype(descriptor);
  ASSERT_NE(prototype, nullptr);
  const auto* msg_default = &prototype->GetReflection()->GetMessage(
      *prototype, field, env_->MutableMessageFactory());

  RunExpressionOptions options;
  options.enable_unknowns = GetParam();

  ASSERT_OK_AND_ASSIGN(
      CelValue result,
      RunExpression(CelProtoWrapper::CreateMessage(prototype, &arena_),
                    "google.api.expr.runtime.nested_ext", false, options));

  ASSERT_TRUE(result.IsMessage());
  EXPECT_THAT(result.MessageOrDie(), Eq(msg_default));
}

TEST_P(SelectStepConformanceTest, GlobalExtensionsWrapperTest) {
  TestExtensions exts;
  google::protobuf::Int32Value* wrapper =
      exts.MutableExtension(int32_wrapper_ext);
  wrapper->set_value(42);
  RunExpressionOptions options;
  options.enable_unknowns = GetParam();

  ASSERT_OK_AND_ASSIGN(
      CelValue result,
      RunExpression(&exts, "google.api.expr.runtime.int32_wrapper_ext", false,
                    options));

  ASSERT_TRUE(result.IsInt64());
  EXPECT_THAT(result.Int64OrDie(), Eq(42L));
}

TEST_P(SelectStepConformanceTest, GlobalExtensionsWrapperUnsetTest) {
  TestExtensions exts;
  RunExpressionOptions options;
  options.enable_wrapper_type_null_unboxing = true;
  options.enable_unknowns = GetParam();

  ASSERT_OK_AND_ASSIGN(
      CelValue result,
      RunExpression(&exts, "google.api.expr.runtime.int32_wrapper_ext", false,
                    options));

  ASSERT_TRUE(result.IsNull());
}

TEST_P(SelectStepConformanceTest, MessageExtensionsEnumTest) {
  TestExtensions exts;
  exts.SetExtension(TestMessageExtensions::enum_ext, TestExtEnum::TEST_EXT_1);
  RunExpressionOptions options;
  options.enable_unknowns = GetParam();

  ASSERT_OK_AND_ASSIGN(
      CelValue result,
      RunExpression(&exts,
                    "google.api.expr.runtime.TestMessageExtensions.enum_ext",
                    false, options));

  ASSERT_TRUE(result.IsInt64());
  EXPECT_THAT(result.Int64OrDie(), Eq(TestExtEnum::TEST_EXT_1));
}

TEST_P(SelectStepConformanceTest, MessageExtensionsRepeatedStringTest) {
  TestExtensions exts;
  exts.AddExtension(TestMessageExtensions::repeated_string_exts, "test1");
  exts.AddExtension(TestMessageExtensions::repeated_string_exts, "test2");
  RunExpressionOptions options;
  options.enable_unknowns = GetParam();

  ASSERT_OK_AND_ASSIGN(
      CelValue result,
      RunExpression(
          &exts,
          "google.api.expr.runtime.TestMessageExtensions.repeated_string_exts",
          false, options));

  ASSERT_TRUE(result.IsList());
  const CelList* cel_list = result.ListOrDie();
  EXPECT_THAT(cel_list->size(), Eq(2));
}

TEST_P(SelectStepConformanceTest, MessageExtensionsRepeatedStringUnsetTest) {
  TestExtensions exts;
  RunExpressionOptions options;
  options.enable_unknowns = GetParam();

  ASSERT_OK_AND_ASSIGN(
      CelValue result,
      RunExpression(
          &exts,
          "google.api.expr.runtime.TestMessageExtensions.repeated_string_exts",
          false, options));

  ASSERT_TRUE(result.IsList());
  const CelList* cel_list = result.ListOrDie();
  EXPECT_THAT(cel_list->size(), Eq(0));
}

TEST_P(SelectStepConformanceTest, NullMessageAccessor) {
  TestMessage message;
  TestMessage* message2 = message.mutable_message_value();
  message2->set_int32_value(1);
  message2->set_string_value("test");
  RunExpressionOptions options;
  options.enable_unknowns = GetParam();
  CelValue value = CelValue::CreateMessageWrapper(
      CelValue::MessageWrapper(&message, TrivialTypeInfo::GetInstance()));

  ASSERT_OK_AND_ASSIGN(CelValue result, RunExpression(value, "message_value",
                                                      /*test=*/false, options));

  ASSERT_TRUE(result.IsError());
  EXPECT_THAT(*result.ErrorOrDie(), StatusIs(absl::StatusCode::kNotFound));

  // same for has
  ASSERT_OK_AND_ASSIGN(result, RunExpression(value, "message_value",
                                             /*test=*/true, options));

  ASSERT_TRUE(result.IsError());
  EXPECT_THAT(*result.ErrorOrDie(), StatusIs(absl::StatusCode::kNotFound));
}

TEST_P(SelectStepConformanceTest, SimpleEnumTest) {
  TestMessage message;
  message.set_enum_value(TestMessage::TEST_ENUM_1);
  RunExpressionOptions options;
  options.enable_unknowns = GetParam();

  ASSERT_OK_AND_ASSIGN(CelValue result,
                       RunExpression(&message, "enum_value", false, options));

  ASSERT_TRUE(result.IsInt64());
  EXPECT_THAT(result.Int64OrDie(), Eq(TestMessage::TEST_ENUM_1));
}

TEST_P(SelectStepConformanceTest, SimpleListTest) {
  TestMessage message;
  message.add_int32_list(1);
  message.add_int32_list(2);
  RunExpressionOptions options;
  options.enable_unknowns = GetParam();

  ASSERT_OK_AND_ASSIGN(CelValue result,
                       RunExpression(&message, "int32_list", false, options));

  ASSERT_TRUE(result.IsList());
  const CelList* cel_list = result.ListOrDie();
  EXPECT_THAT(cel_list->size(), Eq(2));
}

TEST_P(SelectStepConformanceTest, SimpleMapTest) {
  TestMessage message;
  auto map_field = message.mutable_string_int32_map();
  (*map_field)["test0"] = 1;
  (*map_field)["test1"] = 2;
  RunExpressionOptions options;
  options.enable_unknowns = GetParam();

  ASSERT_OK_AND_ASSIGN(
      CelValue result,
      RunExpression(&message, "string_int32_map", false, options));
  ASSERT_TRUE(result.IsMap());

  const CelMap* cel_map = result.MapOrDie();
  EXPECT_THAT(cel_map->size(), Eq(2));
}

TEST_P(SelectStepConformanceTest, MapSimpleInt32Test) {
  std::string key1 = "key1";
  std::string key2 = "key2";
  std::vector<std::pair<CelValue, CelValue>> key_values{
      {CelValue::CreateString(&key1), CelValue::CreateInt64(1)},
      {CelValue::CreateString(&key2), CelValue::CreateInt64(2)}};
  auto map_value = CreateContainerBackedMap(
                       absl::Span<std::pair<CelValue, CelValue>>(key_values))
                       .value();
  RunExpressionOptions options;
  options.enable_unknowns = GetParam();

  ASSERT_OK_AND_ASSIGN(CelValue result,
                       RunExpression(map_value.get(), "key1", false, options));

  ASSERT_TRUE(result.IsInt64());
  EXPECT_EQ(result.Int64OrDie(), 1);
}

// Test Select behavior, when expression to select from is an Error.
TEST_P(SelectStepConformanceTest, CelErrorAsArgument) {
  ExecutionPath path;

  Expr dummy_expr;

  auto& select = dummy_expr.mutable_select_expr();
  select.set_field("position");
  select.set_test_only(false);
  Expr& expr0 = select.mutable_operand();

  auto& ident = expr0.mutable_ident_expr();
  ident.set_name("message");
  ASSERT_OK_AND_ASSIGN(
      auto step1,
      CreateSelectStep(select.field(), select.test_only(),
                       /*enable_wrapper_type_null_unboxing=*/false));

  path.push_back(ExpressionStep::MakeIdentifierStep(ident.name(), expr0.id()));
  path.push_back(
      ExpressionStep::MakeGenericStep(std::move(step1), dummy_expr.id()));

  CelError error = absl::CancelledError();

  cel::RuntimeOptions options;
  if (GetParam()) {
    options.unknown_processing = cel::UnknownProcessingOptions::kAttributeOnly;
  }
  CelExpressionFlatImpl cel_expr(
      env_,
      FlatExpression(std::move(path), /*comprehension_slot_count=*/0,
                     env_->type_registry.GetComposedTypeProvider(), options));
  Activation activation;
  activation.InsertValue("message", CelValue::CreateError(&error));

  ASSERT_OK_AND_ASSIGN(CelValue result, cel_expr.Evaluate(activation, &arena_));
  ASSERT_TRUE(result.IsError());
  EXPECT_THAT(*result.ErrorOrDie(), Eq(error));
}

TEST_F(SelectStepTest, DisableMissingAttributeOK) {
  TestMessage message;
  message.set_bool_value(true);
  ExecutionPath path;

  Expr dummy_expr;

  auto& select = dummy_expr.mutable_select_expr();
  select.set_field("bool_value");
  select.set_test_only(false);
  Expr& expr0 = select.mutable_operand();

  auto& ident = expr0.mutable_ident_expr();
  ident.set_name("message");
  ASSERT_OK_AND_ASSIGN(
      auto step1,
      CreateSelectStep(select.field(), select.test_only(),
                       /*enable_wrapper_type_null_unboxing=*/false));

  path.push_back(ExpressionStep::MakeIdentifierStep(ident.name(), expr0.id()));
  path.push_back(
      ExpressionStep::MakeGenericStep(std::move(step1), dummy_expr.id()));

  CelExpressionFlatImpl cel_expr(
      env_, FlatExpression(std::move(path), /*comprehension_slot_count=*/0,
                           env_->type_registry.GetComposedTypeProvider(),
                           cel::RuntimeOptions{}));
  Activation activation;
  activation.InsertValue("message",
                         CelProtoWrapper::CreateMessage(&message, &arena_));

  ASSERT_OK_AND_ASSIGN(CelValue result, cel_expr.Evaluate(activation, &arena_));
  ASSERT_TRUE(result.IsBool());
  EXPECT_EQ(result.BoolOrDie(), true);

  CelAttributePattern pattern("message", {});
  ASSERT_THAT(activation.SetMissingAttributePatterns({pattern}), IsOk());

  ASSERT_OK_AND_ASSIGN(result, cel_expr.Evaluate(activation, &arena_));
  EXPECT_EQ(result.BoolOrDie(), true);
}

TEST_F(SelectStepTest, UnrecoverableUnknownValueProducesError) {
  TestMessage message;
  message.set_bool_value(true);
  ExecutionPath path;

  Expr dummy_expr;

  auto& select = dummy_expr.mutable_select_expr();
  select.set_field("bool_value");
  select.set_test_only(false);
  Expr& expr0 = select.mutable_operand();

  auto& ident = expr0.mutable_ident_expr();
  ident.set_name("message");
  ASSERT_OK_AND_ASSIGN(
      auto step1,
      CreateSelectStep(select.field(), select.test_only(),
                       /*enable_wrapper_type_null_unboxing=*/false));

  path.push_back(ExpressionStep::MakeIdentifierStep(ident.name(), expr0.id()));
  path.push_back(
      ExpressionStep::MakeGenericStep(std::move(step1), dummy_expr.id()));

  cel::RuntimeOptions options;
  options.enable_missing_attribute_errors = true;
  CelExpressionFlatImpl cel_expr(
      env_,
      FlatExpression(std::move(path), /*comprehension_slot_count=*/0,
                     env_->type_registry.GetComposedTypeProvider(), options));
  Activation activation;
  activation.InsertValue("message",
                         CelProtoWrapper::CreateMessage(&message, &arena_));

  ASSERT_OK_AND_ASSIGN(CelValue result, cel_expr.Evaluate(activation, &arena_));
  ASSERT_TRUE(result.IsBool());
  EXPECT_EQ(result.BoolOrDie(), true);

  CelAttributePattern pattern("message",
                              {CreateCelAttributeQualifierPattern(
                                  CelValue::CreateStringView("bool_value"))});
  ASSERT_THAT(activation.SetMissingAttributePatterns({pattern}), IsOk());

  ASSERT_OK_AND_ASSIGN(result, cel_expr.Evaluate(activation, &arena_));
  EXPECT_THAT(*result.ErrorOrDie(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("MissingAttributeError: message.bool_value")));
}

TEST_F(SelectStepTest, UnknownPatternResolvesToUnknown) {
  TestMessage message;
  message.set_bool_value(true);
  ExecutionPath path;

  Expr dummy_expr;

  auto& select = dummy_expr.mutable_select_expr();
  select.set_field("bool_value");
  select.set_test_only(false);
  Expr& expr0 = select.mutable_operand();

  auto& ident = expr0.mutable_ident_expr();
  ident.set_name("message");
  auto step1_status =
      CreateSelectStep(select.field(), select.test_only(),
                       /*enable_wrapper_type_null_unboxing=*/false);

  ASSERT_THAT(step1_status, IsOk());

  path.push_back(ExpressionStep::MakeIdentifierStep(ident.name(), expr0.id()));
  path.push_back(ExpressionStep::MakeGenericStep(std::move(*step1_status),
                                                 dummy_expr.id()));

  cel::RuntimeOptions options;
  options.unknown_processing = cel::UnknownProcessingOptions::kAttributeOnly;
  CelExpressionFlatImpl cel_expr(
      env_,
      FlatExpression(std::move(path), /*comprehension_slot_count=*/0,
                     env_->type_registry.GetComposedTypeProvider(), options));

  {
    std::vector<CelAttributePattern> unknown_patterns;
    Activation activation;
    activation.InsertValue("message",
                           CelProtoWrapper::CreateMessage(&message, &arena_));
    ASSERT_THAT(activation.SetUnknownAttributePatterns(unknown_patterns),
                IsOk());

    ASSERT_OK_AND_ASSIGN(CelValue result,
                         cel_expr.Evaluate(activation, &arena_));
    ASSERT_TRUE(result.IsBool());
    EXPECT_EQ(result.BoolOrDie(), true);
  }

  const std::string kSegmentCorrect1 = "bool_value";
  const std::string kSegmentIncorrect = "message_value";

  {
    std::vector<CelAttributePattern> unknown_patterns;
    unknown_patterns.push_back(CelAttributePattern("message", {}));
    Activation activation;
    activation.InsertValue("message",
                           CelProtoWrapper::CreateMessage(&message, &arena_));
    ASSERT_THAT(activation.SetUnknownAttributePatterns(unknown_patterns),
                IsOk());

    ASSERT_OK_AND_ASSIGN(CelValue result,
                         cel_expr.Evaluate(activation, &arena_));
    ASSERT_TRUE(result.IsUnknownSet());
  }

  {
    std::vector<CelAttributePattern> unknown_patterns;
    unknown_patterns.push_back(CelAttributePattern(
        "message", {CreateCelAttributeQualifierPattern(
                       CelValue::CreateString(&kSegmentCorrect1))}));
    Activation activation;
    activation.InsertValue("message",
                           CelProtoWrapper::CreateMessage(&message, &arena_));
    ASSERT_THAT(activation.SetUnknownAttributePatterns(unknown_patterns),
                IsOk());

    ASSERT_OK_AND_ASSIGN(CelValue result,
                         cel_expr.Evaluate(activation, &arena_));
    ASSERT_TRUE(result.IsUnknownSet());
  }

  {
    std::vector<CelAttributePattern> unknown_patterns;
    unknown_patterns.push_back(CelAttributePattern(
        "message", {CelAttributeQualifierPattern::CreateWildcard()}));
    Activation activation;
    activation.InsertValue("message",
                           CelProtoWrapper::CreateMessage(&message, &arena_));
    ASSERT_THAT(activation.SetUnknownAttributePatterns(unknown_patterns),
                IsOk());

    ASSERT_OK_AND_ASSIGN(CelValue result,
                         cel_expr.Evaluate(activation, &arena_));
    ASSERT_TRUE(result.IsUnknownSet());
  }

  {
    std::vector<CelAttributePattern> unknown_patterns;
    unknown_patterns.push_back(CelAttributePattern(
        "message", {CreateCelAttributeQualifierPattern(
                       CelValue::CreateString(&kSegmentIncorrect))}));
    Activation activation;
    activation.InsertValue("message",
                           CelProtoWrapper::CreateMessage(&message, &arena_));
    ASSERT_THAT(activation.SetUnknownAttributePatterns(unknown_patterns),
                IsOk());

    ASSERT_OK_AND_ASSIGN(CelValue result,
                         cel_expr.Evaluate(activation, &arena_));
    ASSERT_TRUE(result.IsBool());
    EXPECT_EQ(result.BoolOrDie(), true);
  }
}

TEST_P(SelectStepConformanceTest, TypedSelectStepTest) {
  cel::StructType resolved_operand_type(
      (cel::MessageType(TestAllTypes::descriptor())));
  const google::protobuf::FieldDescriptor* field_desc =
      TestAllTypes::descriptor()->FindFieldByName("single_int64");
  ASSERT_NE(field_desc, nullptr);
  cel::StructTypeField resolved_field((cel::MessageTypeField(field_desc)));

  ASSERT_OK_AND_ASSIGN(
      auto step1, CreateTypedSelectStep(
                      "single_int64", resolved_operand_type, resolved_field,
                      /*test_only=*/false,
                      /*enable_wrapper_type_null_unboxing=*/false,
                      /*enable_optional_types=*/false));

  ExecutionPath path;
  path.push_back(ExpressionStep::MakeIdentifierStep("message"));
  path.push_back(ExpressionStep::MakeGenericStep(std::move(step1)));
  cel::RuntimeOptions options;
  if (GetParam()) {
    options.unknown_processing = cel::UnknownProcessingOptions::kAttributeOnly;
  }
  CelExpressionFlatImpl cel_expr(
      env_,
      FlatExpression(std::move(path), /*comprehension_slot_count=*/0,
                     env_->type_registry.GetComposedTypeProvider(), options));

  TestAllTypes message;
  message.set_single_int64(42);
  Activation activation;
  activation.InsertValue("message",
                         CelProtoWrapper::CreateMessage(&message, &arena_));

  ASSERT_OK_AND_ASSIGN(CelValue result, cel_expr.Evaluate(activation, &arena_));
  ASSERT_TRUE(result.IsInt64());
  EXPECT_EQ(result.Int64OrDie(), 42);
}

TEST_P(SelectStepConformanceTest, TypedSelectStepPropagatesUnknown) {
  cel::StructType resolved_operand_type(
      (cel::MessageType(TestAllTypes::descriptor())));
  const google::protobuf::FieldDescriptor* field_desc =
      TestAllTypes::descriptor()->FindFieldByName("single_int64");
  ASSERT_NE(field_desc, nullptr);
  cel::StructTypeField resolved_field((cel::MessageTypeField(field_desc)));

  ASSERT_OK_AND_ASSIGN(
      auto step1, CreateTypedSelectStep(
                      "single_int64", resolved_operand_type, resolved_field,
                      /*test_only=*/false,
                      /*enable_wrapper_type_null_unboxing=*/false,
                      /*enable_optional_types=*/false));

  ExecutionPath path;
  path.push_back(ExpressionStep::MakeIdentifierStep("message"));
  path.push_back(ExpressionStep::MakeGenericStep(std::move(step1)));
  cel::RuntimeOptions options;
  if (GetParam()) {
    options.unknown_processing = cel::UnknownProcessingOptions::kAttributeOnly;
  }
  CelExpressionFlatImpl cel_expr(
      env_,
      FlatExpression(std::move(path), /*comprehension_slot_count=*/0,
                     env_->type_registry.GetComposedTypeProvider(), options));

  UnknownSet unknown_set(UnknownAttributeSet({CelAttribute("message", {})}));
  Activation activation;
  activation.InsertValue("message", CelValue::CreateUnknownSet(&unknown_set));

  ASSERT_OK_AND_ASSIGN(CelValue result, cel_expr.Evaluate(activation, &arena_));
  ASSERT_TRUE(result.IsUnknownSet());
}

TEST_F(SelectStepTest, TypedSelectStepUnknownPatternResolvesToUnknown) {
  cel::StructType resolved_operand_type(
      (cel::MessageType(TestAllTypes::descriptor())));
  const google::protobuf::FieldDescriptor* field_desc =
      TestAllTypes::descriptor()->FindFieldByName("single_int64");
  ASSERT_NE(field_desc, nullptr);
  cel::StructTypeField resolved_field((cel::MessageTypeField(field_desc)));

  ASSERT_OK_AND_ASSIGN(
      auto step1, CreateTypedSelectStep(
                      "single_int64", resolved_operand_type, resolved_field,
                      /*test_only=*/false,
                      /*enable_wrapper_type_null_unboxing=*/false,
                      /*enable_optional_types=*/false));

  ExecutionPath path;
  path.push_back(ExpressionStep::MakeIdentifierStep("message"));
  path.push_back(ExpressionStep::MakeGenericStep(std::move(step1)));
  cel::RuntimeOptions options;
  options.unknown_processing = cel::UnknownProcessingOptions::kAttributeOnly;
  CelExpressionFlatImpl cel_expr(
      env_,
      FlatExpression(std::move(path), /*comprehension_slot_count=*/0,
                     env_->type_registry.GetComposedTypeProvider(), options));

  TestAllTypes message;
  message.set_single_int64(42);

  std::vector<CelAttributePattern> unknown_patterns;
  unknown_patterns.push_back(CelAttributePattern(
      "message", {CelAttributeQualifierPattern::OfString("single_int64")}));

  Activation activation;
  activation.InsertValue("message",
                         CelProtoWrapper::CreateMessage(&message, &arena_));
  ASSERT_THAT(activation.SetUnknownAttributePatterns(unknown_patterns), IsOk());

  ASSERT_OK_AND_ASSIGN(CelValue result, cel_expr.Evaluate(activation, &arena_));
  ASSERT_TRUE(result.IsUnknownSet());
}

INSTANTIATE_TEST_SUITE_P(UnknownsEnabled, SelectStepConformanceTest,
                         testing::Bool());

}  // namespace

}  // namespace google::api::expr::runtime
