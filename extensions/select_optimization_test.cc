// Copyright 2023 Google LLC
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

#include "extensions/select_optimization.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cel/expr/checked.pb.h"
#include "cel/expr/syntax.pb.h"
#include "google/protobuf/empty.pb.h"
#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "base/ast.h"
#include "base/attribute.h"
#include "base/builtins.h"
#include "checker/type_checker_builder.h"
#include "checker/validation_result.h"
#include "common/ast.h"
#include "common/decl.h"
#include "common/decl_proto.h"
#include "common/expr.h"
#include "common/kind.h"
#include "common/native_type.h"
#include "common/typeinfo.h"
#include "common/value.h"
#include "common/values/custom_struct_value.h"
#include "compiler/compiler.h"
#include "compiler/compiler_factory.h"
#include "compiler/optional.h"
#include "compiler/standard_library.h"
#include "eval/compiler/flat_expr_builder.h"
#include "eval/compiler/flat_expr_builder_extensions.h"
#include "eval/compiler/resolver.h"
#include "eval/eval/evaluator_core.h"
#include "eval/internal/interop.h"
#include "eval/public/cel_type_registry.h"
#include "eval/public/cel_value.h"
#include "eval/public/structs/cel_proto_wrapper.h"
#include "extensions/protobuf/ast_converters.h"
#include "internal/number.h"
#include "internal/status_macros.h"
#include "internal/testing.h"
#include "parser/parser.h"
#include "runtime/activation.h"
#include "runtime/function_adapter.h"
#include "runtime/function_registry.h"
#include "runtime/internal/issue_collector.h"
#include "runtime/internal/runtime_env.h"
#include "runtime/internal/runtime_env_testing.h"
#include "runtime/runtime_issue.h"
#include "runtime/runtime_options.h"
#include "runtime/type_registry.h"
#include "cel/expr/conformance/proto2/test_all_types.pb.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/extension_set.h"
#include "google/protobuf/message.h"
#include "google/protobuf/text_format.h"

namespace cel::extensions {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::cel::expr::conformance::proto2::NestedTestAllTypes;
using ::cel::runtime_internal::NewTestingRuntimeEnv;
using ::cel::runtime_internal::RuntimeEnv;
using ::cel::expr::ParsedExpr;
using ::google::api::expr::parser::Parse;
using ::google::api::expr::runtime::CelProtoWrapper;
using ::google::api::expr::runtime::CelValue;
using ::google::api::expr::runtime::FlatExprBuilder;
using ::google::api::expr::runtime::FlatExpression;
using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::SizeIs;
using ::testing::Truly;

namespace conformancepb = ::cel::expr::conformance;

using MessageWrapper = CelValue::MessageWrapper;

absl::Status ApplyDecl(absl::string_view decl, TypeCheckerBuilder& builder) {
  cel::expr::Decl decl_proto;

  if (!google::protobuf::TextFormat::ParseFromString(decl, &decl_proto)) {
    return absl::InvalidArgumentError("failed to parse decl");
  }
  if (decl_proto.has_ident()) {
    CEL_ASSIGN_OR_RETURN(
        cel::VariableDecl d,
        cel::VariableDeclFromProto(decl_proto.name(), decl_proto.ident(),
                                   builder.descriptor_pool(), builder.arena()));
    CEL_RETURN_IF_ERROR(builder.AddVariable(std::move(d)));
  } else if (decl_proto.has_function()) {
    CEL_ASSIGN_OR_RETURN(
        cel::FunctionDecl d,
        cel::FunctionDeclFromProto(decl_proto.name(), decl_proto.function(),
                                   builder.descriptor_pool(), builder.arena()));
    CEL_RETURN_IF_ERROR(builder.AddFunction(std::move(d)));
  } else {
    return absl::InvalidArgumentError("decl has no ident or function");
  }
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<cel::Compiler>> NewTestCompiler() {
  CompilerOptions options;
  options.parser_options.enable_quoted_identifiers = true;
  CEL_ASSIGN_OR_RETURN(std::unique_ptr<cel::CompilerBuilder> builder,
                       cel::NewCompilerBuilder(
                           google::protobuf::DescriptorPool::generated_pool(), options));

  CEL_RETURN_IF_ERROR(builder->AddLibrary(cel::StandardCompilerLibrary()));
  CEL_RETURN_IF_ERROR(builder->AddLibrary(cel::OptionalCompilerLibrary()));
  auto& checker_builder = builder->GetCheckerBuilder();
  google::protobuf::LinkMessageReflection<conformancepb::proto2::TestAllTypes>();

  checker_builder.set_container("cel.expr.conformance");

  CEL_RETURN_IF_ERROR(ApplyDecl(
      R"pb(
        name: "nested_test_all_types"
        ident {
          type {
            message_type: "cel.expr.conformance.proto2.NestedTestAllTypes"
          }
        }
      )pb",
      checker_builder));
  CEL_RETURN_IF_ERROR(ApplyDecl(
      R"pb(
        name: "test_all_types"
        ident {
          type { message_type: "cel.expr.conformance.proto2.TestAllTypes" }
        }
      )pb",
      checker_builder));
  CEL_RETURN_IF_ERROR(ApplyDecl(
      R"pb(
        name: "a"
        ident {
          type {
            message_type: "cel.expr.conformance.proto2.NestedTestAllTypes"
          }
        }
      )pb",
      checker_builder));

  CEL_RETURN_IF_ERROR(ApplyDecl(
      R"pb(
        name: "b"
        ident {
          type {
            message_type: "cel.expr.conformance.proto2.NestedTestAllTypes"
          }
        }
      )pb",
      checker_builder));

  CEL_RETURN_IF_ERROR(ApplyDecl(
      R"pb(
        name: "custom_predicate"
        function {
          overloads {
            doc: "An example predicate function for checking attribute tracking for "
                 "the result of the optimized select chain."
            overload_id: "custom_predicate_TestAllTypesNestedType"
            params {
              message_type: "cel.expr.conformance.proto2.TestAllTypes.NestedMessage"
            }
            result_type { primitive: BOOL }
          }
        }
      )pb",
      checker_builder));

  return builder->Build();
}

const cel::Compiler& TestCaseCompiler() {
  static const Compiler* compiler = []() {
    auto compiler = NewTestCompiler();
    ABSL_CHECK_OK(compiler);
    return compiler->release();
  }();
  return *compiler;
}

absl::StatusOr<std::unique_ptr<cel::Ast>> CompileForTestCase(
    absl::string_view expr) {
  CEL_ASSIGN_OR_RETURN(cel::ValidationResult r,
                       TestCaseCompiler().Compile(expr));
  if (!r.IsValid()) {
    return absl::InvalidArgumentError(r.FormatError());
  }
  return r.ReleaseAst();
}

class TestPartialQualifyStruct : public CustomStructValueInterface {
 public:
  explicit TestPartialQualifyStruct(Value leaf_value)
      : leaf_value_(std::move(leaf_value)) {}

  std::string DebugString() const override {
    return "TestPartialQualifyStruct";
  }

  absl::Status SerializeTo(
      const google::protobuf::DescriptorPool* descriptor_pool,
      google::protobuf::MessageFactory* message_factory,
      google::protobuf::io::ZeroCopyOutputStream* output) const override {
    return absl::UnimplementedError("SerializeTo");
  }

  absl::Status ConvertToJsonObject(
      const google::protobuf::DescriptorPool* descriptor_pool,
      google::protobuf::MessageFactory* message_factory,
      google::protobuf::Message* json) const override {
    return absl::UnimplementedError("ConvertToJsonObject");
  }

  absl::string_view GetTypeName() const override {
    return "cel.expr.conformance.proto2.NestedTestAllTypes";
  }

  bool IsZeroValue() const override { return false; }

  absl::Status GetFieldByName(absl::string_view name,
                              ProtoWrapperTypeOptions unboxing_options,
                              const google::protobuf::DescriptorPool* descriptor_pool,
                              google::protobuf::MessageFactory* message_factory,
                              google::protobuf::Arena* arena,
                              Value* result) const override {
    if (name == "child" || name == "payload" || name == "standalone_message") {
      *result = CustomStructValue(this, arena);
      return absl::OkStatus();
    }
    if (name == "bb") {
      *result = leaf_value_;
      return absl::OkStatus();
    }
    return NoSuchFieldError(name).ToStatus();
  }

  absl::Status GetFieldByNumber(int64_t number,
                                ProtoWrapperTypeOptions unboxing_options,
                                const google::protobuf::DescriptorPool* descriptor_pool,
                                google::protobuf::MessageFactory* message_factory,
                                google::protobuf::Arena* arena,
                                Value* result) const override {
    return absl::UnimplementedError("GetFieldByNumber");
  }

  absl::StatusOr<bool> HasFieldByName(absl::string_view name) const override {
    if (name == "child" || name == "payload" || name == "standalone_message" ||
        name == "bb") {
      return true;
    }
    return false;
  }

