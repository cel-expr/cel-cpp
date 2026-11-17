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

#include "env/env_runtime.h"

#include <memory>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "env/config.h"
#include "internal/status_macros.h"
#include "runtime/runtime.h"
#include "runtime/runtime_builder.h"
#include "runtime/runtime_builder_factory.h"
#include "runtime/runtime_options.h"
#include "runtime/standard_functions.h"

namespace cel {

void EnvRuntime::RegisterExtensionFunctions(
    absl::string_view name, absl::string_view alias, int version,
    absl::AnyInvocable<absl::Status(RuntimeBuilder&, const RuntimeOptions&)
                           const>
        function_registration_callback) {
  extension_registry_.AddFunctionRegistration(
      name, alias, version, std::move(function_registration_callback));
}

absl::StatusOr<RuntimeBuilder> EnvRuntime::CreateRuntimeBuilder() const {
  return CreateRuntimeBuilder(runtime_options_);
}

absl::StatusOr<RuntimeBuilder> EnvRuntime::CreateRuntimeBuilder(
    const RuntimeOptions& options) const {
  RuntimeOptions runtime_options = options;
  const std::vector<Config::ExtensionConfig>& extension_configs =
      config_.GetExtensionConfigs();
  const Config::ExtensionConfig* optional_extension_config = nullptr;
  for (const Config::ExtensionConfig& extension_config : extension_configs) {
    if (extension_config.name == "optional") {
      optional_extension_config = &extension_config;
      runtime_options.enable_qualified_type_identifiers = true;
    } else if (extension_config.name == "network" ||
               extension_config.name == "cel.lib.ext.network") {
      runtime_options.enable_qualified_type_identifiers = true;
    }
  }

  CEL_ASSIGN_OR_RETURN(
      RuntimeBuilder runtime_builder,
      cel::CreateRuntimeBuilder(descriptor_pool_, runtime_options));

  if (!config_.GetStandardLibraryConfig().disable) {
    CEL_RETURN_IF_ERROR(RegisterStandardFunctions(
        runtime_builder.function_registry(), runtime_options));
  }

  // Register optional extension functions first, because other extensions
  // depend on it (e.g. regex).
  if (optional_extension_config != nullptr) {
    CEL_RETURN_IF_ERROR(extension_registry_.RegisterExtensionFunctions(
        runtime_builder, runtime_options, optional_extension_config->name,
        optional_extension_config->version));
  }

  for (const Config::ExtensionConfig& extension_config : extension_configs) {
    if (&extension_config == optional_extension_config) {
      continue;
    }
    CEL_RETURN_IF_ERROR(extension_registry_.RegisterExtensionFunctions(
        runtime_builder, runtime_options, extension_config.name,
        extension_config.version));
  }
  return runtime_builder;
}

absl::StatusOr<std::unique_ptr<Runtime>> EnvRuntime::NewRuntime() const {
  return NewRuntime(runtime_options_);
}

absl::StatusOr<std::unique_ptr<Runtime>> EnvRuntime::NewRuntime(
    const RuntimeOptions& options) const {
  CEL_ASSIGN_OR_RETURN(RuntimeBuilder runtime_builder,
                       CreateRuntimeBuilder(options));
  return std::move(runtime_builder).Build();
}

}  // namespace cel
