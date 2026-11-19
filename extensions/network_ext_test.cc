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

#include "extensions/network_ext.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cel/expr/syntax.pb.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord_test_helpers.h"
#include "absl/strings/string_view.h"
#include "checker/standard_library.h"
#include "checker/validation_result.h"
#include "common/ast.h"
#include "common/decl.h"
#include "common/kind.h"
#include "common/type.h"
#include "common/value.h"
#include "common/value_testing.h"
#include "compiler/compiler.h"
#include "compiler/compiler_factory.h"
#include "compiler/optional.h"
#include "eval/public/activation.h"
#include "eval/public/cel_expr_builder_factory.h"
#include "eval/public/cel_expression.h"
#include "eval/public/cel_function_registry.h"
#include "eval/public/cel_options.h"
#include "eval/public/cel_value.h"
#include "extensions/protobuf/runtime_adapter.h"
#include "internal/status_macros.h"
#include "internal/testing.h"
#include "internal/testing_descriptor_pool.h"
#include "parser/parser.h"
#include "runtime/activation.h"
#include "runtime/constant_folding.h"
#include "runtime/optional_types.h"
#include "runtime/reference_resolver.h"
#include "runtime/runtime.h"
#include "runtime/runtime_options.h"
#include "runtime/standard_runtime_builder_factory.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/message.h"

namespace cel::extensions {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::cel::test::BoolValueIs;
using ::cel::test::ErrorValueIs;
using ::cel::test::IntValueIs;
using ::cel::test::StringValueIs;
using ::google::api::expr::parser::Parse;
using ::google::api::expr::runtime::CelExpressionBuilder;
using ::google::api::expr::runtime::CelFunctionRegistry;
using ::google::api::expr::runtime::CelValue;
using ::google::api::expr::runtime::CreateCelExpressionBuilder;
using ::google::api::expr::runtime::InterpreterOptions;
using ::testing::AllOf;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::SizeIs;
using ::testing::TestWithParam;
using ::testing::ValuesIn;

using LegacyActivation = google::api::expr::runtime::Activation;

absl::StatusOr<std::unique_ptr<Compiler>> CreateTestCompiler(
    int version = kNetworkExtensionLatestVersion) {
  CEL_ASSIGN_OR_RETURN(
      auto builder, NewCompilerBuilder(internal::GetTestingDescriptorPool()));
  CEL_RETURN_IF_ERROR(builder->AddLibrary(StandardCheckerLibrary()));
  CEL_RETURN_IF_ERROR(builder->AddLibrary(OptionalCompilerLibrary()));
  CEL_RETURN_IF_ERROR(builder->AddLibrary(NetworkCompilerLibrary(version)));
  return std::move(*builder).Build();
}

absl::StatusOr<std::unique_ptr<const Runtime>> CreateTestRuntime(
    int version = kNetworkExtensionLatestVersion,
    bool enable_constant_folding = false) {
  RuntimeOptions options;
  options.enable_qualified_type_identifiers = true;
  CEL_ASSIGN_OR_RETURN(auto builder,
                       CreateStandardRuntimeBuilder(
                           internal::GetTestingDescriptorPool(), options));
  if (enable_constant_folding) {
    CEL_RETURN_IF_ERROR(EnableConstantFolding(builder));
  }
  CEL_RETURN_IF_ERROR(
      EnableReferenceResolver(builder, ReferenceResolverEnabled::kAlways));
  CEL_RETURN_IF_ERROR(EnableOptionalTypes(builder));
  CEL_RETURN_IF_ERROR(RegisterNetworkFunctions(builder, version));
  return std::move(builder).Build();
}

TEST(NetworkExtTest, VersionedRegistration) {
  for (int version : {0, kNetworkExtensionLatestVersion}) {
    ASSERT_OK_AND_ASSIGN(auto compiler, CreateTestCompiler(version));
    ASSERT_OK_AND_ASSIGN(auto runtime, CreateTestRuntime(version));
    ASSERT_OK_AND_ASSIGN(
        ValidationResult validation_result,
        compiler->Compile("cidr('10.0.0.0/8').containsIP(ip('10.1.2.3'))"));
    ASSERT_TRUE(validation_result.IsValid()) << validation_result.FormatError();
    ASSERT_OK_AND_ASSIGN(std::unique_ptr<Ast> ast,
                         validation_result.ReleaseAst());
    ASSERT_OK_AND_ASSIGN(std::unique_ptr<Program> program,
                         runtime->CreateProgram(std::move(ast)));
    google::protobuf::Arena arena;
    Activation activation;
    EXPECT_THAT(program->Evaluate(&arena, activation),
                IsOkAndHolds(BoolValueIs(true)));
  }
}

TEST(NetworkExtTest, BuildFailsWithoutQualifiedTypeIdentifiers) {
  RuntimeOptions options;
  options.enable_qualified_type_identifiers = false;

  ASSERT_OK_AND_ASSIGN(auto builder,
                       CreateStandardRuntimeBuilder(
                           internal::GetTestingDescriptorPool(), options));
  EXPECT_THAT(RegisterNetworkFunctions(builder),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("enable_qualified_type_identifiers")));
}