  absl::StatusOr<bool> HasFieldByNumber(int64_t number) const override {
    return absl::UnimplementedError("HasFieldByNumber");
  }

  absl::Status ForEachField(ForEachFieldCallback callback,
                            const google::protobuf::DescriptorPool* descriptor_pool,
                            google::protobuf::MessageFactory* message_factory,
                            google::protobuf::Arena* arena) const override {
    return absl::OkStatus();
  }

  absl::Status Qualify(absl::Span<const SelectQualifier> qualifiers,
                       bool presence_test,
                       const google::protobuf::DescriptorPool* descriptor_pool,
                       google::protobuf::MessageFactory* message_factory,
                       google::protobuf::Arena* arena, Value* result,
                       int* count) const override {
    if (qualifiers.size() >= 3) {
      *count = 3;
      *result = CustomStructValue(this, arena);
      return absl::OkStatus();
    }
    return absl::UnimplementedError("Qualify unsupported");
  }

  CustomStructValue Clone(google::protobuf::Arena* arena) const override {
    return CustomStructValue(this, arena);
  }

  NativeTypeId GetNativeTypeId() const override {
    return cel::TypeId<TestPartialQualifyStruct>();
  }

 private:
  Value leaf_value_;
};

absl::Status TestBindLegacyValue(absl::string_view variable,
                                 CelValue legacy_value, google::protobuf::Arena* arena,
                                 Activation& act) {
  CEL_ASSIGN_OR_RETURN(Value value,
                       interop_internal::FromLegacyValue(arena, legacy_value));

  act.InsertOrAssignValue(variable, std::move(value));
  return absl::OkStatus();
}

absl::Status TestBindLegacyMessage(absl::string_view variable,
                                   const google::protobuf::Message& message,
                                   google::protobuf::Arena* arena, cel::Activation& act) {
  CelValue legacy_value = CelProtoWrapper::CreateMessage(&message, arena);

  return TestBindLegacyValue(variable, legacy_value, arena, act);
}

class SelectOptimizationTest : public testing::Test {
 public:
  SelectOptimizationTest()
      : env_(NewTestingRuntimeEnv()),
        legacy_registry_(env_->legacy_type_registry),
        type_registry_(env_->type_registry),
        function_registry_(env_->function_registry),
        resolver_("", function_registry_, type_registry_,
                  type_registry_.GetComposedTypeProvider()),
        issue_collector_(RuntimeIssue::Severity::kError),
        context_(env_, resolver_, runtime_options_,
                 type_registry_.GetComposedTypeProvider(), issue_collector_,
                 program_builder_, shared_arena_) {
    runtime_options_.fail_on_warnings = false;
  }

  void SetUp() override {
    google::protobuf::LinkMessageReflection<NestedTestAllTypes>();
    ASSERT_THAT(
        function_registry_.Register(
            UnaryFunctionAdapter<bool, const StructValue&>::CreateDescriptor(
                "custom_predicate", false),
            UnaryFunctionAdapter<bool, const StructValue&>::WrapFunction(
                [](const StructValue&) { return true; })),
        IsOk());
  }

 protected:
  absl_nonnull std::shared_ptr<RuntimeEnv> env_;
  google::api::expr::runtime::CelTypeRegistry& legacy_registry_;
  TypeRegistry& type_registry_;
  FunctionRegistry& function_registry_;
  google::protobuf::Arena arena_;
  RuntimeOptions runtime_options_;
  google::api::expr::runtime::Resolver resolver_;
  cel::runtime_internal::IssueCollector issue_collector_;
  google::api::expr::runtime::ProgramBuilder program_builder_;
  std::shared_ptr<google::protobuf::Arena> shared_arena_;
  google::api::expr::runtime::PlannerContext context_;
};

MATCHER_P2(SelectFieldEntry, id, name, "") {
  const cel::Expr& entry = arg.expr();

  if (entry.list_expr().elements().size() != 2) {
    *result_listener << "want 2-tuple entry, got "
                     << entry.list_expr().elements().size();
    return false;
  }

  int64_t got_id =
      entry.list_expr().elements()[0].expr().const_expr().int64_value();
  absl::string_view got_name =
      entry.list_expr().elements()[1].expr().const_expr().string_value();

  *result_listener << "want " << id << ": '" << name << "'" << " got " << got_id
                   << ": '" << got_name << "'";

  return entry.list_expr().elements()[0].expr().const_expr().int64_value() ==
             id &&
         entry.list_expr().elements()[1].expr().const_expr().string_value() ==
             name;
}

std::string ToString(const AttributeQualifier& qualifier) {
  switch (qualifier.kind()) {
    case Kind::kInt:
      return absl::StrCat(*qualifier.GetInt64Key());
    case Kind::kString:
      return absl::StrCat("'", *qualifier.GetStringKey(), "'");
    case Kind::kUint:
      return absl::StrCat(*qualifier.GetUint64Key());
    case Kind::kBool:
      return absl::StrCat(*qualifier.GetBoolKey());
    default:
      return "<unspecified>";
  }
}

MATCHER_P(SelectQualifier, qualifier,
          absl::StrCat("SelectQualifier: ", ToString(qualifier))) {
  const cel::Expr& entry = arg.expr();

  if (!entry.has_const_expr()) {
    *result_listener << "wanted const_expr";
    return false;
  }

  cel::AttributeQualifier got_qualifier;
  if (entry.const_expr().has_int64_value()) {
    got_qualifier = AttributeQualifier::OfInt(entry.const_expr().int64_value());
  } else if (entry.const_expr().has_string_value()) {
    got_qualifier =
        AttributeQualifier::OfString(entry.const_expr().string_value());
  } else if (entry.const_expr().has_bool_value()) {
    got_qualifier = AttributeQualifier::OfBool(entry.const_expr().bool_value());
  } else if (entry.const_expr().has_uint64_value()) {
    got_qualifier =
        AttributeQualifier::OfUint(entry.const_expr().uint64_value());
  }

  *result_listener << "want " << ToString(qualifier) << " got "
                   << ToString(got_qualifier);

  return qualifier == got_qualifier;
}

TEST_F(SelectOptimizationTest, AstTransformSelect) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<cel::Ast> ast,
      CompileForTestCase(
          "nested_test_all_types.child.payload.standalone_message.bb"));

  SelectOptimizationAstUpdater updater;
  EXPECT_THAT(updater.UpdateAst(context_, *ast), IsOk());

  const auto& attr_call = ast->root_expr().call_expr();
  EXPECT_EQ(attr_call.function(), "cel.@attribute");

  ASSERT_THAT(attr_call.args(), SizeIs(2));

  EXPECT_EQ(attr_call.args()[0].ident_expr().name(), "nested_test_all_types");

  EXPECT_THAT(
      attr_call.args()[1].list_expr().elements(),
      ElementsAre(SelectFieldEntry(1, "child"), SelectFieldEntry(2, "payload"),
                  SelectFieldEntry(23, "standalone_message"),
                  SelectFieldEntry(1, "bb")));
}

TEST_F(SelectOptimizationTest, AstTransformSelectPresence) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<cel::Ast> ast,
      CompileForTestCase(
          "has(nested_test_all_types.child.payload.standalone_message.bb)"));

  SelectOptimizationAstUpdater updater;
  EXPECT_THAT(updater.UpdateAst(context_, *ast), IsOk());

  const auto& attr_call = ast->root_expr().call_expr();
  EXPECT_EQ(attr_call.function(), "cel.@hasField");

  ASSERT_THAT(attr_call.args(), SizeIs(2));

  EXPECT_EQ(attr_call.args()[0].ident_expr().name(), "nested_test_all_types");

  EXPECT_THAT(
      attr_call.args()[1].list_expr().elements(),
      ElementsAre(SelectFieldEntry(1, "child"), SelectFieldEntry(2, "payload"),
                  SelectFieldEntry(23, "standalone_message"),
                  SelectFieldEntry(1, "bb")));
}

TEST_F(SelectOptimizationTest, AstTransformComplexSelect) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<cel::Ast> ast,
      CompileForTestCase(
          "((false)? a.child.child : b.child).child.payload.single_int64"));

  SelectOptimizationAstUpdater updater;
  EXPECT_THAT(updater.UpdateAst(context_, *ast), IsOk());

  const auto& attr_call = ast->root_expr().call_expr();
  EXPECT_EQ(attr_call.function(), "cel.@attribute");

  ASSERT_THAT(attr_call.args(), SizeIs(2));

  EXPECT_THAT(
      attr_call.args()[1].list_expr().elements(),
      ElementsAre(SelectFieldEntry(1, "child"), SelectFieldEntry(2, "payload"),
                  SelectFieldEntry(2, "single_int64")));

  const auto& operand = attr_call.args()[0];

  EXPECT_EQ(operand.call_expr().function(), cel::builtin::kTernary);
  ASSERT_THAT(operand.call_expr().args(), SizeIs(3));

  const auto& true_branch = operand.call_expr().args()[1];

  EXPECT_EQ(true_branch.call_expr().function(), "cel.@attribute");
  ASSERT_THAT(true_branch.call_expr().args(), SizeIs(2));

  EXPECT_THAT(
      true_branch.call_expr().args()[1].list_expr().elements(),
      ElementsAre(SelectFieldEntry(1, "child"), SelectFieldEntry(1, "child")));
}

