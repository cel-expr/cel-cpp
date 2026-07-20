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

#include "policy/internal/optimizer_expr_factory.h"

#include <memory>
#include <string>
#include <vector>

#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "common/ast.h"
#include "common/ast_proto.h"
#include "common/ast_rewrite.h"
#include "common/decl.h"
#include "common/expr.h"
#include "common/expr_factory.h"
#include "common/expr_printer.h"
#include "common/source.h"
#include "common/type.h"
#include "compiler/compiler.h"
#include "compiler/compiler_factory.h"
#include "compiler/standard_library.h"
#include "internal/status_macros.h"
#include "internal/testing.h"
#include "internal/testing_descriptor_pool.h"
#include "tools/cel_unparser.h"

namespace cel {

using ::testing::SizeIs;

// Expose protected members of OptimizerExprFactory for use in tests
//
// These allow setting explicit IDs which is not safe for the optimizing
// factory.
class TestOptimizerExprFactory final : public OptimizerExprFactory {
 public:
  using OptimizerExprFactory::OptimizerExprFactory;

  using OptimizerExprFactory::NewBoolConst;
  using OptimizerExprFactory::NewCall;
  using OptimizerExprFactory::NewComprehension;
  using OptimizerExprFactory::NewIdent;
  using OptimizerExprFactory::NewList;
  using OptimizerExprFactory::NewListElement;
  using OptimizerExprFactory::NewMap;
  using OptimizerExprFactory::NewMapEntry;
  using OptimizerExprFactory::NewMemberCall;
  using OptimizerExprFactory::NewSelect;
  using OptimizerExprFactory::NewStruct;
  using OptimizerExprFactory::NewStructField;
  using OptimizerExprFactory::NewUnspecified;
  using OptimizerExprFactory::NextId;
};

namespace {

class ReplaceExprRewriter final : public AstRewriterBase {
 public:
  ReplaceExprRewriter(ExprId old_id, const Expr& replacement)
      : old_id_(old_id), replacement_(replacement) {}

  bool PreVisitRewrite(Expr& expr) override {
    if (expr.id() == old_id_) {
      expr = replacement_;
      return true;
    }
    return false;
  }