TEST(NetworkExtTest, BuildFailsWithoutHeterogeneousEquality) {
  RuntimeOptions options;
  options.enable_qualified_type_identifiers = true;
  // NOLINTNEXTLINE(clang-diagnostic-deprecated-declarations)
  options.enable_heterogeneous_equality = false;

  ASSERT_OK_AND_ASSIGN(auto builder,
                       CreateStandardRuntimeBuilder(
                           internal::GetTestingDescriptorPool(), options));
  EXPECT_THAT(RegisterNetworkFunctions(builder),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("enable_heterogeneous_equality")));
}

TEST(NetworkExtTest, StringConversionDisabled) {
  RuntimeOptions options;
  options.enable_qualified_type_identifiers = true;
  options.enable_string_conversion = false;

  ASSERT_OK_AND_ASSIGN(auto builder,
                       CreateStandardRuntimeBuilder(
                           internal::GetTestingDescriptorPool(), options));
  ASSERT_THAT(RegisterNetworkFunctions(builder), IsOk());
  ASSERT_OK_AND_ASSIGN(auto runtime, std::move(builder).Build());

  ASSERT_OK_AND_ASSIGN(cel::expr::ParsedExpr parsed,
                       Parse("string(ip('192.168.0.1'))"));
  EXPECT_THAT(ProtobufRuntimeAdapter::CreateProgram(*runtime, parsed),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(NetworkExtTest, LegacyRuntimeRegistryOverloads) {
  InterpreterOptions options;
  CelFunctionRegistry* null_registry = nullptr;
  EXPECT_THAT(RegisterNetworkFunctions(null_registry, options),
              StatusIs(absl::StatusCode::kInvalidArgument));

  CelFunctionRegistry registry;
  ASSERT_THAT(RegisterNetworkFunctions(&registry, options, /*version=*/0),
              IsOk());
  EXPECT_THAT(registry.FindStaticOverloads("isIP", false, {cel::Kind::kString}),
              SizeIs(1));
  EXPECT_THAT(
      registry.FindStaticOverloads("isCIDR", false, {cel::Kind::kString}),
      SizeIs(1));
  EXPECT_THAT(registry.FindStaticOverloads("ip.isCanonical", false,
                                           {cel::Kind::kString}),
              SizeIs(1));
  EXPECT_THAT(registry.FindStaticOverloads("ip", false, {cel::Kind::kString}),
              IsEmpty());
  EXPECT_THAT(registry.FindStaticOverloads("cidr", false, {cel::Kind::kString}),
              IsEmpty());
}

TEST(NetworkExtTest, LegacyRuntimeStringFunctions) {
  InterpreterOptions options;
  options.enable_qualified_type_identifiers = true;
  options.enable_qualified_identifier_rewrites = true;

  std::unique_ptr<CelExpressionBuilder> builder = CreateCelExpressionBuilder(
      internal::GetTestingDescriptorPool(), nullptr, options);
  ASSERT_THAT(RegisterNetworkFunctions(builder->GetRegistry(), options),
              IsOk());

  auto eval = [&](absl::string_view expr_str,
                  google::protobuf::Arena* arena) -> absl::StatusOr<CelValue> {
    CEL_ASSIGN_OR_RETURN(cel::expr::ParsedExpr parsed, Parse(expr_str));
    CEL_ASSIGN_OR_RETURN(
        auto program,
        builder->CreateExpression(&parsed.expr(), &parsed.source_info()));
    LegacyActivation activation;
    return program->Evaluate(activation, arena);
  };

  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(CelValue r1, eval("isIP('192.168.0.1')", &arena));
  ASSERT_TRUE(r1.IsBool());
  EXPECT_TRUE(r1.BoolOrDie());

  ASSERT_OK_AND_ASSIGN(CelValue r2, eval("isIP('01.2.3.4')", &arena));
  ASSERT_TRUE(r2.IsBool());
  EXPECT_FALSE(r2.BoolOrDie());

  ASSERT_OK_AND_ASSIGN(CelValue r3, eval("isCIDR('2001:db8::/32')", &arena));
  ASSERT_TRUE(r3.IsBool());
  EXPECT_TRUE(r3.BoolOrDie());

  ASSERT_OK_AND_ASSIGN(CelValue r4, eval("isCIDR('192.168.0.0/08')", &arena));
  ASSERT_TRUE(r4.IsBool());
  EXPECT_FALSE(r4.BoolOrDie());

  ASSERT_OK_AND_ASSIGN(CelValue r5,
                       eval("ip.isCanonical('2001:db8::1')", &arena));
  ASSERT_TRUE(r5.IsBool());
  EXPECT_TRUE(r5.BoolOrDie());

  ASSERT_OK_AND_ASSIGN(CelValue r6,
                       eval("ip.isCanonical('2001:DB8::1')", &arena));
  ASSERT_TRUE(r6.IsBool());
  EXPECT_FALSE(r6.BoolOrDie());

  ASSERT_OK_AND_ASSIGN(CelValue r7, eval("ip.isCanonical('invalid')", &arena));
  EXPECT_TRUE(r7.IsError());
}

TEST(NetworkExtTest, OpaqueValueInterfaceCloningAndConstantFolding) {
  ASSERT_OK_AND_ASSIGN(auto compiler, CreateTestCompiler());
  ASSERT_OK_AND_ASSIGN(auto runtime,
                       CreateTestRuntime(kNetworkExtensionLatestVersion,
                                         /*enable_constant_folding=*/true));

  auto compile_program = [&](absl::string_view expr_str)
      -> absl::StatusOr<std::unique_ptr<Program>> {
    CEL_ASSIGN_OR_RETURN(ValidationResult validation_result,
                         compiler->Compile(expr_str));
    if (!validation_result.IsValid()) {
      return absl::InvalidArgumentError(validation_result.FormatError());
    }
    CEL_ASSIGN_OR_RETURN(std::unique_ptr<Ast> ast,
                         validation_result.ReleaseAst());
    return runtime->CreateProgram(std::move(ast));
  };

  google::protobuf::Arena arena1;
  google::protobuf::Arena arena2;
  Activation activation;

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Program> ip_program,
                       compile_program("ip('2001:0DB8::1')"));
  ASSERT_OK_AND_ASSIGN(Value ip_val, ip_program->Evaluate(&arena1, activation));
  ASSERT_TRUE(ip_val.IsOpaque());
  EXPECT_EQ(ip_val.DebugString(), "2001:db8::1");
  EXPECT_EQ(ip_val.GetTypeName(), "net.IP");
  EXPECT_EQ(ip_val.GetOpaque().GetRuntimeType(), NetIpType());

  OpaqueValue cloned_ip = ip_val.GetOpaque().Clone(&arena2);
  EXPECT_EQ(cloned_ip.DebugString(), "2001:db8::1");
  EXPECT_EQ(cloned_ip.GetTypeName(), "net.IP");
  EXPECT_EQ(cloned_ip.GetRuntimeType(), NetIpType());

  Value eq_result;
  ASSERT_THAT(ip_val.Equal(cloned_ip, internal::GetTestingDescriptorPool(),
                           google::protobuf::MessageFactory::generated_factory(), &arena2,
                           &eq_result),
              IsOk());
  EXPECT_THAT(eq_result, BoolValueIs(true));

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Program> cidr_program,
                       compile_program("cidr('192.168.1.5/24')"));
  ASSERT_OK_AND_ASSIGN(Value cidr_val,
                       cidr_program->Evaluate(&arena1, activation));
  ASSERT_TRUE(cidr_val.IsOpaque());
  EXPECT_EQ(cidr_val.DebugString(), "192.168.1.5/24");
  EXPECT_EQ(cidr_val.GetTypeName(), "net.CIDR");
  EXPECT_EQ(cidr_val.GetOpaque().GetRuntimeType(), NetCidrType());

  OpaqueValue cloned_cidr = cidr_val.GetOpaque().Clone(&arena2);
  EXPECT_EQ(cloned_cidr.DebugString(), "192.168.1.5/24");
  EXPECT_EQ(cloned_cidr.GetTypeName(), "net.CIDR");
  EXPECT_EQ(cloned_cidr.GetRuntimeType(), NetCidrType());

  ASSERT_THAT(cidr_val.Equal(cloned_cidr, internal::GetTestingDescriptorPool(),
                             google::protobuf::MessageFactory::generated_factory(),
                             &arena2, &eq_result),
              IsOk());
  EXPECT_THAT(eq_result, BoolValueIs(true));

  ASSERT_THAT(ip_val.Equal(cloned_cidr, internal::GetTestingDescriptorPool(),
                           google::protobuf::MessageFactory::generated_factory(), &arena2,
                           &eq_result),
              IsOk());
  EXPECT_THAT(eq_result, BoolValueIs(false));
}

