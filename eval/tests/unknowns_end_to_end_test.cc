// Integration tests for unknown processing in the C++ CEL runtime. The
// semantics of some of the tested expressions can be complicated because isn't
// possible to represent unknown values or errors directly in CEL -- declaring
// the unknowns is particular to the runtime.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cel/expr/syntax.pb.h"
#include "google/protobuf/struct.pb.h"
#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "base/attribute.h"
#include "base/attribute_set.h"
#include "base/function_result.h"
#include "common/function_descriptor.h"
#include "common/kind.h"
#include "common/native_type.h"
#include "common/unknown.h"
#include "common/value.h"
#include "extensions/protobuf/runtime_adapter.h"
#include "internal/testing.h"
#include "parser/parser.h"
#include "runtime/activation.h"
#include "runtime/function.h"
#include "runtime/internal/errors.h"
#include "runtime/runtime.h"
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
using ::cel::AttributePattern;
using ::cel::AttributeQualifierPattern;
using ::cel::AttributeSet;
using ::cel::BoolValue;
using ::cel::CreateStandardRuntimeBuilder;
using ::cel::CustomMapValue;
using ::cel::CustomMapValueInterface;
using ::cel::ErrorValue;
using ::cel::Function;
using ::cel::FunctionDescriptor;
using ::cel::IntValue;
using ::cel::Kind;
using ::cel::ListValue;
using ::cel::NativeTypeId;
using ::cel::Runtime;
using ::cel::RuntimeOptions;
using ::cel::Unknown;
using ::cel::UnknownProcessingOptions;
using ::cel::UnknownValue;
using ::cel::Value;
using ::cel::ValueIterator;
using ::cel::ValueIteratorPtr;
using ::cel::extensions::ProtobufRuntimeAdapter;
using ::cel::runtime_internal::CreateUnknownFunctionResultError;
using ::cel::runtime_internal::kErrNoMatchingOverload;
using ::cel::expr::Expr;
using ::cel::expr::ParsedExpr;
using ::google::api::expr::parser::Parse;
using ::google::protobuf::Arena;
using ::testing::ElementsAre;
using ::testing::HasSubstr;

enum class FunctionResponse { kUnknown, kTrue, kFalse };

FunctionDescriptor CreateDescriptor(absl::string_view name,
                                    Kind type = Kind::kString) {
  return FunctionDescriptor(name, false, {type});
}

class FunctionImpl : public Function {
 public:
  explicit FunctionImpl(FunctionResponse response) : response_(response) {}

  absl::StatusOr<Value> Invoke(absl::Span<const Value> arguments,
                               const InvokeContext& context) const override {
    switch (response_) {
      case FunctionResponse::kUnknown:
        return ErrorValue::From(
            CreateUnknownFunctionResultError("help message"), context.arena());
      case FunctionResponse::kTrue:
        return BoolValue(true);
      case FunctionResponse::kFalse:
        return BoolValue(false);
    }
  }

 private:
  FunctionResponse response_;
};

// Custom map allowing arbitrary key Values (including UnknownValue and
// ErrorValue) to test comprehension attribute-trail handling.
class TestKeyTypesMap final : public CustomMapValueInterface {
 public:
  explicit TestKeyTypesMap(std::vector<std::pair<Value, Value>> entries)
      : entries_(std::move(entries)) {}

  size_t Size() const override { return entries_.size(); }

  absl::Status ListKeys(const google::protobuf::DescriptorPool* absl_nonnull,
                        google::protobuf::MessageFactory* absl_nonnull,
                        google::protobuf::Arena* absl_nonnull,
                        ListValue* absl_nonnull) const override {
    return absl::UnimplementedError("ListKeys not implemented");
  }

  absl::StatusOr<absl_nonnull ValueIteratorPtr> NewIterator() const override {
    class Iterator final : public ValueIterator {
     public:
      explicit Iterator(const std::vector<std::pair<Value, Value>>& entries)
          : entries_(entries) {}

      bool HasNext() override { return idx_ < entries_.size(); }

      absl::Status Next(const google::protobuf::DescriptorPool* absl_nonnull,
                        google::protobuf::MessageFactory* absl_nonnull,
                        google::protobuf::Arena* absl_nonnull,
                        Value* absl_nonnull result) override {
        if (!HasNext()) {
          return absl::OutOfRangeError("end of iterator");
        }
        *result = entries_[idx_++].first;
        return absl::OkStatus();
      }

      absl::StatusOr<bool> Next2(const google::protobuf::DescriptorPool* absl_nonnull,
                                 google::protobuf::MessageFactory* absl_nonnull,
                                 google::protobuf::Arena* absl_nonnull,
                                 Value* absl_nullable key,
                                 Value* absl_nullable value) override {
        if (!HasNext()) {
          return false;
        }
        if (key != nullptr) {
          *key = entries_[idx_].first;
        }
        if (value != nullptr) {
          *value = entries_[idx_].second;
        }
        ++idx_;
        return true;
      }

     private:
      const std::vector<std::pair<Value, Value>>& entries_;
      size_t idx_ = 0;
    };
    return std::make_unique<Iterator>(entries_);
  }

  std::string DebugString() const override { return "TestKeyTypesMap"; }

  absl::Status ConvertToJsonObject(
      const google::protobuf::DescriptorPool* absl_nonnull,
      google::protobuf::MessageFactory* absl_nonnull,
      google::protobuf::Message* absl_nonnull) const override {
    return absl::UnimplementedError("ConvertToJsonObject not implemented");
  }

