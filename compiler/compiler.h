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

#ifndef THIRD_PARTY_CEL_CPP_COMPILER_COMPILER_INTERFACE_H_
#define THIRD_PARTY_CEL_CPP_COMPILER_COMPILER_INTERFACE_H_

#include <memory>
#include <string>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "checker/checker_options.h"
#include "checker/type_checker.h"
#include "checker/type_checker_builder.h"
#include "checker/validation_result.h"
#include "common/source.h"
#include "parser/options.h"
#include "parser/parser_interface.h"
#include "validator/validator.h"
#include "google/protobuf/arena.h"

namespace cel {

class Compiler;
class CompilerBuilder;

// A CompilerLibrary represents a package of CEL configuration that can be
// added to a Compiler.
//
// It may contain either or both of a Parser configuration and a
// TypeChecker configuration.
struct CompilerLibrary {
  // Optional identifier to avoid collisions re-adding the same library.
  // If id is empty, it is not considered.
  std::string id;
  // Optional callback for configuring the parser.
  ParserBuilderConfigurer configure_parser;
  // Optional callback for configuring the type checker.
  TypeCheckerBuilderConfigurer configure_checker;

  CompilerLibrary(std::string id, ParserBuilderConfigurer configure_parser,
                  TypeCheckerBuilderConfigurer configure_checker = nullptr)
      : id(std::move(id)),
        configure_parser(std::move(configure_parser)),
        configure_checker(std::move(configure_checker)) {}

  CompilerLibrary(std::string id,
                  TypeCheckerBuilderConfigurer configure_checker)
      : id(std::move(id)),
        configure_parser(std::move(nullptr)),
        configure_checker(std::move(configure_checker)) {}

  // Convenience conversion from the CheckerLibrary type.
  //
  // Note: if a related CompilerLibrary exists, prefer to use that to
  // include expected parser configuration.
  static CompilerLibrary FromCheckerLibrary(CheckerLibrary checker_library) {
    return CompilerLibrary(std::move(checker_library.id),
                           /*configure_parser=*/nullptr,
                           std::move(checker_library.configure));
  }

  // For backwards compatibility. To be removed.
  // NOLINTNEXTLINE(google-explicit-constructor)
  CompilerLibrary(CheckerLibrary checker_library)
      : id(std::move(checker_library.id)),
        configure_parser(nullptr),
        configure_checker(std::move(checker_library.configure)) {}
};

struct CompilerLibrarySubset {
  // The id of the library to subset. Only one subset can be applied per
  // library id.
  //
  // Must be non-empty.
  std::string library_id;
  ParserLibrarySubset::MacroPredicate should_include_macro;
  TypeCheckerSubset::FunctionPredicate should_include_overload;
  // TODO(uncreated-issue/71): to faithfully report the subset back, we need to track
  // the default (include or exclude) behavior for each of the predicates.
};

// General options for configuring the underlying parser and checker.
struct CompilerOptions {
  ParserOptions parser_options;
  CheckerOptions checker_options;
  // If true, parse errors will be adapted to issues where possible.
  bool adapt_parser_errors = false;
};

// Interface for CEL CompilerBuilder objects.
//
// Builder implementations do not provide any synchronization themselves,
// but create thread-compatible Compiler instances.
class CompilerBuilder {
 public:
  virtual ~CompilerBuilder() = default;

  virtual absl::Status AddLibrary(CompilerLibrary library) = 0;
  virtual absl::Status AddLibrarySubset(CompilerLibrarySubset subset) = 0;

  virtual TypeCheckerBuilder& GetCheckerBuilder() = 0;
  virtual ParserBuilder& GetParserBuilder() = 0;
  virtual Validator& GetValidator() = 0;

  virtual absl::StatusOr<std::unique_ptr<Compiler>> Build() = 0;
};

// Interface for CEL Compiler objects.
//
// For CEL, compilation is the process of bundling the parse and type-check
// passes.
//
// Compiler instances should be thread-compatible.
class Compiler {
 public:
  virtual ~Compiler() = default;

  absl::StatusOr<ValidationResult> Compile(
      const Source& source, google::protobuf::Arena* absl_nullable arena) const {
    return CompileImpl(source, arena);
  }

  absl::StatusOr<ValidationResult> Compile(const Source& source) const {
    return CompileImpl(source, nullptr);
  }

  absl::StatusOr<ValidationResult> Compile(
      absl::string_view source, absl::string_view description,
      google::protobuf::Arena* absl_nullable arena) const;

  absl::StatusOr<ValidationResult> Compile(absl::string_view source) const {
    return Compile(source, "<input>", nullptr);
  }

  absl::StatusOr<ValidationResult> Compile(
      absl::string_view source, absl::string_view description) const {
    return Compile(source, description, nullptr);
  }

  // Accessor for the underlying type checker.
  virtual const TypeChecker& GetTypeChecker() const = 0;

  // Accessor for the underlying parser.
  virtual const Parser& GetParser() const = 0;

  // Accessor for the underlying validator.
  virtual const Validator& GetValidator() const = 0;

  // Returns a builder initialized with the configuration of this compiler.
  //
  // The returned builder is a copy of the validated environment and may
  // behave differently than the builder that created this compiler.
  //
  // The returned builder does not share state with the compiler and may be
  // modified independently.
  virtual std::unique_ptr<CompilerBuilder> ToBuilder() const = 0;

 protected:
  virtual absl::StatusOr<ValidationResult> CompileImpl(
      const Source& source, google::protobuf::Arena* absl_nullable arena) const = 0;
};

inline absl::StatusOr<ValidationResult> Compiler::Compile(
    absl::string_view source, absl::string_view description,
    google::protobuf::Arena* absl_nullable arena) const {
  absl::StatusOr<SourcePtr> source_obj =
      NewSource(source, std::string(description));
  if (!source_obj.ok()) {
    return source_obj.status();
  }
  absl::StatusOr<ValidationResult> result = CompileImpl(**source_obj, arena);
  if (result.ok()) {
    result->SetSource(std::move(*source_obj));
  }
  return result;
}

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMPILER_COMPILER_INTERFACE_H_
