// Copyright 2024 Google LLC
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

#include "conformance/service.h"

#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include "google/api/expr/conformance/v1alpha1/conformance_service.pb.h"
#include "cel/expr/syntax.pb.h"
#include "google/api/expr/v1alpha1/checked.pb.h"
#include "google/api/expr/v1alpha1/eval.pb.h"
#include "google/api/expr/v1alpha1/syntax.pb.h"
#include "google/api/expr/v1alpha1/value.pb.h"
#include "google/protobuf/duration.pb.h"
#include "google/protobuf/empty.pb.h"
#include "google/protobuf/struct.pb.h"
#include "google/protobuf/timestamp.pb.h"
#include "google/rpc/code.pb.h"
#include "google/rpc/status.pb.h"
#include "absl/log/absl_check.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "checker/optional.h"
#include "checker/standard_library.h"
#include "checker/type_checker_builder.h"
#include "checker/type_checker_builder_factory.h"
#include "common/ast.h"
#include "common/ast_proto.h"
#include "common/decl_proto_v1alpha1.h"
#include "common/internal/value_conversion.h"
#include "common/source.h"
#include "common/value.h"
#include "eval/public/activation.h"
#include "eval/public/builtin_func_registrar.h"
#include "eval/public/cel_expr_builder_factory.h"
#include "eval/public/cel_expression.h"
#include "eval/public/cel_options.h"
#include "eval/public/cel_value.h"
#include "eval/public/transform_utility.h"
#include "extensions/bindings_ext.h"
#include "extensions/comprehensions_v2.h"
#include "extensions/comprehensions_v2_functions.h"
#include "extensions/comprehensions_v2_macros.h"
#include "extensions/encoders.h"
#include "extensions/lists_functions.h"
#include "extensions/math_ext.h"
#include "extensions/math_ext_decls.h"
#include "extensions/math_ext_macros.h"
#include "extensions/proto_ext.h"
#include "extensions/protobuf/enum_adapter.h"
#include "extensions/select_optimization.h"
#include "extensions/set_membership_optimization.h"
#include "extensions/sets_functions.h"
#include "extensions/strings.h"
#include "internal/status_macros.h"
#include "parser/macro_registry.h"
#include "parser/options.h"
#include "parser/parser.h"
#include "parser/standard_macros.h"
#include "runtime/activation.h"
#include "runtime/constant_folding.h"
#include "runtime/optional_types.h"
#include "runtime/reference_resolver.h"
#include "runtime/regex_precompilation.h"
#include "runtime/runtime.h"
#include "runtime/runtime_options.h"
#include "runtime/standard_runtime_builder_factory.h"
#include "testutil/test_macros.h"
#include "cel/expr/conformance/proto2/test_all_types.pb.h"
#include "cel/expr/conformance/proto2/test_all_types_extensions.pb.h"
#include "cel/expr/conformance/proto3/test_all_types.pb.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"

using ::cel::CreateStandardRuntimeBuilder;
using ::cel::Runtime;
using ::cel::RuntimeOptions;
using ::cel::extensions::RegisterProtobufEnum;
using ::cel::test::ConvertWireCompatProto;
using ::cel::test::FromExprValue;
using ::cel::test::ToExprValue;

using ::google::protobuf::Arena;

