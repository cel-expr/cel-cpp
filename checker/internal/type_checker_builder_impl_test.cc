// Copyright 2024 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "checker/internal/type_checker_builder_impl.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "checker/checker_options.h"
#include "checker/internal/test_ast_helpers.h"
#include "checker/type_checker.h"
#include "checker/type_checker_builder.h"
#include "checker/validation_result.h"
#include "common/ast.h"
#include "common/decl.h"
#include "common/type.h"
#include "common/type_introspector.h"
#include "internal/testing.h"
#include "internal/testing_descriptor_pool.h"
#include "google/protobuf/arena.h"

namespace cel::checker_internal {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;

struct ContextDeclsTestCase {
  std::string expr;
  TypeSpec expected_type;
};

class ContextDeclsFieldsDefinedTest
    : public testing::TestWithParam<ContextDeclsTestCase> {};

TEST_P(ContextDeclsFieldsDefinedTest, ContextDeclsFieldsDefined) {
  TypeCheckerBuilderImpl builder(internal::GetSharedTestingDescriptorPool(),
                                 {});
  ASSERT_THAT(
      builder.AddContextDeclaration("cel.expr.conformance.proto3.TestAllTypes"),
      IsOk());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<TypeChecker> type_checker,
                       builder.Build());
  ASSERT_OK_AND_ASSIGN(auto ast, MakeTestParsedAst(GetParam().expr));
  ASSERT_OK_AND_ASSIGN(ValidationResult result,
                       type_checker->Check(std::move(ast)));

  ASSERT_TRUE(result.IsValid());

  EXPECT_EQ(result.GetAst()->GetReturnType(), GetParam().expected_type);
}

INSTANTIATE_TEST_SUITE_P(
    TestAllTypes, ContextDeclsFieldsDefinedTest,
    testing::Values(
        ContextDeclsTestCase{"single_int64", TypeSpec(PrimitiveType::kInt64)},
        ContextDeclsTestCase{"single_uint32", TypeSpec(PrimitiveType::kUint64)},
        ContextDeclsTestCase{"single_double", TypeSpec(PrimitiveType::kDouble)},
        ContextDeclsTestCase{"single_string", TypeSpec(PrimitiveType::kString)},
        ContextDeclsTestCase{"single_any", TypeSpec(WellKnownTypeSpec::kAny)},
        ContextDeclsTestCase{"single_duration",
                             TypeSpec(WellKnownTypeSpec::kDuration)},
        ContextDeclsTestCase{
            "single_bool_wrapper",
            TypeSpec(PrimitiveTypeWrapper(PrimitiveType::kBool))},
        ContextDeclsTestCase{
            "list_value",
            TypeSpec(ListTypeSpec(std::make_unique<TypeSpec>(DynTypeSpec())))},
        ContextDeclsTestCase{
            "standalone_message",
            TypeSpec(MessageTypeSpec(
                "cel.expr.conformance.proto3.TestAllTypes.NestedMessage"))},
        ContextDeclsTestCase{"standalone_enum",
                             TypeSpec(PrimitiveType::kInt64)},
        ContextDeclsTestCase{"repeated_bytes",
                             TypeSpec(ListTypeSpec(std::make_unique<TypeSpec>(
                                 PrimitiveType::kBytes)))},
        ContextDeclsTestCase{"repeated_nested_enum",
                             TypeSpec(ListTypeSpec(std::make_unique<TypeSpec>(
                                 PrimitiveType::kInt64)))},
        ContextDeclsTestCase{
            "repeated_nested_message",
            TypeSpec(ListTypeSpec(std::make_unique<TypeSpec>(MessageTypeSpec(
                "cel.expr.conformance.proto3.TestAllTypes.NestedMessage"))))},
        ContextDeclsTestCase{
            "map_int32_timestamp",
            TypeSpec(MapTypeSpec(
                std::make_unique<TypeSpec>(PrimitiveType::kInt64),
                std::make_unique<TypeSpec>(WellKnownTypeSpec::kTimestamp)))},
        ContextDeclsTestCase{
            "single_struct",
            TypeSpec(
                MapTypeSpec(std::make_unique<TypeSpec>(PrimitiveType::kString),
                            std::make_unique<TypeSpec>(DynTypeSpec())))}));