 private:
  ExprId old_id_;
  const Expr& replacement_;
};

void ReplaceExprInTree(Expr& expr, ExprId old_id, const Expr& replacement) {
  ReplaceExprRewriter rewriter(old_id, replacement);
  AstRewrite(expr, rewriter);
}

absl::StatusOr<std::unique_ptr<Compiler>> CreateTestCompiler() {
  CompilerOptions opts;
  opts.parser_options.add_macro_calls = true;
  CEL_ASSIGN_OR_RETURN(
      auto builder, cel::NewCompilerBuilder(
                        cel::internal::GetSharedTestingDescriptorPool(), opts));
  CEL_RETURN_IF_ERROR(builder->AddLibrary(cel::StandardCompilerLibrary()));
  CEL_RETURN_IF_ERROR(builder->GetCheckerBuilder().AddVariable(
      cel::MakeVariableDecl("to_replace", cel::DynType())));
  return builder->Build();
}

TEST(OptimizerExprFactory, CopyUnspecified) {
  TestOptimizerExprFactory factory{Ast()};
  EXPECT_EQ(factory.Copy(factory.NewUnspecified()), factory.NewUnspecified(2));
}

TEST(OptimizerExprFactory, CopyIdent) {
  TestOptimizerExprFactory factory{Ast()};
  EXPECT_EQ(factory.Copy(factory.NewIdent("foo")), factory.NewIdent(2, "foo"));
}

TEST(OptimizerExprFactory, CopyConst) {
  TestOptimizerExprFactory factory{Ast()};
  EXPECT_EQ(factory.Copy(factory.NewBoolConst(true)),
            factory.NewBoolConst(2, true));
}

TEST(OptimizerExprFactory, CopySelect) {
  TestOptimizerExprFactory factory{Ast()};
  EXPECT_EQ(factory.Copy(factory.NewSelect(factory.NewIdent("foo"), "bar")),
            factory.NewSelect(3, factory.NewIdent(4, "foo"), "bar"));
}

TEST(OptimizerExprFactory, CopyCall) {
  TestOptimizerExprFactory factory{Ast()};
  std::vector<Expr> copied_args;
  copied_args.reserve(1);
  copied_args.push_back(factory.NewIdent(6, "baz"));
  EXPECT_EQ(factory.Copy(factory.NewMemberCall("bar", factory.NewIdent("foo"),
                                               factory.NewIdent("baz"))),
            factory.NewMemberCall(4, "bar", factory.NewIdent(5, "foo"),
                                  absl::MakeSpan(copied_args)));
}

TEST(OptimizerExprFactory, CopyList) {
  TestOptimizerExprFactory factory{Ast()};
  std::vector<ListExprElement> copied_elements;
  copied_elements.reserve(1);
  copied_elements.push_back(factory.NewListElement(factory.NewIdent(4, "foo")));
  EXPECT_EQ(factory.Copy(factory.NewList(
                factory.NewListElement(factory.NewIdent("foo")))),
            factory.NewList(3, absl::MakeSpan(copied_elements)));
}

TEST(OptimizerExprFactory, CopyStruct) {
  TestOptimizerExprFactory factory{Ast()};
  std::vector<StructExprField> copied_fields;
  copied_fields.reserve(1);
  copied_fields.push_back(
      factory.NewStructField(5, "bar", factory.NewIdent(6, "baz")));
  EXPECT_EQ(factory.Copy(factory.NewStruct(
                "foo", factory.NewStructField("bar", factory.NewIdent("baz")))),
            factory.NewStruct(4, "foo", absl::MakeSpan(copied_fields)));
}

TEST(OptimizerExprFactory, CopyMap) {
  TestOptimizerExprFactory factory{Ast()};
  std::vector<MapExprEntry> copied_entries;
  copied_entries.reserve(1);
  copied_entries.push_back(factory.NewMapEntry(6, factory.NewIdent(7, "bar"),
                                               factory.NewIdent(8, "baz")));
  EXPECT_EQ(factory.Copy(factory.NewMap(factory.NewMapEntry(
                factory.NewIdent("bar"), factory.NewIdent("baz")))),
            factory.NewMap(5, absl::MakeSpan(copied_entries)));
}

TEST(OptimizerExprFactory, CopyComprehension) {
  TestOptimizerExprFactory factory{Ast()};
  EXPECT_EQ(
      factory.Copy(factory.NewComprehension(
          "foo", factory.NewList(), "bar", factory.NewBoolConst(true),
          factory.NewIdent("baz"), factory.NewIdent("foo"),
          factory.NewIdent("bar"))),
      factory.NewComprehension(
          7, "foo", factory.NewList(8, std::vector<ListExprElement>()), "bar",
          factory.NewBoolConst(9, true), factory.NewIdent(10, "baz"),
          factory.NewIdent(11, "foo"), factory.NewIdent(12, "bar")));
}

TEST(OptimizerExprFactory, RemapSourceInfo) {
  TestOptimizerExprFactory factory{Ast()};
  Expr orig = factory.NewIdent("foo");  // allocates ID 1
  Expr copied = factory.Copy(orig);     // copies ID 1 to mapped ID 2

  SourceInfo info;
  info.mutable_positions()[1] = 42;  // old ID 1 has position 42

  SourceInfo remapped = factory.RemapSourceInfo(info, 10);

  // remapped should have ID 2 mapped to position 42 + 10 = 52
  auto it = remapped.positions().find(2);
  ASSERT_NE(it, remapped.positions().end());
  EXPECT_EQ(it->second, 52);
}

TEST(OptimizerExprFactory, RemapSourceInfoWithMacroCalls) {
  TestOptimizerExprFactory factory{Ast()};
  Expr orig = factory.NewIdent("foo");  // allocates ID 1
  Expr copied = factory.Copy(orig);     // copies ID 1 to mapped ID 2

  SourceInfo info;
  // old ID 1 has macro call with ID 3
  info.mutable_macro_calls()[1] = factory.NewIdent("bar");

  SourceInfo remapped = factory.RemapSourceInfo(info, 10);

  // remapped should have ID 2 mapped to the copied macro call
  // since "bar" has ID 3, Copy(bar) should map ID 3 to ID 4

  auto it = remapped.macro_calls().find(2);
  ASSERT_NE(it, remapped.macro_calls().end());

  // The macro call should be an Ident with new ID 4
  EXPECT_EQ(it->second.id(), 4);
  EXPECT_TRUE(it->second.has_ident_expr());
  EXPECT_EQ(it->second.ident_expr().name(), "bar");
}

TEST(OptimizerExprFactory, ReportError) {
  TestOptimizerExprFactory factory{Ast()};
  Expr err_expr = factory.ReportError("something went wrong");

  // err_expr should be unspecified with ID 1
  EXPECT_EQ(err_expr.id(), 1);
  EXPECT_EQ(err_expr.kind_case(), ExprKindCase::kUnspecifiedExpr);

  // issues_ should have 1 entry with ID 1 and correct message
  ASSERT_EQ(factory.issues().size(), 1);
  EXPECT_EQ(factory.issues()[0].location, 1);
  EXPECT_EQ(factory.issues()[0].message, "something went wrong");
}

TEST(OptimizerExprFactory, ReportErrorAt) {
  TestOptimizerExprFactory factory{Ast()};
  Expr orig = factory.NewIdent("foo");  // allocates ID 1
  Expr copied = factory.Copy(orig);     // copies ID 1 to mapped ID 2

  Expr err_expr = factory.ReportErrorAtCopy(orig, "error on foo");

  // err_expr should be unspecified with ID 3 (NextId)
  EXPECT_EQ(err_expr.id(), 3);
  EXPECT_EQ(err_expr.kind_case(), ExprKindCase::kUnspecifiedExpr);

  // issues_ should have 1 entry with mapped ID 2 and correct message
  ASSERT_EQ(factory.issues().size(), 1);
  EXPECT_EQ(factory.issues()[0].location, 2);
  EXPECT_EQ(factory.issues()[0].message, "error on foo");
}

TEST(OptimizerExprFactory, MergeSourceInfo) {
  // Create a base AST with some source info
  SourceInfo base_info;
  base_info.set_syntax_version("cel1");
  base_info.set_location("test.cel");
  base_info.mutable_positions()[1] = 10;

  Ast base_ast(Expr(), std::move(base_info));

  TestOptimizerExprFactory factory{std::move(base_ast)};

  // Create a new source info to merge
  SourceInfo new_info;
  new_info.mutable_positions()[2] = 20;

  factory.MergeSourceInfo(new_info);

  // The merged source info should have both positions
  const auto& merged_info = factory.ast().source_info();
  EXPECT_EQ(merged_info.syntax_version(), "cel1");
  EXPECT_EQ(merged_info.location(), "test.cel");

  auto it1 = merged_info.positions().find(1);
  ASSERT_NE(it1, merged_info.positions().end());
  EXPECT_EQ(it1->second, 10);

  auto it2 = merged_info.positions().find(2);
  ASSERT_NE(it2, merged_info.positions().end());
  EXPECT_EQ(it2->second, 20);
}

TEST(OptimizerExprFactory, MergeSourceInfoConflict) {
  SourceInfo base_info;
  base_info.mutable_positions()[1] = 10;

  Ast base_ast(Expr(), std::move(base_info));
  TestOptimizerExprFactory factory{std::move(base_ast)};

  SourceInfo new_info;
  new_info.mutable_positions()[1] = 20;  // conflicting ID 1

  factory.MergeSourceInfo(new_info);

  // Should report an error for the conflict
  ASSERT_EQ(factory.issues().size(), 1);
  EXPECT_EQ(factory.issues()[0].location, 1);
  EXPECT_EQ(factory.issues()[0].message, "conflicting ID in positions merge");
}

TEST(OptimizerExprFactory, RecordReplacement) {
  SourceInfo base_info;
  base_info.mutable_positions()[1] = 10;
  base_info.mutable_positions()[2] = 20;

  TestOptimizerExprFactory factory{Ast()};

  // macro_calls[1] maps ID 1 to macro call "bar(foo)" (where "foo" has ID 1)
  base_info.mutable_macro_calls()[1] =
      factory.NewCall("bar", factory.NewIdent(1, "foo"));

  // macro_calls[2] maps ID 2 to macro call "baz(foo)" (where "foo" has ID 1)
  base_info.mutable_macro_calls()[2] =
      factory.NewCall("baz", factory.NewIdent(1, "foo"));

  Ast base_ast(Expr(), std::move(base_info));
  TestOptimizerExprFactory optimizer{std::move(base_ast)};

  // Record the replacement of ID 1 by a new Ident "replacement" with ID 3
  optimizer.RecordReplacement(1, factory.NewIdent(3, "replacement"));

  const auto& result_info = optimizer.ast().source_info();

  // 1. ID 1 should be erased from positions
  EXPECT_EQ(result_info.positions().find(1), result_info.positions().end());
  EXPECT_NE(result_info.positions().find(2), result_info.positions().end());

  // 2. ID 1 should be erased from macro_calls keys
  EXPECT_EQ(result_info.macro_calls().find(1), result_info.macro_calls().end());

  // 3. macro_calls[2] should still exist, but its argument referencing ID 1
  // should be replaced with the Ident "replacement" with ID 3 inline
  auto it = result_info.macro_calls().find(2);
  ASSERT_NE(it, result_info.macro_calls().end());

  const Expr& macro_expr = it->second;
  ASSERT_TRUE(macro_expr.has_call_expr());
  ASSERT_EQ(macro_expr.call_expr().args().size(), 1);

  const Expr& arg = macro_expr.call_expr().args()[0];
  EXPECT_EQ(arg.id(), 3);
  EXPECT_TRUE(arg.has_ident_expr());
  EXPECT_EQ(arg.ident_expr().name(), "replacement");
}

class IdAdorner : public cel::ExpressionAdorner {
 public:
  std::string Adorn(const cel::Expr& e) const override {
    return absl::StrCat("#", e.id());
  }