  CustomMapValue Clone(google::protobuf::Arena* absl_nonnull arena) const override {
    return CustomMapValue(
        google::protobuf::Arena::Create<TestKeyTypesMap>(arena, entries_), arena);
  }

 protected:
  absl::StatusOr<bool> Find(const Value& key,
                            const google::protobuf::DescriptorPool* absl_nonnull,
                            google::protobuf::MessageFactory* absl_nonnull,
                            google::protobuf::Arena* absl_nonnull,
                            Value* absl_nonnull result) const override {
    return false;
  }

  absl::StatusOr<bool> Has(const Value& key,
                           const google::protobuf::DescriptorPool* absl_nonnull,
                           google::protobuf::MessageFactory* absl_nonnull,
                           google::protobuf::Arena* absl_nonnull) const override {
    return false;
  }

 private:
  NativeTypeId GetNativeTypeId() const override {
    return NativeTypeId::For<TestKeyTypesMap>();
  }

  std::vector<std::pair<Value, Value>> entries_;
};

// Text fixture for unknowns. Holds on to state needed for execution to work
// correctly.
class UnknownsTest : public testing::Test {
 public:
  void PrepareBuilder(UnknownProcessingOptions opts) {
    RuntimeOptions options;
    options.unknown_processing = opts;
    ASSERT_OK_AND_ASSIGN(
        auto builder, CreateStandardRuntimeBuilder(
                          google::protobuf::DescriptorPool::generated_pool(), options));
    ASSERT_THAT(builder.function_registry().RegisterLazyFunction(
                    CreateDescriptor("F1")),
                IsOk());
    ASSERT_THAT(builder.function_registry().RegisterLazyFunction(
                    CreateDescriptor("F2")),
                IsOk());
    ASSERT_OK_AND_ASSIGN(runtime_, std::move(builder).Build());
  }

 protected:
  Arena arena_;
  Activation activation_;
  std::unique_ptr<Runtime> runtime_;
};

MATCHER_P(FunctionCallIs, fn_name, "") {
  const cel::FunctionResult& result = arg;
  return result.name() == fn_name;
}

MATCHER_P(AttributeIs, attr, "") {
  const cel::Attribute& result = arg;
  return result.AsString().value_or("") == attr;
}

TEST_F(UnknownsTest, NoUnknowns) {
  PrepareBuilder(UnknownProcessingOptions::kDisabled);

  activation_.InsertOrAssignValue("var1", IntValue(3));
  activation_.InsertOrAssignValue("var2", IntValue(5));
  ASSERT_TRUE(activation_.InsertFunction(
      CreateDescriptor("F1"),
      std::make_unique<FunctionImpl>(FunctionResponse::kFalse)));
  ASSERT_TRUE(activation_.InsertFunction(
      CreateDescriptor("F2"),
      std::make_unique<FunctionImpl>(FunctionResponse::kTrue)));

  ASSERT_OK_AND_ASSIGN(
      ParsedExpr expr,
      Parse("var1 > 3 && F1('arg1') || var2 > 3 && F2('arg2')"));
  auto plan = ProtobufRuntimeAdapter::CreateProgram(*runtime_, expr);
  ASSERT_THAT(plan, IsOk());

  ASSERT_OK_AND_ASSIGN(Value response,
                       plan.value()->Evaluate(&arena_, activation_));

  ASSERT_TRUE(response.IsBool()) << response.DebugString();
  EXPECT_TRUE(response.GetBool().NativeValue());
}

TEST_F(UnknownsTest, UnknownAttributes) {
  PrepareBuilder(UnknownProcessingOptions::kAttributeOnly);
  ASSERT_THAT(activation_.SetUnknownPatterns({AttributePattern("var1", {})}),
              IsOk());
  activation_.InsertOrAssignValue("var2", IntValue(3));
  ASSERT_TRUE(activation_.InsertFunction(
      CreateDescriptor("F1"),
      std::make_unique<FunctionImpl>(FunctionResponse::kTrue)));
  ASSERT_TRUE(activation_.InsertFunction(
      CreateDescriptor("F2"),
      std::make_unique<FunctionImpl>(FunctionResponse::kFalse)));

  ASSERT_OK_AND_ASSIGN(
      ParsedExpr expr,
      Parse("var1 > 3 && F1('arg1') || var2 > 3 && F2('arg2')"));
  auto plan = ProtobufRuntimeAdapter::CreateProgram(*runtime_, expr);
  ASSERT_THAT(plan, IsOk());

  ASSERT_OK_AND_ASSIGN(Value response,
                       plan.value()->Evaluate(&arena_, activation_));

  ASSERT_TRUE(response.IsUnknown());
  EXPECT_THAT(response.GetUnknown().ToAttributeSet(),
              ElementsAre(AttributeIs("var1")));
}