TEST_F(SelectOptimizationTest, AstTransformMapIndexTraversal) {
  // nested_test_all_types.payload.map_string_message['$not_a_field'].bb
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<cel::Ast> ast,
                       CompileForTestCase("nested_test_all_types.payload.map_"
                                          "string_message['$not_a_field'].bb"));

  SelectOptimizationAstUpdater updater;
  EXPECT_THAT(updater.UpdateAst(context_, *ast), IsOk());

  const auto& attr_call = ast->root_expr().call_expr();
  EXPECT_EQ(attr_call.function(), "cel.@attribute");

  ASSERT_THAT(attr_call.args(), SizeIs(2));

  EXPECT_THAT(
      attr_call.args()[1].list_expr().elements(),
      ElementsAre(SelectFieldEntry(2, "payload"),
                  SelectFieldEntry(227, "map_string_message"),
                  SelectQualifier(AttributeQualifier::OfString("$not_a_field")),
                  SelectFieldEntry(1, "bb")));

  const auto& operand = attr_call.args()[0];

  EXPECT_EQ(operand.ident_expr().name(), "nested_test_all_types");
}

TEST_F(SelectOptimizationTest, AstTransformMapIndexUnsupportedConstant) {
  // nested_test_all_types.payload.map_string_message['$not_a_field'].bb
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<cel::Ast> ast,
                       CompileForTestCase("nested_test_all_types.payload.map_"
                                          "string_message['$not_a_field'].bb"));

  // Type-checker shouldn't allow a bytes key, so simulating here for
  // coverage.
  ast->mutable_root_expr()
      .mutable_select_expr()
      .mutable_operand()
      .mutable_call_expr()
      .mutable_args()[1]
      .mutable_const_expr()
      .set_bytes_value("$not_a_field");

  // We don't fail here, but we also don't optimize past the map lookup with
  // an unsupported constant key.
  SelectOptimizationAstUpdater updater;
  EXPECT_THAT(updater.UpdateAst(context_, *ast), IsOk());
  EXPECT_EQ(ast->root_expr().call_expr().function(), "cel.@attribute");
  ASSERT_THAT(ast->root_expr().call_expr().args(), SizeIs(2));
  EXPECT_EQ(ast->root_expr().call_expr().args()[0].call_expr().function(),
            "_[_]");
  // cel.@attribute(
  //   cel.@attribute(
  //       nested_test_all_types,
  //       [payload, map_string_message])[b'$not_a_field'],
  //  [bb])
  EXPECT_THAT(ast->root_expr().call_expr().args()[1].list_expr().elements(),
              SizeIs(1));
}

TEST_F(SelectOptimizationTest, AstTransformMapIndexHeterogeneousDoubleKey) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<cel::Ast> ast,
      CompileForTestCase("nested_test_all_types.payload.single_any[1.0].bb"));

  SelectOptimizationAstUpdater updater;
  EXPECT_THAT(updater.UpdateAst(context_, *ast), IsOk());
  EXPECT_EQ(ast->root_expr().select_expr().field(), "bb");
  // TODO(uncreated-issue/51): Right now we don't optimize past a dyn/any field
  // and discard the select optimization if the root isn't a message, so we will
  // consider the double as a candidate then discard it.
  EXPECT_THAT(ast->root_expr().select_expr().operand().call_expr().function(),
              "cel.@attribute");
  ASSERT_THAT(ast->root_expr().select_expr().operand().call_expr().args(),
              SizeIs(2));
  EXPECT_THAT(ast->root_expr()
                  .select_expr()
                  .operand()
                  .call_expr()
                  .args()[1]
                  .list_expr()
                  .elements(),
              SizeIs(3));
}

TEST_F(SelectOptimizationTest, AstTransformMapIndexHeterogeneousDoubleKeyUint) {
  constexpr uint64_t kBigUint =
      static_cast<uint64_t>(internal::kMaxDoubleRepresentableAsUint);
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<cel::Ast> ast,
      CompileForTestCase(absl::StrCat(
          "nested_test_all_types.payload.single_any[", kBigUint, ".0].bb")));

  SelectOptimizationAstUpdater updater;
  EXPECT_THAT(updater.UpdateAst(context_, *ast), IsOk());
  EXPECT_EQ(ast->root_expr().select_expr().field(), "bb");
  // TODO(uncreated-issue/51): Right now we don't optimize past a dyn/any field
  // and discard additional select steps.
  EXPECT_THAT(ast->root_expr().select_expr().operand().call_expr().function(),
              "cel.@attribute");
  ASSERT_THAT(ast->root_expr().select_expr().operand().call_expr().args(),
              SizeIs(2));
  EXPECT_THAT(ast->root_expr()
                  .select_expr()
                  .operand()
                  .call_expr()
                  .args()[1]
                  .list_expr()
                  .elements(),
              SizeIs(3));
}

TEST_F(SelectOptimizationTest, AstTransformFilterToMessageRoot) {
  // {'field_like_key':
  // nested_test_all_types}.field_like_key.payload.single_int64
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<cel::Ast> ast,
      CompileForTestCase(
          "{'field_like_key': "
          "nested_test_all_types}.field_like_key.payload.single_int64"));

  SelectOptimizationAstUpdater updater;
  EXPECT_THAT(updater.UpdateAst(context_, *ast), IsOk());

  const auto& attr_call = ast->root_expr().call_expr();
  EXPECT_EQ(attr_call.function(), "cel.@attribute");

  ASSERT_THAT(attr_call.args(), SizeIs(2));

  EXPECT_THAT(attr_call.args()[1].list_expr().elements(),
              ElementsAre(SelectFieldEntry(2, "payload"),
                          SelectFieldEntry(2, "single_int64")));

  const auto& operand = attr_call.args()[0];

  EXPECT_EQ(operand.select_expr().field(), "field_like_key");
}

TEST_F(SelectOptimizationTest, AstTransformMapDotTraversal) {
  // nested_test_all_types.payload.map_string_message.field_like_key.bb
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<cel::Ast> ast,
                       CompileForTestCase("nested_test_all_types.payload.map_"
                                          "string_message.field_like_key.bb"));

  SelectOptimizationAstUpdater updater;
  EXPECT_THAT(updater.UpdateAst(context_, *ast), IsOk());

  const auto& attr_call = ast->root_expr().call_expr();
  EXPECT_EQ(attr_call.function(), "cel.@attribute");

  ASSERT_THAT(attr_call.args(), SizeIs(2));

  EXPECT_THAT(attr_call.args()[1].list_expr().elements(),
              ElementsAre(SelectFieldEntry(2, "payload"),
                          SelectFieldEntry(227, "map_string_message"),
                          SelectQualifier(
                              AttributeQualifier::OfString("field_like_key")),
                          SelectFieldEntry(1, "bb")));

  const auto& operand = attr_call.args()[0];

  EXPECT_EQ(operand.ident_expr().name(), "nested_test_all_types");
}

TEST_F(SelectOptimizationTest, AstTransformAnyDotTraversal) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<cel::Ast> ast,
      CompileForTestCase(
          "nested_test_all_types.payload.single_any.single_int64"));

  SelectOptimizationAstUpdater updater;
  EXPECT_THAT(updater.UpdateAst(context_, *ast), IsOk());

  // When fully supported, we'd expect this to collapse to one attribute call.
  const auto& attr_call = ast->root_expr().select_expr().operand().call_expr();
  EXPECT_EQ(attr_call.function(), "cel.@attribute");

  ASSERT_THAT(attr_call.args(), SizeIs(2));

  EXPECT_THAT(attr_call.args()[1].list_expr().elements(),
              ElementsAre(SelectFieldEntry(2, "payload"),
                          SelectFieldEntry(100, "single_any")));

  const auto& operand = attr_call.args()[0];

  EXPECT_EQ(operand.ident_expr().name(), "nested_test_all_types");
}