namespace google::api::expr::runtime {

namespace {

google::rpc::Code ToGrpcCode(absl::StatusCode code) {
  return static_cast<google::rpc::Code>(code);
}

using ConformanceServiceInterface =
    ::cel_conformance::ConformanceServiceInterface;

// Return a normalized raw expr for evaluation.
cel::expr::Expr ExtractExpr(
    const conformance::v1alpha1::EvalRequest& request) {
  const v1alpha1::Expr* expr = nullptr;

  // For now, discard type-check information if any.
  if (request.has_parsed_expr()) {
    expr = &request.parsed_expr().expr();
  } else if (request.has_checked_expr()) {
    expr = &request.checked_expr().expr();
  }
  cel::expr::Expr out;
  if (expr != nullptr) {
    ABSL_CHECK(ConvertWireCompatProto(*expr, &out));  // Crash OK
  }
  return out;
}

absl::Status LegacyParse(const conformance::v1alpha1::ParseRequest& request,
                         conformance::v1alpha1::ParseResponse& response,
                         bool enable_optional_syntax,
                         bool enable_variadic_logical_operators,
                         bool enable_pratt_parser) {
  if (request.cel_source().empty()) {
    return absl::InvalidArgumentError("no source code");
  }
  cel::ParserOptions options;
  options.enable_optional_syntax = enable_optional_syntax;
  options.enable_quoted_identifiers = true;
  options.enable_variadic_logical_operators = enable_variadic_logical_operators;
  options.enable_pratt_parser = enable_pratt_parser;
  cel::MacroRegistry macros;
  CEL_RETURN_IF_ERROR(cel::RegisterStandardMacros(macros, options));
  CEL_RETURN_IF_ERROR(
      cel::extensions::RegisterComprehensionsV2Macros(macros, options));
  CEL_RETURN_IF_ERROR(cel::extensions::RegisterBindingsMacros(macros, options));
  CEL_RETURN_IF_ERROR(cel::extensions::RegisterMathMacros(macros, options));
  CEL_RETURN_IF_ERROR(cel::extensions::RegisterProtoMacros(macros, options));
  CEL_RETURN_IF_ERROR(cel::extensions::RegisterListsMacros(macros, options));
  CEL_RETURN_IF_ERROR(cel::test::RegisterTestMacros(macros));
  CEL_ASSIGN_OR_RETURN(auto source, cel::NewSource(request.cel_source(),
                                                   request.source_location()));
  CEL_ASSIGN_OR_RETURN(auto parsed_expr,
                       parser::Parse(*source, macros, options));
  ABSL_CHECK(  // Crash OK
      ConvertWireCompatProto(parsed_expr, response.mutable_parsed_expr()));
  return absl::OkStatus();
}

absl::Status CheckImpl(google::protobuf::Arena* arena,
                       const conformance::v1alpha1::CheckRequest& request,
                       conformance::v1alpha1::CheckResponse& response) {
  cel::expr::ParsedExpr parsed_expr;

  ABSL_CHECK(ConvertWireCompatProto(request.parsed_expr(),  // Crash OK
                                    &parsed_expr));

  CEL_ASSIGN_OR_RETURN(std::unique_ptr<cel::Ast> ast,
                       cel::CreateAstFromParsedExpr(parsed_expr));

  absl::string_view location = parsed_expr.source_info().location();
  std::unique_ptr<cel::Source> source;
  if (absl::StartsWith(location, "Source: ")) {
    location = absl::StripPrefix(location, "Source: ");
    CEL_ASSIGN_OR_RETURN(source, cel::NewSource(location));
  }

  CEL_ASSIGN_OR_RETURN(
      std::unique_ptr<cel::TypeCheckerBuilder> builder,
      cel::CreateTypeCheckerBuilder(google::protobuf::DescriptorPool::generated_pool()));

  if (!request.no_std_env()) {
    CEL_RETURN_IF_ERROR(builder->AddLibrary(cel::StandardCheckerLibrary()));
    CEL_RETURN_IF_ERROR(builder->AddLibrary(cel::OptionalCheckerLibrary()));
    CEL_RETURN_IF_ERROR(
        builder->AddLibrary(cel::extensions::BindingsCheckerLibrary()));
    CEL_RETURN_IF_ERROR(
        builder->AddLibrary(cel::extensions::StringsCheckerLibrary()));
    CEL_RETURN_IF_ERROR(
        builder->AddLibrary(cel::extensions::MathCheckerLibrary()));
    CEL_RETURN_IF_ERROR(
        builder->AddLibrary(cel::extensions::EncodersCheckerLibrary()));
    CEL_RETURN_IF_ERROR(
        builder->AddLibrary(cel::extensions::ComprehensionsV2CheckerLibrary()));
    CEL_RETURN_IF_ERROR(
        builder->AddLibrary(cel::extensions::ListsCheckerLibrary()));
    CEL_RETURN_IF_ERROR(
        builder->AddLibrary(cel::extensions::SetsCheckerLibrary()));
  }

  for (const auto& decl : request.type_env()) {
    const auto& name = decl.name();
    if (decl.has_function()) {
      CEL_ASSIGN_OR_RETURN(
          auto fn_decl, cel::FunctionDeclFromV1Alpha1Proto(
                            name, decl.function(),
                            google::protobuf::DescriptorPool::generated_pool(), arena));
      CEL_RETURN_IF_ERROR(builder->MergeFunction(std::move(fn_decl)));
    } else if (decl.has_ident()) {
      CEL_ASSIGN_OR_RETURN(
          auto var_decl, cel::VariableDeclFromV1Alpha1Proto(
                             name, decl.ident(),
                             google::protobuf::DescriptorPool::generated_pool(), arena));
      CEL_RETURN_IF_ERROR(builder->AddVariable(std::move(var_decl)));
    }
  }
  builder->set_container(request.container());

  CEL_ASSIGN_OR_RETURN(auto checker, std::move(*builder).Build());

  CEL_ASSIGN_OR_RETURN(auto validation_result, checker->Check(std::move(ast)));

  for (const auto& checker_issue : validation_result.GetIssues()) {
    auto* issue = response.add_issues();
    issue->set_code(ToGrpcCode(absl::StatusCode::kInvalidArgument));
    if (source) {
      issue->set_message(checker_issue.ToDisplayString(*source));
    } else {
      issue->set_message(checker_issue.message());
    }
  }

  const cel::Ast* checked_ast = validation_result.GetAst();
  if (!validation_result.IsValid() || checked_ast == nullptr) {
    return absl::OkStatus();
  }
  cel::expr::CheckedExpr pb_checked_ast;
  CEL_RETURN_IF_ERROR(
      cel::AstToCheckedExpr(*validation_result.GetAst(), &pb_checked_ast));
  ABSL_CHECK(ConvertWireCompatProto(pb_checked_ast,  // Crash OK
                                    response.mutable_checked_expr()));
  return absl::OkStatus();
}

class LegacyConformanceServiceImpl : public ConformanceServiceInterface {
 public:
  static absl::StatusOr<std::unique_ptr<LegacyConformanceServiceImpl>> Create(
      bool optimize, bool recursive, bool extended_optimizations,
      bool enable_variadic_logical_operators, bool enable_pratt_parser) {
    static auto* constant_arena = new Arena();

    google::protobuf::LinkMessageReflection<
        cel::expr::conformance::proto3::TestAllTypes>();
    google::protobuf::LinkMessageReflection<
        cel::expr::conformance::proto2::TestAllTypes>();
    google::protobuf::LinkMessageReflection<
        cel::expr::conformance::proto3::NestedTestAllTypes>();
    google::protobuf::LinkMessageReflection<
        cel::expr::conformance::proto2::NestedTestAllTypes>();
    google::protobuf::LinkExtensionReflection(cel::expr::conformance::proto2::int32_ext);
    google::protobuf::LinkExtensionReflection(cel::expr::conformance::proto2::nested_ext);
    google::protobuf::LinkExtensionReflection(
        cel::expr::conformance::proto2::test_all_types_ext);
    google::protobuf::LinkExtensionReflection(
        cel::expr::conformance::proto2::nested_enum_ext);
    google::protobuf::LinkExtensionReflection(
        cel::expr::conformance::proto2::repeated_test_all_types);
    google::protobuf::LinkExtensionReflection(
        cel::expr::conformance::proto2::Proto2ExtensionScopedMessage::
            int64_ext);
    google::protobuf::LinkExtensionReflection(
        cel::expr::conformance::proto2::Proto2ExtensionScopedMessage::
            message_scoped_nested_ext);
    google::protobuf::LinkExtensionReflection(
        cel::expr::conformance::proto2::Proto2ExtensionScopedMessage::
            nested_enum_ext);
    google::protobuf::LinkExtensionReflection(
        cel::expr::conformance::proto2::Proto2ExtensionScopedMessage::
            message_scoped_repeated_test_all_types);

    InterpreterOptions options;
    options.enable_qualified_type_identifiers = true;
    options.enable_timestamp_duration_overflow_errors = true;
    options.enable_heterogeneous_equality = true;
    options.enable_empty_wrapper_null_unboxing = true;
    options.enable_qualified_identifier_rewrites = true;
    options.fail_on_warnings = false;

    if (optimize) {
      std::cerr << "Enabling optimizations" << std::endl;
      options.constant_folding = true;
      options.constant_arena = constant_arena;
      options.enable_typed_field_access = true;
    }

    if (extended_optimizations) {
      std::cerr << "Enabling extended optimizations" << std::endl;
      options.enable_select_optimization = true;
    }

    if (recursive) {
      options.max_recursion_depth = 48;
    }

    std::unique_ptr<CelExpressionBuilder> builder =
        CreateCelExpressionBuilder(options);
    auto type_registry = builder->GetTypeRegistry();
    type_registry->Register(
        cel::expr::conformance::proto2::GlobalEnum_descriptor());
    type_registry->Register(
        cel::expr::conformance::proto3::GlobalEnum_descriptor());
    type_registry->Register(
        cel::expr::conformance::proto2::TestAllTypes::NestedEnum_descriptor());
    type_registry->Register(
        cel::expr::conformance::proto3::TestAllTypes::NestedEnum_descriptor());
    CEL_RETURN_IF_ERROR(
        RegisterBuiltinFunctions(builder->GetRegistry(), options));
    CEL_RETURN_IF_ERROR(cel::extensions::RegisterComprehensionsV2Functions(
        builder->GetRegistry(), options));
    CEL_RETURN_IF_ERROR(cel::extensions::RegisterEncodersFunctions(
        builder->GetRegistry(), options));
    CEL_RETURN_IF_ERROR(cel::extensions::RegisterStringsFunctions(
        builder->GetRegistry(), options));
    CEL_RETURN_IF_ERROR(cel::extensions::RegisterMathExtensionFunctions(
        builder->GetRegistry(), options));
    CEL_RETURN_IF_ERROR(cel::extensions::RegisterSetsFunctions(
        builder->GetRegistry(), options));

    return absl::WrapUnique(new LegacyConformanceServiceImpl(
        std::move(builder), enable_variadic_logical_operators,
        enable_pratt_parser));
  }