TEST_F(UnknownsTest, UnknownAttributesPruning) {
  PrepareBuilder(UnknownProcessingOptions::kAttributeOnly);
  ASSERT_THAT(activation_.SetUnknownPatterns({AttributePattern("var1", {})}),
              IsOk());
  activation_.InsertOrAssignValue("var2", IntValue(5));
  ASSERT_TRUE(activation_.InsertFunction(
      CreateDescriptor("F1"),
      std::make_unique<FunctionImpl>(FunctionResponse::kTrue)));
  ASSERT_TRUE(activation_.InsertFunction(
      CreateDescriptor("F2"),
      std::make_unique<FunctionImpl>(FunctionResponse::kTrue)));

  ASSERT_OK_AND_ASSIGN(
      ParsedExpr expr,
      Parse("var1 > 3 && F1('arg1') || var2 > 3 && F2('arg2')"));
  auto plan = ProtobufRuntimeAdapter::CreateProgram(*runtime_, expr);
  ASSERT_THAT(plan, IsOk());

  ASSERT_OK_AND_ASSIGN(Value response,
                       plan.value()->Evaluate(&arena_, activation_));

  ASSERT_TRUE(response.IsBool());
  EXPECT_TRUE(response.GetBool().NativeValue());
}

TEST_F(UnknownsTest, UnknownFunctionsWithoutOptionError) {
  PrepareBuilder(UnknownProcessingOptions::kAttributeOnly);
  activation_.InsertOrAssignValue("var1", IntValue(5));
  activation_.InsertOrAssignValue("var2", IntValue(3));
  ASSERT_TRUE(activation_.InsertFunction(
      CreateDescriptor("F1"),
      std::make_unique<FunctionImpl>(FunctionResponse::kUnknown)));
  ASSERT_TRUE(activation_.InsertFunction(
      CreateDescriptor("F2"),
      std::make_unique<FunctionImpl>(FunctionResponse::kFalse)));

  ASSERT_OK_AND_ASSIGN(
      ParsedExpr expr,
      Parse("var1 > 3 && F1('arg1') || var2 > 3 && F2('arg2')"));
  auto plan = ProtobufRuntimeAdapter::CreateProgram(*runtime_, expr);
  ASSERT_THAT(plan, IsOk());

  ASSERT_OK_AND_ASSIGN(Value response,
                       plan.value()->Evaluate(&arena_, activation_));

  ASSERT_TRUE(response.IsError());
  EXPECT_EQ(response.GetError().NativeValue().code(),
            absl::StatusCode::kUnavailable);
}

TEST_F(UnknownsTest, UnknownFunctions) {
  PrepareBuilder(UnknownProcessingOptions::kAttributeAndFunction);
  activation_.InsertOrAssignValue("var1", IntValue(5));
  activation_.InsertOrAssignValue("var2", IntValue(5));
  ASSERT_TRUE(activation_.InsertFunction(
      CreateDescriptor("F1"),
      std::make_unique<FunctionImpl>(FunctionResponse::kUnknown)));
  ASSERT_TRUE(activation_.InsertFunction(
      CreateDescriptor("F2"),
      std::make_unique<FunctionImpl>(FunctionResponse::kFalse)));

  ASSERT_OK_AND_ASSIGN(
      ParsedExpr expr,
      Parse("var1 > 3 && F1('arg1') || var2 > 3 && F2('arg2')"));
  auto plan = ProtobufRuntimeAdapter::CreateProgram(*runtime_, expr);
  ASSERT_THAT(plan, IsOk());

  ASSERT_OK_AND_ASSIGN(Value response,
                       plan.value()->Evaluate(&arena_, activation_));

  ASSERT_TRUE(response.IsUnknown()) << response.DebugString();
  EXPECT_THAT(response.GetUnknown().ToFunctionResultSet(),
              ElementsAre(FunctionCallIs("F1")));
}

TEST_F(UnknownsTest, UnknownsMerge) {
  PrepareBuilder(UnknownProcessingOptions::kAttributeAndFunction);
  activation_.InsertOrAssignValue("var1", IntValue(5));
  ASSERT_THAT(activation_.SetUnknownPatterns({AttributePattern("var2", {})}),
              IsOk());

  ASSERT_TRUE(activation_.InsertFunction(
      CreateDescriptor("F1"),
      std::make_unique<FunctionImpl>(FunctionResponse::kUnknown)));
  ASSERT_TRUE(activation_.InsertFunction(
      CreateDescriptor("F2"),
      std::make_unique<FunctionImpl>(FunctionResponse::kTrue)));

  ASSERT_OK_AND_ASSIGN(
      ParsedExpr expr,
      Parse("var1 > 3 && F1('arg1') || var2 > 3 && F2('arg2')"));
  auto plan = ProtobufRuntimeAdapter::CreateProgram(*runtime_, expr);
  ASSERT_THAT(plan, IsOk());

  ASSERT_OK_AND_ASSIGN(Value response,
                       plan.value()->Evaluate(&arena_, activation_));

  ASSERT_TRUE(response.IsUnknown()) << response.DebugString();
  EXPECT_THAT(response.GetUnknown().ToFunctionResultSet(),
              ElementsAre(FunctionCallIs("F1")));
  EXPECT_THAT(response.GetUnknown().ToAttributeSet(),
              ElementsAre(AttributeIs("var2")));
}