  std::string AdornStructField(const cel::StructExprField& e) const override {
    return absl::StrCat("#", e.id());
  }

  std::string AdornMapEntry(const cel::MapExprEntry& e) const override {
    return absl::StrCat("#", e.id());
  }
};

TEST(OptimizerExprFactory, UnparseCopiedMacroCall) {
  // Arrange: create an template expression and one to inline.
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> compiler,
                       CreateTestCompiler());

  ASSERT_OK_AND_ASSIGN(auto basis_result,
                       compiler->Compile("[1].map(x, x + to_replace)"));
  ASSERT_TRUE(basis_result.IsValid());
  ASSERT_OK_AND_ASSIGN(auto basis_ast, basis_result.ReleaseAst());

  ASSERT_OK_AND_ASSIGN(auto copy_result,
                       compiler->Compile("[1].filter(x, x > 2).size()"));
  ASSERT_TRUE(copy_result.IsValid());
  ASSERT_OK_AND_ASSIGN(auto copy_ast, copy_result.ReleaseAst());

  // Locate the "to_replace" IdentExpr node in reference_map
  ExprId to_replace_id = 0;
  for (const auto& [id, ref] : basis_ast->reference_map()) {
    if (ref.name() == "to_replace") {
      to_replace_id = id;
      break;
    }
  }
  ASSERT_NE(to_replace_id, 0);