TEST_F(SelectOptimizationTest, AstTransformRepeated) {
  // nested_test_all_types.payload.repeated_nested_message[1].bb
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<cel::Ast> ast,
      CompileForTestCase(
          "nested_test_all_types.payload.repeated_nested_message[1].bb"));

  SelectOptimizationAstUpdater updater;
  EXPECT_THAT(updater.UpdateAst(context_, *ast), IsOk());

  // When fully supported, we'd expect this to collapse to one attribute call.
  const auto& attr_call = ast->root_expr().call_expr();
  EXPECT_EQ(attr_call.function(), "cel.@attribute");

  ASSERT_THAT(attr_call.args(), SizeIs(2));

  EXPECT_THAT(attr_call.args()[1].list_expr().elements(),
              ElementsAre(SelectFieldEntry(2, "payload"),
                          SelectFieldEntry(51, "repeated_nested_message"),
                          SelectQualifier(AttributeQualifier::OfInt(1)),
                          SelectFieldEntry(1, "bb")));

  const auto& operand = attr_call.args()[0];

  EXPECT_EQ(operand.ident_expr().name(), "nested_test_all_types");
}

TEST_F(SelectOptimizationTest, AstTransformParseOnlyNotUpdated) {
  google::protobuf::LinkMessageReflection<NestedTestAllTypes>();

  FlatExprBuilder builder(env_, runtime_options_);

  builder.AddAstTransform(std::make_unique<SelectOptimizationAstUpdater>());

  // nested_test_all_types.payload.repeated_nested_message[1].bb
  ASSERT_OK_AND_ASSIGN(
      ParsedExpr expr,
      Parse("nested_test_all_types.payload.repeated_nested_message[1].bb"));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<cel::Ast> ast,
                       CreateAstFromParsedExpr(expr));
  ASSERT_OK_AND_ASSIGN(FlatExpression plan,
                       builder.CreateExpressionImpl(std::move(ast), nullptr));

  NestedTestAllTypes var;

  var.mutable_payload()->add_repeated_nested_message();
  var.mutable_payload()->add_repeated_nested_message()->set_bb(42);

  cel::Activation act;
  ASSERT_THAT(TestBindLegacyMessage("nested_test_all_types", var, &arena_, act),
              IsOk());

  auto state = plan.MakeEvaluatorState(env_->descriptor_pool.get(),
                                       env_->MutableMessageFactory(), &arena_);
  ASSERT_OK_AND_ASSIGN(
      Value result,
      plan.EvaluateWithCallback(
          act, /*embedder_context=*/nullptr,
          google::api::expr::runtime::EvaluationListener(), state));

  ASSERT_TRUE(result->Is<IntValue>()) << result->DebugString();

  EXPECT_EQ(result.GetInt().NativeValue(), 42);
}

TEST_F(SelectOptimizationTest, ProgramOptimizerUnoptimizedAst) {
  google::protobuf::LinkMessageReflection<NestedTestAllTypes>();

  FlatExprBuilder builder(env_, runtime_options_);

  builder.AddProgramOptimizer(CreateSelectOptimizationProgramOptimizer());

  // nested_test_all_types.child.payload.standalone_message.bb
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<cel::Ast> ast,
      CompileForTestCase(
          "nested_test_all_types.child.payload.standalone_message.bb"));

  ASSERT_OK_AND_ASSIGN(FlatExpression plan,
                       builder.CreateExpressionImpl(std::move(ast), nullptr));

  NestedTestAllTypes var;

  var.mutable_child()->mutable_payload()->mutable_standalone_message()->set_bb(
      42);

  cel::Activation act;
  ASSERT_THAT(TestBindLegacyMessage("nested_test_all_types", var, &arena_, act),
              IsOk());

  auto state = plan.MakeEvaluatorState(env_->descriptor_pool.get(),
                                       env_->MutableMessageFactory(), &arena_);
  ASSERT_OK_AND_ASSIGN(
      Value result,
      plan.EvaluateWithCallback(
          act, /*embedder_context=*/nullptr,
          google::api::expr::runtime::EvaluationListener(), state));

  ASSERT_TRUE(result->Is<IntValue>()) << result->DebugString();

  EXPECT_EQ(result.GetInt().NativeValue(), 42);
}

TEST_F(SelectOptimizationTest, MissingAttributeIndependentOfUnknown) {
  google::protobuf::LinkMessageReflection<NestedTestAllTypes>();

  RuntimeOptions options = runtime_options_;
  options.unknown_processing = UnknownProcessingOptions::kDisabled;
  options.enable_missing_attribute_errors = true;

  FlatExprBuilder builder(env_, options);

  builder.AddAstTransform(std::make_unique<SelectOptimizationAstUpdater>());
  builder.AddProgramOptimizer(CreateSelectOptimizationProgramOptimizer());

  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<cel::Ast> ast,
      CompileForTestCase("custom_predicate(nested_test_all_types.child.payload."
                         "standalone_message)"));

  ASSERT_OK_AND_ASSIGN(FlatExpression plan,
                       builder.CreateExpressionImpl(std::move(ast), nullptr));

  cel::Activation act;
  // activation only uses a ptr to the underlying message, persist them.
  NestedTestAllTypes var;

  act.SetMissingPatterns(
      {AttributePattern("nested_test_all_types",
                        {
                            AttributeQualifierPattern::OfString("child"),
                            AttributeQualifierPattern::OfString("payload"),
                        })});

  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        child { payload { standalone_message { bb: 20 } } }
      )pb",
      &var));
  ASSERT_THAT(TestBindLegacyMessage("nested_test_all_types", var, &arena_, act),
              IsOk());

  auto state = plan.MakeEvaluatorState(env_->descriptor_pool.get(),
                                       env_->MutableMessageFactory(), &arena_);
  ASSERT_OK_AND_ASSIGN(
      Value result,
      plan.EvaluateWithCallback(
          act, /*embedder_context=*/nullptr,
          google::api::expr::runtime::EvaluationListener(), state));

  ASSERT_TRUE(result->Is<ErrorValue>()) << result->DebugString();
  EXPECT_THAT(result.GetError().NativeValue(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("nested_test_all_types.child.payload")));
}

TEST_F(SelectOptimizationTest, NullUnboxingOptionHonored) {
  google::protobuf::LinkMessageReflection<NestedTestAllTypes>();

  RuntimeOptions options = runtime_options_;
  options.enable_empty_wrapper_null_unboxing = true;

  FlatExprBuilder builder(env_, options);

  builder.AddAstTransform(std::make_unique<SelectOptimizationAstUpdater>());
  builder.AddProgramOptimizer(CreateSelectOptimizationProgramOptimizer());

  // nested_test_all_types.payload.single_int64_wrapper
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<cel::Ast> ast,
      CompileForTestCase("nested_test_all_types.payload.single_int64_wrapper"));

  ASSERT_OK_AND_ASSIGN(FlatExpression plan,
                       builder.CreateExpressionImpl(std::move(ast), nullptr));

  cel::Activation act;
  // activation only uses a ptr to the underlying message, persist them.
  NestedTestAllTypes var;

  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        payload {}
      )pb",
      &var));
  ASSERT_THAT(TestBindLegacyMessage("nested_test_all_types", var, &arena_, act),
              IsOk());

  auto state = plan.MakeEvaluatorState(env_->descriptor_pool.get(),
                                       env_->MutableMessageFactory(), &arena_);
  ASSERT_OK_AND_ASSIGN(
      Value result,
      plan.EvaluateWithCallback(
          act, /*embedder_context=*/nullptr,
          google::api::expr::runtime::EvaluationListener(), state));

  ASSERT_TRUE(result->Is<NullValue>()) << result->DebugString();
}

using ActivationSetupFn =
    std::function<absl::Status(google::protobuf::Arena*, Activation&)>;

struct ProgramOptimizerTestCase {
  std::string case_name;
  std::string expr;
  // identifier -> NestedTestAllTypes textproto
  absl::flat_hash_map<std::string, std::string> vars;
  ActivationSetupFn setup_activation;
  std::function<void(const absl::StatusOr<Value>&)> expectations;
};

class SelectOptimizationProgramOptimizerTest
    : public SelectOptimizationTest,
      public testing::WithParamInterface<ProgramOptimizerTestCase> {};

TEST_P(SelectOptimizationProgramOptimizerTest, Default) {
  const ProgramOptimizerTestCase& test_case = GetParam();
  google::protobuf::LinkMessageReflection<NestedTestAllTypes>();

  RuntimeOptions options = runtime_options_;
  options.unknown_processing = UnknownProcessingOptions::kAttributeAndFunction;
  options.enable_missing_attribute_errors = true;

  FlatExprBuilder builder(env_, options);

  builder.AddAstTransform(std::make_unique<SelectOptimizationAstUpdater>());
  builder.AddProgramOptimizer(CreateSelectOptimizationProgramOptimizer());

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<cel::Ast> ast,
                       CompileForTestCase(test_case.expr));

  ASSERT_OK_AND_ASSIGN(FlatExpression plan,
                       builder.CreateExpressionImpl(std::move(ast), nullptr));

  cel::Activation act;
  // activation only uses a ptr to the underlying message, persist them.
  std::vector<std::unique_ptr<NestedTestAllTypes>> vars;
  for (const auto& entry : test_case.vars) {
    vars.push_back(std::make_unique<NestedTestAllTypes>());
    ASSERT_TRUE(
        google::protobuf::TextFormat::ParseFromString(entry.second, vars.back().get()));
    ASSERT_THAT(TestBindLegacyMessage(entry.first, *vars.back(), &arena_, act),
                IsOk());
  }

  if (test_case.setup_activation != nullptr) {
    ASSERT_THAT(test_case.setup_activation(&arena_, act), IsOk());
  }

  auto state = plan.MakeEvaluatorState(env_->descriptor_pool.get(),
                                       env_->MutableMessageFactory(), &arena_);
  absl::StatusOr<Value> result = plan.EvaluateWithCallback(
      act, /*embedder_context=*/nullptr,
      google::api::expr::runtime::EvaluationListener(), state);

  ASSERT_NO_FATAL_FAILURE(test_case.expectations(result));
}