constexpr char kListCompExistsExpr[] = R"pb(
  id: 25
  comprehension_expr {
    iter_var: "x"
    iter_range {
      id: 1
      list_expr {
        elements {
          id: 2
          const_expr { int64_value: 1 }
        }
        elements {
          id: 3
          const_expr { int64_value: 2 }
        }
        elements {
          id: 4
          const_expr { int64_value: 3 }
        }
        elements {
          id: 5
          const_expr { int64_value: 4 }
        }
        elements {
          id: 6
          const_expr { int64_value: 5 }
        }
        elements {
          id: 7
          const_expr { int64_value: 6 }
        }
        elements {
          id: 8
          const_expr { int64_value: 7 }
        }
        elements {
          id: 9
          const_expr { int64_value: 8 }
        }
        elements {
          id: 10
          const_expr { int64_value: 9 }
        }
        elements {
          id: 11
          const_expr { int64_value: 10 }
        }
      }
    }
    accu_var: "__result__"
    accu_init {
      id: 18
      const_expr { bool_value: false }
    }
    loop_condition {
      id: 21
      call_expr {
        function: "@not_strictly_false"
        args {
          id: 20
          call_expr {
            function: "!_"
            args {
              id: 19
              ident_expr { name: "__result__" }
            }
          }
        }
      }
    }
    loop_step {
      id: 23
      call_expr {
        function: "_||_"
        args {
          id: 22
          ident_expr { name: "__result__" }
        }
        args {
          id: 16
          call_expr {
            function: "_>_"
            args {
              id: 14
              call_expr {
                function: "Fn"
                args {
                  id: 15
                  ident_expr { name: "x" }
                }
              }
            }
            args {
              id: 17
              const_expr { int64_value: 2 }
            }
          }
        }
      }
    }
    result {
      id: 24
      ident_expr { name: "__result__" }
    }
  })pb";

// Text fixture for comprehension tests. Holds on to state needed for execution
// to work correctly.
class UnknownsCompTest : public testing::Test {
 public:
  void PrepareBuilder(UnknownProcessingOptions opts) {
    RuntimeOptions options;
    options.unknown_processing = opts;
    ASSERT_OK_AND_ASSIGN(
        auto builder, CreateStandardRuntimeBuilder(
                          google::protobuf::DescriptorPool::generated_pool(), options));
    ASSERT_THAT(builder.function_registry().RegisterLazyFunction(
                    CreateDescriptor("Fn", Kind::kInt)),
                IsOk());
    ASSERT_OK_AND_ASSIGN(runtime_, std::move(builder).Build());
    ASSERT_TRUE(
        google::protobuf::TextFormat::ParseFromString(kListCompExistsExpr, &expr_))
        << "error parsing expr";
  }

 protected:
  Arena arena_;
  Activation activation_;
  std::unique_ptr<Runtime> runtime_;
  Expr expr_;
};

TEST_F(UnknownsCompTest, UnknownsMerge) {
  PrepareBuilder(UnknownProcessingOptions::kAttributeAndFunction);

  ASSERT_TRUE(activation_.InsertFunction(
      CreateDescriptor("Fn", Kind::kInt),
      std::make_unique<FunctionImpl>(FunctionResponse::kUnknown)));

  // [1, 2, 3, 4, 5, 6, 7, 8, 9, 10].exists(x, Fn(x) > 5)
  auto build_status = ProtobufRuntimeAdapter::CreateProgram(*runtime_, expr_);
  ASSERT_THAT(build_status, IsOk());

  auto eval_status = build_status.value()->Evaluate(&arena_, activation_);
  ASSERT_THAT(eval_status, IsOk());
  Value response = eval_status.value();

  ASSERT_TRUE(response.IsUnknown()) << response.DebugString();
  EXPECT_THAT(response.GetUnknown().ToFunctionResultSet(), testing::SizeIs(1));
}

constexpr char kListCompCondExpr[] = R"pb(
  id: 25
  comprehension_expr {
    iter_var: "x"
    iter_range {
      id: 1
      list_expr {
        elements {
          id: 2
          const_expr { int64_value: 1 }
        }
        elements {
          id: 3

          const_expr { int64_value: 2 }

        }
        elements {
          id: 11
          const_expr { int64_value: 3 }
        }
      }
    }
    accu_var: "__result__"
    accu_init {
      id: 18
      const_expr { int64_value: 0 }
    }
    loop_condition {
      id: 21
      call_expr {
        function: "_<=_"
        args {
          id: 20
          ident_expr { name: "__result__" }
        }
        args {
          id: 19
          const_expr { int64_value: 1 }
        }
      }
    }
    loop_step {
      id: 23
      call_expr {
        function: "_?_:_"
        args {
          id: 22
          call_expr: {
            function: "Fn"
            args {
              id: 4
              ident_expr { name: "x" }
            }
          }
        }
        args {
          id: 14
          call_expr {
            function: "_+_"
            args {
              id: 15
              ident_expr { name: "__result__" }
            }
            args {
              id: 17
              const_expr { int64_value: 1 }
            }
          }
        }
        args {
          id: 16
          ident_expr { name: "__result__" }
        }
      }
    }
    result {
      id: 24
      call_expr {
        function: "_==_"
        args {
          id: 27
          ident_expr { name: "__result__" }
        }
        args {
          id: 26
          const_expr { int64_value: 1 }
        }
      }
    }
  })pb";