  void Parse(const conformance::v1alpha1::ParseRequest& request,
             conformance::v1alpha1::ParseResponse& response) override {
    auto status =
        LegacyParse(request, response, /*enable_optional_syntax=*/false,
                    enable_variadic_logical_operators_, enable_pratt_parser_);
    if (!status.ok()) {
      auto* issue = response.add_issues();
      issue->set_code(ToGrpcCode(status.code()));
      issue->set_message(status.message());
    }
  }

  void Check(const conformance::v1alpha1::CheckRequest& request,
             conformance::v1alpha1::CheckResponse& response) override {
    google::protobuf::Arena arena;
    auto status = CheckImpl(&arena, request, response);
    if (!status.ok()) {
      auto* issue = response.add_issues();
      issue->set_code(ToGrpcCode(status.code()));
      issue->set_message(status.message());
    }
  }

  absl::Status Eval(const conformance::v1alpha1::EvalRequest& request,
                    conformance::v1alpha1::EvalResponse& response) override {
    Arena arena;
    cel::expr::SourceInfo source_info;
    cel::expr::Expr expr = ExtractExpr(request);
    builder_->set_container(request.container());
    absl::StatusOr<std::unique_ptr<CelExpression>> cel_expression_status =
        absl::InternalError(
            "no expression provided in ConformanceService::Eval");

    if (request.has_parsed_expr()) {
      cel::expr::ParsedExpr parsed_expr;
      if (!ConvertWireCompatProto(request.parsed_expr(), &parsed_expr)) {
        return absl::InternalError(
            "failed to convert versioned ParsedExpr to unversioned");
      }
      cel_expression_status = builder_->CreateExpression(
          &parsed_expr.expr(), &parsed_expr.source_info());
    } else if (request.has_checked_expr()) {
      cel::expr::CheckedExpr checked_expr;
      if (!ConvertWireCompatProto(request.checked_expr(), &checked_expr)) {
        return absl::InternalError(
            "failed to convert versioned CheckedExpr to unversioned");
      }
      cel_expression_status = builder_->CreateExpression(&checked_expr);
    }

    if (!cel_expression_status.ok()) {
      return absl::InternalError(cel_expression_status.status().ToString(
          absl::StatusToStringMode::kWithEverything));
    }

    auto cel_expression = std::move(cel_expression_status.value());
    Activation activation;

    for (const auto& pair : request.bindings()) {
      auto* import_value = Arena::Create<cel::expr::Value>(&arena);
      ABSL_CHECK(ConvertWireCompatProto(pair.second.value(),  // Crash OK
                                        import_value));
      auto import_status = ValueToCelValue(*import_value, &arena);
      if (!import_status.ok()) {
        return absl::InternalError(import_status.status().ToString(
            absl::StatusToStringMode::kWithEverything));
      }
      activation.InsertValue(pair.first, import_status.value());
    }

    auto eval_status = cel_expression->Evaluate(activation, &arena);
    if (!eval_status.ok()) {
      *response.mutable_result()
           ->mutable_error()
           ->add_errors()
           ->mutable_message() = eval_status.status().ToString(
          absl::StatusToStringMode::kWithEverything);
      return absl::OkStatus();
    }

    CelValue result = eval_status.value();
    if (result.IsError()) {
      *response.mutable_result()
           ->mutable_error()
           ->add_errors()
           ->mutable_message() = std::string(result.ErrorOrDie()->ToString(
          absl::StatusToStringMode::kWithEverything));
    } else {
      cel::expr::Value export_value;
      auto export_status = CelValueToValue(result, &export_value);
      if (!export_status.ok()) {
        return absl::InternalError(
            export_status.ToString(absl::StatusToStringMode::kWithEverything));
      }
      auto* result_value = response.mutable_result()->mutable_value();
      ABSL_CHECK(  // Crash OK
          ConvertWireCompatProto(export_value, result_value));
    }
    return absl::OkStatus();
  }