TEST(ContextDeclsWithProtoTypeMaskTest, ErrorOnEmptyFieldPaths) {
  TypeCheckerBuilderImpl builder(internal::GetSharedTestingDescriptorPool(),
                                 {});
  ASSERT_THAT(builder.AddContextDeclarationWithProtoTypeMask(
                  "cel.expr.conformance.proto3.TestAllTypes", {}),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "field paths cannot be the empty set"));
}

TEST(ContextDeclsWithProtoTypeMaskTest, ErrorOnUnknownFieldPath) {
  TypeCheckerBuilderImpl builder(internal::GetSharedTestingDescriptorPool(),
                                 {});
  ASSERT_THAT(
      builder.AddContextDeclarationWithProtoTypeMask(
          "cel.expr.conformance.proto3.TestAllTypes", {"unknown_field"}),
      StatusIs(absl::StatusCode::kInvalidArgument,
               "could not select field 'unknown_field' from type "
               "'cel.expr.conformance.proto3.TestAllTypes'"));
}

class ContextDeclsWithProtoTypeMaskFieldsDefinedTest
    : public testing::TestWithParam<ContextDeclsTestCase> {};

std::string LogFieldName(absl::string_view field_name, absl::string_view expr) {
  return absl::StrCat("field_name: ", field_name, ", expr: ", expr);
}

TEST_P(ContextDeclsWithProtoTypeMaskFieldsDefinedTest,
       ContextDeclsWithProtoTypeMaskFieldsDefined) {
  TypeCheckerBuilderImpl builder(internal::GetSharedTestingDescriptorPool(),
                                 {});
  ASSERT_THAT(
      builder.AddContextDeclarationWithProtoTypeMask(
          "cel.expr.conformance.proto3.TestAllTypes", {GetParam().expr}),
      IsOk());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<TypeChecker> type_checker,
                       builder.Build());
  std::vector<std::string> field_names = {"single_int64",
                                          "single_uint32",
                                          "single_double",
                                          "single_string",
                                          "single_any",
                                          "single_duration",
                                          "single_bool_wrapper",
                                          "list_value",
                                          "standalone_message",
                                          "standalone_enum",
                                          "repeated_bytes",
                                          "repeated_nested_enum",
                                          "repeated_nested_message",
                                          "map_int32_timestamp",
                                          "single_struct"};
  for (auto& field_name : field_names) {
    ASSERT_OK_AND_ASSIGN(auto ast, MakeTestParsedAst(field_name));
    ASSERT_OK_AND_ASSIGN(ValidationResult result,
                         type_checker->Check(std::move(ast)));
    if (field_name == GetParam().expr) {
      // The field name that is part of the proto type mask is visible.
      ASSERT_TRUE(result.IsValid())
          << LogFieldName(field_name, GetParam().expr);
      EXPECT_EQ(result.GetAst()->GetReturnType(), GetParam().expected_type)
          << LogFieldName(field_name, GetParam().expr);
    } else {
      // The field names that are not part of the proto type mask are not
      // visible.
      EXPECT_FALSE(result.IsValid())
          << LogFieldName(field_name, GetParam().expr);
    }
  }
}

INSTANTIATE_TEST_SUITE_P(
    TestAllTypes, ContextDeclsWithProtoTypeMaskFieldsDefinedTest,
    testing::Values(
        ContextDeclsTestCase{"single_int64", TypeSpec(PrimitiveType::kInt64)},
        ContextDeclsTestCase{"single_uint32", TypeSpec(PrimitiveType::kUint64)},
        ContextDeclsTestCase{"single_double", TypeSpec(PrimitiveType::kDouble)},
        ContextDeclsTestCase{"single_string", TypeSpec(PrimitiveType::kString)},
        ContextDeclsTestCase{"single_any", TypeSpec(WellKnownTypeSpec::kAny)},
        ContextDeclsTestCase{"single_duration",
                             TypeSpec(WellKnownTypeSpec::kDuration)},
        ContextDeclsTestCase{
            "single_bool_wrapper",
            TypeSpec(PrimitiveTypeWrapper(PrimitiveType::kBool))},
        ContextDeclsTestCase{
            "list_value",
            TypeSpec(ListTypeSpec(std::make_unique<TypeSpec>(DynTypeSpec())))},
        ContextDeclsTestCase{
            "standalone_message",
            TypeSpec(MessageTypeSpec(
                "cel.expr.conformance.proto3.TestAllTypes.NestedMessage"))},
        ContextDeclsTestCase{"standalone_enum",
                             TypeSpec(PrimitiveType::kInt64)},
        ContextDeclsTestCase{"repeated_bytes",
                             TypeSpec(ListTypeSpec(std::make_unique<TypeSpec>(
                                 PrimitiveType::kBytes)))},
        ContextDeclsTestCase{"repeated_nested_enum",
                             TypeSpec(ListTypeSpec(std::make_unique<TypeSpec>(
                                 PrimitiveType::kInt64)))},
        ContextDeclsTestCase{
            "repeated_nested_message",
            TypeSpec(ListTypeSpec(std::make_unique<TypeSpec>(MessageTypeSpec(
                "cel.expr.conformance.proto3.TestAllTypes.NestedMessage"))))},
        ContextDeclsTestCase{
            "map_int32_timestamp",
            TypeSpec(MapTypeSpec(
                std::make_unique<TypeSpec>(PrimitiveType::kInt64),
                std::make_unique<TypeSpec>(WellKnownTypeSpec::kTimestamp)))},
        ContextDeclsTestCase{
            "single_struct",
            TypeSpec(
                MapTypeSpec(std::make_unique<TypeSpec>(PrimitiveType::kString),
                            std::make_unique<TypeSpec>(DynTypeSpec())))}));

