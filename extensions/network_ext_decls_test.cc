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

#include "extensions/network_ext_decls.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "checker/standard_library.h"
#include "checker/validation_result.h"
#include "common/ast.h"
#include "common/decl.h"
#include "common/source.h"
#include "common/type.h"
#include "compiler/compiler.h"
#include "compiler/compiler_factory.h"
#include "internal/status_macros.h"
#include "internal/testing.h"
#include "internal/testing_descriptor_pool.h"
#include "validator/validator.h"

namespace cel::extensions {
namespace {

using ::absl_testing::IsOk;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::TestWithParam;
using ::testing::ValuesIn;

absl::StatusOr<std::unique_ptr<Compiler>> CreateTestCompiler(
    int version = kNetworkExtensionLatestVersion) {
  CEL_ASSIGN_OR_RETURN(
      auto builder, NewCompilerBuilder(internal::GetTestingDescriptorPool()));
  CEL_RETURN_IF_ERROR(builder->AddLibrary(StandardCheckerLibrary()));
  CEL_RETURN_IF_ERROR(builder->AddLibrary(NetworkCompilerLibrary(version)));
  return std::move(*builder).Build();
}

TEST(NetworkExtDeclsTest, TypeNamesAndIds) {
  EXPECT_EQ(kNetworkExtensionLatestVersion, 1);
  EXPECT_EQ(NetIpType().name(), "net.IP");
  EXPECT_EQ(NetCidrType().name(), "net.CIDR");
  EXPECT_EQ(NetworkCheckerLibrary().id, "cel.lib.ext.network");
  EXPECT_EQ(NetworkCheckerLibrary(0).id, "cel.lib.ext.network");
  EXPECT_EQ(NetworkCompilerLibrary().id, "cel.lib.ext.network");
  EXPECT_EQ(NetworkCompilerLibrary(0).id, "cel.lib.ext.network");
  EXPECT_EQ(NetworkValidator().id(), "cel.validator.network");
}

struct NetworkCheckerCase {
  std::string expr;
  std::string error_substr = "";
};

class NetworkExtDeclsCheckerTest : public TestWithParam<NetworkCheckerCase> {};

TEST_P(NetworkExtDeclsCheckerTest, CompileVersionsZeroAndLatest) {
  for (int version : {0, kNetworkExtensionLatestVersion}) {
    ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> compiler,
                         CreateTestCompiler(version));
    ASSERT_OK_AND_ASSIGN(ValidationResult result,
                         compiler->Compile(GetParam().expr));
    absl::string_view error_substr = GetParam().error_substr;
    EXPECT_EQ(result.IsValid(), error_substr.empty())
        << "version: " << version << " expr: " << GetParam().expr
        << " issues: " << result.FormatError();
    if (!error_substr.empty()) {
      EXPECT_THAT(result.FormatError(), HasSubstr(error_substr));
    }
  }
}

INSTANTIATE_TEST_SUITE_P(
    NetworkExtDeclsCheckerTest, NetworkExtDeclsCheckerTest,
    ValuesIn(std::vector<NetworkCheckerCase>{
        // All 17 functions / 21 overloads + net.IP / net.CIDR type variables.
        {"type(ip('192.168.0.1')) == net.IP"},
        {"type(cidr('192.168.0.0/24')) == net.CIDR"},
        {"isIP('192.168.0.1')"},
        {"isCIDR('192.168.0.0/24')"},
        {"ip.isCanonical('2001:db8::1')"},
        {"string(ip('192.168.0.1')) == '192.168.0.1'"},
        {"string(cidr('192.168.0.0/24')) == '192.168.0.0/24'"},
        {"string(42) == '42'"},
        {"ip('192.168.0.1').family() == 4"},
        {"ip('0.0.0.0').isUnspecified()"},
        {"ip('127.0.0.1').isLoopback()"},
        {"ip('8.8.8.8').isGlobalUnicast()"},
        {"ip('224.0.0.1').isLinkLocalMulticast()"},
        {"ip('169.254.1.1').isLinkLocalUnicast()"},
        {"cidr('192.168.0.1/24').ip() == ip('192.168.0.1')"},
        {"cidr('192.168.0.1/24').masked() == cidr('192.168.0.0/24')"},
        {"cidr('192.168.0.0/24').isMask()"},
        {"cidr('192.168.0.0/24').prefixLength() == 24"},
        {"cidr('192.168.0.0/24').containsIP(ip('192.168.0.1'))"},
        {"cidr('192.168.0.0/24').containsIP('192.168.0.1')"},
        {"cidr('192.168.0.0/24').containsCIDR(cidr('192.168.0.0/25'))"},
        {"cidr('192.168.0.0/24').containsCIDR('192.168.0.0/25')"},

        // Static type-mismatch errors.
        {"isIP(cidr('192.168.0.0/24'))",
         "found no matching overload for 'isIP' applied to '(net.CIDR)'"},
        {"ip('192.168.0.1') == cidr('192.168.0.1/32')",
         "found no matching overload for '_==_' applied to '(net.IP, "
         "net.CIDR)'"},
        {"ip('192.168.0.1').prefixLength()",
         "found no matching overload for 'prefixLength' applied to '(net.IP)'"},
        {"cidr('192.168.0.0/24').family()",
         "found no matching overload for 'family' applied to '(net.CIDR)'"},
        {"string(ip('192.168.0.1')) == 1",
         "found no matching overload for '_==_' applied to '(string, int)'"},
    }));

class NetworkExtDeclsValidatorTest : public TestWithParam<NetworkCheckerCase> {
};

TEST_P(NetworkExtDeclsValidatorTest, ValidatesLiterals) {
  ASSERT_OK_AND_ASSIGN(
      auto builder, NewCompilerBuilder(internal::GetTestingDescriptorPool()));
  ASSERT_THAT(builder->AddLibrary(StandardCheckerLibrary()), IsOk());
  ASSERT_THAT(builder->AddLibrary(NetworkCompilerLibrary()), IsOk());
  ASSERT_THAT(builder->GetCheckerBuilder().AddVariable(
                  MakeVariableDecl("addr", StringType())),
              IsOk());
  builder->GetValidator().AddValidation(NetworkValidator());
  ASSERT_OK_AND_ASSIGN(auto compiler, std::move(*builder).Build());

  ASSERT_OK_AND_ASSIGN(ValidationResult result,
                       compiler->Compile(GetParam().expr));
  EXPECT_EQ(result.IsValid(), GetParam().error_substr.empty())
      << "Expression: " << GetParam().expr
      << " issues: " << result.FormatError();
  if (!GetParam().error_substr.empty()) {
    EXPECT_THAT(result.FormatError(), HasSubstr(GetParam().error_substr));
  }
}

INSTANTIATE_TEST_SUITE_P(
    NetworkExtDeclsValidatorTest, NetworkExtDeclsValidatorTest,
    ValuesIn(std::vector<NetworkCheckerCase>{
        {"ip('192.168.0.1')"},
        {"ip(addr)"},
        {"cidr('192.168.0.1/24').ip()"},
        {"cidr('2001:db8::/32')"},
        {"ip('192.168.0.1.0')", "invalid ip argument"},
        {"ip('fe80::1%en0')", "invalid ip argument"},
        {"ip('::ffff:192.168.0.1')", "invalid ip argument"},
        {"cidr('192.168.0.0/')", "invalid cidr argument"},
        {"cidr('10.0.0.0/08')", "invalid cidr argument"},
        {"cidr('::ffff:192.168.0.1/24')", "invalid cidr argument"},
    }));

TEST(NetworkExtDeclsValidatorRawAstTest, IgnoresNonStringAndMemberCalls) {
  ASSERT_OK_AND_ASSIGN(
      auto builder, NewCompilerBuilder(internal::GetTestingDescriptorPool()));
  ASSERT_OK_AND_ASSIGN(auto compiler, std::move(*builder).Build());
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<Source> source,
      NewSource("ip(123) == x.ip('invalid') && cidr('a', 'b')"));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Ast> ast,
                       compiler->GetParser().Parse(*source));

  Validator validator;
  validator.AddValidation(NetworkValidator());
  Validator::ValidationOutput output = validator.Validate(*ast);
  EXPECT_TRUE(output.valid);
  EXPECT_THAT(output.issues, IsEmpty());
}

}  // namespace
}  // namespace cel::extensions