TEST_P(SelectOptimizationProgramOptimizerTest, ForceFallbackImpl) {
  const ProgramOptimizerTestCase& test_case = GetParam();
  google::protobuf::LinkMessageReflection<NestedTestAllTypes>();

  RuntimeOptions options = runtime_options_;
  options.unknown_processing = UnknownProcessingOptions::kAttributeAndFunction;
  options.enable_missing_attribute_errors = true;

  FlatExprBuilder builder(env_, options);
  SelectOptimizationOptions select_options;
  select_options.force_fallback_implementation = true;

  builder.AddAstTransform(std::make_unique<SelectOptimizationAstUpdater>());
  builder.AddProgramOptimizer(
      CreateSelectOptimizationProgramOptimizer(select_options));

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<cel::Ast> ast,
                       CompileForTestCase(test_case.expr));

  ASSERT_OK_AND_ASSIGN(FlatExpression plan,
                       builder.CreateExpressionImpl(std::move(ast), nullptr));

  cel::Activation act;
  // activation only uses a ptr to the underlying message, persist them.
  std::vector<std::unique_ptr<NestedTestAllTypes>> vars;
  for (const auto& entry : test_case.vars) {
    vars.push_back(std::make_unique<NestedTestAllTypes>());
    ASSERT_TRUE(
        google::protobuf::TextFormat::ParseFromString(entry.second, vars.back().get()));
    ASSERT_THAT(TestBindLegacyMessage(entry.first, *vars.back(), &arena_, act),
                IsOk());
  }

  if (test_case.setup_activation != nullptr) {
    ASSERT_THAT(test_case.setup_activation(&arena_, act), IsOk());
  }

  auto state = plan.MakeEvaluatorState(env_->descriptor_pool.get(),
                                       env_->MutableMessageFactory(), &arena_);
  absl::StatusOr<Value> result = plan.EvaluateWithCallback(
      act, /*embedder_context=*/nullptr,
      google::api::expr::runtime::EvaluationListener(), state);

  ASSERT_NO_FATAL_FAILURE(test_case.expectations(result));
}