TEST(ContextDeclsWithProtoTypeMaskTest, FieldsInMaskAreVisibleFieldAccess) {
  TypeCheckerBuilderImpl builder(internal::GetSharedTestingDescriptorPool(),
                                 {});
  ASSERT_THAT(builder.AddContextDeclarationWithProtoTypeMask(
                  "cel.expr.conformance.proto3.NestedTestAllTypes",
                  {"payload.standalone_message.bb", "payload.single_int32"}),
              IsOk());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<TypeChecker> type_checker,
                       builder.Build());
  // Visible field: standalone_message.bb
  ASSERT_OK_AND_ASSIGN(auto ast,
                       MakeTestParsedAst("payload.standalone_message.bb"));
  ASSERT_OK_AND_ASSIGN(ValidationResult result,
                       type_checker->Check(std::move(ast)));
  ASSERT_TRUE(result.IsValid());
  EXPECT_EQ(result.GetAst()->GetReturnType(), TypeSpec(PrimitiveType::kInt64));
  // Visible field: single_int32
  ASSERT_OK_AND_ASSIGN(ast, MakeTestParsedAst("payload.single_int32"));
  ASSERT_OK_AND_ASSIGN(result, type_checker->Check(std::move(ast)));
  ASSERT_TRUE(result.IsValid());
  EXPECT_EQ(result.GetAst()->GetReturnType(), TypeSpec(PrimitiveType::kInt64));
  // Not Visible field: single_int64
  ASSERT_OK_AND_ASSIGN(ast, MakeTestParsedAst("payload.single_int64"));
  ASSERT_OK_AND_ASSIGN(result, type_checker->Check(std::move(ast)));
  ASSERT_FALSE(result.IsValid());
}

TEST(ContextDeclsWithProtoTypeMaskTest, FieldsInMaskAreVisibleFieldAssignment) {
  TypeCheckerBuilderImpl builder(internal::GetSharedTestingDescriptorPool(),
                                 {});
  ASSERT_THAT(builder.AddContextDeclarationWithProtoTypeMask(
                  "cel.expr.conformance.proto3.NestedTestAllTypes",
                  {"payload.standalone_message.bb", "payload.single_int32"}),
              IsOk());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<TypeChecker> type_checker,
                       builder.Build());
  // Visible field: standalone_message.bb
  ASSERT_OK_AND_ASSIGN(
      auto ast,
      MakeTestParsedAst(
          R"(cel.expr.conformance.proto3.TestAllTypes.NestedMessage{bb: 12345})"));
  ASSERT_OK_AND_ASSIGN(ValidationResult result,
                       type_checker->Check(std::move(ast)));
  ASSERT_TRUE(result.IsValid());
  // Visible field: single_int32
  ASSERT_OK_AND_ASSIGN(
      ast,
      MakeTestParsedAst(
          R"(cel.expr.conformance.proto3.TestAllTypes{single_int32: 12345})"));
  ASSERT_OK_AND_ASSIGN(result, type_checker->Check(std::move(ast)));
  ASSERT_TRUE(result.IsValid());
  // Not Visible field: single_int64
  ASSERT_OK_AND_ASSIGN(
      ast,
      MakeTestParsedAst(
          R"(cel.expr.conformance.proto3.TestAllTypes{single_int64: 12345})"));
  ASSERT_OK_AND_ASSIGN(result, type_checker->Check(std::move(ast)));
  ASSERT_FALSE(result.IsValid());
}

