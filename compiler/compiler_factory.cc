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

#include "compiler/compiler_factory.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "checker/type_check_issue.h"
#include "checker/type_checker.h"
#include "checker/type_checker_builder.h"
#include "checker/type_checker_builder_factory.h"
#include "checker/validation_result.h"
#include "common/ast.h"
#include "common/source.h"
#include "compiler/compiler.h"
#include "internal/status_macros.h"
#include "parser/parser.h"
#include "parser/parser_interface.h"
#include "validator/validator.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"

namespace cel {

namespace {

class CompilerImpl : public Compiler {
 public:
  CompilerImpl(std::unique_ptr<TypeChecker> type_checker,
               std::unique_ptr<Parser> parser,
               // Copy the validator in case builder is reused.
               Validator validator, CompilerOptions options)
      : type_checker_(std::move(type_checker)),
        parser_(std::move(parser)),
        validator_(std::move(validator)),
        options_(options) {}

  std::unique_ptr<CompilerBuilder> ToBuilder() const override;

  const TypeChecker& GetTypeChecker() const override { return *type_checker_; }
  const Parser& GetParser() const override { return *parser_; }
  const Validator& GetValidator() const override { return validator_; }

 protected:
  absl::StatusOr<ValidationResult> CompileImpl(
      const Source& source, google::protobuf::Arena* arena) const override {
    std::vector<cel::ParseIssue> parse_issues;
    absl::StatusOr<std::unique_ptr<cel::Ast>> ast =
        parser_->Parse(source, &parse_issues);
    if (!ast.ok()) {
      if (!options_.adapt_parser_errors ||
          ast.status().code() != absl::StatusCode::kInvalidArgument ||
          parse_issues.empty()) {
        return ast.status();
      }
      std::vector<TypeCheckIssue> check_issues;
      check_issues.reserve(parse_issues.size());
      for (const auto& issue : parse_issues) {
        check_issues.push_back(TypeCheckIssue::CreateError(
            issue.location(), std::string(issue.message())));
      }
      return ValidationResult(std::move(check_issues));
    }
    CEL_ASSIGN_OR_RETURN(ValidationResult result,
                         type_checker_->Check(*std::move(ast), arena));

    if (!validator_.validations().empty()) {
      validator_.UpdateValidationResult(result);
    }
    return result;
  }

 private:
  std::unique_ptr<TypeChecker> type_checker_;
  std::unique_ptr<Parser> parser_;
  Validator validator_;
  CompilerOptions options_;
};

class CompilerBuilderImpl : public CompilerBuilder {
 public:
  CompilerBuilderImpl(std::unique_ptr<TypeCheckerBuilder> type_checker_builder,
                      std::unique_ptr<ParserBuilder> parser_builder,
                      Validator validator, CompilerOptions options)
      : type_checker_builder_(std::move(type_checker_builder)),
        parser_builder_(std::move(parser_builder)),
        validator_(std::move(validator)),
        options_(options) {}

  absl::Status AddLibrary(CompilerLibrary library) override {
    if (!library.id.empty()) {
      auto [it, inserted] = library_ids_.insert(library.id);

      if (!inserted) {
        return absl::AlreadyExistsError(
            absl::StrCat("library already exists: ", library.id));
      }
    }

    if (library.configure_checker) {
      CEL_RETURN_IF_ERROR(type_checker_builder_->AddLibrary({
          .id = library.id,
          .configure = std::move(library.configure_checker),
      }));
    }
    if (library.configure_parser) {
      CEL_RETURN_IF_ERROR(parser_builder_->AddLibrary({
          .id = library.id,
          .configure = std::move(library.configure_parser),
      }));
    }
    return absl::OkStatus();
  }

  absl::Status AddLibrarySubset(CompilerLibrarySubset subset) override {
    if (subset.library_id.empty()) {
      return absl::InvalidArgumentError("library id must not be empty");
    }
    std::string library_id = subset.library_id;

    auto [it, inserted] = subsets_.insert(library_id);
    if (!inserted) {
      return absl::AlreadyExistsError(
          absl::StrCat("library subset already exists for: ", library_id));
    }

    if (subset.should_include_macro) {
      CEL_RETURN_IF_ERROR(parser_builder_->AddLibrarySubset({
          library_id,
          std::move(subset.should_include_macro),
      }));
    }
    if (subset.should_include_overload) {
      CEL_RETURN_IF_ERROR(type_checker_builder_->AddLibrarySubset(
          {library_id, std::move(subset.should_include_overload)}));
    }
    return absl::OkStatus();
  }

  ParserBuilder& GetParserBuilder() override { return *parser_builder_; }
  TypeCheckerBuilder& GetCheckerBuilder() override {
    return *type_checker_builder_;
  }
  Validator& GetValidator() override { return validator_; }

  absl::StatusOr<std::unique_ptr<Compiler>> Build() override {
    CEL_ASSIGN_OR_RETURN(auto parser, parser_builder_->Build());
    CEL_ASSIGN_OR_RETURN(auto type_checker, type_checker_builder_->Build());
    return std::make_unique<CompilerImpl>(
        std::move(type_checker), std::move(parser), validator_, options_);
  }

 private:
  std::unique_ptr<TypeCheckerBuilder> type_checker_builder_;
  std::unique_ptr<ParserBuilder> parser_builder_;
  Validator validator_;
  CompilerOptions options_;

  absl::flat_hash_set<std::string> library_ids_;
  absl::flat_hash_set<std::string> subsets_;
};

std::unique_ptr<CompilerBuilder> CompilerImpl::ToBuilder() const {
  return std::make_unique<CompilerBuilderImpl>(
      type_checker_->ToBuilder(), parser_->ToBuilder(), validator_, options_);
}

}  // namespace

absl::StatusOr<std::unique_ptr<CompilerBuilder>> NewCompilerBuilder(
    std::shared_ptr<const google::protobuf::DescriptorPool> descriptor_pool,
    CompilerOptions options) {
  if (descriptor_pool == nullptr) {
    return absl::InvalidArgumentError("descriptor_pool must not be null");
  }
  CEL_ASSIGN_OR_RETURN(auto type_checker_builder,
                       CreateTypeCheckerBuilder(std::move(descriptor_pool),
                                                options.checker_options));
  auto parser_builder = NewParserBuilder(options.parser_options);

  return std::make_unique<CompilerBuilderImpl>(std::move(type_checker_builder),
                                               std::move(parser_builder),
                                               Validator(), options);
}

}  // namespace cel