 private:
  LegacyConformanceServiceImpl(std::unique_ptr<CelExpressionBuilder> builder,
                               bool enable_variadic_logical_operators,
                               bool enable_pratt_parser)
      : builder_(std::move(builder)),
        enable_variadic_logical_operators_(enable_variadic_logical_operators),
        enable_pratt_parser_(enable_pratt_parser) {}

  std::unique_ptr<CelExpressionBuilder> builder_;
  bool enable_variadic_logical_operators_;
  bool enable_pratt_parser_;
};

class ModernConformanceServiceImpl : public ConformanceServiceInterface {
 public:
  static absl::StatusOr<std::unique_ptr<ModernConformanceServiceImpl>> Create(
      bool optimize, bool recursive, bool extended_optimizations,
      bool enable_variadic_logical_operators, bool enable_pratt_parser) {
    google::protobuf::LinkMessageReflection<
        cel::expr::conformance::proto3::TestAllTypes>();
    google::protobuf::LinkMessageReflection<
        cel::expr::conformance::proto2::TestAllTypes>();
    google::protobuf::LinkMessageReflection<
        cel::expr::conformance::proto3::NestedTestAllTypes>();
    google::protobuf::LinkMessageReflection<
        cel::expr::conformance::proto2::NestedTestAllTypes>();
    google::protobuf::LinkExtensionReflection(cel::expr::conformance::proto2::int32_ext);
    google::protobuf::LinkExtensionReflection(cel::expr::conformance::proto2::nested_ext);
    google::protobuf::LinkExtensionReflection(
        cel::expr::conformance::proto2::test_all_types_ext);
    google::protobuf::LinkExtensionReflection(
        cel::expr::conformance::proto2::nested_enum_ext);
    google::protobuf::LinkExtensionReflection(
        cel::expr::conformance::proto2::repeated_test_all_types);
    google::protobuf::LinkExtensionReflection(
        cel::expr::conformance::proto2::Proto2ExtensionScopedMessage::
            int64_ext);
    google::protobuf::LinkExtensionReflection(
        cel::expr::conformance::proto2::Proto2ExtensionScopedMessage::
            message_scoped_nested_ext);
    google::protobuf::LinkExtensionReflection(
        cel::expr::conformance::proto2::Proto2ExtensionScopedMessage::
            nested_enum_ext);
    google::protobuf::LinkExtensionReflection(
        cel::expr::conformance::proto2::Proto2ExtensionScopedMessage::
            message_scoped_repeated_test_all_types);

    RuntimeOptions options;
    options.enable_qualified_type_identifiers = true;
    options.enable_timestamp_duration_overflow_errors = true;
    options.enable_heterogeneous_equality = true;
    options.enable_empty_wrapper_null_unboxing = true;
    // Planning warnings are expected in conformance tests, but the test expects
    // failure to happen at evaluation time so we ignore them.
    options.fail_on_warnings = false;
    if (recursive) {
      options.max_recursion_depth = 48;
    }

    return absl::WrapUnique(new ModernConformanceServiceImpl(
        options, optimize, extended_optimizations,
        enable_variadic_logical_operators, enable_pratt_parser));
  }