// Text fixture for comprehension tests affecting the condition step.
// Holds on to state needed for execution to work correctly.
class UnknownsCompCondTest : public testing::Test {
 public:
  void PrepareBuilder(UnknownProcessingOptions opts) {
    RuntimeOptions options;
    options.unknown_processing = opts;
    ASSERT_OK_AND_ASSIGN(
        auto builder, CreateStandardRuntimeBuilder(
                          google::protobuf::DescriptorPool::generated_pool(), options));
    ASSERT_THAT(builder.function_registry().RegisterLazyFunction(
                    CreateDescriptor("Fn", Kind::kInt)),
                IsOk());
    ASSERT_OK_AND_ASSIGN(runtime_, std::move(builder).Build());
    ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(kListCompCondExpr, &expr_))
        << "error parsing expr";
  }

 protected:
  Arena arena_;
  Activation activation_;
  std::unique_ptr<Runtime> runtime_;
  Expr expr_;
};

TEST_F(UnknownsCompCondTest, UnknownConditionReturned) {
  PrepareBuilder(UnknownProcessingOptions::kAttributeAndFunction);

  ASSERT_TRUE(activation_.InsertFunction(
      CreateDescriptor("Fn", Kind::kInt),
      std::make_unique<FunctionImpl>(FunctionResponse::kUnknown)));

  // [1, 2, 3].exists_one(x, Fn(x))
  auto build_status = ProtobufRuntimeAdapter::CreateProgram(*runtime_, expr_);
  ASSERT_THAT(build_status, IsOk());

  auto eval_status = build_status.value()->Evaluate(&arena_, activation_);
  ASSERT_THAT(eval_status, IsOk());
  Value response = eval_status.value();

  ASSERT_TRUE(response.IsUnknown()) << response.DebugString();
  // The comprehension ends on the first non-bool condition, so we only get one
  // call captured in the UnknownValue.
  EXPECT_THAT(response.GetUnknown().ToFunctionResultSet(), testing::SizeIs(1));
}

TEST_F(UnknownsCompCondTest, ErrorConditionReturned) {
  PrepareBuilder(UnknownProcessingOptions::kAttributeAndFunction);

  // No implementation for Fn(int64) provided in activation -- this turns into
  // an ErrorValue.
  // [1, 2, 3].exists_one(x, Fn(x))
  auto build_status = ProtobufRuntimeAdapter::CreateProgram(*runtime_, expr_);
  ASSERT_THAT(build_status, IsOk());

  auto eval_status = build_status.value()->Evaluate(&arena_, activation_);
  ASSERT_THAT(eval_status, IsOk());
  Value response = eval_status.value();

  ASSERT_TRUE(response.IsError()) << response.DebugString();
  EXPECT_THAT(
      response.GetError().NativeValue(),
      StatusIs(absl::StatusCode::kUnknown, HasSubstr(kErrNoMatchingOverload)));
}

constexpr char kListCompExistsWithAttrExpr[] = R"pb(
  id: 25
  comprehension_expr {
    iter_var: "x"
    iter_range {
      id: 1
      ident_expr { name: "var" }
    }
    accu_var: "__result__"
    accu_init {
      id: 18
      const_expr { bool_value: false }
    }
    loop_condition {
      id: 21
      call_expr {
        function: "@not_strictly_false"
        args {
          id: 20
          call_expr {
            function: "!_"
            args {
              id: 19
              ident_expr { name: "__result__" }
            }
          }
        }
      }
    }
    loop_step {
      id: 23
      call_expr {
        function: "_||_"
        args {
          id: 22
          ident_expr { name: "__result__" }
        }
        args {
          id: 16
          call_expr {
            function: "Fn"
            args {
              id: 15
              ident_expr { name: "x" }
            }
          }
        }
      }
    }
    result {
      id: 24
      ident_expr { name: "__result__" }
    }
  })pb";

TEST(UnknownsIterAttrTest, IterAttributeTrail) {
  RuntimeOptions options;
  Expr expr;
  Activation activation;
  Arena arena;

  protobuf::Value element;
  protobuf::Value& value =
      element.mutable_struct_value()->mutable_fields()->operator[]("elem1");
  value.set_number_value(1);
  protobuf::ListValue list;
  *list.add_values() = element;
  *list.add_values() = element;
  *list.add_values() = element;

  options.unknown_processing = UnknownProcessingOptions::kAttributeAndFunction;
  ASSERT_OK_AND_ASSIGN(auto builder,
                       CreateStandardRuntimeBuilder(
                           google::protobuf::DescriptorPool::generated_pool(), options));
  ASSERT_THAT(builder.function_registry().RegisterLazyFunction(
                  CreateDescriptor("Fn", Kind::kMap)),
              IsOk());
  ASSERT_OK_AND_ASSIGN(auto runtime, std::move(builder).Build());
  ASSERT_TRUE(
      google::protobuf::TextFormat::ParseFromString(kListCompExistsWithAttrExpr, &expr))
      << "error parsing expr";

  // var.exists(x, Fn(x))
  auto plan = ProtobufRuntimeAdapter::CreateProgram(*runtime, expr).value();

  activation.InsertOrAssignValue(
      "var",
      Value::WrapMessage(&list, google::protobuf::DescriptorPool::generated_pool(),
                         google::protobuf::MessageFactory::generated_factory(), &arena));

  // var[1]['elem1'] is unknown
  ASSERT_THAT(activation.SetUnknownPatterns({AttributePattern(
                  "var",
                  {
                      AttributeQualifierPattern::OfInt(1),
                      AttributeQualifierPattern::OfString("elem1"),
                  })}),
              IsOk());

  ASSERT_TRUE(activation.InsertFunction(
      CreateDescriptor("Fn", Kind::kMap),
      std::make_unique<FunctionImpl>(FunctionResponse::kFalse)));

  Value response = plan->Evaluate(&arena, activation).value();

  ASSERT_TRUE(response.IsUnknown()) << response.DebugString();
  ASSERT_EQ(response.GetUnknown().ToAttributeSet().size(), 1);
  // 'var[1]' is partially unknown when we make the function call so we treat it
  // as unknown.
  ASSERT_EQ(
      response.GetUnknown().ToAttributeSet().begin()->qualifier_path().size(),
      1);
}