TEST(ContextDeclsTest, ErrorOnDuplicateContextDeclaration) {
  TypeCheckerBuilderImpl builder(internal::GetSharedTestingDescriptorPool(),
                                 {});
  ASSERT_THAT(
      builder.AddContextDeclaration("cel.expr.conformance.proto3.TestAllTypes"),
      IsOk());
  EXPECT_THAT(
      builder.AddContextDeclaration("cel.expr.conformance.proto3.TestAllTypes"),
      StatusIs(absl::StatusCode::kAlreadyExists,
               "context declaration 'cel.expr.conformance.proto3.TestAllTypes' "
               "already exists"));
}

TEST(ContextDeclsWithProtoTypeMaskTest, ErrorOnDuplicateContextDeclaration) {
  TypeCheckerBuilderImpl builder(internal::GetSharedTestingDescriptorPool(),
                                 {});
  ASSERT_THAT(
      builder.AddContextDeclarationWithProtoTypeMask(
          "cel.expr.conformance.proto3.TestAllTypes", {"standalone_message"}),
      IsOk());
  EXPECT_THAT(
      builder.AddContextDeclaration("cel.expr.conformance.proto3.TestAllTypes"),
      StatusIs(absl::StatusCode::kAlreadyExists,
               "context declaration 'cel.expr.conformance.proto3.TestAllTypes' "
               "already exists"));
}

TEST(ContextDeclsTest, ErrorOnContextDeclarationNotFound) {
  TypeCheckerBuilderImpl builder(internal::GetSharedTestingDescriptorPool(),
                                 {});
  EXPECT_THAT(
      builder.AddContextDeclaration("com.example.UnknownType"),
      StatusIs(absl::StatusCode::kNotFound,
               "context declaration 'com.example.UnknownType' not found"));
}

TEST(ContextDeclsWithProtoTypeMaskTest, ErrorOnContextDeclarationNotFound) {
  TypeCheckerBuilderImpl builder(internal::GetSharedTestingDescriptorPool(),
                                 {});
  EXPECT_THAT(
      builder.AddContextDeclarationWithProtoTypeMask("com.example.UnknownType",
                                                     {"any_field_name"}),
      StatusIs(absl::StatusCode::kNotFound,
               "context declaration 'com.example.UnknownType' not found"));
}

TEST(ContextDeclsTest, ErrorOnNonStructMessageType) {
  TypeCheckerBuilderImpl builder(internal::GetSharedTestingDescriptorPool(),
                                 {});
  EXPECT_THAT(
      builder.AddContextDeclaration("google.protobuf.Timestamp"),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          "context declaration 'google.protobuf.Timestamp' is not a struct"));
}

TEST(ContextDeclsWithProtoTypeMaskTest, ErrorOnNonStructMessageType) {
  TypeCheckerBuilderImpl builder(internal::GetSharedTestingDescriptorPool(),
                                 {});
  EXPECT_THAT(
      builder.AddContextDeclarationWithProtoTypeMask(
          "google.protobuf.Timestamp", {"any_field_name"}),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          "context declaration 'google.protobuf.Timestamp' is not a struct"));
}

TEST(ContextDeclsTest, CustomStructNotSupported) {
  TypeCheckerBuilderImpl builder(internal::GetSharedTestingDescriptorPool(),
                                 {});
  class MyTypeProvider : public cel::TypeIntrospector {
   public:
    absl::StatusOr<std::optional<Type>> FindTypeImpl(
        absl::string_view name) const override {
      if (name == "com.example.MyStruct") {
        return common_internal::MakeBasicStructType("com.example.MyStruct");
      }
      return std::nullopt;
    }
  };

  builder.AddTypeProvider(std::make_unique<MyTypeProvider>());

  EXPECT_THAT(builder.AddContextDeclaration("com.example.MyStruct"),
              StatusIs(absl::StatusCode::kNotFound,
                       "context declaration 'com.example.MyStruct' not found"));
}

