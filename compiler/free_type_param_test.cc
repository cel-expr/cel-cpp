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

// Tests for type inference of expressions with free (unconstrained) type
// parameters, in particular when they are bound to a lazily evaluated
// variable (cel.bind, cel.@block).

#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "checker/type_checker_builder.h"
#include "checker/validation_result.h"
#include "common/ast.h"
#include "common/decl.h"
#include "common/expr.h"
#include "common/navigable_ast.h"
#include "common/type.h"
#include "compiler/compiler.h"
#include "compiler/compiler_factory.h"
#include "compiler/optional.h"
#include "compiler/standard_library.h"
#include "extensions/bindings_ext.h"
#include "internal/status_macros.h"
#include "internal/testing.h"
#include "internal/testing_descriptor_pool.h"
#include "testutil/test_macros.h"
#include "google/protobuf/arena.h"

namespace cel {
namespace {

using ::absl_testing::IsOk;

// Formats a type recursively, e.g. list(list(dyn)).
//
// Leaf types are formatted with their name so that special placeholder types
// are distinguishable from dyn.
std::string DescribeType(const Type& type) {
  TypeParameters params = type.GetParameters();
  if (params.empty()) {
    return std::string(type.name());
  }
  std::vector<std::string> parts;
  for (const Type& param : params) {
    parts.push_back(DescribeType(param));
  }
  return absl::StrCat(type.name(), "(", absl::StrJoin(parts, ", "), ")");
}

// Collects the ids of empty list literals in pre-order.
std::vector<ExprId> CollectEmptyLists(const Expr& expr) {
  std::vector<ExprId> ids;
  NavigableAst navigable_ast = NavigableAst::Build(expr);
  for (const NavigableAstNode& node :
       navigable_ast.Root().DescendantsPreorder()) {
    if (node.expr()->has_list_expr() &&
        node.expr()->list_expr().elements().empty()) {
      ids.push_back(node.expr()->id());
    }
  }
  return ids;
}

absl::StatusOr<std::unique_ptr<Compiler>> NewTestCompiler(
    CompilerOptions options = {}) {
  CEL_ASSIGN_OR_RETURN(
      std::unique_ptr<CompilerBuilder> builder,
      NewCompilerBuilder(cel::internal::GetSharedTestingDescriptorPool(),
                         options));
  CEL_RETURN_IF_ERROR(builder->AddLibrary(StandardCompilerLibrary()));
  CEL_RETURN_IF_ERROR(builder->AddLibrary(OptionalCompilerLibrary()));
  CEL_RETURN_IF_ERROR(
      builder->AddLibrary(extensions::BindingsCompilerLibrary()));
  CEL_RETURN_IF_ERROR(
      builder->GetParserBuilder().AddLibrary(test::TestMacrosParserLibrary()));
  return builder->Build();
}

CompilerOptions PolymorphicBindOptions() {
  CompilerOptions options;
  options.checker_options.use_polymorphic_bind = true;
  return options;
}

struct FreeTypeParamTestCase {
  std::string expr;
  // Expected DescribeType of the root resolved type, or empty if the
  // expression is expected to fail type checking.
  std::string expected_type;
  // If non-empty, the expected DescribeType of each empty list literal in the
  // checked AST (pre-order), joined with ", ".
  std::string expected_empty_list_types = "";
};

void PrintTo(const FreeTypeParamTestCase& test_case, std::ostream* os) {
  *os << "{expr: \"" << test_case.expr << "\", expected_type: \""
      << (test_case.expected_type.empty() ? "<error>" : test_case.expected_type)
      << "\"";
  if (!test_case.expected_empty_list_types.empty()) {
    *os << ", expected_empty_list_types: \""
        << test_case.expected_empty_list_types << "\"";
  }
  *os << "}";
}

// Checks `test_case.expr` with the given options and compares the root type
// (and optionally the empty list literal types) with the expectations.
void CheckFreeTypeParamCase(const FreeTypeParamTestCase& test_case,
                            const CompilerOptions& options) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> compiler,
                       NewTestCompiler(options));
  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(ValidationResult result,
                       compiler->Compile(test_case.expr, "<input>", &arena));

  if (test_case.expected_type.empty()) {
    EXPECT_FALSE(result.IsValid());
    return;
  }
  ASSERT_TRUE(result.IsValid()) << result.FormatError();
  const Ast* ast = result.GetAst();
  ASSERT_NE(ast, nullptr);
  const auto& resolved = result.GetResolvedTypeMap();
  auto it = resolved.find(ast->root_expr().id());
  ASSERT_TRUE(it != resolved.end());
  EXPECT_EQ(DescribeType(it->second), test_case.expected_type);