TEST(NetworkExtTest, FragmentedCordStringValues) {
  ASSERT_OK_AND_ASSIGN(
      auto builder, NewCompilerBuilder(internal::GetTestingDescriptorPool()));
  ASSERT_THAT(builder->AddLibrary(StandardCheckerLibrary()), IsOk());
  ASSERT_THAT(builder->AddLibrary(NetworkCompilerLibrary()), IsOk());
  ASSERT_THAT(builder->GetCheckerBuilder().AddVariable(
                  MakeVariableDecl("ip_var", StringType())),
              IsOk());
  ASSERT_THAT(builder->GetCheckerBuilder().AddVariable(
                  MakeVariableDecl("cidr_var", StringType())),
              IsOk());
  ASSERT_THAT(builder->GetCheckerBuilder().AddVariable(
                  MakeVariableDecl("sub_cidr_var", StringType())),
              IsOk());
  ASSERT_OK_AND_ASSIGN(auto compiler, std::move(*builder).Build());
  ASSERT_OK_AND_ASSIGN(auto runtime, CreateTestRuntime());

  const std::string expr =
      "isIP(ip_var) && isCIDR(cidr_var) && ip.isCanonical(ip_var) && "
      "cidr(cidr_var).containsIP(ip(ip_var)) && "
      "cidr(cidr_var).containsIP(ip_var) && "
      "cidr(cidr_var).containsCIDR(sub_cidr_var)";

  ASSERT_OK_AND_ASSIGN(ValidationResult validation_result,
                       compiler->Compile(expr));
  ASSERT_TRUE(validation_result.IsValid()) << validation_result.FormatError();
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Ast> ast,
                       validation_result.ReleaseAst());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Program> program,
                       runtime->CreateProgram(std::move(ast)));

  google::protobuf::Arena arena;
  Activation activation;
  activation.InsertOrAssignValue(
      "ip_var",
      StringValue::From(absl::MakeFragmentedCord({"2001:", "db8", "::", "1"}),
                        &arena));
  activation.InsertOrAssignValue(
      "cidr_var",
      StringValue::From(absl::MakeFragmentedCord({"2001:db", "8::/", "32"}),
                        &arena));
  activation.InsertOrAssignValue(
      "sub_cidr_var",
      StringValue::From(absl::MakeFragmentedCord({"2001:", "db8::", "/64"}),
                        &arena));

  EXPECT_THAT(program->Evaluate(&arena, activation),
              IsOkAndHolds(BoolValueIs(true)));
}