TEST(UnknownsIterAttrTest, IterAttributeTrailMapKeyTypes) {
  RuntimeOptions options;
  Expr expr;
  Activation activation;
  Arena arena;

  Unknown unknown_set(AttributeSet({cel::Attribute("foo")}));
  absl::Status error = absl::CancelledError();

  std::vector<std::pair<Value, Value>> backing;
  backing.push_back(
      {cel::common_internal::MakeUnknownValue(unknown_set, &arena),
       BoolValue(false)});
  backing.push_back({ErrorValue::From(error, &arena), BoolValue(false)});
  backing.push_back({BoolValue(true), BoolValue(false)});

  options.unknown_processing = UnknownProcessingOptions::kAttributeAndFunction;
  ASSERT_OK_AND_ASSIGN(auto builder,
                       CreateStandardRuntimeBuilder(
                           google::protobuf::DescriptorPool::generated_pool(), options));
  ASSERT_THAT(builder.function_registry().RegisterLazyFunction(
                  CreateDescriptor("Fn", Kind::kBool)),
              IsOk());
  ASSERT_OK_AND_ASSIGN(auto runtime, std::move(builder).Build());
  ASSERT_TRUE(
      google::protobuf::TextFormat::ParseFromString(kListCompExistsWithAttrExpr, &expr))
      << "error parsing expr";

  // var.exists(x, Fn(x))
  auto plan = ProtobufRuntimeAdapter::CreateProgram(*runtime, expr).value();

  activation.InsertOrAssignValue(
      "var",
      CustomMapValue(Arena::Create<TestKeyTypesMap>(&arena, std::move(backing)),
                     &arena));

  ASSERT_TRUE(activation.InsertFunction(
      CreateDescriptor("Fn", Kind::kBool),
      std::make_unique<FunctionImpl>(FunctionResponse::kFalse)));

  Value response = plan->Evaluate(&arena, activation).value();

  ASSERT_TRUE(response.IsUnknown()) << response.DebugString();
  Unknown got(response.GetUnknown().ToAttributeSet(),
              response.GetUnknown().ToFunctionResultSet());
  ASSERT_EQ(got, unknown_set);
}

TEST(UnknownsIterAttrTest, IterAttributeTrailMapKeyTypesShortcutted) {
  RuntimeOptions options;
  Expr expr;
  Activation activation;
  Arena arena;

  Unknown unknown_set(AttributeSet({cel::Attribute("foo")}));
  absl::Status error = absl::CancelledError();

  std::vector<std::pair<Value, Value>> backing;
  backing.push_back(
      {cel::common_internal::MakeUnknownValue(unknown_set, &arena),
       BoolValue(false)});
  backing.push_back({ErrorValue::From(error, &arena), BoolValue(false)});
  backing.push_back({BoolValue(true), BoolValue(false)});

  options.unknown_processing = UnknownProcessingOptions::kAttributeAndFunction;
  ASSERT_OK_AND_ASSIGN(auto builder,
                       CreateStandardRuntimeBuilder(
                           google::protobuf::DescriptorPool::generated_pool(), options));
  ASSERT_THAT(builder.function_registry().RegisterLazyFunction(
                  CreateDescriptor("Fn", Kind::kBool)),
              IsOk());
  ASSERT_OK_AND_ASSIGN(auto runtime, std::move(builder).Build());
  ASSERT_TRUE(
      google::protobuf::TextFormat::ParseFromString(kListCompExistsWithAttrExpr, &expr))
      << "error parsing expr";

  // var.exists(x, Fn(x))
  auto plan = ProtobufRuntimeAdapter::CreateProgram(*runtime, expr).value();

  activation.InsertOrAssignValue(
      "var",
      CustomMapValue(Arena::Create<TestKeyTypesMap>(&arena, std::move(backing)),
                     &arena));

  ASSERT_TRUE(activation.InsertFunction(
      CreateDescriptor("Fn", Kind::kBool),
      std::make_unique<FunctionImpl>(FunctionResponse::kTrue)));

  Value response = plan->Evaluate(&arena, activation).value();
  ASSERT_TRUE(response.IsBool()) << response.DebugString();
  ASSERT_TRUE(response.GetBool().NativeValue());
}