  if (!test_case.expected_empty_list_types.empty()) {
    std::vector<std::string> types;
    for (ExprId id : CollectEmptyLists(ast->root_expr())) {
      auto type_it = resolved.find(id);
      types.push_back(type_it == resolved.end()
                          ? "<missing>"
                          : DescribeType(type_it->second));
    }
    EXPECT_EQ(absl::StrJoin(types, ", "), test_case.expected_empty_list_types);
  }
}

// With CheckerOptions::use_polymorphic_bind.
class FreeTypeParamTest : public testing::TestWithParam<FreeTypeParamTestCase> {
};

TEST_P(FreeTypeParamTest, Check) {
  CheckFreeTypeParamCase(GetParam(), PolymorphicBindOptions());
}

INSTANTIATE_TEST_SUITE_P(
    FreeTypeParams, FreeTypeParamTest,
    testing::ValuesIn(std::vector<FreeTypeParamTestCase>{
        // References to a bound value with free type params are inferred
        // independently.
        {"cel.bind(x, [], x + [1] == [1] && x + ['a'] == ['a'])", "bool"},
        {"cel.bind(x, {}, x['foo'] == 1 || x['foo'] == '2')", "bool"},
        {"cel.bind(o, optional.none(), "
         "o.orValue(1) == 1 && o.orValue('a') == 'a')",
         "bool"},
        {"cel.block([[]], "
         "cel.index(0) + [1] == [1] && cel.index(0) + ['a'] == ['a'])",
         "bool"},
        {"cel.bind(x, [][0], [x, 1] == [1] && [x, 'a'] == ['a'])", "bool"},
        {"cel.bind(x, {}, x.foo == 1 && x['a'] == 'b')", "bool"},
        // Unconstrained references.
        {"cel.bind(x, [], x)", "list(dyn)"},
        {"cel.bind(x, {}, x)", "map(dyn, dyn)"},
        {"cel.bind(o, optional.none(), o)", "optional_type(dyn)"},
        {"cel.bind(x, [][0], x)", "dyn"},
        {"cel.bind(x, {}, x.foo)", "dyn"},
        // A top-level free param does not widen the surrounding expression.
        {"[cel.bind(x, [][0], x), 1]", "list(int)"},
        {"cel.block([[][0]], [cel.index(0), 1])", "list(int)"},
        // Constrained references.
        {"cel.bind(x, [], x + [1])", "list(int)",
         // #unused iter range, accu init.
         "list(dyn), list(dyn)"},
        {"cel.bind(x, [1], x + [2])", "list(int)"},
        {"cel.bind(x, [], cel.bind(y, x + [1], y + x))", "list(int)"},
        {"optional.none().optMap(v, v).orValue(1)", "int"},
        // Free type params shared with an enclosing variable are not
        // generalized (Hindley-Milner): references to x constrain y.
        {"[[]].map(y, cel.bind(x, y, x + [1] == [1] && x + ['a'] == ['a']))",
         ""},
        {"[[]].map(y, cel.bind(x, y, x + [1]))", "list(list(int))",
         // element of the iter range, map accu init, #unused bind range.
         "list(int), list(list(int)), list(dyn)"},
        // An enclosing binding that is itself generalized doesn't prevent
        // generalization (each reference to x is a fresh instance).
        {"cel.bind(x, [], "
         "cel.bind(y, x, y + [1] == [1] && y + ['a'] == ['a']))",
         "bool"},
        // Non-bind comprehensions are not generalized.
        {"[].map(v, v) + [1]", "list(int)",
         // iter range, accu init.
         "list(int), list(int)"},
        // Free type params without bindings.
        {"([] + ['ab']) + ([1] + [])", ""},
        {"[[], ['ab'], [], [1]]", "list(dyn)"},
        {"[[]] + [[[]]] + [[[[]]]]", "list(list(list(list(dyn))))"},
        {"[]", "list(dyn)"},
        {"{}", "map(dyn, dyn)"},
        {"cel.bind(x, [[]] + [[[]]] + [[[[]]]], x)",
         "list(list(list(list(dyn))))"},
    }));

// Without CheckerOptions::use_polymorphic_bind (the default), bindings are
// not generalized: references share the bound value's type parameters, and
// block indexes are finalized (free type params become dyn).
class PolymorphicBindDisabledTest
    : public testing::TestWithParam<FreeTypeParamTestCase> {};

TEST_P(PolymorphicBindDisabledTest, Check) {
  CheckFreeTypeParamCase(GetParam(), CompilerOptions());
}