TEST(ContextDeclsWithProtoTypeMaskTest, CustomStructNotSupported) {
  TypeCheckerBuilderImpl builder(internal::GetSharedTestingDescriptorPool(),
                                 {});
  class MyTypeProvider : public cel::TypeIntrospector {
   public:
    absl::StatusOr<std::optional<Type>> FindTypeImpl(
        absl::string_view name) const override {
      if (name == "com.example.MyStruct") {
        return common_internal::MakeBasicStructType("com.example.MyStruct");
      }
      return std::nullopt;
    }
  };

  builder.AddTypeProvider(std::make_unique<MyTypeProvider>());

  EXPECT_THAT(builder.AddContextDeclarationWithProtoTypeMask(
                  "com.example.MyStruct", {"any_field_name"}),
              StatusIs(absl::StatusCode::kNotFound,
                       "context declaration 'com.example.MyStruct' not found"));
}

TEST(ContextDeclsTest, ErrorOnOverlappingContextDeclaration) {
  TypeCheckerBuilderImpl builder(internal::GetSharedTestingDescriptorPool(),
                                 {});
  ASSERT_THAT(
      builder.AddContextDeclaration("cel.expr.conformance.proto3.TestAllTypes"),
      IsOk());
  // We resolve the context declaration variables at the Build() call, so the
  // error surfaces then.
  ASSERT_THAT(
      builder.AddContextDeclaration("cel.expr.conformance.proto2.TestAllTypes"),
      IsOk());

  EXPECT_THAT(
      builder.Build(),
      StatusIs(absl::StatusCode::kAlreadyExists,
               "variable 'single_int32' declared multiple times (from context "
               "declaration: 'cel.expr.conformance.proto2.TestAllTypes')"));
}

TEST(ContextDeclsWithProtoTypeMaskTest, ErrorOnOverlappingContextDeclaration) {
  TypeCheckerBuilderImpl builder(internal::GetSharedTestingDescriptorPool(),
                                 {});
  ASSERT_THAT(
      builder.AddContextDeclaration("cel.expr.conformance.proto3.TestAllTypes"),
      IsOk());
  // We resolve the context declaration variables at the Build() call, so the
  // error surfaces then.
  ASSERT_THAT(builder.AddContextDeclarationWithProtoTypeMask(
                  "cel.expr.conformance.proto2.TestAllTypes", {"single_int32"}),
              IsOk());

  EXPECT_THAT(
      builder.Build(),
      StatusIs(absl::StatusCode::kAlreadyExists,
               "variable 'single_int32' declared multiple times (from context "
               "declaration: 'cel.expr.conformance.proto2.TestAllTypes')"));
}

TEST(ContextDeclsWithProtoTypeMaskTest,
     ErrorOnOverlappingContextDeclarationBothProtoTypeMasks) {
  TypeCheckerBuilderImpl builder(internal::GetSharedTestingDescriptorPool(),
                                 {});
  ASSERT_THAT(builder.AddContextDeclarationWithProtoTypeMask(
                  "cel.expr.conformance.proto3.TestAllTypes", {"single_int32"}),
              IsOk());
  // We resolve the context declaration variables at the Build() call, so the
  // error surfaces then.
  ASSERT_THAT(builder.AddContextDeclarationWithProtoTypeMask(
                  "cel.expr.conformance.proto2.TestAllTypes", {"single_int32"}),
              IsOk());

  EXPECT_THAT(
      builder.Build(),
      StatusIs(absl::StatusCode::kAlreadyExists,
               "variable 'single_int32' declared multiple times (from context "
               "declaration: 'cel.expr.conformance.proto2.TestAllTypes')"));
}

TEST(ContextDeclsWithProtoTypeMaskTest,
     NonOverlappingContextDeclarationBothProtoTypeMasks) {
  TypeCheckerBuilderImpl builder(internal::GetSharedTestingDescriptorPool(),
                                 {});
  ASSERT_THAT(builder.AddContextDeclarationWithProtoTypeMask(
                  "cel.expr.conformance.proto3.TestAllTypes", {"single_int32"}),
              IsOk());
  // We resolve the context declaration variables at the Build() call, so the
  // error surfaces then.
  ASSERT_THAT(builder.AddContextDeclarationWithProtoTypeMask(
                  "cel.expr.conformance.proto2.NestedTestAllTypes",
                  {"payload.single_int64"}),
              IsOk());

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<TypeChecker> type_checker,
                       builder.Build());
  ASSERT_OK_AND_ASSIGN(auto ast, MakeTestParsedAst("single_int32"));
  ASSERT_OK_AND_ASSIGN(ValidationResult result,
                       type_checker->Check(std::move(ast)));
  ASSERT_TRUE(result.IsValid());
  ASSERT_OK_AND_ASSIGN(ast, MakeTestParsedAst("payload.single_int64"));
  ASSERT_OK_AND_ASSIGN(result, type_checker->Check(std::move(ast)));
}