  absl::StatusOr<std::unique_ptr<const cel::Runtime>> Setup(
      absl::string_view container) {
    RuntimeOptions options(options_);
    options.container = std::string(container);
    if (enable_optimizations_) {
      options.enable_typed_field_access = true;
    }
    CEL_ASSIGN_OR_RETURN(
        auto builder, CreateStandardRuntimeBuilder(
                          google::protobuf::DescriptorPool::generated_pool(), options));

    if (enable_optimizations_) {
      CEL_RETURN_IF_ERROR(cel::extensions::EnableConstantFolding(
          builder, google::protobuf::MessageFactory::generated_factory()));
      CEL_RETURN_IF_ERROR(cel::extensions::EnableRegexPrecompilation(builder));
    }
    CEL_RETURN_IF_ERROR(cel::EnableReferenceResolver(
        builder, cel::ReferenceResolverEnabled::kAlways));
    if (enable_extended_optimizations_) {
      CEL_RETURN_IF_ERROR(cel::extensions::EnableSelectOptimization(builder));
      CEL_RETURN_IF_ERROR(
          cel::extensions::EnableSetMembershipOptimization(builder));
    }

    auto& type_registry = builder.type_registry();
    // Use linked pbs in the generated descriptor pool.
    CEL_RETURN_IF_ERROR(RegisterProtobufEnum(
        type_registry,
        cel::expr::conformance::proto2::GlobalEnum_descriptor()));
    CEL_RETURN_IF_ERROR(RegisterProtobufEnum(
        type_registry,
        cel::expr::conformance::proto3::GlobalEnum_descriptor()));
    CEL_RETURN_IF_ERROR(RegisterProtobufEnum(
        type_registry,
        cel::expr::conformance::proto2::TestAllTypes::NestedEnum_descriptor()));
    CEL_RETURN_IF_ERROR(RegisterProtobufEnum(
        type_registry,
        cel::expr::conformance::proto3::TestAllTypes::NestedEnum_descriptor()));

    CEL_RETURN_IF_ERROR(cel::extensions::RegisterComprehensionsV2Functions(
        builder.function_registry(), options));
    CEL_RETURN_IF_ERROR(cel::extensions::EnableOptionalTypes(builder));
    CEL_RETURN_IF_ERROR(cel::extensions::RegisterEncodersFunctions(
        builder.function_registry(), options));
    CEL_RETURN_IF_ERROR(cel::extensions::RegisterStringsFunctions(
        builder.function_registry(), options));
    CEL_RETURN_IF_ERROR(cel::extensions::RegisterMathExtensionFunctions(
        builder.function_registry(), options));
    CEL_RETURN_IF_ERROR(cel::extensions::RegisterListsFunctions(
        builder.function_registry(), options));
    CEL_RETURN_IF_ERROR(cel::extensions::RegisterSetsFunctions(
        builder.function_registry(), options));

    return std::move(builder).Build();
  }