INSTANTIATE_TEST_SUITE_P(
    TestCases, SelectOptimizationProgramOptimizerTest,
    testing::ValuesIn<ProgramOptimizerTestCase>({
        {
            "chained_select_success",
            "nested_test_all_types.child.payload.standalone_message.bb",
            {{"nested_test_all_types",
              R"pb(
                child { payload { standalone_message { bb: 42 } } }
              )pb"}},
            ActivationSetupFn(),
            [](const absl::StatusOr<Value>& got) {
              ASSERT_OK_AND_ASSIGN(Value result, got);
              ASSERT_TRUE(result->Is<IntValue>()) << result->DebugString();
              EXPECT_EQ(result.GetInt().NativeValue(), 42);
            },
        },
        {
            "chained_select_defaults_success",
            "nested_test_all_types.child.payload.standalone_message.bb",
            {{"nested_test_all_types", R"pb()pb"}},
            ActivationSetupFn(),

            [](const absl::StatusOr<Value>& got) {
              ASSERT_OK_AND_ASSIGN(Value result, got);
              ASSERT_TRUE(result->Is<IntValue>()) << result->DebugString();
              EXPECT_EQ(result.GetInt().NativeValue(), 0);
            },
        },
        {
            "chained_select_partial_success",
            "nested_test_all_types.child.payload.standalone_message.bb",
            {},
            [](google::protobuf::Arena* arena, Activation& act) {
              auto* custom_struct =
                  google::protobuf::Arena::Create<TestPartialQualifyStruct>(arena,
                                                                  IntValue(42));
              act.InsertOrAssignValue("nested_test_all_types",
                                      CustomStructValue(custom_struct, arena));
              return absl::OkStatus();
            },
            [](const absl::StatusOr<Value>& got) {
              ASSERT_OK_AND_ASSIGN(Value result, got);
              ASSERT_TRUE(result->Is<IntValue>()) << result->DebugString();
              EXPECT_EQ(result.GetInt().NativeValue(), 42);
            },
        },
        {
            "chained_select_presence_partial_present",
            "has(nested_test_all_types.child.payload.standalone_message.bb)",
            {},
            [](google::protobuf::Arena* arena, Activation& act) {
              auto* custom_struct =
                  google::protobuf::Arena::Create<TestPartialQualifyStruct>(arena,
                                                                  IntValue(42));
              act.InsertOrAssignValue("nested_test_all_types",
                                      CustomStructValue(custom_struct, arena));
              return absl::OkStatus();
            },
            [](const absl::StatusOr<Value>& got) {
              ASSERT_OK_AND_ASSIGN(Value result, got);
              ASSERT_TRUE(result->Is<BoolValue>()) << result->DebugString();
              EXPECT_TRUE(result.GetBool().NativeValue());
            },
        },
        {
            "chained_select_not_bound",
            "nested_test_all_types.child.payload.standalone_message.bb",
            {},  // not set
            ActivationSetupFn(),

            [](const absl::StatusOr<Value>& got) {
              ASSERT_OK_AND_ASSIGN(Value result, got);
              ASSERT_TRUE(result->Is<ErrorValue>()) << result->DebugString();
              EXPECT_THAT(result.GetError().NativeValue(),
                          StatusIs(absl::StatusCode::kUnknown,
                                   HasSubstr("nested_test_all_types")));
            },
        },
        {
            // Some clients will use maps to represent a protobuf message at
            // runtime. This is not yet supported.
            "chained_select_map_as_root_unsupported",
            "nested_test_all_types.child.payload.standalone_message.bb",
            {},  // not set
            [](google::protobuf::Arena* arena, Activation& act) -> absl::Status {
              auto builder = cel::NewMapValueBuilder(arena);
              CEL_RETURN_IF_ERROR(builder->Put(
                  cel::StringValue::WrapUnsafe("child"), cel::NullValue()));

              auto value = std::move(*builder).Build();

              act.InsertOrAssignValue("nested_test_all_types",
                                      std::move(value));

              return absl::OkStatus();
            },

            [](const absl::StatusOr<Value>& got) {
              EXPECT_THAT(got.status(),
                          StatusIs(absl::StatusCode::kInvalidArgument));
            },
        },
        {
            // Some clients will use maps to represent a protobuf at runtime,
            // this is not yet supported.
            "chained_select_noncontainer_as_root_unsupported",
            "nested_test_all_types.child.payload.standalone_message.bb",
            {},  // not set
            [](google::protobuf::Arena* arena, Activation& act) {
              act.InsertOrAssignValue("nested_test_all_types",
                                      cel::DurationValue(absl::Seconds(1)));
              return absl::OkStatus();
            },

            [](const absl::StatusOr<Value>& got) {
              EXPECT_THAT(got.status(),
                          StatusIs(absl::StatusCode::kInvalidArgument));
            },
        },
        {
            "complex_select_success",
            "((false)? a.child.child : b.child).child.payload.single_int64",
            {{"a", ""},
             {"b",
              R"pb(
                child { child { payload { single_int64: -42 } } }
              )pb"}},
            ActivationSetupFn(),

            [](const absl::StatusOr<Value>& got) {
              ASSERT_OK_AND_ASSIGN(Value result, got);
              ASSERT_TRUE(result->Is<IntValue>()) << result->DebugString();
              EXPECT_EQ(result.GetInt().NativeValue(), -42);
            },
        },
        {
            "chained_select_presence_present",
            "has(nested_test_all_types.child.payload.standalone_message.bb)",
            {{"nested_test_all_types",
              R"pb(
                child { payload { standalone_message { bb: 2 } } }
              )pb"}},
            ActivationSetupFn(),
            [](const absl::StatusOr<Value>& got) {
              ASSERT_OK_AND_ASSIGN(Value result, got);
              ASSERT_TRUE(result->Is<BoolValue>()) << result->DebugString();
              EXPECT_TRUE(result.GetBool().NativeValue());
            },
        },
        {
            "chained_select_presence_not_present",
            "has(nested_test_all_types.child.payload.standalone_message.bb)",
            {{"nested_test_all_types",
              R"pb(
                child { payload { standalone_message {} } }
              )pb"}},
            ActivationSetupFn(),
            [](const absl::StatusOr<Value>& got) {
              ASSERT_OK_AND_ASSIGN(Value result, got);
              ASSERT_TRUE(result->Is<BoolValue>()) << result->DebugString();
              EXPECT_FALSE(result.GetBool().NativeValue());
            },
        },
        {
            "select_with_map_supported",
            "nested_test_all_types.payload.map_string_message['$not_a_field']."
            "bb",
            {{"nested_test_all_types",
              R"pb(
                payload {
                  map_string_message {
                    key: "$not_a_field",
                    value { bb: 5 }
                  }
                }
              )pb"}},
            ActivationSetupFn(),

            [](const absl::StatusOr<Value>& got) {
              ASSERT_OK_AND_ASSIGN(Value result, got);
              ASSERT_TRUE(result->Is<IntValue>()) << result->DebugString();
              EXPECT_EQ(result.GetInt().NativeValue(), 5);
            },
        },
        {
            "select_with_map_no_such_key",
            "nested_test_all_types.payload.map_string_message['$not_a_field']."
            "bb",
            {{"nested_test_all_types",
              R"pb(
                payload {
                  map_string_message {
                    key: "a_different_field",
                    value { bb: 5 }
                  }
                }
              )pb"}},
            ActivationSetupFn(),
            [](const absl::StatusOr<Value>& got) {
              ASSERT_OK_AND_ASSIGN(Value result, got);
              ASSERT_TRUE(result->Is<ErrorValue>()) << result->DebugString();
              EXPECT_THAT(result.GetError().NativeValue(),
                          StatusIs(absl::StatusCode::kNotFound,
                                   AllOf(HasSubstr("Key not found"),
                                         HasSubstr("$not_a_field"))));
            },
        },
        {
            "select_with_repeated_supported",
            "nested_test_all_types.payload.repeated_nested_message[1].bb",
            {{"nested_test_all_types",
              R"pb(
                payload {
                  repeated_nested_message {}
                  repeated_nested_message { bb: 7 }
                }
              )pb"}},
            ActivationSetupFn(),
            [](const absl::StatusOr<Value>& got) {
              ASSERT_OK_AND_ASSIGN(Value result, got);
              ASSERT_TRUE(result->Is<IntValue>()) << result->DebugString();
              EXPECT_EQ(result.GetInt().NativeValue(), 7);
            },
        },
        {
            "select_with_repeated_index_out_of_bounds",
            "nested_test_all_types.payload.repeated_nested_message[1].bb",
            {{"nested_test_all_types",
              R"pb(
                payload { repeated_nested_message {} }
              )pb"}},
            ActivationSetupFn(),
            [](const absl::StatusOr<Value>& got) {
              ASSERT_OK_AND_ASSIGN(Value result, got);
              ASSERT_TRUE(result->Is<ErrorValue>()) << result->DebugString();
              EXPECT_THAT(result.GetError().NativeValue(),
                          StatusIs(absl::StatusCode::kInvalidArgument,
                                   HasSubstr("index out of bounds")));
            },
        },
        {
            "unknown_field",
            "((false)? a.child.child : b.child).child.payload.single_int64",
            {{"a", ""},
             {"b",
              R"pb(
                child { child { payload { single_int64: -42 } } }
              )pb"}},
            [](google::protobuf::Arena*, Activation& act) {
              act.SetUnknownPatterns({AttributePattern(
                  "b", {AttributeQualifierPattern::OfString("child"),
                        AttributeQualifierPattern::OfString("child")})});
              return absl::OkStatus();
            },

            [](const absl::StatusOr<Value>& got) {
              ASSERT_OK_AND_ASSIGN(Value result, got);
              ASSERT_TRUE(result->Is<UnknownValue>()) << result->DebugString();
              EXPECT_THAT(
                  result.GetUnknown().attribute_set(),
                  ElementsAre(Eq(Attribute(
                      "b", {
                               AttributeQualifier::OfString("child"),
                               AttributeQualifier::OfString("child"),
                               AttributeQualifier::OfString("payload"),
                               AttributeQualifier::OfString("single_int64"),
                           }))));
            },
        },
        {
            "unknown_field_partial",
            "((false)? a.child.child : b.child).child.payload.single_int64",
            {{"a", ""},
             {"b",
              R"pb(
                child { child { payload { single_int64: -42 } } }
              )pb"}},
            [](google::protobuf::Arena*, Activation& act) {
              act.SetUnknownPatterns({AttributePattern(
                  "b", {AttributeQualifierPattern::OfString("child"),
                        AttributeQualifierPattern::OfString("child"),
                        AttributeQualifierPattern::OfString("child")})});
              return absl::OkStatus();
            },

            [](const absl::StatusOr<Value>& got) {
              ASSERT_OK_AND_ASSIGN(Value result, got);
              ASSERT_TRUE(result->Is<IntValue>()) << result->DebugString();
              EXPECT_EQ(result.GetInt().NativeValue(), -42);
            },
        },
        {
            "unknown_ident",
            "((false)? a.child.child : b.child).child.payload.single_int64",
            {{"a", ""},
             {"b",
              R"pb(
                child { child { payload { single_int64: -42 } } }
              )pb"}},
            [](google::protobuf::Arena*, Activation& act) {
              act.SetUnknownPatterns({
                  AttributePattern("b", {}),
              });
              return absl::OkStatus();
            },

            [](const absl::StatusOr<Value>& got) {
              ASSERT_OK_AND_ASSIGN(Value result, got);
              ASSERT_TRUE(result->Is<UnknownValue>()) << result->DebugString();
              EXPECT_THAT(result.GetUnknown().attribute_set(),
                          ElementsAre(Truly([](const Attribute& attr) {
                            return attr.variable_name() == "b";
                          })));
            },
        },
        {
            "unknown_pruned",
            "((false)? a.child.child : b.child).child.payload.single_int64",
            {{"a", ""},
             {"b",
              R"pb(
                child { child { payload { single_int64: -42 } } }
              )pb"}},
            [](google::protobuf::Arena*, Activation& act) {
              act.SetUnknownPatterns({
                  AttributePattern("a", {}),
              });
              return absl::OkStatus();
            },
            [](const absl::StatusOr<Value>& got) {
              ASSERT_OK_AND_ASSIGN(Value result, got);
              ASSERT_TRUE(result->Is<IntValue>()) << result->DebugString();
              EXPECT_EQ(result.GetInt().NativeValue(), -42);
            },
        },
        {
            "missing_field",
            "custom_predicate(nested_test_all_types.child.payload.standalone_"
            "message)",
            {{"nested_test_all_types",
              R"pb(
                child { payload { standalone_message { bb: 20 } } }
              )pb"}},
            [](google::protobuf::Arena*, Activation& act) {
              act.SetMissingPatterns({AttributePattern(
                  "nested_test_all_types",
                  {
                      AttributeQualifierPattern::OfString("child"),
                  })});
              return absl::OkStatus();
            },

            [](const absl::StatusOr<Value>& got) {
              ASSERT_OK_AND_ASSIGN(Value result, got);
              ASSERT_TRUE(result->Is<ErrorValue>()) << result->DebugString();
              EXPECT_THAT(result.GetError().NativeValue().message(),
                          HasSubstr("nested_test_all_types.child.payload."
                                    "standalone_message"));
            },
        },
        {
            "missing_field_partial",
            "custom_predicate(nested_test_all_types.child.payload.standalone_"
            "message)",
            {{"nested_test_all_types",
              R"pb(
                child { payload { standalone_message { bb: 20 } } }
              )pb"}},
            [](google::protobuf::Arena*, Activation& act) {
              act.SetMissingPatterns({AttributePattern(
                  "b", {AttributeQualifierPattern::OfString("child"),
                        AttributeQualifierPattern::OfString("child")})});
              return absl::OkStatus();
            },

            [](const absl::StatusOr<Value>& got) {
              ASSERT_OK_AND_ASSIGN(Value result, got);
              ASSERT_TRUE(result->Is<BoolValue>()) << result->DebugString();
              EXPECT_TRUE(result.GetBool().NativeValue());
            },
        },
        {
            "select_wrapper_int_leaf",
            "nested_test_all_types.payload.single_int64_wrapper",
            {{"nested_test_all_types",
              R"pb(
                payload { single_int64_wrapper { value: 10 } }
              )pb"}},
            nullptr,
            [](const absl::StatusOr<Value>& got) {
              ASSERT_OK_AND_ASSIGN(Value result, got);
              ASSERT_TRUE(result->Is<IntValue>()) << result->DebugString();
              EXPECT_EQ(result.GetInt().NativeValue(), 10);
            },
        },
        {
            "select_repeated_leaf",
            "nested_test_all_types.payload.repeated_int64",
            {{"nested_test_all_types",
              R"pb(
                payload { repeated_int64: 10 }
              )pb"}},
            nullptr,
            [](const absl::StatusOr<Value>& got) {
              ASSERT_OK_AND_ASSIGN(Value result, got);
              ASSERT_TRUE(result->Is<ListValue>()) << result->DebugString();
            },
        },
        {
            "select_map_leaf",
            "nested_test_all_types.payload.map_string_int64",
            {{"nested_test_all_types",
              R"pb(
                payload { map_string_int64 { key: "key", value: 12 } }
              )pb"}},
            nullptr,
            [](const absl::StatusOr<Value>& got) {
              ASSERT_OK_AND_ASSIGN(Value result, got);
              ASSERT_TRUE(result->Is<MapValue>()) << result->DebugString();
            },
        },
        {
            "select_with_map_dot",
            "nested_test_all_types.payload.map_string_message.field_like_key."
            "bb",
            {{"nested_test_all_types",
              R"pb(
                payload {
                  map_string_message {
                    key: "field_like_key",
                    value { bb: 42 }
                  }
                }
              )pb"}},
            nullptr,
            [](const absl::StatusOr<Value>& got) {
              ASSERT_OK_AND_ASSIGN(Value result, got);
              ASSERT_TRUE(result->Is<IntValue>()) << result->DebugString();
              EXPECT_EQ(result.GetInt().NativeValue(), 42);
            },
        },
        {
            "select_with_map_bool",
            "nested_test_all_types.payload.map_bool_message[false].bb",
            {{"nested_test_all_types",
              R"pb(
                payload {
                  map_bool_message {
                    key: false,
                    value { bb: 42 }
                  }
                }
              )pb"}},
            nullptr,
            [](const absl::StatusOr<Value>& got) {
              ASSERT_OK_AND_ASSIGN(Value result, got);
              ASSERT_TRUE(result->Is<IntValue>()) << result->DebugString();
              EXPECT_EQ(result.GetInt().NativeValue(), 42);
            },
        },
        {
            "select_with_map_int",
            "nested_test_all_types.payload.map_int64_message[-1].bb",
            {{"nested_test_all_types",
              R"pb(
                payload {
                  map_int64_message {
                    key: -1,
                    value { bb: 42 }
                  }
                }
              )pb"}},
            nullptr,
            [](const absl::StatusOr<Value>& got) {
              ASSERT_OK_AND_ASSIGN(Value result, got);
              ASSERT_TRUE(result->Is<IntValue>()) << result->DebugString();
              EXPECT_EQ(result.GetInt().NativeValue(), 42);
            },
        },
        {
            "select_with_map_uint",
            "nested_test_all_types.payload.map_uint64_message[1u].bb",
            {{"nested_test_all_types",
              R"pb(
                payload {
                  map_uint64_message {
                    key: 1,
                    value { bb: 42 }
                  }
                }
              )pb"}},
            nullptr,
            [](const absl::StatusOr<Value>& got) {
              ASSERT_OK_AND_ASSIGN(Value result, got);
              ASSERT_TRUE(result->Is<IntValue>()) << result->DebugString();
              EXPECT_EQ(result.GetInt().NativeValue(), 42);
            },
        },
        {
            "select_with_repeated",
            "nested_test_all_types.payload.repeated_nested_message[1].bb",
            {{"nested_test_all_types",
              R"pb(
                payload {
                  repeated_nested_message {}
                  repeated_nested_message { bb: 42 }
                }
              )pb"}},
            nullptr,
            [](const absl::StatusOr<Value>& got) {
              ASSERT_OK_AND_ASSIGN(Value result, got);
              ASSERT_TRUE(result->Is<IntValue>()) << result->DebugString();
              EXPECT_EQ(result.GetInt().NativeValue(), 42);
            },
        },
        {
            "select_with_any",
            "nested_test_all_types.payload.single_any.single_int64",
            {{"nested_test_all_types",
              R"pb(
                payload {
                  single_any {
                    [type.googleapis.com/cel.expr.conformance.proto2
                         .TestAllTypes] { single_int64: 42 }
                  }
                }
              )pb"}},
            nullptr,
            [](const absl::StatusOr<Value>& got) {
              ASSERT_OK_AND_ASSIGN(Value result, got);
              ASSERT_TRUE(result->Is<IntValue>()) << result->DebugString();
              EXPECT_EQ(result.GetInt().NativeValue(), 42);
            },
        },
        {
            "has_repeated_leaf_true",
            "has(nested_test_all_types.payload.repeated_int64)",
            {{"nested_test_all_types",
              R"pb(
                payload { repeated_int64: 42 }
              )pb"}},
            nullptr,
            [](const absl::StatusOr<Value>& got) {
              ASSERT_OK_AND_ASSIGN(Value result, got);
              ASSERT_TRUE(result->Is<BoolValue>()) << result->DebugString();
              EXPECT_TRUE(result.GetBool().NativeValue());
            },
        },
        {
            "has_repeated_leaf_false",
            "has(nested_test_all_types.payload.repeated_int64)",
            {{"nested_test_all_types",
              R"pb(
                payload {}
              )pb"}},
            nullptr,
            [](const absl::StatusOr<Value>& got) {
              ASSERT_OK_AND_ASSIGN(Value result, got);
              ASSERT_TRUE(result->Is<BoolValue>()) << result->DebugString();
              EXPECT_FALSE(result.GetBool().NativeValue());
            },
        },
        {
            "has_map_leaf_true",
            "has(nested_test_all_types.payload.map_string_int64)",
            {{"nested_test_all_types",
              R"pb(
                payload { map_string_int64 { key: "string" value: 12 } }
              )pb"}},
            nullptr,
            [](const absl::StatusOr<Value>& got) {
              ASSERT_OK_AND_ASSIGN(Value result, got);
              ASSERT_TRUE(result->Is<BoolValue>()) << result->DebugString();
              EXPECT_TRUE(result.GetBool().NativeValue());
            },
        },
        {
            "has_map_leaf_false",
            "has(nested_test_all_types.payload.map_string_int64)",
            {{"nested_test_all_types",
              R"pb(
                payload {}
              )pb"}},
            nullptr,
            [](const absl::StatusOr<Value>& got) {
              ASSERT_OK_AND_ASSIGN(Value result, got);
              ASSERT_TRUE(result->Is<BoolValue>()) << result->DebugString();
              EXPECT_FALSE(result.GetBool().NativeValue());
            },
        },
        {
            "has_map_field_like_key",
            "has(nested_test_all_types.payload.map_string_int64.field_like_"
            "key)",
            {{"nested_test_all_types",
              R"pb(
                payload { map_string_int64 { key: "field_like_key" value: 12 } }
              )pb"}},
            nullptr,
            [](const absl::StatusOr<Value>& got) {
              ASSERT_OK_AND_ASSIGN(Value result, got);
              ASSERT_TRUE(result->Is<BoolValue>()) << result->DebugString();
              EXPECT_TRUE(result.GetBool().NativeValue());
            },
        },
        {
            "has_map_field_like_key_false",
            "has(nested_test_all_types.payload.map_string_int64.field_like_"
            "key)",
            {{"nested_test_all_types",
              R"pb(
                payload { map_string_int64 { key: "wrong_key" value: 12 } }
              )pb"}},
            nullptr,
            [](const absl::StatusOr<Value>& got) {
              ASSERT_OK_AND_ASSIGN(Value result, got);
              ASSERT_TRUE(result->Is<BoolValue>()) << result->DebugString();
              EXPECT_FALSE(result.GetBool().NativeValue());
            },
        },
        {
            "select_wrong_runtime_type",
            "test_all_types.single_int64",
            {{}},
            [](google::protobuf::Arena* arena, Activation& activation) {
              activation.InsertOrAssignValue("test_all_types",
                                             cel::IntValue(42));
              return absl::OkStatus();
            },
            [](const absl::StatusOr<Value>& got) {
              EXPECT_THAT(got, StatusIs(absl::StatusCode::kInvalidArgument,
                                        HasSubstr("Expected struct type")));
            },
        },
        {
            "select_with_struct",
            "nested_test_all_types.payload.single_struct['key']['subkey']",
            {{"nested_test_all_types",
              R"pb(payload {
                     single_struct {
                       fields {
                         key: "key"
                         value {
                           struct_value {
                             fields {
                               key: "subkey"
                               value { bool_value: true }
                             }
                           }
                         }
                       }
                     }
                   })pb"}},
            nullptr,
            [](const absl::StatusOr<Value>& got) {
              ASSERT_OK_AND_ASSIGN(Value result, got);
              ASSERT_TRUE(result->Is<BoolValue>()) << result->DebugString();
              EXPECT_TRUE(result.GetBool().NativeValue());
            },
        },
        {
            "select_with_list_value",
            "nested_test_all_types.payload.list_value[0]['subkey']",
            {{"nested_test_all_types",
              R"pb(payload {
                     list_value {
                       values {
                         struct_value {
                           fields {
                             key: "subkey"
                             value { bool_value: true }
                           }
                         }
                       }
                     }
                   })pb"}},
            nullptr,
            [](const absl::StatusOr<Value>& got) {
              ASSERT_OK_AND_ASSIGN(Value result, got);
              ASSERT_TRUE(result->Is<BoolValue>()) << result->DebugString();
              EXPECT_TRUE(result.GetBool().NativeValue());
            },
        },
        {
            "select_with_value",
            "nested_test_all_types.payload.single_value['key']['subkey']",
            {{"nested_test_all_types",
              R"pb(payload {
                     single_value {
                       struct_value {
                         fields {
                           key: "key"
                           value {
                             struct_value {
                               fields {
                                 key: "subkey"
                                 value { bool_value: true }
                               }
                             }
                           }
                         }
                       }
                     }
                   })pb"}},
            nullptr,
            [](const absl::StatusOr<Value>& got) {
              ASSERT_OK_AND_ASSIGN(Value result, got);
              ASSERT_TRUE(result->Is<BoolValue>()) << result->DebugString();
              EXPECT_TRUE(result.GetBool().NativeValue());
            },
        },
    }),

    [](const testing::TestParamInfo<ProgramOptimizerTestCase>& info) {
      return info.param.case_name;
    });

// Tests covering unexpected / malformed ASTs.
//
// These cases shouldn't be possible under normal usage, but are possible if
// there's a bug in the optimizer implementation or if a hand-rolled AST is
// used.
class SelectOptimizationUnexpectedAstTest : public SelectOptimizationTest {
 public:
  SelectOptimizationUnexpectedAstTest()
      : SelectOptimizationTest(), next_id_(1) {}

  Expr NextExpr() {
    Expr result;
    result.set_id(next_id_++);
    return result;
  }

  cel::ListExprElement NextListExprElement() {
    cel::ListExprElement element;
    element.set_expr(NextExpr());
    return element;
  }

 protected:
  int64_t next_id_;
};

TEST_F(SelectOptimizationUnexpectedAstTest, WrongArgumentCount) {
  std::unique_ptr<Ast> ast = std::make_unique<Ast>(NextExpr(), SourceInfo());

  ast->mutable_root_expr().mutable_call_expr().set_function(kCelAttribute);
  ast->mutable_root_expr()
      .mutable_call_expr()
      .mutable_args()
      .emplace_back(NextExpr())
      .mutable_ident_expr()
      .set_name("ident");

  FlatExprBuilder builder(env_, runtime_options_);

  builder.AddProgramOptimizer(CreateSelectOptimizationProgramOptimizer());

  EXPECT_THAT(builder.CreateExpressionImpl(std::move(ast), nullptr),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(SelectOptimizationUnexpectedAstTest, EmptySelectPath) {
  std::unique_ptr<Ast> ast = std::make_unique<Ast>(NextExpr(), SourceInfo());

  ast->mutable_root_expr().mutable_call_expr().set_function(kCelAttribute);
  ast->mutable_root_expr()
      .mutable_call_expr()
      .mutable_args()
      .emplace_back(NextExpr())
      .mutable_ident_expr()
      .set_name("ident");
  ast->mutable_root_expr()
      .mutable_call_expr()
      .mutable_args()
      .emplace_back(NextExpr())
      .mutable_list_expr();

  FlatExprBuilder builder(env_, runtime_options_);

  builder.AddProgramOptimizer(CreateSelectOptimizationProgramOptimizer());

  EXPECT_THAT(builder.CreateExpressionImpl(std::move(ast), nullptr),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(SelectOptimizationUnexpectedAstTest, MalformedSelectPathNotPair) {
  std::unique_ptr<Ast> ast = std::make_unique<Ast>(NextExpr(), SourceInfo());

  ast->mutable_root_expr().mutable_call_expr().set_function(kCelAttribute);
  ast->mutable_root_expr()
      .mutable_call_expr()
      .mutable_args()
      .emplace_back(NextExpr())
      .mutable_ident_expr()
      .set_name("ident");
  auto& select_step_list = ast->mutable_root_expr()
                               .mutable_call_expr()
                               .mutable_args()
                               .emplace_back(NextExpr())
                               .mutable_list_expr();

  auto& select_step_element = select_step_list.mutable_elements()
                                  .emplace_back(NextListExprElement())
                                  .mutable_expr()
                                  .mutable_list_expr();

  select_step_element.mutable_elements()
      .emplace_back(NextListExprElement())
      .mutable_expr()
      .mutable_const_expr()
      .set_string_value("field");

  FlatExprBuilder builder(env_, runtime_options_);

  builder.AddProgramOptimizer(CreateSelectOptimizationProgramOptimizer());

  EXPECT_THAT(builder.CreateExpressionImpl(std::move(ast), nullptr),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(SelectOptimizationUnexpectedAstTest, MalformedSelectPathWrongPairTypes) {
  std::unique_ptr<Ast> ast = std::make_unique<Ast>(NextExpr(), SourceInfo());

  ast->mutable_root_expr().mutable_call_expr().set_function(kCelAttribute);
  ast->mutable_root_expr()
      .mutable_call_expr()
      .mutable_args()
      .emplace_back(NextExpr())
      .mutable_ident_expr()
      .set_name("ident");
  auto& select_step_list = ast->mutable_root_expr()
                               .mutable_call_expr()
                               .mutable_args()
                               .emplace_back(NextExpr())
                               .mutable_list_expr();

  auto& select_step_element = select_step_list.mutable_elements()
                                  .emplace_back(NextListExprElement())
                                  .mutable_expr()
                                  .mutable_list_expr();

  select_step_element.mutable_elements()
      .emplace_back(NextListExprElement())
      .mutable_expr()
      .mutable_const_expr()
      .set_string_value("field");

  select_step_element.mutable_elements()
      .emplace_back(NextListExprElement())
      .mutable_expr()
      .mutable_const_expr()
      .set_int64_value(1);

  FlatExprBuilder builder(env_, runtime_options_);

  builder.AddProgramOptimizer(CreateSelectOptimizationProgramOptimizer());

  EXPECT_THAT(builder.CreateExpressionImpl(std::move(ast), nullptr),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(SelectOptimizationUnexpectedAstTest,
       MalformedSelectPathUnsupportedConstant) {
  std::unique_ptr<Ast> ast = std::make_unique<Ast>(NextExpr(), SourceInfo());

  ast->mutable_root_expr().mutable_call_expr().set_function(kCelAttribute);
  ast->mutable_root_expr()
      .mutable_call_expr()
      .mutable_args()
      .emplace_back(NextExpr())
      .mutable_ident_expr()
      .set_name("ident");
  auto& select_step_list = ast->mutable_root_expr()
                               .mutable_call_expr()
                               .mutable_args()
                               .emplace_back(NextExpr())
                               .mutable_list_expr();

  auto& select_step_element = select_step_list.mutable_elements()
                                  .emplace_back(NextListExprElement())
                                  .mutable_expr();

  select_step_element.mutable_const_expr().set_bytes_value("bytes_key");

  FlatExprBuilder builder(env_, runtime_options_);

  builder.AddProgramOptimizer(CreateSelectOptimizationProgramOptimizer());

  EXPECT_THAT(builder.CreateExpressionImpl(std::move(ast), nullptr),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(SelectOptimizationUnexpectedAstTest, OptionalNotYetSupported) {
  std::unique_ptr<Ast> ast = std::make_unique<Ast>(NextExpr(), SourceInfo());

  ast->mutable_root_expr().mutable_call_expr().set_function(kCelAttribute);
  auto& call_args = ast->mutable_root_expr().mutable_call_expr().mutable_args();
  call_args.emplace_back(NextExpr()).mutable_ident_expr().set_name("ident");

  auto& list_expr = call_args.emplace_back(NextExpr()).mutable_list_expr();
  auto& fields = list_expr.mutable_elements()
                     .emplace_back(NextListExprElement())
                     .mutable_expr()
                     .mutable_list_expr()
                     .mutable_elements();

  fields.emplace_back(NextListExprElement())
      .mutable_expr()
      .mutable_const_expr()
      .set_int64_value(1);
  fields.emplace_back(NextListExprElement())
      .mutable_expr()
      .mutable_const_expr()
      .set_string_value("field");

  call_args.emplace_back(NextExpr()).mutable_const_expr().set_int64_value(0);

  FlatExprBuilder builder(env_, runtime_options_);

  builder.AddProgramOptimizer(CreateSelectOptimizationProgramOptimizer());

  EXPECT_THAT(builder.CreateExpressionImpl(std::move(ast), nullptr),
              StatusIs(absl::StatusCode::kUnimplemented));
}

}  // namespace
}  // namespace cel::extensions