INSTANTIATE_TEST_SUITE_P(
    PolymorphicBindDisabled, PolymorphicBindDisabledTest,
    testing::ValuesIn(std::vector<FreeTypeParamTestCase>{
        // The first reference binds the shared type param.
        {"cel.bind(x, [], x + [1] == [1] && x + ['a'] == ['a'])", ""},
        {"cel.bind(x, {}, x['foo'] == 1 || x['foo'] == '2')", ""},
        // The block index is finalized to list(dyn), which widens.
        {"cel.block([[][0]], [cel.index(0), 1])", "list(dyn)"},
        // The init node is narrowed by the (shared) reference.
        {"cel.bind(x, [], x + [1])", "list(int)",
         // #unused iter range, accu init.
         "list(dyn), list(int)"},
    }));

TEST(GenericResultTypeTest, EnabledViaToBuilder) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> compiler, NewTestCompiler());
  std::unique_ptr<CompilerBuilder> builder = compiler->ToBuilder();
  builder->GetCheckerBuilder().mutable_options().use_polymorphic_bind = true;
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> updated, builder->Build());
  EXPECT_TRUE(
      updated->GetTypeChecker().ToBuilder()->options().use_polymorphic_bind);

  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(ValidationResult result,
                       updated->Compile("[]", "<input>", &arena));
  ASSERT_TRUE(result.IsValid()) << result.FormatError();
  ASSERT_TRUE(result.GetGenericResultType().has_value());
  EXPECT_EQ(DescribeType(*result.GetGenericResultType()), "list(T1)");

  // The type inference changes are enabled too.
  ASSERT_OK_AND_ASSIGN(
      result,
      updated->Compile("cel.bind(x, [], x + [1] == [1] && x + ['a'] == ['a'])",
                       "<input>", &arena));
  EXPECT_TRUE(result.IsValid()) << result.FormatError();
}

struct GenericResultTypeTestCase {
  std::string expr;
  // Expected DescribeType of the generic result type.
  std::string expected_generic_type;
  // Expected DescribeType of the root resolved type (unaffected by the
  // option).
  std::string expected_resolved_type;
};

void PrintTo(const GenericResultTypeTestCase& test_case, std::ostream* os) {
  *os << "{expr: \"" << test_case.expr << "\", expected_generic_type: \""
      << test_case.expected_generic_type << "\", expected_resolved_type: \""
      << test_case.expected_resolved_type << "\"}";
}

class GenericResultTypeTest
    : public testing::TestWithParam<GenericResultTypeTestCase> {};

TEST_P(GenericResultTypeTest, Check) {
  const GenericResultTypeTestCase& test_case = GetParam();
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> compiler,
                       NewTestCompiler(PolymorphicBindOptions()));
  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(ValidationResult result,
                       compiler->Compile(test_case.expr, "<input>", &arena));
  ASSERT_TRUE(result.IsValid()) << result.FormatError();
  ASSERT_TRUE(result.GetGenericResultType().has_value());
  EXPECT_EQ(DescribeType(*result.GetGenericResultType()),
            test_case.expected_generic_type);
  EXPECT_EQ(DescribeType(result.GetResolvedTypeMap().at(
                result.GetAst()->root_expr().id())),
            test_case.expected_resolved_type);
}

INSTANTIATE_TEST_SUITE_P(
    GenericResultTypes, GenericResultTypeTest,
    testing::ValuesIn(std::vector<GenericResultTypeTestCase>{
        {"[]", "list(T1)", "list(dyn)"},
        {"{}", "map(T1, T2)", "map(dyn, dyn)"},
        {"optional.none()", "optional_type(T1)", "optional_type(dyn)"},
        {"[[]] + [[[]]]", "list(list(list(T1)))", "list(list(list(dyn)))"},
        {"cel.bind(x, [], x)", "list(T1)", "list(dyn)"},
        {"[][0]", "T1", "dyn"},
        {"[[], {}]", "list(dyn)", "list(dyn)"},
        {"[] + [1]", "list(int)", "list(int)"},
        {"1", "int", "int"},
    }));

TEST(GenericResultTypeTest, NotPopulatedByDefault) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> compiler, NewTestCompiler());
  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(ValidationResult result,
                       compiler->Compile("[]", "<input>", &arena));
  ASSERT_TRUE(result.IsValid()) << result.FormatError();
  EXPECT_FALSE(result.GetGenericResultType().has_value());
}

TEST(GenericResultTypeTest, NotPopulatedWithoutArena) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> compiler,
                       NewTestCompiler(PolymorphicBindOptions()));
  ASSERT_OK_AND_ASSIGN(ValidationResult result, compiler->Compile("[]"));
  ASSERT_TRUE(result.IsValid()) << result.FormatError();
  EXPECT_FALSE(result.GetGenericResultType().has_value());
}

