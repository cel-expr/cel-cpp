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

#ifndef THIRD_PARTY_CEL_CPP_EXTENSIONS_STRINGS_MODERN_H_
#define THIRD_PARTY_CEL_CPP_EXTENSIONS_STRINGS_MODERN_H_

#include "absl/status/status.h"
#include "checker/type_checker_builder.h"
#include "compiler/compiler.h"
#include "runtime/function_registry.h"
#include "runtime/runtime_options.h"

namespace cel::extensions {

inline constexpr int kStringsExtensionLatestVersion = 4;

struct StringsExtensionOptions {
  int version = kStringsExtensionLatestVersion;

  // Maximum precision allowed for floating point format specifiers in
  // format() function. This is used for both fixed and scientific notations.
  // Value must be in the range [0, 1000], otherwise clamped.
  //
  // Does not affect default precisions for %e and %f format specifiers.
  int max_precision = 1000;
};

// Register extension functions for strings.
absl::Status RegisterStringsFunctions(
    FunctionRegistry& registry, const RuntimeOptions& options,
    const StringsExtensionOptions& extension_options = {});

CheckerLibrary StringsCheckerLibrary(
    const StringsExtensionOptions& extension_options = {});

inline CheckerLibrary StringsCheckerLibrary(int version) {
  StringsExtensionOptions options;
  options.version = version;
  return StringsCheckerLibrary(options);
}

inline CompilerLibrary StringsCompilerLibrary(
    const StringsExtensionOptions& options = {}) {
  return CompilerLibrary::FromCheckerLibrary(StringsCheckerLibrary(options));
}

inline CompilerLibrary StringsCompilerLibrary(int version) {
  StringsExtensionOptions options;
  options.version = version;
  return StringsCompilerLibrary(options);
}

}  // namespace cel::extensions

#endif  // THIRD_PARTY_CEL_CPP_EXTENSIONS_STRINGS_MODERN_H_
