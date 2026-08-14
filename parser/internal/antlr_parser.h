// Copyright 2026 Google LLC
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

#ifndef THIRD_PARTY_CEL_CPP_PARSER_INTERNAL_ANTLR_PARSER_H_
#define THIRD_PARTY_CEL_CPP_PARSER_INTERNAL_ANTLR_PARSER_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "common/ast.h"
#include "common/source.h"
#include "parser/macro.h"
#include "parser/macro_registry.h"
#include "parser/options.h"
#include "parser/parser_interface.h"

namespace cel {
class EnrichedSourceInfo;
}  // namespace cel

namespace cel::parser_internal {

class AntlrParserImpl final : public cel::Parser {
 public:
  explicit AntlrParserImpl(const cel::ParserOptions& options,
                           cel::MacroRegistry macro_registry,
                           absl::flat_hash_set<std::string> library_ids)
      : options_(options),
        macro_registry_(std::move(macro_registry)),
        library_ids_(std::move(library_ids)) {}

  ~AntlrParserImpl() override = default;

  absl::StatusOr<std::unique_ptr<cel::Ast>> ParseImpl(
      const cel::Source& source,
      std::vector<cel::ParseIssue>* absl_nullable parse_issues) const override;

  absl::StatusOr<std::unique_ptr<cel::Source>> PrepareSourceImpl(
      absl::string_view input, absl::string_view description) const override;

  std::unique_ptr<cel::ParserBuilder> ToBuilder() const override;

 private:
  cel::ParserOptions options_;
  cel::MacroRegistry macro_registry_;
  absl::flat_hash_set<std::string> library_ids_;
};

absl::StatusOr<std::unique_ptr<cel::Ast>> AntlrParseImpl(
    const cel::Source& source, const cel::MacroRegistry& registry,
    const ParserOptions& options,
    std::vector<cel::ParseIssue>* parse_issues = nullptr,
    cel::EnrichedSourceInfo* enriched_source_info = nullptr);

class AntlrParserBuilderImpl final : public cel::ParserBuilder {
 public:
  explicit AntlrParserBuilderImpl(const cel::ParserOptions& options)
      : options_(options) {}

  cel::ParserOptions& GetOptions() override { return options_; }

  absl::Status AddMacro(const cel::Macro& macro) override;

  absl::Status AddLibrary(cel::ParserLibrary library) override;

  absl::Status AddLibrarySubset(cel::ParserLibrarySubset subset) override;

  absl::StatusOr<std::unique_ptr<cel::Parser>> Build() override;

 private:
  friend class AntlrParserImpl;

  cel::ParserOptions options_;
  std::vector<cel::Macro> macros_;
  absl::flat_hash_set<std::string> library_ids_;
  std::vector<cel::ParserLibrary> libraries_;
  absl::flat_hash_map<std::string, cel::ParserLibrarySubset> library_subsets_;
};

inline std::unique_ptr<cel::ParserBuilder> NewAntlrParserBuilder(
    const cel::ParserOptions& options = cel::ParserOptions()) {
  return std::make_unique<AntlrParserBuilderImpl>(options);
}

}  // namespace cel::parser_internal

#endif  // THIRD_PARTY_CEL_CPP_PARSER_INTERNAL_ANTLR_PARSER_H_