// A variable declared with a generic result type is instantiated
// independently at each reference (e.g. composing policy variables).
TEST(GenericResultTypeTest, DeclareVariableWithGenericResultType) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> compiler,
                       NewTestCompiler(PolymorphicBindOptions()));
  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(ValidationResult var_result,
                       compiler->Compile("{}", "<input>", &arena));
  ASSERT_TRUE(var_result.GetGenericResultType().has_value());

  std::unique_ptr<CompilerBuilder> builder = compiler->ToBuilder();
  // Type parameter name validation is enabled by default.
  ASSERT_TRUE(builder->GetCheckerBuilder()
                  .options()
                  .enable_type_parameter_name_validation);
  ASSERT_THAT(builder->GetCheckerBuilder().AddVariable(
                  MakeVariableDecl("v", *var_result.GetGenericResultType())),
              IsOk());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> composed, builder->Build());

  ASSERT_OK_AND_ASSIGN(
      ValidationResult result,
      composed->Compile("v['foo'] == 1 || v['foo'] == 'a'", "<input>", &arena));
  EXPECT_TRUE(result.IsValid()) << result.FormatError();

  ASSERT_OK_AND_ASSIGN(result,
                       composed->Compile("[v, {'a': 1}]", "<input>", &arena));
  ASSERT_TRUE(result.IsValid()) << result.FormatError();
  EXPECT_EQ(DescribeType(result.GetResolvedTypeMap().at(
                result.GetAst()->root_expr().id())),
            "list(map(string, int))");
}

// Bindings that reference a declared variable with a parameterized type (e.g.
// a stitched policy variable `v: list(A)`).
//
// Each reference to `v` is instantiated with fresh type variables, so the
// declared type params are never shared with a binding: a binding of `v` is
// generalized like a binding of `[]`. Type variables of an instance that flow
// into an enclosing local variable (here, the iteration variable `e`) are part
// of the environment and stay monomorphic.
struct DeclaredGenericVariableTestCase {
  std::string expr;
  // Expected DescribeType of the root resolved type, or empty if the
  // expression is expected to fail type checking.
  std::string expected_type;
};

void PrintTo(const DeclaredGenericVariableTestCase& test_case,
             std::ostream* os) {
  *os << "{expr: \"" << test_case.expr << "\", expected_type: \""
      << (test_case.expected_type.empty() ? "<error>" : test_case.expected_type)
      << "\"}";
}

class DeclaredGenericVariableTest
    : public testing::TestWithParam<DeclaredGenericVariableTestCase> {};

TEST_P(DeclaredGenericVariableTest, Check) {
  const DeclaredGenericVariableTestCase& test_case = GetParam();
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> compiler,
                       NewTestCompiler(PolymorphicBindOptions()));
  std::unique_ptr<CompilerBuilder> builder = compiler->ToBuilder();
  TypeCheckerBuilder& checker_builder = builder->GetCheckerBuilder();
  ASSERT_THAT(checker_builder.AddVariable(MakeVariableDecl(
                  "v", ListType(checker_builder.arena(), TypeParamType("A")))),
              IsOk());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> with_v, builder->Build());

  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(ValidationResult result,
                       with_v->Compile(test_case.expr, "<input>", &arena));
  if (test_case.expected_type.empty()) {
    EXPECT_FALSE(result.IsValid());
    return;
  }
  ASSERT_TRUE(result.IsValid()) << result.FormatError();
  EXPECT_EQ(DescribeType(result.GetResolvedTypeMap().at(
                result.GetAst()->root_expr().id())),
            test_case.expected_type);
}

INSTANTIATE_TEST_SUITE_P(
    DeclaredGenericVariables, DeclaredGenericVariableTest,
    testing::ValuesIn(std::vector<DeclaredGenericVariableTestCase>{
        // References to v are already independent.
        {"v + [1] == [1] && v + ['a'] == ['a']", "bool"},
        // Bindings of v are generalized.
        {"cel.bind(x, v, x + [1] == [1] && x + ['a'] == ['a'])", "bool"},
        {"cel.block([v], "
         "cel.index(0) + [1] == [1] && cel.index(0) + ['a'] == ['a'])",
         "bool"},
        {"cel.bind(x, v, cel.bind(y, x, y + [1] == [1] && y + ['a'] == ['a']))",
         "bool"},
        // The element type of this instance of v is shared with the iteration
        // variable e, so a binding of e is not generalized.
        {"v.map(e, cel.bind(x, e, x + 1))", "list(int)"},
        {"v.map(e, cel.bind(x, e, x == 1 && x == 'a'))", ""},
    }));

}  // namespace
}  // namespace cel
