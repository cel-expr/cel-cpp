// Copyright 2023 Google LLC
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
#include "runtime/standard/regex_functions.h"

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "base/builtins.h"
#include "common/function_descriptor.h"
#include "common/kind.h"
#include "common/value.h"
#include "internal/re2_options.h"
#include "internal/status_macros.h"
#include "runtime/function.h"
#include "runtime/function_registry.h"
#include "runtime/runtime_options.h"
#include "re2/re2.h"

namespace cel {
namespace {

class RegexMatchFunction final : public Function {
 public:
  explicit RegexMatchFunction(int max_size) : max_size_(max_size) {}

  absl::StatusOr<Value> Invoke(absl::Span<const Value> args,
                               const InvokeContext& context) const override {
    if (args.size() != 2) {
      return absl::InvalidArgumentError(
          "unexpected number of arguments for binary function");
    }
    if (!args[0].IsString() || !args[1].IsString()) {
      return absl::InvalidArgumentError("expected string value");
    }
    RE2 re2(args[1].GetString().ToString(), cel::internal::MakeRE2Options());
    CEL_RETURN_IF_ERROR(cel::internal::CheckRE2(re2, max_size_))
        .With(ErrorValueReturn(context.arena()));
    return BoolValue(RE2::PartialMatch(args[0].GetString().ToString(), re2));
  }

 private:
  int max_size_;
};

}  // namespace

absl::Status RegisterRegexFunctions(FunctionRegistry& registry,
                                    const RuntimeOptions& options) {
  if (options.enable_regex) {
    // bind str.matches(re) and matches(str, re)
    for (bool receiver_style : {true, false}) {
      CEL_RETURN_IF_ERROR(registry.Register(
          FunctionDescriptor(cel::builtin::kRegexMatch, receiver_style,
                             {Kind::kString, Kind::kString}),
          std::make_unique<RegexMatchFunction>(
              options.regex_max_program_size)));
    }
  }  // if options.enable_regex

  return absl::OkStatus();
}

}  // namespace cel
