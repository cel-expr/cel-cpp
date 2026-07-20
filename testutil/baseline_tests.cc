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

#include "testutil/baseline_tests.h"

#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "common/ast.h"
#include "common/expr.h"
#include "common/expr_printer.h"
#include "extensions/protobuf/ast_converters.h"

namespace cel::test {
namespace {

std::string FormatReference(const cel::Reference& r) {
  if (r.overload_id().empty()) {
    return r.name();
  }
  return absl::StrJoin(r.overload_id(), "|");
}

class TypeAdorner : public ExpressionAdorner {
 public:
  explicit TypeAdorner(const Ast& ast) : ast_(ast) {}

  std::string Adorn(const Expr& e) const override {
    std::string s;

    auto t = ast_.type_map().find(e.id());
    if (t != ast_.type_map().end()) {
      absl::StrAppend(&s, "~", FormatTypeSpec(t->second));
    }
    if (const auto r = ast_.reference_map().find(e.id());
        r != ast_.reference_map().end()) {
      absl::StrAppend(&s, "^", FormatReference(r->second));
    }
    return s;
  }

  std::string AdornStructField(const StructExprField& e) const override {
    return "";
  }

  std::string AdornMapEntry(const MapExprEntry& e) const override { return ""; }

 private:
  const Ast& ast_;
};

}  // namespace

std::string FormatBaselineAst(const Ast& ast) {
  TypeAdorner adorner(ast);
  ExprPrinter printer(adorner);
  return printer.Print(ast.root_expr());
}

std::string FormatBaselineCheckedExpr(
    const cel::expr::CheckedExpr& checked) {
  auto ast = cel::extensions::CreateAstFromCheckedExpr(checked);
  if (!ast.ok()) {
    return ast.status().ToString();
  }
  return FormatBaselineAst(**ast);
}

}  // namespace cel::test
