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

#include "env/env_std_extensions.h"

#include "checker/optional.h"
#include "compiler/optional.h"
#include "env/env.h"
#include "extensions/bindings_ext.h"
#include "extensions/comprehensions_v2.h"
#include "extensions/encoders_modern.h"
#include "extensions/lists_functions.h"
#include "extensions/math_ext_decls.h"
#include "extensions/proto_ext.h"
#include "extensions/regex_ext_modern.h"
#include "extensions/sets_functions_modern.h"
#include "extensions/strings_modern.h"

namespace cel {

void RegisterStandardExtensions(Env& env) {
  env.RegisterCompilerLibrary("cel.lib.ext.bindings", "bindings", 0, []() {
    return extensions::BindingsCompilerLibrary();
  });
  env.RegisterCompilerLibrary("cel.lib.ext.encoders", "encoders", 0, []() {
    return extensions::EncodersCompilerLibrary();
  });
  for (int version = 0; version <= extensions::kListsExtensionLatestVersion;
       ++version) {
    env.RegisterCompilerLibrary(
        "cel.lib.ext.lists", "lists", version,
        [version]() { return extensions::ListsCompilerLibrary(version); });
  }
  for (int version = 0; version <= extensions::kMathExtensionLatestVersion;
       ++version) {
    env.RegisterCompilerLibrary(
        "cel.lib.ext.math", "math", version,
        [version]() { return extensions::MathCompilerLibrary(version); });
  }
  for (int version = 0; version <= kOptionalExtensionLatestVersion; ++version) {
    env.RegisterCompilerLibrary("optional", "", version, [version]() {
      return OptionalCompilerLibrary(version);
    });
  }
  env.RegisterCompilerLibrary("cel.lib.ext.protos", "protos", 0, []() {
    return extensions::ProtoExtCompilerLibrary();
  });
  env.RegisterCompilerLibrary("cel.lib.ext.sets", "sets", 0, []() {
    return extensions::SetsCompilerLibrary();
  });
  for (int version = 0; version <= extensions::kStringsExtensionLatestVersion;
       ++version) {
    env.RegisterCompilerLibrary(
        "cel.lib.ext.strings", "strings", version,
        [version]() { return extensions::StringsCompilerLibrary(version); });
  }
  env.RegisterCompilerLibrary(
      "cel.lib.ext.comprev2", "two-var-comprehensions", 0,
      []() { return extensions::ComprehensionsV2CompilerLibrary(); });
  env.RegisterCompilerLibrary("cel.lib.ext.regex", "regex", 0, []() {
    return extensions::RegexExtCompilerLibrary();
  });
}

}  // namespace cel