TEST(NetworkExtTest, CidrInvalidIpErrorIsNotDoubleWrapped) {
  ASSERT_OK_AND_ASSIGN(auto runtime, CreateTestRuntime());
  ASSERT_OK_AND_ASSIGN(cel::expr::ParsedExpr parsed,
                       Parse("cidr('invalid/24')"));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Program> program,
                       ProtobufRuntimeAdapter::CreateProgram(*runtime, parsed));
  google::protobuf::Arena arena;
  Activation activation;
  ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));
  ASSERT_TRUE(result.IsError());
  EXPECT_THAT(
      result.GetError().NativeValue(),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          AllOf(HasSubstr("CIDR 'invalid/24' parse error during conversion "
                          "from string: unable to parse IP"),
                Not(HasSubstr("CIDR 'invalid' parse error")))));
}

enum class ExpectedKind {
  kBoolTrue,
  kBoolFalse,
  kInt,
  kString,
  kInvalidArgError,
  kNoMatchingOverloadError,
};

struct NetworkEvalCase {
  std::string expr;
  ExpectedKind expected_kind;
  std::string expected_str = "";
  int64_t expected_int = 0;
  bool test_checked = true;
  bool test_parse_only = true;
};

class NetworkExtEvalTest : public TestWithParam<NetworkEvalCase> {
 public:
  void SetUp() override {
    ASSERT_OK_AND_ASSIGN(compiler_, CreateTestCompiler());
    ASSERT_OK_AND_ASSIGN(runtime_, CreateTestRuntime());
  }

