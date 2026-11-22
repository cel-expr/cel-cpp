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

#include "extensions/strings.h"

#include "absl/status/status.h"
#include "eval/public/cel_function_registry.h"
#include "eval/public/cel_options.h"

namespace cel::extensions {

absl::Status RegisterStringsFunctions(
    google::api::expr::runtime::CelFunctionRegistry* registry,
    const google::api::expr::runtime::InterpreterOptions& options,
    const StringsExtensionOptions& extension_options) {
  return RegisterStringsFunctions(
      registry->InternalGetRegistry(),
      google::api::expr::runtime::ConvertToRuntimeOptions(options),
      extension_options);
}

}  // namespace cel::extensions
