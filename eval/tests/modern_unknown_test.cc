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

#include <memory>
#include <utility>

#include "google/protobuf/struct.pb.h"
#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "base/attribute.h"
#include "common/value.h"
#include "extensions/bindings_ext.h"
#include "extensions/comprehensions_v2_macros.h"
#include "internal/testing.h"
#include "internal/testing_descriptor_pool.h"
#include "internal/testing_message_factory.h"
#include "parser/options.h"
#include "parser/parser.h"
#include "parser/parser_interface.h"
#include "runtime/activation.h"
#include "runtime/constant_folding.h"
#include "runtime/optional_types.h"
#include "runtime/reference_resolver.h"
#include "runtime/runtime.h"
#include "runtime/runtime_options.h"
#include "runtime/standard_runtime_builder_factory.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/json/json.h"
#include "google/protobuf/text_format.h"

namespace {

using absl_testing::IsOk;
using ::testing::UnorderedElementsAre;

class ModernUnknownTest : public ::testing::Test {
 public:
  void SetUp() override {
    auto parser_builder = cel::NewParserBuilder(cel::ParserOptions{
        .enable_optional_syntax = true,
    });
    ABSL_CHECK_OK(cel::extensions::RegisterComprehensionsV2Macros(  // Crash OK
        *parser_builder));
    for (const auto& macro : cel::extensions::bindings_macros()) {
      ABSL_CHECK_OK(parser_builder->AddMacro(macro));  // Crash OK
    }
    auto status_or_parser = parser_builder->Build();
    ABSL_CHECK_OK(status_or_parser);  // Crash OK
    parser_ = *std::move(status_or_parser);
    auto status_or_runtime_builder = cel::CreateStandardRuntimeBuilder(
        cel::internal::GetTestingDescriptorPool(),
        cel::RuntimeOptions{
            .unknown_processing =
                cel::UnknownProcessingOptions::kAttributeAndFunction,
            .enable_missing_attribute_errors = true,
            .enable_qualified_type_identifiers = true});
    ABSL_CHECK_OK(status_or_runtime_builder);  // Crash OK
    auto runtime_builder = *std::move(status_or_runtime_builder);
    ABSL_CHECK_OK(cel::EnableReferenceResolver(
        runtime_builder,
        cel::ReferenceResolverEnabled::kCheckedExpressionOnly));
    ABSL_CHECK_OK(cel::extensions::EnableConstantFolding(
        runtime_builder, cel::internal::GetTestingMessageFactory()));
    ABSL_CHECK_OK(cel::extensions::EnableOptionalTypes(runtime_builder));
    auto status_or_runtime = std::move(runtime_builder).Build();
    ABSL_CHECK_OK(status_or_runtime);  // Crash OK
    runtime_ = *std::move(status_or_runtime);
  }

  std::unique_ptr<cel::Program> CreateProgram(absl::string_view text) {
    auto status_or_source = parser_->PrepareSource(text);
    ABSL_CHECK_OK(status_or_source);  // Crash OK
    auto source = *std::move(status_or_source);
    auto status_or_ast = parser_->Parse(*source);
    ABSL_CHECK_OK(status_or_ast);  // Crash OK
    auto ast = *std::move(status_or_ast);
    auto status_or_program = runtime_->CreateProgram(std::move(ast));
    ABSL_CHECK_OK(status_or_program);  // Crash OK
    return *std::move(status_or_program);
  }

  void TearDown() override {
    parser_.reset();
    runtime_.reset();
  }

  template <typename T>
  void BindProtoToActivation(absl::string_view variable,
                             absl::string_view text) {
    T message;
    ABSL_CHECK(
        google::protobuf::TextFormat::ParseFromString(text, &message));  // Crash OK
    activation_.InsertOrAssignValue(
        variable,
        cel::Value::FromMessage(
            std::move(message), cel::internal::GetTestingDescriptorPool(),
            cel::internal::GetTestingMessageFactory(), &arena_));
  }