TEST(ContextDeclsTest, ErrorOnOverlappingVariableDeclaration) {
  TypeCheckerBuilderImpl builder(internal::GetSharedTestingDescriptorPool(),
                                 {});
  ASSERT_THAT(
      builder.AddContextDeclaration("cel.expr.conformance.proto3.TestAllTypes"),
      IsOk());
  ASSERT_THAT(builder.AddVariable(MakeVariableDecl("single_int64", IntType())),
              IsOk());

  EXPECT_THAT(builder.Build(),
              StatusIs(absl::StatusCode::kAlreadyExists,
                       "variable 'single_int64' declared multiple times"));
}

TEST(ContextDeclsWithProtoTypeMaskTest, ErrorOnOverlappingVariableDeclaration) {
  TypeCheckerBuilderImpl builder(internal::GetSharedTestingDescriptorPool(),
                                 {});
  ASSERT_THAT(builder.AddContextDeclarationWithProtoTypeMask(
                  "cel.expr.conformance.proto3.TestAllTypes", {"single_int64"}),
              IsOk());
  ASSERT_THAT(builder.AddVariable(MakeVariableDecl("single_int64", IntType())),
              IsOk());

  EXPECT_THAT(builder.Build(),
              StatusIs(absl::StatusCode::kAlreadyExists,
                       "variable 'single_int64' declared multiple times"));
}

TEST(ContextDeclsWithProtoTypeMaskTest, NonOverlappingVariableDeclaration) {
  TypeCheckerBuilderImpl builder(internal::GetSharedTestingDescriptorPool(),
                                 {});
  ASSERT_THAT(builder.AddContextDeclarationWithProtoTypeMask(
                  "cel.expr.conformance.proto3.TestAllTypes", {"single_int32"}),
              IsOk());
  ASSERT_THAT(builder.AddVariable(MakeVariableDecl("single_int64", IntType())),
              IsOk());

  EXPECT_THAT(builder.Build(), IsOk());
}

TEST(TypeCheckerBuilderImplTest,
     InvalidTypeParamNameVariableValidationDisabled) {
  CheckerOptions options;
  options.enable_type_parameter_name_validation = false;
  TypeCheckerBuilderImpl builder(internal::GetSharedTestingDescriptorPool(),
                                 options);
  ASSERT_THAT(builder.AddVariable(MakeVariableDecl("x", TypeParamType(""))),
              IsOk());
  ASSERT_THAT(builder.AddOrReplaceVariable(
                  MakeVariableDecl("x", TypeParamType("T% foo"))),
              IsOk());
}

TEST(TypeCheckerBuilderImplTest, ErrorOnUnspecifiedMessageType) {
  CheckerOptions options;
  options.enable_type_parameter_name_validation = true;
  TypeCheckerBuilderImpl builder(internal::GetSharedTestingDescriptorPool(),
                                 options);
  ASSERT_THAT(
      builder.AddVariable(MakeVariableDecl("x", MessageType())),
      StatusIs(absl::StatusCode::kInvalidArgument,
               "an empty message type cannot be used in a type declaration"));
}

TEST(TypeCheckerBuilderImplTest, ErrorOnInvalidTypeParamNameVariable) {
  CheckerOptions options;
  options.enable_type_parameter_name_validation = true;
  TypeCheckerBuilderImpl builder(internal::GetSharedTestingDescriptorPool(),
                                 options);
  ASSERT_THAT(builder.AddVariable(MakeVariableDecl("x", TypeParamType(""))),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "type parameter name '' is not a valid identifier"));
  ASSERT_THAT(
      builder.AddOrReplaceVariable(
          MakeVariableDecl("x", TypeParamType("T% foo"))),
      StatusIs(absl::StatusCode::kInvalidArgument,
               "type parameter name 'T% foo' is not a valid identifier"));
}

