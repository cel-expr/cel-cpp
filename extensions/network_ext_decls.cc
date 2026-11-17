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

#include <string>
#include <utility>

#include "absl/base/no_destructor.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "checker/internal/builtins_arena.h"
#include "checker/type_checker_builder.h"
#include "common/constant.h"
#include "common/decl.h"
#include "common/expr.h"
#include "common/navigable_ast.h"
#include "common/standard_definitions.h"
#include "common/type.h"
#include "compiler/compiler.h"
#include "internal/network_addr.h"
#include "internal/status_macros.h"
#include "validator/validator.h"

namespace cel::extensions {
namespace {

using ::cel::checker_internal::BuiltinsArena;

constexpr absl::string_view kNetworkExtensionName = "cel.lib.ext.network";
constexpr absl::string_view kNetworkValidatorId = "cel.validator.network";
constexpr absl::string_view kNetIpTypeName = "net.IP";
constexpr absl::string_view kNetCidrTypeName = "net.CIDR";

template <typename... Overloads>
absl::Status AddFunctionDecl(TypeCheckerBuilder& builder,
                             absl::string_view name, Overloads&&... overloads) {
  CEL_ASSIGN_OR_RETURN(FunctionDecl decl,
                       MakeFunctionDecl(std::string(name),
                                        std::forward<Overloads>(overloads)...));
  return builder.AddFunction(std::move(decl));
}

absl::Status RegisterNetworkCheckerDecls(TypeCheckerBuilder& builder) {
  CEL_RETURN_IF_ERROR(builder.AddVariable(MakeVariableDecl(
      kNetIpTypeName, TypeType(builder.arena(), NetIpType()))));
  CEL_RETURN_IF_ERROR(builder.AddVariable(MakeVariableDecl(
      kNetCidrTypeName, TypeType(builder.arena(), NetCidrType()))));

  CEL_ASSIGN_OR_RETURN(
      FunctionDecl string_decl,
      MakeFunctionDecl(
          std::string(StandardFunctions::kString),
          MakeOverloadDecl("ip_to_string", StringType(), NetIpType()),
          MakeOverloadDecl("cidr_to_string", StringType(), NetCidrType())));
  CEL_RETURN_IF_ERROR(builder.MergeFunction(std::move(string_decl)));

  CEL_RETURN_IF_ERROR(AddFunctionDecl(
      builder, "cidr",
      MakeOverloadDecl("string_to_cidr", NetCidrType(), StringType())));
  CEL_RETURN_IF_ERROR(AddFunctionDecl(
      builder, "containsCIDR",
      MakeMemberOverloadDecl("cidr_contains_cidr", BoolType(), NetCidrType(),
                             NetCidrType()),
      MakeMemberOverloadDecl("cidr_contains_cidr_string", BoolType(),
                             NetCidrType(), StringType())));
  CEL_RETURN_IF_ERROR(AddFunctionDecl(
      builder, "containsIP",
      MakeMemberOverloadDecl("cidr_contains_ip_ip", BoolType(), NetCidrType(),
                             NetIpType()),
      MakeMemberOverloadDecl("cidr_contains_ip_string", BoolType(),
                             NetCidrType(), StringType())));
  CEL_RETURN_IF_ERROR(AddFunctionDecl(
      builder, "family",
      MakeMemberOverloadDecl("ip_family", IntType(), NetIpType())));
  CEL_RETURN_IF_ERROR(AddFunctionDecl(
      builder, "ip",
      MakeOverloadDecl("string_to_ip", NetIpType(), StringType()),
      MakeMemberOverloadDecl("cidr_ip", NetIpType(), NetCidrType())));
  CEL_RETURN_IF_ERROR(AddFunctionDecl(
      builder, "ip.isCanonical",
      MakeOverloadDecl("ip_is_canonical", BoolType(), StringType())));
  CEL_RETURN_IF_ERROR(
      AddFunctionDecl(builder, "isCIDR",
                      MakeOverloadDecl("is_cidr", BoolType(), StringType())));
  CEL_RETURN_IF_ERROR(AddFunctionDecl(
      builder, "isGlobalUnicast",
      MakeMemberOverloadDecl("ip_is_global_unicast", BoolType(), NetIpType())));
  CEL_RETURN_IF_ERROR(AddFunctionDecl(
      builder, "isIP", MakeOverloadDecl("is_ip", BoolType(), StringType())));
  CEL_RETURN_IF_ERROR(
      AddFunctionDecl(builder, "isLinkLocalMulticast",
                      MakeMemberOverloadDecl("ip_is_link_local_multicast",
                                             BoolType(), NetIpType())));
  CEL_RETURN_IF_ERROR(
      AddFunctionDecl(builder, "isLinkLocalUnicast",
                      MakeMemberOverloadDecl("ip_is_link_local_unicast",
                                             BoolType(), NetIpType())));
  CEL_RETURN_IF_ERROR(AddFunctionDecl(
      builder, "isLoopback",
      MakeMemberOverloadDecl("ip_is_loopback", BoolType(), NetIpType())));
  CEL_RETURN_IF_ERROR(AddFunctionDecl(
      builder, "isMask",
      MakeMemberOverloadDecl("cidr_is_mask", BoolType(), NetCidrType())));
  CEL_RETURN_IF_ERROR(AddFunctionDecl(
      builder, "isUnspecified",
      MakeMemberOverloadDecl("ip_is_unspecified", BoolType(), NetIpType())));
  CEL_RETURN_IF_ERROR(AddFunctionDecl(
      builder, "masked",
      MakeMemberOverloadDecl("cidr_masked", NetCidrType(), NetCidrType())));
  CEL_RETURN_IF_ERROR(AddFunctionDecl(
      builder, "prefixLength",
      MakeMemberOverloadDecl("cidr_prefix_length", IntType(), NetCidrType())));

  return absl::OkStatus();
}

bool ValidateNetworkLiterals(ValidationContext& context) {
  bool valid = true;
  for (const NavigableAstNode& node :
       context.navigable_ast().Root().DescendantsPostorder()) {
    if (node.node_kind() != NodeKind::kCall) {
      continue;
    }
    const CallExpr& call_expr = node.expr()->call_expr();
    if (call_expr.has_target() || call_expr.args().size() != 1) {
      continue;
    }
    const Expr& arg_expr = call_expr.args()[0];
    if (!arg_expr.has_const_expr() ||
        !arg_expr.const_expr().has_string_value()) {
      continue;
    }
    absl::string_view literal = arg_expr.const_expr().string_value();
    if (call_expr.function() == "ip") {
      if (absl::Status status = internal::ParseIp(literal).status();
          !status.ok()) {
        context.ReportErrorAt(
            arg_expr.id(),
            absl::StrCat("invalid ip argument: ", status.message()));
        valid = false;
      }
    } else if (call_expr.function() == "cidr") {
      if (absl::Status status = internal::ParseCidr(literal).status();
          !status.ok()) {
        context.ReportErrorAt(
            arg_expr.id(),
            absl::StrCat("invalid cidr argument: ", status.message()));
        valid = false;
      }
    }
  }
  return valid;
}

}  // namespace

OpaqueType NetIpType() {
  static const absl::NoDestructor<OpaqueType> kInstance(
      BuiltinsArena(), kNetIpTypeName, absl::Span<const Type>{});
  return *kInstance;
}

OpaqueType NetCidrType() {
  static const absl::NoDestructor<OpaqueType> kInstance(
      BuiltinsArena(), kNetCidrTypeName, absl::Span<const Type>{});
  return *kInstance;
}

CheckerLibrary NetworkCheckerLibrary(int version) {
  (void)version;
  return {.id = std::string(kNetworkExtensionName),
          .configure = RegisterNetworkCheckerDecls};
}

CompilerLibrary NetworkCompilerLibrary(int version) {
  return CompilerLibrary::FromCheckerLibrary(NetworkCheckerLibrary(version));
}

Validation NetworkValidator() {
  return Validation(&ValidateNetworkLiterals, kNetworkValidatorId);
}

}  // namespace cel::extensions