  // Act: implement the optimization.
  TestOptimizerExprFactory factory{std::move(*basis_ast)};
  Expr copied_expr = factory.Copy(copy_ast->root_expr());
  SourceInfo remapped_info = factory.RemapSourceInfo(copy_ast->source_info());
  factory.MergeSourceInfo(remapped_info);

  ReplaceExprInTree(factory.mutable_ast().mutable_root_expr(), to_replace_id,
                    copied_expr);
  factory.RecordReplacement(to_replace_id, copied_expr);

  // Test AST structure.
  EXPECT_EQ(cel::ExprPrinter(IdAdorner()).Print(factory.ast().root_expr()),
            R"(__comprehension__(
  // Variable
  x,
  // Target
  [
    1#2
  ]#1,
  // Accumulator
  @result,
  // Init
  []#8,
  // LoopCondition
  true#9,
  // LoopStep
  _+_(
    @result#10,
    [
      _+_(
        x#5,
        __comprehension__(
          // Variable
          x,
          // Target
          [
            1#18
          ]#17,
          // Accumulator
          @result,
          // Init
          []#19,
          // LoopCondition
          true#20,
          // LoopStep
          _?_:_(
            _>_(
              x#23,
              2#24
            )#22,
            _+_(
              @result#26,
              [
                x#28
              ]#27
            )#25,
            @result#29
          )#21,
          // Result
          @result#30)#16.size()#15
      )#6
    ]#11
  )#12,
  // Result
  @result#13)#14)");

  // Check that the structure is compatible with unparser.
  cel::expr::ParsedExpr optimized_parsed;
  auto status = AstToParsedExpr(factory.ast(), &optimized_parsed);
  ASSERT_THAT(status, absl_testing::IsOk());
  ASSERT_OK_AND_ASSIGN(std::string unparsed,
                       google::api::expr::Unparse(optimized_parsed));

  EXPECT_EQ(unparsed, "[1].map(x, x + [1].filter(x, x > 2).size())");

  const CallExpr& call_expr = factory.mutable_ast()
                                  .mutable_source_info()
                                  .mutable_macro_calls()[14]
                                  .mutable_call_expr();
  ASSERT_THAT(call_expr.args(), SizeIs(2));
  ASSERT_THAT(call_expr.args()[1].call_expr().args(), SizeIs(2));
  EXPECT_EQ(call_expr.args()[1].call_expr().args()[1].id(), 15);

  EXPECT_EQ(call_expr.args()[1].call_expr().args()[1].call_expr().target().id(),
            16);
  EXPECT_EQ(call_expr.args()[1]
                .call_expr()
                .args()[1]
                .call_expr()
                .target()
                .kind_case(),
            ExprKindCase::kUnspecifiedExpr);
}

TEST(OptimizerExprFactory, CopyMultipleAstsWithConsumeRenumbers) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> compiler,
                       CreateTestCompiler());