TEST(TypeCheckerBuilderImplTest, ErrorOnTooDeepTypeNestingVariable) {
  CheckerOptions options;
  options.max_type_decl_nesting = 2;
  google::protobuf::Arena arena;

  TypeCheckerBuilderImpl builder(internal::GetSharedTestingDescriptorPool(),
                                 options);
  ASSERT_THAT(builder.AddVariable(
                  MakeVariableDecl("x", TypeType(&arena, TypeParamType("T")))),
              IsOk());
  ASSERT_THAT(
      builder.AddOrReplaceVariable(MakeVariableDecl(
          "x", TypeType(&arena, TypeType(&arena, TypeParamType("T% foo"))))),
      StatusIs(absl::StatusCode::kInvalidArgument,
               "type nesting limit of 2 exceeded"));
}

TEST(TypeCheckerBuilderImplTest, ErrorOnInvalidTypeParamNameFunction) {
  CheckerOptions options;
  options.enable_type_parameter_name_validation = true;
  TypeCheckerBuilderImpl builder(internal::GetSharedTestingDescriptorPool(),
                                 options);
  google::protobuf::Arena arena;

  ASSERT_OK_AND_ASSIGN(
      auto fn_decl,
      MakeFunctionDecl(
          "type2",
          MakeOverloadDecl("type2", TypeType(&arena, TypeParamType("")),
                           TypeParamType(""))));
  ASSERT_THAT(builder.AddFunction(fn_decl),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "type parameter name '' is not a valid identifier"));
}

TEST(TypeCheckerBuilderImplTest, ErrorOnTooDeepTypeNestingFunction) {
  CheckerOptions options;
  options.max_type_decl_nesting = 2;
  google::protobuf::Arena arena;

  TypeCheckerBuilderImpl builder(internal::GetSharedTestingDescriptorPool(),
                                 options);
  ASSERT_OK_AND_ASSIGN(
      auto fn_decl,
      MakeFunctionDecl(
          "add", MakeOverloadDecl("add_int", IntType(), IntType(), IntType())));
  ASSERT_THAT(builder.AddFunction(fn_decl), IsOk());

  Type list_type = ListType(&arena, ListType(&arena, IntType()));

  ASSERT_OK_AND_ASSIGN(
      fn_decl,
      MakeFunctionDecl("add", MakeOverloadDecl("add_list_list_int", list_type,
                                               list_type, list_type)));

  ASSERT_THAT(builder.MergeFunction(fn_decl),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "type nesting limit of 2 exceeded"));
}

TEST(TypeCheckerBuilderImplTest, ReplaceVariable) {
  TypeCheckerBuilderImpl builder(internal::GetSharedTestingDescriptorPool(),
                                 {});
  ASSERT_THAT(
      builder.AddContextDeclaration("cel.expr.conformance.proto3.TestAllTypes"),
      IsOk());
  ASSERT_THAT(builder.AddOrReplaceVariable(
                  MakeVariableDecl("single_int64", StringType())),
              IsOk());

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<TypeChecker> type_checker,
                       builder.Build());
  ASSERT_OK_AND_ASSIGN(auto ast, MakeTestParsedAst("single_int64"));
  ASSERT_OK_AND_ASSIGN(ValidationResult result,
                       type_checker->Check(std::move(ast)));

  ASSERT_TRUE(result.IsValid());

  const auto& checked_ast = *result.GetAst();

  EXPECT_EQ(checked_ast.GetReturnType(), TypeSpec(PrimitiveType::kString));
}

TEST(TypeCheckerBuilderImplTest, LazyArenaInitialization) {
  auto builder = std::make_unique<TypeCheckerBuilderImpl>(
      internal::GetSharedTestingDescriptorPool(), CheckerOptions{});

  ASSERT_THAT(builder->AddLibrary(CheckerLibrary{
                  .id = "test_lib",
                  .configure = [](TypeCheckerBuilder& builder) -> absl::Status {
                    auto l = ListType(builder.arena(), IntType());
                    return builder.AddVariable(MakeVariableDecl("foo", l));
                  },
              }),
              IsOk());

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<TypeChecker> type_checker,
                       builder->Build());
  builder.reset();
  ASSERT_OK_AND_ASSIGN(auto ast, MakeTestParsedAst("foo"));
  ASSERT_OK_AND_ASSIGN(ValidationResult result,
                       type_checker->Check(std::move(ast)));

  ASSERT_TRUE(result.IsValid());

  const auto& checked_ast = *result.GetAst();

  EXPECT_EQ(checked_ast.GetReturnType(),
            TypeSpec(ListTypeSpec(
                std::make_unique<TypeSpec>(PrimitiveType::kInt64))));
}

}  // namespace
}  // namespace cel::checker_internal