  void BindJsonToActivation(absl::string_view variable,
                            absl::string_view text) {
    google::protobuf::Value message;
    ABSL_CHECK_OK(
        google::protobuf::json::JsonStringToMessage(text, &message));  // Crash OK
    activation_.InsertOrAssignValue(
        variable,
        cel::Value::FromMessage(
            std::move(message), cel::internal::GetTestingDescriptorPool(),
            cel::internal::GetTestingMessageFactory(), &arena_));
  }

  absl::StatusOr<cel::Value> Evaluate(const cel::Program& program) {
    return program.Evaluate(&arena_, cel::internal::GetTestingMessageFactory(),
                            activation_);
  }

 protected:
  std::unique_ptr<cel::Parser> parser_;
  std::unique_ptr<cel::Runtime> runtime_;
  google::protobuf::Arena arena_;
  cel::Activation activation_;
};

TEST_F(ModernUnknownTest, And) {
  auto program = CreateProgram(R"cel(var1 && var2)cel");

  {
    // unk && true
    ASSERT_THAT(activation_.SetUnknownPatterns({cel::AttributePattern("var1")}),
                IsOk());
    activation_.InsertOrAssignValue("var2", cel::TrueValue());
    ASSERT_OK_AND_ASSIGN(auto result, Evaluate(*program));
    ASSERT_TRUE(result.IsUnknown());
    EXPECT_THAT(result.GetUnknown().ToAttributeSet(),
                UnorderedElementsAre(cel::Attribute("var1")));
  }
  {
    // true && unk
    activation_.InsertOrAssignValue("var1", cel::TrueValue());
    ASSERT_THAT(activation_.SetUnknownPatterns({cel::AttributePattern("var2")}),
                IsOk());
    ASSERT_OK_AND_ASSIGN(auto result, Evaluate(*program));
    ASSERT_TRUE(result.IsUnknown());
    EXPECT_THAT(result.GetUnknown().ToAttributeSet(),
                UnorderedElementsAre(cel::Attribute("var2")));
  }
  {
    // false && unk
    activation_.InsertOrAssignValue("var1", cel::FalseValue());
    ASSERT_THAT(activation_.SetUnknownPatterns({cel::AttributePattern("var2")}),
                IsOk());
    ASSERT_OK_AND_ASSIGN(auto result, Evaluate(*program));
    ASSERT_TRUE(result.IsFalse());
  }
  {
    // unk && false
    ASSERT_THAT(activation_.SetUnknownPatterns({cel::AttributePattern("var1")}),
                IsOk());
    activation_.InsertOrAssignValue("var2", cel::FalseValue());
    ASSERT_OK_AND_ASSIGN(auto result, Evaluate(*program));
    ASSERT_TRUE(result.IsFalse());
  }
  {
    // error && unk
    activation_.InsertOrAssignValue(
        "var1", cel::ErrorValue::From(absl::CancelledError(), &arena_));
    ASSERT_THAT(activation_.SetUnknownPatterns({cel::AttributePattern("var2")}),
                IsOk());
    ASSERT_OK_AND_ASSIGN(auto result, Evaluate(*program));
    ASSERT_TRUE(result.IsUnknown());
    EXPECT_THAT(result.GetUnknown().ToAttributeSet(),
                UnorderedElementsAre(cel::Attribute("var2")));
  }
  {
    // unk && error
    ASSERT_THAT(activation_.SetUnknownPatterns({cel::AttributePattern("var1")}),
                IsOk());
    activation_.InsertOrAssignValue(
        "var2", cel::ErrorValue::From(absl::CancelledError(), &arena_));
    ASSERT_OK_AND_ASSIGN(auto result, Evaluate(*program));
    ASSERT_TRUE(result.IsUnknown());
    EXPECT_THAT(result.GetUnknown().ToAttributeSet(),
                UnorderedElementsAre(cel::Attribute("var1")));
  }
  {
    // unk && unk
    ASSERT_THAT(activation_.SetUnknownPatterns({cel::AttributePattern("var1"),
                                                cel::AttributePattern("var2")}),
                IsOk());
    ASSERT_OK_AND_ASSIGN(auto result, Evaluate(*program));
    ASSERT_TRUE(result.IsUnknown());
    EXPECT_THAT(
        result.GetUnknown().ToAttributeSet(),
        UnorderedElementsAre(cel::Attribute("var1"), cel::Attribute("var2")));
  }
}

TEST_F(ModernUnknownTest, Or) {
  auto program = CreateProgram(R"cel(var1 || var2)cel");

  {
    // unk || true
    ASSERT_THAT(activation_.SetUnknownPatterns({cel::AttributePattern("var1")}),
                IsOk());
    activation_.InsertOrAssignValue("var2", cel::TrueValue());
    ASSERT_OK_AND_ASSIGN(auto result, Evaluate(*program));
    ASSERT_TRUE(result.IsTrue());
  }
  {
    // true || unk
    activation_.InsertOrAssignValue("var1", cel::TrueValue());
    ASSERT_THAT(activation_.SetUnknownPatterns({cel::AttributePattern("var2")}),
                IsOk());
    ASSERT_OK_AND_ASSIGN(auto result, Evaluate(*program));
    ASSERT_TRUE(result.IsTrue());
  }
  {
    // false || unk
    activation_.InsertOrAssignValue("var1", cel::FalseValue());
    ASSERT_THAT(activation_.SetUnknownPatterns({cel::AttributePattern("var2")}),
                IsOk());
    ASSERT_OK_AND_ASSIGN(auto result, Evaluate(*program));
    ASSERT_TRUE(result.IsUnknown());
    EXPECT_THAT(result.GetUnknown().ToAttributeSet(),
                UnorderedElementsAre(cel::Attribute("var2")));
  }
  {
    // unk || false
    ASSERT_THAT(activation_.SetUnknownPatterns({cel::AttributePattern("var1")}),
                IsOk());
    activation_.InsertOrAssignValue("var2", cel::FalseValue());
    ASSERT_OK_AND_ASSIGN(auto result, Evaluate(*program));
    ASSERT_TRUE(result.IsUnknown());
    EXPECT_THAT(result.GetUnknown().ToAttributeSet(),
                UnorderedElementsAre(cel::Attribute("var1")));
  }
  {
    // error || unk
    activation_.InsertOrAssignValue(
        "var1", cel::ErrorValue::From(absl::CancelledError(), &arena_));
    ASSERT_THAT(activation_.SetUnknownPatterns({cel::AttributePattern("var2")}),
                IsOk());
    ASSERT_OK_AND_ASSIGN(auto result, Evaluate(*program));
    ASSERT_TRUE(result.IsUnknown());
    EXPECT_THAT(result.GetUnknown().ToAttributeSet(),
                UnorderedElementsAre(cel::Attribute("var2")));
  }
  {
    // unk || error
    ASSERT_THAT(activation_.SetUnknownPatterns({cel::AttributePattern("var1")}),
                IsOk());
    activation_.InsertOrAssignValue(
        "var2", cel::ErrorValue::From(absl::CancelledError(), &arena_));
    ASSERT_OK_AND_ASSIGN(auto result, Evaluate(*program));
    ASSERT_TRUE(result.IsUnknown());
    EXPECT_THAT(result.GetUnknown().ToAttributeSet(),
                UnorderedElementsAre(cel::Attribute("var1")));
  }
  {
    // unk || unk
    ASSERT_THAT(activation_.SetUnknownPatterns({cel::AttributePattern("var1"),
                                                cel::AttributePattern("var2")}),
                IsOk());
    ASSERT_OK_AND_ASSIGN(auto result, Evaluate(*program));
    ASSERT_TRUE(result.IsUnknown());
    EXPECT_THAT(
        result.GetUnknown().ToAttributeSet(),
        UnorderedElementsAre(cel::Attribute("var1"), cel::Attribute("var2")));
  }
}

TEST_F(ModernUnknownTest, Ternary) {
  auto program = CreateProgram(R"cel(var1 ? var2 : var3)cel");

  {
    // unk ? true : false
    ASSERT_THAT(activation_.SetUnknownPatterns({cel::AttributePattern("var1")}),
                IsOk());
    activation_.InsertOrAssignValue("var2", cel::TrueValue());
    activation_.InsertOrAssignValue("var3", cel::FalseValue());
    ASSERT_OK_AND_ASSIGN(auto result, Evaluate(*program));
    ASSERT_TRUE(result.IsUnknown());
    EXPECT_THAT(result.GetUnknown().ToAttributeSet(),
                UnorderedElementsAre(cel::Attribute("var1")));
  }
  {
    // true ? unk : false
    activation_.InsertOrAssignValue("var1", cel::TrueValue());
    ASSERT_THAT(activation_.SetUnknownPatterns({cel::AttributePattern("var2")}),
                IsOk());
    activation_.InsertOrAssignValue("var3", cel::FalseValue());
    ASSERT_OK_AND_ASSIGN(auto result, Evaluate(*program));
    ASSERT_TRUE(result.IsUnknown());
    EXPECT_THAT(result.GetUnknown().ToAttributeSet(),
                UnorderedElementsAre(cel::Attribute("var2")));
  }
  {
    // false ? unk : false
    activation_.InsertOrAssignValue("var1", cel::FalseValue());
    ASSERT_THAT(activation_.SetUnknownPatterns({cel::AttributePattern("var2")}),
                IsOk());
    activation_.InsertOrAssignValue("var3", cel::FalseValue());
    ASSERT_OK_AND_ASSIGN(auto result, Evaluate(*program));
    ASSERT_TRUE(result.IsFalse());
  }
  {
    // error ? unk : false
    activation_.InsertOrAssignValue(
        "var1", cel::ErrorValue::From(absl::CancelledError(), &arena_));
    ASSERT_THAT(activation_.SetUnknownPatterns({cel::AttributePattern("var2")}),
                IsOk());
    activation_.InsertOrAssignValue("var3", cel::FalseValue());
    ASSERT_OK_AND_ASSIGN(auto result, Evaluate(*program));
    ASSERT_TRUE(result.IsError());
  }
  {
    // unk ? error : false
    ASSERT_THAT(activation_.SetUnknownPatterns({cel::AttributePattern("var1")}),
                IsOk());
    activation_.InsertOrAssignValue(
        "var2", cel::ErrorValue::From(absl::CancelledError(), &arena_));
    ASSERT_OK_AND_ASSIGN(auto result, Evaluate(*program));
    activation_.InsertOrAssignValue("var3", cel::FalseValue());
    ASSERT_TRUE(result.IsUnknown());
    EXPECT_THAT(result.GetUnknown().ToAttributeSet(),
                UnorderedElementsAre(cel::Attribute("var1")));
  }
  {
    // unk ? unk : false
    ASSERT_THAT(activation_.SetUnknownPatterns({cel::AttributePattern("var1"),
                                                cel::AttributePattern("var2")}),
                IsOk());
    activation_.InsertOrAssignValue("var3", cel::FalseValue());
    ASSERT_OK_AND_ASSIGN(auto result, Evaluate(*program));
    ASSERT_TRUE(result.IsUnknown());
    EXPECT_THAT(result.GetUnknown().ToAttributeSet(),
                UnorderedElementsAre(cel::Attribute("var1")));
  }
  {
    // unk ? true : unk
    ASSERT_THAT(activation_.SetUnknownPatterns({cel::AttributePattern("var1"),
                                                cel::AttributePattern("var3")}),
                IsOk());
    activation_.InsertOrAssignValue("var2", cel::TrueValue());
    ASSERT_OK_AND_ASSIGN(auto result, Evaluate(*program));
    ASSERT_TRUE(result.IsUnknown());
    EXPECT_THAT(result.GetUnknown().ToAttributeSet(),
                UnorderedElementsAre(cel::Attribute("var1")));
  }
}

TEST_F(ModernUnknownTest, ListUnknown) {
  auto program = CreateProgram(R"cel([var1, var2])cel");
  ASSERT_THAT(activation_.SetUnknownPatterns({cel::AttributePattern("var1"),
                                              cel::AttributePattern("var2")}),
              IsOk());
  ASSERT_OK_AND_ASSIGN(auto result, Evaluate(*program));
  ASSERT_TRUE(result.IsUnknown());
  EXPECT_THAT(
      result.GetUnknown().ToAttributeSet(),
      UnorderedElementsAre(cel::Attribute("var1"), cel::Attribute("var2")));
}

TEST_F(ModernUnknownTest, ListErrorUnknown) {
  auto program = CreateProgram(R"cel([var1, 1 / 0])cel");
  ASSERT_THAT(activation_.SetUnknownPatterns({cel::AttributePattern("var1")}),
              IsOk());
  ASSERT_OK_AND_ASSIGN(auto result, Evaluate(*program));
  EXPECT_TRUE(result.IsError());
}

TEST_F(ModernUnknownTest, MapUnknown) {
  auto program = CreateProgram(R"cel({"foo": var1, "bar": var2})cel");
  ASSERT_THAT(activation_.SetUnknownPatterns({cel::AttributePattern("var1"),
                                              cel::AttributePattern("var2")}),
              IsOk());
  ASSERT_OK_AND_ASSIGN(auto result, Evaluate(*program));
  ASSERT_TRUE(result.IsUnknown());
  EXPECT_THAT(
      result.GetUnknown().ToAttributeSet(),
      UnorderedElementsAre(cel::Attribute("var1"), cel::Attribute("var2")));
}

TEST_F(ModernUnknownTest, MapErrorUnknown) {
  auto program = CreateProgram(R"cel({"foo": var1, "bar": 1 / 0})cel");
  ASSERT_THAT(activation_.SetUnknownPatterns({cel::AttributePattern("var1")}),
              IsOk());
  ASSERT_OK_AND_ASSIGN(auto result, Evaluate(*program));
  EXPECT_TRUE(result.IsError());
}

TEST_F(ModernUnknownTest, StructUnknown) {
  auto program = CreateProgram(
      R"cel(cel.expr.conformance.proto3.TestAllTypes{single_int64: var1, single_uint64: var2})cel");
  ASSERT_THAT(activation_.SetUnknownPatterns({cel::AttributePattern("var1"),
                                              cel::AttributePattern("var2")}),
              IsOk());
  ASSERT_OK_AND_ASSIGN(auto result, Evaluate(*program));
  ASSERT_TRUE(result.IsUnknown());
  EXPECT_THAT(
      result.GetUnknown().ToAttributeSet(),
      UnorderedElementsAre(cel::Attribute("var1"), cel::Attribute("var2")));
}

TEST_F(ModernUnknownTest, StructErrorUnknown) {
  auto program = CreateProgram(
      R"cel(cel.expr.conformance.proto3.TestAllTypes{single_int64: var1, single_uint64: 1u / 0u})cel");
  ASSERT_THAT(activation_.SetUnknownPatterns({cel::AttributePattern("var1")}),
              IsOk());
  ASSERT_OK_AND_ASSIGN(auto result, Evaluate(*program));
  EXPECT_TRUE(result.IsError());
}

TEST_F(ModernUnknownTest, Variable) {
  auto program = CreateProgram(R"cel(var1)cel");
  ASSERT_THAT(activation_.SetUnknownPatterns({cel::AttributePattern("var1")}),
              IsOk());
  BindJsonToActivation("var1", R"json({})json");
  ASSERT_OK_AND_ASSIGN(auto result, Evaluate(*program));
  ASSERT_TRUE(result.IsUnknown());
  EXPECT_THAT(result.GetUnknown().ToAttributeSet(),
              UnorderedElementsAre(cel::Attribute("var1")));
}

TEST_F(ModernUnknownTest, ReturnPartiallyUnknownValue) {
  // This is an unfortunate but existing bug that we should eventually fix. We
  // return a partial unknown from evaluation.
  auto program = CreateProgram(R"cel(var1)cel");
  ASSERT_THAT(activation_.SetUnknownPatterns({cel::AttributePattern(
                  "var1", {cel::AttributeQualifierPattern::Wildcard()})}),
              IsOk());
  BindJsonToActivation("var1", R"json({})json");
  ASSERT_OK_AND_ASSIGN(auto result, Evaluate(*program));
  ASSERT_TRUE(result.IsMap());
}

TEST_F(ModernUnknownTest, MapVariableSelectStringQualifier) {
  auto program = CreateProgram(R"cel(var1.k)cel");
  ASSERT_THAT(activation_.SetUnknownPatterns({cel::AttributePattern(
                  "var1", {cel::AttributeQualifierPattern::OfString("k")})}),
              IsOk());
  BindJsonToActivation("var1", R"json({})json");
  ASSERT_OK_AND_ASSIGN(auto result, Evaluate(*program));
  ASSERT_TRUE(result.IsUnknown());
  EXPECT_THAT(result.GetUnknown().ToAttributeSet(),
              UnorderedElementsAre(cel::Attribute(
                  "var1", {cel::AttributeQualifier::OfString("k")})));
}

TEST_F(ModernUnknownTest, MapVariableSelectWildcardQualifier) {
  auto program = CreateProgram(R"cel(var1.k)cel");
  ASSERT_THAT(activation_.SetUnknownPatterns({cel::AttributePattern(
                  "var1", {cel::AttributeQualifierPattern::Wildcard()})}),
              IsOk());
  BindJsonToActivation("var1", R"json({})json");
  ASSERT_OK_AND_ASSIGN(auto result, Evaluate(*program));
  ASSERT_TRUE(result.IsUnknown());
  EXPECT_THAT(result.GetUnknown().ToAttributeSet(),
              UnorderedElementsAre(cel::Attribute(
                  "var1", {cel::AttributeQualifier::OfString("k")})));
}

TEST_F(ModernUnknownTest, ListVariableIndexIntQualifier) {
  auto program = CreateProgram(R"cel(var1[0])cel");
  ASSERT_THAT(activation_.SetUnknownPatterns({cel::AttributePattern(
                  "var1", {cel::AttributeQualifierPattern::OfInt(0)})}),
              IsOk());
  BindJsonToActivation("var1", R"json([])json");
  ASSERT_OK_AND_ASSIGN(auto result, Evaluate(*program));
  ASSERT_TRUE(result.IsUnknown());
  EXPECT_THAT(result.GetUnknown().ToAttributeSet(),
              UnorderedElementsAre(
                  cel::Attribute("var1", {cel::AttributeQualifier::OfInt(0)})));
}

TEST_F(ModernUnknownTest, ListVariableIndexWildcardQualifier) {
  auto program = CreateProgram(R"cel(var1[0])cel");
  ASSERT_THAT(activation_.SetUnknownPatterns({cel::AttributePattern(
                  "var1", {cel::AttributeQualifierPattern::Wildcard()})}),
              IsOk());
  BindJsonToActivation("var1", R"json([])json");
  ASSERT_OK_AND_ASSIGN(auto result, Evaluate(*program));
  ASSERT_TRUE(result.IsUnknown());
  EXPECT_THAT(result.GetUnknown().ToAttributeSet(),
              UnorderedElementsAre(
                  cel::Attribute("var1", {cel::AttributeQualifier::OfInt(0)})));
}

}  // namespace
