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

#ifndef THIRD_PARTY_CEL_CPP_POLICY_CEL_POLICY_VALIDATION_RESULT_H_
#define THIRD_PARTY_CEL_CPP_POLICY_CEL_POLICY_VALIDATION_RESULT_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "common/ast.h"
#include "common/decl.h"
#include "policy/cel_policy.h"
#include "policy/cel_policy_parse_result.h"

namespace cel {

// CelPolicyValidationResult holds the result of policy compilation.
//
// Policy compilation/validation errors are captured in issues.
class CelPolicyValidationResult {
 public:
  CelPolicyValidationResult(
      std::unique_ptr<Ast> ast, std::vector<CelPolicyIssue> issues,
      std::shared_ptr<const CelPolicySource> source = nullptr)
      : ast_(std::move(ast)),
        issues_(std::move(issues)),
        source_(std::move(source)) {}

  explicit CelPolicyValidationResult(
      std::vector<CelPolicyIssue> issues,
      std::shared_ptr<const CelPolicySource> source = nullptr)
      : ast_(nullptr), issues_(std::move(issues)), source_(std::move(source)) {}

  // Returns true if validation succeeded and an AST is present.
  bool IsValid() const { return ast_ != nullptr; }

  // Returns the AST if validation was successful.
  const Ast* absl_nullable GetAst() const { return ast_.get(); }

  // Moves out and returns the AST.
  absl::StatusOr<std::unique_ptr<Ast>> ReleaseAst() {
    if (ast_ == nullptr) {
      return absl::FailedPreconditionError(
          "CelPolicyValidationResult is empty. Check for CelPolicyIssues.");
    }
    return std::move(ast_);
  }

  // Returns the list of issues encountered during compilation.
  absl::Span<const CelPolicyIssue> GetIssues() const { return issues_; }

  // Returns the contained policy source, if any.
  const CelPolicySource* absl_nullable GetSource() const {
    return source_.get();
  }

  // Returns a formatted error string of the compiled issues.
  std::string FormatIssues() const;

 private:
  absl_nullable std::unique_ptr<Ast> ast_;
  std::vector<CelPolicyIssue> issues_;
  std::shared_ptr<const CelPolicySource> source_;
};

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_POLICY_CEL_POLICY_VALIDATION_RESULT_H_
