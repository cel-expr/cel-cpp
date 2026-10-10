// Copyright 2022 Google LLC
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

#include "eval/eval/regex_match_step.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "common/value.h"
#include "eval/eval/evaluator_core.h"
#include "eval/eval/expression_step_base.h"
#include "eval/eval/expression_step_logic.h"
#include "re2/re2.h"

namespace google::api::expr::runtime {

namespace {

class RegexMatchStep final : public ExpressionStepBase {
 public:
  explicit RegexMatchStep(std::shared_ptr<const RE2> re2)
      : ExpressionStepBase(), re2_(std::move(re2)) {}

  void Evaluate(ExecutionFrame* frame) const override {
    if (!frame->value_stack().HasEnough(1)) {
      frame->Abort(absl::InternalError(
          "Insufficient arguments supplied for regular expression match"));
      return;
    }
    auto& subject_and_result = frame->value_stack().Peek();
    if (subject_and_result.IsUnknown() || subject_and_result.IsError()) {
      return;
    }
    if (!subject_and_result.IsString()) {
      // We can only get here if something is seriously wrong, we verified that
      // we should have gotten a string.
      frame->Abort(absl::InternalError(
          "First argument for regular expression match must be a string"));
      return;
    }
    std::string scratch;
    subject_and_result = cel::BoolValue(RE2::PartialMatch(
        subject_and_result.GetString().ToStringView(&scratch), *re2_));
  }

 private:
  const std::shared_ptr<const RE2> re2_;
};

}  // namespace

absl::StatusOr<std::unique_ptr<ExpressionStepLogic>> CreateRegexMatchStep(
    std::shared_ptr<const RE2> re2) {
  return std::make_unique<RegexMatchStep>(std::move(re2));
}

}  // namespace google::api::expr::runtime