  void Parse(const conformance::v1alpha1::ParseRequest& request,
             conformance::v1alpha1::ParseResponse& response) override {
    auto status =
        LegacyParse(request, response, /*enable_optional_syntax=*/true,
                    enable_variadic_logical_operators_, enable_pratt_parser_);
    if (!status.ok()) {
      auto* issue = response.add_issues();
      issue->set_code(ToGrpcCode(status.code()));
      issue->set_message(status.message());
    }
  }

  void Check(const conformance::v1alpha1::CheckRequest& request,
             conformance::v1alpha1::CheckResponse& response) override {
    google::protobuf::Arena arena;
    auto status = CheckImpl(&arena, request, response);
    if (!status.ok()) {
      auto* issue = response.add_issues();
      issue->set_code(ToGrpcCode(status.code()));
      issue->set_message(status.message());
    }
  }

  absl::Status Eval(const conformance::v1alpha1::EvalRequest& request,
                    conformance::v1alpha1::EvalResponse& response) override {
    google::protobuf::Arena arena;

    auto runtime_status = Setup(request.container());
    if (!runtime_status.ok()) {
      return absl::InternalError(runtime_status.status().ToString(
          absl::StatusToStringMode::kWithEverything));
    }
    std::unique_ptr<const cel::Runtime> runtime =
        std::move(runtime_status).value();

    auto program_status = Plan(*runtime, request);
    if (!program_status.ok()) {
      return absl::InternalError(program_status.status().ToString(
          absl::StatusToStringMode::kWithEverything));
    }
    std::unique_ptr<cel::TraceableProgram> program =
        std::move(program_status).value();
    cel::Activation activation;

    for (const auto& pair : request.bindings()) {
      cel::expr::Value import_value;
      ABSL_CHECK(ConvertWireCompatProto(pair.second.value(),  // Crash OK
                                        &import_value));
      auto import_status =
          FromExprValue(import_value, runtime->GetDescriptorPool(),
                        runtime->GetMessageFactory(), &arena);
      if (!import_status.ok()) {
        return absl::InternalError(import_status.status().ToString(
            absl::StatusToStringMode::kWithEverything));
      }

      activation.InsertOrAssignValue(pair.first,
                                     std::move(import_status).value());
    }

    auto eval_status = program->Evaluate(&arena, activation);
    if (!eval_status.ok()) {
      *response.mutable_result()
           ->mutable_error()
           ->add_errors()
           ->mutable_message() = eval_status.status().ToString(
          absl::StatusToStringMode::kWithEverything);
      return absl::OkStatus();
    }

    cel::Value result = eval_status.value();
    if (result->Is<cel::ErrorValue>()) {
      const absl::Status& error = result.GetError().NativeValue();
      *response.mutable_result()
           ->mutable_error()
           ->add_errors()
           ->mutable_message() = std::string(
          error.ToString(absl::StatusToStringMode::kWithEverything));
    } else {
      auto export_status = ToExprValue(result, runtime->GetDescriptorPool(),
                                       runtime->GetMessageFactory(), &arena);
      if (!export_status.ok()) {
        return absl::InternalError(export_status.status().ToString(
            absl::StatusToStringMode::kWithEverything));
      }
      auto* result_value = response.mutable_result()->mutable_value();
      ABSL_CHECK(  // Crash OK
          ConvertWireCompatProto(*export_status, result_value));
    }
    return absl::OkStatus();
  }