  absl::StatusOr<Value> EvalChecked(absl::string_view expr_str) {
    CEL_ASSIGN_OR_RETURN(ValidationResult validation_result,
                         compiler_->Compile(expr_str));
    if (!validation_result.IsValid()) {
      return absl::InvalidArgumentError(validation_result.FormatError());
    }
    CEL_ASSIGN_OR_RETURN(std::unique_ptr<cel::Ast> ast,
                         validation_result.ReleaseAst());
    CEL_ASSIGN_OR_RETURN(std::unique_ptr<cel::Program> program,
                         runtime_->CreateProgram(std::move(ast)));
    Activation activation;
    return program->Evaluate(&arena_, activation);
  }

  absl::StatusOr<Value> EvalParseOnly(absl::string_view expr_str) {
    CEL_ASSIGN_OR_RETURN(cel::expr::ParsedExpr parsed_expr,
                         Parse(expr_str));
    CEL_ASSIGN_OR_RETURN(
        std::unique_ptr<cel::Program> program,
        ProtobufRuntimeAdapter::CreateProgram(*runtime_, parsed_expr));
    Activation activation;
    return program->Evaluate(&arena_, activation);
  }

  void VerifyResult(const absl::StatusOr<Value>& result,
                    const NetworkEvalCase& test_case) {
    switch (test_case.expected_kind) {
      case ExpectedKind::kBoolTrue:
        EXPECT_THAT(result, IsOkAndHolds(BoolValueIs(true)))
            << "Expression: " << test_case.expr;
        break;
      case ExpectedKind::kBoolFalse:
        EXPECT_THAT(result, IsOkAndHolds(BoolValueIs(false)))
            << "Expression: " << test_case.expr;
        break;
      case ExpectedKind::kInt:
        EXPECT_THAT(result, IsOkAndHolds(IntValueIs(test_case.expected_int)))
            << "Expression: " << test_case.expr;
        break;
      case ExpectedKind::kString:
        EXPECT_THAT(result, IsOkAndHolds(StringValueIs(test_case.expected_str)))
            << "Expression: " << test_case.expr;
        break;
      case ExpectedKind::kInvalidArgError:
        EXPECT_THAT(result, IsOkAndHolds(ErrorValueIs(
                                StatusIs(absl::StatusCode::kInvalidArgument,
                                         HasSubstr(test_case.expected_str)))))
            << "Expression: " << test_case.expr;
        break;
      case ExpectedKind::kNoMatchingOverloadError:
        EXPECT_THAT(result, IsOkAndHolds(ErrorValueIs(
                                StatusIs(absl::StatusCode::kUnknown,
                                         HasSubstr("No matching overloads")))))
            << "Expression: " << test_case.expr;
        break;
    }
  }

