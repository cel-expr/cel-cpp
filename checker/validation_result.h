// Copyright 2024 Google LLC
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

#ifndef THIRD_PARTY_CEL_CPP_CHECKER_VALIDATION_RESULT_H_
#define THIRD_PARTY_CEL_CPP_CHECKER_VALIDATION_RESULT_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "checker/type_check_issue.h"
#include "common/ast.h"
#include "common/decl.h"
#include "common/source.h"
#include "common/type.h"

namespace cel {

// ValidationResult holds the result of type checking.
//
// Error states are captured as type check issues where possible.
class ValidationResult {
 public:
  using TypeMap = absl::flat_hash_map<int64_t, Type>;

  ValidationResult(std::unique_ptr<Ast> ast, std::vector<TypeCheckIssue> issues)
      : ast_(std::move(ast)), issues_(std::move(issues)) {}

  explicit ValidationResult(std::vector<TypeCheckIssue> issues)
      : ast_(nullptr), issues_(std::move(issues)) {}

  bool IsValid() const { return ast_ != nullptr; }

  // Returns the AST if validation was successful.
  //
  // This is a non-null pointer if IsValid() is true.
  const Ast* absl_nullable GetAst() const { return ast_.get(); }

  absl::StatusOr<std::unique_ptr<Ast>> ReleaseAst() {
    if (ast_ == nullptr) {
      return absl::FailedPreconditionError(
          "ValidationResult is empty. Check for TypeCheckIssues.");
    }
    return std::move(ast_);
  }

  absl::Span<const TypeCheckIssue> GetIssues() const { return issues_; }

  void AddIssue(TypeCheckIssue issue) { issues_.push_back(std::move(issue)); }

  // The source expression may optionally be set if it is available.
  const cel::Source* absl_nullable GetSource() const { return source_.get(); }

  void SetSource(std::unique_ptr<Source> source) {
    source_ = std::move(source);
  }

  absl_nullable std::unique_ptr<cel::Source> ReleaseSource() {
    return std::move(source_);
  }

  // Returns the resolved type map for the AST.
  //
  // Only populated if the AST was checked with an explicit arena.
  //
  // The type entries may have storage in the arena or reference type
  // information from the type checker that produced the AST. This means the map
  // is only valid as long as both the type checker and the arena are valid.
  const TypeMap& GetResolvedTypeMap() const { return resolved_type_map_; }
  void SetResolvedTypeMap(TypeMap resolved_type_map) {
    resolved_type_map_ = std::move(resolved_type_map);
  }

  // Returns the type of the root expression with free type parameters
  // preserved as named type parameters (T1, T2, ...).
  //
  // Only populated if the AST was checked with an explicit arena and
  // CheckerOptions::use_polymorphic_bind is enabled. Has the same
  // lifetime requirements as the resolved type map.
  std::optional<Type> GetGenericResultType() const {
    return generic_result_type_;
  }
  void SetGenericResultType(Type type) { generic_result_type_ = type; }

  // Returns a string representation of the issues in the result suitable for
  // display.
  //
  // The result is empty if no issues are present.
  //
  // The result is formatted similarly to CEL-Java and CEL-Go, but we do not
  // give strong guarantees on the format or stability.
  //
  // Example:
  //
  // ERROR: <source description>:1:3: Issue1
  //  | source.cel
  //  | ..^
  // INFORMATION: <source description>:-1:-1: Issue2
  std::string FormatError() const;

 private:
  absl_nullable std::unique_ptr<Ast> ast_;
  TypeMap resolved_type_map_;
  std::optional<Type> generic_result_type_;
  std::vector<TypeCheckIssue> issues_;
  absl_nullable std::unique_ptr<Source> source_;
};

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_CHECKER_VALIDATION_RESULT_H_
