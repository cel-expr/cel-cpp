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

#include "env/runtime_std_extensions.h"

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "checker/optional.h"
#include "env/env_runtime.h"
#include "env/internal/runtime_ext_registry.h"
#include "extensions/encoders_modern.h"
#include "extensions/lists_functions.h"
#include "extensions/math_ext_decls.h"
#include "extensions/math_ext_modern.h"
#include "extensions/regex_ext_modern.h"
#include "extensions/sets_functions_modern.h"
#include "extensions/strings_modern.h"
#include "runtime/optional_types.h"
#include "runtime/runtime_builder.h"
#include "runtime/runtime_options.h"

namespace cel {

void RegisterStandardExtensions(EnvRuntime& env_runtime) {
  env_internal::RuntimeExtensionRegistry& registry =
      env_runtime.GetRuntimeExtensionRegistry();
  registry.AddFunctionRegistration(
      "cel.lib.ext.bindings", "bindings", 0,
      [](RuntimeBuilder& runtime_builder,
         const RuntimeOptions& runtime_options) -> absl::Status {
        // No runtime functions to register.
        return absl::OkStatus();
      });

  registry.AddFunctionRegistration(
      "cel.lib.ext.encoders", "encoders", 0,
      [](RuntimeBuilder& runtime_builder,
         const RuntimeOptions& runtime_options) -> absl::Status {
        return cel::extensions::RegisterEncodersFunctions(
            runtime_builder.function_registry(), runtime_options);
      });

  for (int version = 0; version <= extensions::kListsExtensionLatestVersion;
       ++version) {
    registry.AddFunctionRegistration(
        "cel.lib.ext.lists", "lists", version,
        [version](RuntimeBuilder& runtime_builder,
                  const RuntimeOptions& runtime_options) -> absl::Status {
          return cel::extensions::RegisterListsFunctions(
              runtime_builder.function_registry(), runtime_options, version);
        });
  }

  for (int version = 0; version <= extensions::kMathExtensionLatestVersion;
       ++version) {
    registry.AddFunctionRegistration(
        "cel.lib.ext.math", "math", version,
        [version](RuntimeBuilder& runtime_builder,
                  const RuntimeOptions& runtime_options) -> absl::Status {
          return cel::extensions::RegisterMathExtensionFunctions(
              runtime_builder.function_registry(), runtime_options, version);
        });
  }

  for (int version = 0; version <= cel::kOptionalExtensionLatestVersion;
       ++version) {
    registry.AddFunctionRegistration(
        "cel.lib.ext.optional", "optional", version,
        [](RuntimeBuilder& runtime_builder,
           const RuntimeOptions& runtime_options) -> absl::Status {
          return cel::extensions::EnableOptionalTypes(runtime_builder);
        });
  }

  registry.AddFunctionRegistration(
      "cel.lib.ext.protos", "protos", 0,
      [](RuntimeBuilder& runtime_builder,
         const RuntimeOptions& runtime_options) -> absl::Status {
        // No runtime functions to register.
        return absl::OkStatus();
      });

  registry.AddFunctionRegistration(
      "cel.lib.ext.sets", "sets", 0,
      [](RuntimeBuilder& runtime_builder,
         const RuntimeOptions& runtime_options) -> absl::Status {
        return cel::extensions::RegisterSetsFunctions(
            runtime_builder.function_registry(), runtime_options);
      });

  for (int version = 0; version <= extensions::kStringsExtensionLatestVersion;
       ++version) {
    registry.AddFunctionRegistration(
        "cel.lib.ext.strings", "strings", version,
        [version](RuntimeBuilder& runtime_builder,
                  const RuntimeOptions& runtime_options) -> absl::Status {
          cel::extensions::StringsExtensionOptions strings_options;
          strings_options.version = version;
          return cel::extensions::RegisterStringsFunctions(
              runtime_builder.function_registry(), runtime_options,
              strings_options);
        });
  }

  registry.AddFunctionRegistration(
      "cel.lib.ext.comprev2", "two-var-comprehensions", 0,
      [](RuntimeBuilder& runtime_builder,
         const RuntimeOptions& runtime_options) -> absl::Status {
        // No runtime functions to register.
        return absl::OkStatus();
      });

  registry.AddFunctionRegistration(
      "cel.lib.ext.regex", "regex", 0,
      [](RuntimeBuilder& runtime_builder,
         const RuntimeOptions& runtime_options) -> absl::Status {
        return cel::extensions::RegisterRegexExtensionFunctions(
            runtime_builder);
      });
}

}  // namespace cel