  google::protobuf::Arena arena_;
  std::unique_ptr<Compiler> compiler_;
  std::unique_ptr<const Runtime> runtime_;
};

TEST_P(NetworkExtEvalTest, CheckedAndParseOnly) {
  const NetworkEvalCase& test_case = GetParam();
  if (test_case.test_checked) {
    VerifyResult(EvalChecked(test_case.expr), test_case);
  }
  if (test_case.test_parse_only) {
    VerifyResult(EvalParseOnly(test_case.expr), test_case);
  }
}

std::vector<NetworkEvalCase> NetworkEvalCases() {
  return {
      // IPv4 parsing, string formatting, type, and predicates.
      {"string(ip('192.168.0.1'))", ExpectedKind::kString, "192.168.0.1"},
      {"string(ip('0.0.0.0'))", ExpectedKind::kString, "0.0.0.0"},
      {"type(ip('192.168.0.1')) == net.IP", ExpectedKind::kBoolTrue},
      {"ip('192.168.0.1').family()", ExpectedKind::kInt, "", 4},
      {"ip('0.0.0.0').isUnspecified()", ExpectedKind::kBoolTrue},
      {"ip('127.0.0.1').isUnspecified()", ExpectedKind::kBoolFalse},
      {"ip('127.0.0.1').isLoopback()", ExpectedKind::kBoolTrue},
      {"ip('1.2.3.4').isLoopback()", ExpectedKind::kBoolFalse},
      {"ip('169.254.169.254').isLinkLocalUnicast()", ExpectedKind::kBoolTrue},
      {"ip('192.168.0.1').isLinkLocalUnicast()", ExpectedKind::kBoolFalse},
      {"ip('224.0.0.1').isLinkLocalMulticast()", ExpectedKind::kBoolTrue},
      {"ip('224.0.1.1').isLinkLocalMulticast()", ExpectedKind::kBoolFalse},
      {"ip('8.8.8.8').isGlobalUnicast()", ExpectedKind::kBoolTrue},
      {"ip('255.255.255.255').isGlobalUnicast()", ExpectedKind::kBoolFalse},

      // IPv6 parsing, RFC 5952 formatting, and predicates.
      {"string(ip('2001:0DB8:0000:0000:0008:0800:200C:417A'))",
       ExpectedKind::kString, "2001:db8::8:800:200c:417a"},
      {"ip('2001:db8::68').family()", ExpectedKind::kInt, "", 6},
      {"ip('::').isUnspecified()", ExpectedKind::kBoolTrue},
      {"ip('::1').isLoopback()", ExpectedKind::kBoolTrue},
      {"ip('fe80::1').isLinkLocalUnicast()", ExpectedKind::kBoolTrue},
      {"ip('ff02::1').isLinkLocalMulticast()", ExpectedKind::kBoolTrue},
      {"ip('2001:db8::abcd').isGlobalUnicast()", ExpectedKind::kBoolTrue},

      // isIP / isCIDR / ip.isCanonical.
      {"isIP('192.168.0.1')", ExpectedKind::kBoolTrue},
      {"isIP('01.2.3.4')", ExpectedKind::kBoolFalse},
      {"isIP('::ffff:192.168.0.1')", ExpectedKind::kBoolFalse},
      {"isCIDR('2001:db8::/32')", ExpectedKind::kBoolTrue},
      {"isCIDR('192.168.0.0/08')", ExpectedKind::kBoolFalse},
      {"ip.isCanonical('2001:db8::68')", ExpectedKind::kBoolTrue},
      {"ip.isCanonical('2001:DB8::68')", ExpectedKind::kBoolFalse},

      // CIDR operations.
      {"type(cidr('192.168.0.0/24')) == net.CIDR", ExpectedKind::kBoolTrue},
      {"string(cidr('192.168.0.1/24'))", ExpectedKind::kString,
       "192.168.0.1/24"},
      {"cidr('192.168.0.1/24').ip() == ip('192.168.0.1')",
       ExpectedKind::kBoolTrue},
      {"cidr('192.168.0.1/24').masked() == cidr('192.168.0.0/24')",
       ExpectedKind::kBoolTrue},
      {"cidr('192.168.0.1/24').isMask()", ExpectedKind::kBoolFalse},
      {"cidr('192.168.0.0/24').isMask()", ExpectedKind::kBoolTrue},
      {"cidr('2001:db8::1:2/112').masked() == cidr('2001:db8::1:0/112')",
       ExpectedKind::kBoolTrue},
      {"cidr('192.168.0.0/24').prefixLength()", ExpectedKind::kInt, "", 24},
      {"cidr('192.168.0.1/24').containsIP(ip('192.168.0.0'))",
       ExpectedKind::kBoolTrue},
      {"cidr('192.168.0.0/24').containsIP('192.168.1.0')",
       ExpectedKind::kBoolFalse},
      {"cidr('192.168.1.5/24').containsCIDR(cidr('192.168.1.130/25'))",
       ExpectedKind::kBoolTrue},
      {"cidr('192.168.0.0/24').containsCIDR('192.168.0.0/23')",
       ExpectedKind::kBoolFalse},

      // Equality, inequality, & heterogeneous equality.
      {"ip('2001:db8::1') == ip('2001:DB8::1')", ExpectedKind::kBoolTrue},
      {"ip('2001:db8::1') != ip('2001:db8::2')", ExpectedKind::kBoolTrue},
      {"cidr('192.168.0.1/24') == cidr('192.168.0.0/24')",
       ExpectedKind::kBoolFalse},
      {"dyn(ip('192.168.0.1')) == cidr('192.168.0.1/32')",
       ExpectedKind::kBoolFalse},
      {"dyn(ip('192.168.0.1')) == optional.of(1)", ExpectedKind::kBoolFalse},
      {"ip('192.168.0.1') == cidr('192.168.0.1/32')", ExpectedKind::kBoolFalse,
       "", 0, /*test_checked=*/false, /*test_parse_only=*/true},
      {"cidr('192.168.0.0/24') == optional.none()", ExpectedKind::kBoolFalse,
       "", 0, /*test_checked=*/false, /*test_parse_only=*/true},

      // Runtime parse error propagation across all string-parsing functions.
      {"ip('invalid')", ExpectedKind::kInvalidArgError, "parse error"},
      {"cidr('invalid/24')", ExpectedKind::kInvalidArgError, "parse error"},
      {"ip.isCanonical('127.0.0.1.0')", ExpectedKind::kInvalidArgError,
       "parse error"},
      {"cidr('192.168.0.0/24').containsIP('not-an-ip')",
       ExpectedKind::kInvalidArgError, "parse error"},
      {"cidr('192.168.0.0/24').containsCIDR('not-a-cidr')",
       ExpectedKind::kInvalidArgError, "parse error"},

      // Parse-only runtime overload safety for wrong opaque types.
      {"ip('192.168.0.1').prefixLength()",
       ExpectedKind::kNoMatchingOverloadError, "", 0, /*test_checked=*/false,
       /*test_parse_only=*/true},
      {"ip('192.168.0.1').masked()", ExpectedKind::kNoMatchingOverloadError, "",
       0, /*test_checked=*/false, /*test_parse_only=*/true},
      {"ip('192.168.0.1').isMask()", ExpectedKind::kNoMatchingOverloadError, "",
       0, /*test_checked=*/false, /*test_parse_only=*/true},
      {"ip('192.168.0.1').ip()", ExpectedKind::kNoMatchingOverloadError, "", 0,
       /*test_checked=*/false, /*test_parse_only=*/true},
      {"cidr('192.168.0.0/24').family()",
       ExpectedKind::kNoMatchingOverloadError, "", 0, /*test_checked=*/false,
       /*test_parse_only=*/true},
      {"cidr('0.0.0.0/0').isUnspecified()",
       ExpectedKind::kNoMatchingOverloadError, "", 0, /*test_checked=*/false,
       /*test_parse_only=*/true},
      {"cidr('192.168.0.0/24').isLoopback()",
       ExpectedKind::kNoMatchingOverloadError, "", 0, /*test_checked=*/false,
       /*test_parse_only=*/true},
      {"cidr('192.168.0.0/24').isGlobalUnicast()",
       ExpectedKind::kNoMatchingOverloadError, "", 0, /*test_checked=*/false,
       /*test_parse_only=*/true},
      {"cidr('224.0.0.0/24').isLinkLocalMulticast()",
       ExpectedKind::kNoMatchingOverloadError, "", 0, /*test_checked=*/false,
       /*test_parse_only=*/true},
      {"cidr('169.254.0.0/16').isLinkLocalUnicast()",
       ExpectedKind::kNoMatchingOverloadError, "", 0, /*test_checked=*/false,
       /*test_parse_only=*/true},
      {"cidr('192.168.0.0/24').containsIP(cidr('192.168.0.1/32'))",
       ExpectedKind::kNoMatchingOverloadError, "", 0, /*test_checked=*/false,
       /*test_parse_only=*/true},
      {"ip('192.168.0.1').containsIP('192.168.0.1')",
       ExpectedKind::kNoMatchingOverloadError, "", 0, /*test_checked=*/false,
       /*test_parse_only=*/true},
      {"cidr('192.168.0.0/24').containsCIDR(ip('192.168.0.1'))",
       ExpectedKind::kNoMatchingOverloadError, "", 0, /*test_checked=*/false,
       /*test_parse_only=*/true},
      {"ip('192.168.0.1').containsCIDR('192.168.0.0/24')",
       ExpectedKind::kNoMatchingOverloadError, "", 0, /*test_checked=*/false,
       /*test_parse_only=*/true},
      {"string(optional.none())", ExpectedKind::kNoMatchingOverloadError, "", 0,
       /*test_checked=*/false, /*test_parse_only=*/true},
      {"optional.none().family()", ExpectedKind::kNoMatchingOverloadError, "",
       0, /*test_checked=*/false, /*test_parse_only=*/true},
      {"optional.none().containsIP('192.168.0.1')",
       ExpectedKind::kNoMatchingOverloadError, "", 0, /*test_checked=*/false,
       /*test_parse_only=*/true},
  };
}

INSTANTIATE_TEST_SUITE_P(NetworkExtEvalTest, NetworkExtEvalTest,
                         ValuesIn(NetworkEvalCases()));

}  // namespace
}  // namespace cel::extensions