constexpr char kMapElementsComp[] = R"pb(
  id: 25
  comprehension_expr {
    iter_var: "x"
    iter_range {
      id: 1
      ident_expr { name: "var" }
    }
    accu_var: "__result__"
    accu_init {
      id: 2
      list_expr {}
    }
    loop_condition {
      id: 3
      const_expr { bool_value: true }
    }
    loop_step {
      id: 4
      call_expr {
        function: "_+_"
        args {
          id: 5
          ident_expr { name: "__result__" }
        }
        args {
          id: 6
          list_expr {
            elements {
              id: 9
              call_expr {
                function: "Fn"
                args {
                  id: 7
                  select_expr {
                    field: "key"
                    operand {
                      id: 8
                      ident_expr { name: "x" }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
    result {
      id: 9
      ident_expr { name: "__result__" }
    }
  })pb";

// TODO(issues/67): Expected behavior for maps with unknown keys/values in a
// comprehension is a little unclear and the test coverage is a bit sparse.
// A few more tests should be added for coverage and to help document.
TEST(UnknownsIterAttrTest, IterAttributeTrailMap) {
  RuntimeOptions options;
  Expr expr;
  Activation activation;
  Arena arena;

  protobuf::Value element;
  protobuf::Value& value =
      element.mutable_struct_value()->mutable_fields()->operator[]("key");
  value.set_number_value(1);
  protobuf::ListValue list;
  *list.add_values() = element;
  *list.add_values() = element;
  *list.add_values() = element;

  options.unknown_processing = UnknownProcessingOptions::kAttributeAndFunction;
  ASSERT_OK_AND_ASSIGN(auto builder,
                       CreateStandardRuntimeBuilder(
                           google::protobuf::DescriptorPool::generated_pool(), options));
  ASSERT_THAT(builder.function_registry().RegisterLazyFunction(
                  CreateDescriptor("Fn", Kind::kDouble)),
              IsOk());
  ASSERT_OK_AND_ASSIGN(auto runtime, std::move(builder).Build());
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(kMapElementsComp, &expr))
      << "error parsing expr";
  activation.InsertOrAssignValue(
      "var",
      Value::WrapMessage(&list, google::protobuf::DescriptorPool::generated_pool(),
                         google::protobuf::MessageFactory::generated_factory(), &arena));

  // var[1]['key'] is unknown
  ASSERT_THAT(activation.SetUnknownPatterns({AttributePattern(
                  "var",
                  {
                      AttributeQualifierPattern::OfInt(1),
                      AttributeQualifierPattern::OfString("key"),
                  })}),
              IsOk());

  ASSERT_TRUE(activation.InsertFunction(
      CreateDescriptor("Fn", Kind::kDouble),
      std::make_unique<FunctionImpl>(FunctionResponse::kFalse)));

  auto plan = ProtobufRuntimeAdapter::CreateProgram(*runtime, expr).value();
  Value response = plan->Evaluate(&arena, activation).value();

  ASSERT_TRUE(response.IsUnknown()) << response.DebugString();
  ASSERT_EQ(response.GetUnknown().ToAttributeSet().size(), 1);
  // 'var[1].key' is unknown when we make the Fn function call.
  // comprehension is:  ((([] + false) + unk) + false) -> unk
  ASSERT_EQ(
      response.GetUnknown().ToAttributeSet().begin()->qualifier_path().size(),
      2);
}

constexpr char kFilterElementsComp[] = R"pb(
  id: 25
  comprehension_expr {
    iter_var: "x"
    iter_range {
      id: 1
      ident_expr { name: "var" }
    }
    accu_var: "__result__"
    accu_init {
      id: 2
      list_expr {}
    }
    loop_condition {
      id: 3
      const_expr { bool_value: true }
    }
    loop_step {
      id: 4
      call_expr {
        function: "_?_:_"
        args {
          id: 5
          select_expr {
            field: "filter_key"
            operand {
              id: 6
              ident_expr { name: "x" }
            }
          }
        }
        args {
          id: 7
          call_expr {
            function: "_+_"
            args {
              id: 8
              ident_expr { name: "__result__" }
            }
            args {
              id: 9
              list_expr {
                elements {
                  id: 10
                  select_expr {
                    field: "value_key"
                    operand {
                      id: 12
                      ident_expr { name: "x" }
                    }
                  }
                }
              }
            }
          }
        }
        args {
          id: 13
          ident_expr { name: "__result__" }
        }
      }
    }
    result {
      id: 14
      ident_expr { name: "__result__" }
    }
  })pb";

TEST(UnknownsIterAttrTest, IterAttributeTrailExact) {
  RuntimeOptions options;
  Activation activation;
  Arena arena;

  ASSERT_OK_AND_ASSIGN(ParsedExpr expr, Parse("list_var.exists(x, x)"));

  protobuf::Value element;
  element.set_bool_value(false);
  protobuf::ListValue list;
  *list.add_values() = element;
  *list.add_values() = element;
  *list.add_values() = element;

  (*list.mutable_values())[0].set_bool_value(true);

  options.unknown_processing = UnknownProcessingOptions::kAttributeAndFunction;
  ASSERT_OK_AND_ASSIGN(auto builder,
                       CreateStandardRuntimeBuilder(
                           google::protobuf::DescriptorPool::generated_pool(), options));
  ASSERT_OK_AND_ASSIGN(auto runtime, std::move(builder).Build());
  activation.InsertOrAssignValue(
      "list_var",
      Value::WrapMessage(&list, google::protobuf::DescriptorPool::generated_pool(),
                         google::protobuf::MessageFactory::generated_factory(), &arena));

  // list_var[0]
  std::vector<AttributePattern> unknown_attribute_patterns;
  unknown_attribute_patterns.push_back(
      AttributePattern("list_var", {AttributeQualifierPattern::OfInt(0)}));
  ASSERT_THAT(
      activation.SetUnknownPatterns(std::move(unknown_attribute_patterns)),
      IsOk());

  ASSERT_OK_AND_ASSIGN(auto plan,
                       ProtobufRuntimeAdapter::CreateProgram(*runtime, expr));

  Value response = plan->Evaluate(&arena, activation).value();

  ASSERT_TRUE(response.IsUnknown()) << response.DebugString();
  ASSERT_EQ(response.GetUnknown().ToAttributeSet().size(), 1);

  ASSERT_EQ(
      response.GetUnknown().ToAttributeSet().begin()->qualifier_path().size(),
      1);
}

TEST(UnknownsIterAttrTest, IterAttributeTrailFilterValues) {
  RuntimeOptions options;
  Expr expr;
  Activation activation;
  Arena arena;

  protobuf::Value element;
  protobuf::Value* value =
      &element.mutable_struct_value()->mutable_fields()->operator[](
          "filter_key");
  value->set_bool_value(true);
  value = &element.mutable_struct_value()->mutable_fields()->operator[](
      "value_key");
  value->set_number_value(1.0);
  protobuf::ListValue list;
  *list.add_values() = element;
  *list.add_values() = element;
  *list.add_values() = element;

  options.unknown_processing = UnknownProcessingOptions::kAttributeAndFunction;
  ASSERT_OK_AND_ASSIGN(auto builder,
                       CreateStandardRuntimeBuilder(
                           google::protobuf::DescriptorPool::generated_pool(), options));
  ASSERT_OK_AND_ASSIGN(auto runtime, std::move(builder).Build());
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(kFilterElementsComp, &expr))
      << "error parsing expr";
  activation.InsertOrAssignValue(
      "var",
      Value::WrapMessage(&list, google::protobuf::DescriptorPool::generated_pool(),
                         google::protobuf::MessageFactory::generated_factory(), &arena));

  // var[1]['value_key'] is unknown
  ASSERT_THAT(activation.SetUnknownPatterns({AttributePattern(
                  "var",
                  {
                      AttributeQualifierPattern::OfInt(1),
                      AttributeQualifierPattern::OfString("value_key"),
                  })}),
              IsOk());

  auto plan = ProtobufRuntimeAdapter::CreateProgram(*runtime, expr).value();
  Value response = plan->Evaluate(&arena, activation).value();

  ASSERT_TRUE(response.IsUnknown()) << response.DebugString();
  ASSERT_EQ(response.GetUnknown().ToAttributeSet().size(), 1);
  // 'var[1].value_key' is unknown when we make the cons function call.
  // comprehension is:  ((([] + [1]) + unk) + [1]) -> unk
  ASSERT_EQ(
      response.GetUnknown().ToAttributeSet().begin()->qualifier_path().size(),
      2);
}

TEST(UnknownsIterAttrTest, IterAttributeTrailFilterConditions) {
  RuntimeOptions options;
  Expr expr;
  Activation activation;
  Arena arena;

  protobuf::Value element;
  protobuf::Value* value =
      &element.mutable_struct_value()->mutable_fields()->operator[](
          "filter_key");
  value->set_bool_value(true);
  value = &element.mutable_struct_value()->mutable_fields()->operator[](
      "value_key");
  value->set_number_value(1.0);
  protobuf::ListValue list;
  *list.add_values() = element;
  *list.add_values() = element;
  *list.add_values() = element;

  options.unknown_processing = UnknownProcessingOptions::kAttributeAndFunction;
  ASSERT_OK_AND_ASSIGN(auto builder,
                       CreateStandardRuntimeBuilder(
                           google::protobuf::DescriptorPool::generated_pool(), options));
  ASSERT_OK_AND_ASSIGN(auto runtime, std::move(builder).Build());
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(kFilterElementsComp, &expr))
      << "error parsing expr";
  activation.InsertOrAssignValue(
      "var",
      Value::WrapMessage(&list, google::protobuf::DescriptorPool::generated_pool(),
                         google::protobuf::MessageFactory::generated_factory(), &arena));

  // var[1]['value_key'] is unknown
  ASSERT_THAT(activation.SetUnknownPatterns(
                  {AttributePattern(
                       "var",
                       {
                           AttributeQualifierPattern::OfInt(1),
                           AttributeQualifierPattern::OfString("filter_key"),
                       }),
                   AttributePattern(
                       "var",
                       {
                           AttributeQualifierPattern::OfInt(0),
                           AttributeQualifierPattern::OfString("filter_key"),
                       })}),
              IsOk());

  auto plan = ProtobufRuntimeAdapter::CreateProgram(*runtime, expr).value();
  Value response = plan->Evaluate(&arena, activation).value();

  // 'var[1].filter_key' is unknown when we make the ternary call.
  // Since the unknown is expressed in a conditional jump, the behavior is to
  // ignore the possible outcomes
  // loop0: (unk{0})? [] + [1] : [] -> unk{0}
  // loop1: (unk{1})? unk{0} + [1] : unk{0} -> unk{1}
  // loop2: (true)? unk{1} + [1] : unk{1} -> unk{1}
  // result: unk{1}
  ASSERT_TRUE(response.IsUnknown()) << response.DebugString();
  ASSERT_EQ(response.GetUnknown().ToAttributeSet().size(), 1);
  ASSERT_EQ(
      response.GetUnknown().ToAttributeSet().begin()->qualifier_path().size(),
      2);
}

}  // namespace
}  // namespace runtime
}  // namespace expr
}  // namespace api
}  // namespace google