  ASSERT_OK_AND_ASSIGN(auto ast1_result, compiler->Compile("[1]"));
  ASSERT_TRUE(ast1_result.IsValid());
  ASSERT_OK_AND_ASSIGN(auto ast1, ast1_result.ReleaseAst());

  ASSERT_OK_AND_ASSIGN(auto ast2_result, compiler->Compile("2"));
  ASSERT_TRUE(ast2_result.IsValid());
  ASSERT_OK_AND_ASSIGN(auto ast2, ast2_result.ReleaseAst());

  TestOptimizerExprFactory factory{Ast()};

  Expr copied1 = factory.Copy(ast1->root_expr());
  auto renumbers1 = factory.ConsumeRenumbers();

  Expr copied2 = factory.Copy(ast2->root_expr());
  auto renumbers2 = factory.ConsumeRenumbers();

  EXPECT_EQ(renumbers1.size(), 2);
  EXPECT_EQ(renumbers2.size(), 1);

  EXPECT_NE(copied1.id(), copied2.id());
  EXPECT_GT(copied2.id(), copied1.id());
}

TEST(OptimizerExprFactory, MaxIdVisitorExprKinds) {
  ASSERT_OK_AND_ASSIGN(auto compiler, CreateTestCompiler());

  // Expression that covers all the kinds.
  ASSERT_OK_AND_ASSIGN(auto source, NewSource(R"cel(
                       Struct{field : 1} ||
                       {'key' : 'value'} || [1].exists(x, x) || foo(bar))cel"));
  ASSERT_OK_AND_ASSIGN(auto ast, compiler->GetParser().Parse(*source));

  TestOptimizerExprFactory factory{std::move(*ast)};

  EXPECT_EQ(factory.NextId(), 26);
}

TEST(OptimizerExprFactory, CopyListElement) {
  TestOptimizerExprFactory factory{Ast()};
  ListExprElement orig = factory.NewListElement(factory.NewIdent("foo"));
  ListExprElement copied = factory.Copy(orig);
  EXPECT_EQ(copied.expr(), factory.NewIdent(2, "foo"));
}

TEST(OptimizerExprFactory, CopyStructField) {
  TestOptimizerExprFactory factory{Ast()};
  StructExprField orig = factory.NewStructField("bar", factory.NewIdent("baz"));
  StructExprField copied = factory.Copy(orig);
  EXPECT_EQ(copied.id(), 3);
  EXPECT_EQ(copied.name(), "bar");
  EXPECT_EQ(copied.value(), factory.NewIdent(4, "baz"));
}

TEST(OptimizerExprFactory, CopyMapEntry) {
  TestOptimizerExprFactory factory{Ast()};
  MapExprEntry orig =
      factory.NewMapEntry(factory.NewIdent("bar"), factory.NewIdent("baz"));
  MapExprEntry copied = factory.Copy(orig);
  EXPECT_EQ(copied.id(), 4);
  EXPECT_EQ(copied.key(), factory.NewIdent(5, "bar"));
  EXPECT_EQ(copied.value(), factory.NewIdent(6, "baz"));
}

TEST(OptimizerExprFactory, MergeSourceInfoMacroConflict) {
  SourceInfo base_info;
  base_info.mutable_macro_calls()[1] = Expr();

  Ast base_ast(Expr(), std::move(base_info));
  TestOptimizerExprFactory factory{std::move(base_ast)};

  SourceInfo new_info;
  new_info.mutable_macro_calls()[1] = Expr();

  factory.MergeSourceInfo(new_info);

  ASSERT_EQ(factory.issues().size(), 1);
  EXPECT_EQ(factory.issues()[0].location, 1);
  EXPECT_EQ(factory.issues()[0].message, "conflicting ID in macro calls merge");
}

}  // namespace
}  // namespace cel