 private:
  ModernConformanceServiceImpl(const RuntimeOptions& options,
                               bool enable_optimizations,
                               bool enable_extended_optimizations,
                               bool enable_variadic_logical_operators,
                               bool enable_pratt_parser)
      : options_(options),
        enable_optimizations_(enable_optimizations),
        enable_extended_optimizations_(enable_extended_optimizations),
        enable_variadic_logical_operators_(enable_variadic_logical_operators),
        enable_pratt_parser_(enable_pratt_parser) {}

  static absl::StatusOr<std::unique_ptr<cel::TraceableProgram>> Plan(
      const cel::Runtime& runtime,
      const conformance::v1alpha1::EvalRequest& request) {
    std::unique_ptr<cel::Ast> ast;
    if (request.has_parsed_expr()) {
      cel::expr::ParsedExpr unversioned;
      ABSL_CHECK(ConvertWireCompatProto(request.parsed_expr(),  // Crash OK
                                        &unversioned));

      CEL_ASSIGN_OR_RETURN(
          ast, cel::CreateAstFromParsedExpr(std::move(unversioned)));

    } else if (request.has_checked_expr()) {
      cel::expr::CheckedExpr unversioned;
      ABSL_CHECK(ConvertWireCompatProto(request.checked_expr(),  // Crash OK
                                        &unversioned));
      CEL_ASSIGN_OR_RETURN(
          ast, cel::CreateAstFromCheckedExpr(std::move(unversioned)));
    }
    if (ast == nullptr) {
      return absl::InternalError("no expression provided");
    }

    return runtime.CreateTraceableProgram(std::move(ast));
  }

  RuntimeOptions options_;
  bool enable_optimizations_;
  bool enable_extended_optimizations_;
  bool enable_variadic_logical_operators_;
  bool enable_pratt_parser_;
};

}  // namespace

}  // namespace google::api::expr::runtime

namespace cel_conformance {

absl::StatusOr<std::unique_ptr<ConformanceServiceInterface>>
NewConformanceService(const ConformanceServiceOptions& options) {
  if (options.modern) {
    return google::api::expr::runtime::ModernConformanceServiceImpl::Create(
        options.optimize, options.recursive, options.extended_optimizations,
        options.enable_variadic_logical_operators, options.enable_pratt_parser);
  } else {
    return google::api::expr::runtime::LegacyConformanceServiceImpl::Create(
        options.optimize, options.recursive, options.extended_optimizations,
        options.enable_variadic_logical_operators, options.enable_pratt_parser);
  }
}

}  // namespace cel_conformance
