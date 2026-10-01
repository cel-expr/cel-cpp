// Copyright 2026 Google LLC
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

#include "env/env.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "checker/type_checker_builder.h"
#include "common/constant.h"
#include "common/container.h"
#include "common/decl.h"
#include "common/signature.h"
#include "common/type.h"
#include "common/typedef/type_ref.h"
#include "compiler/compiler.h"
#include "compiler/compiler_factory.h"
#include "compiler/standard_library.h"
#include "env/config.h"
#include "internal/status_macros.h"
#include "parser/macro.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"

namespace cel {
namespace {

bool ShouldIncludeMacro(const Config::StandardLibraryConfig& config,
                        absl::string_view macro) {
  if (config.disable_macros) {
    return false;
  }
  if (config.excluded_macros.contains(macro)) {
    return false;
  }
  if (!config.included_macros.empty() &&
      !config.included_macros.contains(macro)) {
    return false;
  }
  return true;
}

bool ShouldIncludeFunction(const Config::StandardLibraryConfig& config,
                           absl::string_view function,
                           const OverloadDecl& overload) {
  if (config.excluded_functions.empty() && config.included_functions.empty()) {
    return true;
  }

  if (!config.excluded_functions.empty()) {
    if (config.excluded_functions.contains(std::make_pair(
            std::string(function), std::string(overload.id()))) ||
        config.excluded_functions.contains(
            std::make_pair(std::string(function), ""))) {
      return false;
    }
    absl::StatusOr<std::string> signature =
        MakeOverloadSignature(function, overload.args(), overload.member());
    if (signature.ok() && config.excluded_functions.contains(std::make_pair(
                              std::string(function), *std::move(signature)))) {
      return false;
    }
  }

  if (!config.included_functions.empty()) {
    if (config.included_functions.contains(std::make_pair(
            std::string(function), std::string(overload.id()))) ||
        config.included_functions.contains(
            std::make_pair(std::string(function), ""))) {
      return true;
    }
    // Ok to call MakeOverloadSignature() again, because in practice either
    // included or excluded functions may be specified, but not both.
    absl::StatusOr<std::string> signature =
        MakeOverloadSignature(function, overload.args(), overload.member());
    if (signature.ok() && config.included_functions.contains(std::make_pair(
                              std::string(function), *std::move(signature)))) {
      return true;
    }
    return false;
  }

  return true;  // Never reached
}

absl::StatusOr<CompilerLibrarySubset> MakeStdlibSubset(
    const Config::StandardLibraryConfig& standard_library_config) {
  CompilerLibrarySubset subset;
  subset.library_id = "stdlib";
  // Capturing by reference is safe. The returned CompilerLibrarySubset's
  // callbacks are only used during CompilerBuilder::Build() to configure
  // contributed functions and macros. They are not retained by the constructed
  // Compiler instance. The referenced config outlives the Build() call.
  subset.should_include_macro = [&standard_library_config](const Macro& macro) {
    return ShouldIncludeMacro(standard_library_config, macro.function());
  };
  subset.should_include_overload = [&standard_library_config](
                                       absl::string_view function,
                                       const OverloadDecl& overload) {
    return ShouldIncludeFunction(standard_library_config, function, overload);
  };
  return subset;
}

absl::StatusOr<FunctionDecl> FunctionConfigToFunctionDecl(
    const Config::FunctionConfig& function_config, google::protobuf::Arena* arena,
    const google::protobuf::DescriptorPool* descriptor_pool) {
  FunctionDecl function_decl;
  function_decl.set_name(function_config.name);
  for (const Config::FunctionOverloadConfig& overload_config :
       function_config.overload_configs) {
    OverloadDecl overload_decl;
    overload_decl.set_id(overload_config.overload_id);
    overload_decl.set_member(overload_config.is_member_function);
    for (const TypeRef& parameter : overload_config.parameters) {
      CEL_ASSIGN_OR_RETURN(Type parameter_type,
                           TypeRefToType(parameter, descriptor_pool, arena));
      overload_decl.mutable_args().push_back(parameter_type);
    }
    CEL_ASSIGN_OR_RETURN(
        Type return_type,
        TypeRefToType(overload_config.return_type, descriptor_pool, arena));
    overload_decl.set_result(return_type);
    CEL_RETURN_IF_ERROR(function_decl.AddOverload(overload_decl));
  }
  return function_decl;
}

}  // namespace

Env::Env() {
  compiler_options_.parser_options.enable_quoted_identifiers = true;
  compiler_options_.adapt_parser_errors = true;
}

absl::StatusOr<std::unique_ptr<CompilerBuilder>> Env::NewCompilerBuilder()
    const {
  CEL_ASSIGN_OR_RETURN(
      std::unique_ptr<CompilerBuilder> compiler_builder,
      cel::NewCompilerBuilder(descriptor_pool_, compiler_options_));
  cel::TypeCheckerBuilder& checker_builder =
      compiler_builder->GetCheckerBuilder();

  ExpressionContainer container;
  CEL_RETURN_IF_ERROR(
      container.SetContainer(config_.GetContainerConfig().name));
  for (const auto& abbr : config_.GetContainerConfig().abbreviations) {
    CEL_RETURN_IF_ERROR(container.AddAbbreviation(abbr));
  }

  if (!config_.GetContextType().empty()) {
    CEL_RETURN_IF_ERROR(
        checker_builder.AddContextDeclaration(config_.GetContextType()));
  }
  for (const auto& alias : config_.GetContainerConfig().aliases) {
    CEL_RETURN_IF_ERROR(container.AddAlias(alias.alias, alias.qualified_name));
  }
  checker_builder.SetExpressionContainer(std::move(container));

  if (!config_.GetStandardLibraryConfig().disable) {
    CEL_RETURN_IF_ERROR(
        compiler_builder->AddLibrary(StandardCompilerLibrary()));
    CEL_ASSIGN_OR_RETURN(CompilerLibrarySubset standard_library_subset,
                         MakeStdlibSubset(config_.GetStandardLibraryConfig()));
    CEL_RETURN_IF_ERROR(
        compiler_builder->AddLibrarySubset(std::move(standard_library_subset)));
  }
  for (const Config::ExtensionConfig& extension_config :
       config_.GetExtensionConfigs()) {
    CEL_ASSIGN_OR_RETURN(CompilerLibrary library,
                         extension_registry_.GetCompilerLibrary(
                             extension_config.name, extension_config.version));
    CEL_RETURN_IF_ERROR(compiler_builder->AddLibrary(std::move(library)));
  }

  google::protobuf::Arena* arena = checker_builder.arena();
  for (const Config::VariableConfig& variable_config :
       config_.GetVariableConfigs()) {
    VariableDecl variable_decl;
    variable_decl.set_name(variable_config.name);
    CEL_ASSIGN_OR_RETURN(Type type,
                         TypeRefToType(variable_config.type_info,
                                       descriptor_pool_.get(), arena));
    variable_decl.set_type(type);
    if (variable_config.value.has_value()) {
      variable_decl.set_value(variable_config.value);
    }
    CEL_RETURN_IF_ERROR(checker_builder.AddVariable(variable_decl));
  }

  for (const Config::FunctionConfig& function_config :
       config_.GetFunctionConfigs()) {
    CEL_ASSIGN_OR_RETURN(FunctionDecl function_decl,
                         FunctionConfigToFunctionDecl(function_config, arena,
                                                      descriptor_pool_.get()));
    CEL_RETURN_IF_ERROR(checker_builder.AddFunction(function_decl));
  }

  return compiler_builder;
}

absl::StatusOr<std::unique_ptr<Compiler>> Env::NewCompiler() const {
  CEL_ASSIGN_OR_RETURN(std::unique_ptr<CompilerBuilder> compiler_builder,
                       NewCompilerBuilder());
  return compiler_builder->Build();
}
}  // namespace cel
