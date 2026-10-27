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

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "checker/type_checker_builder.h"
#include "checker/validation_result.h"
#include "common/ast.h"
#include "common/decl.h"
#include "common/legacy_value.h"
#include "common/minimal_descriptor_pool.h"
#include "common/type.h"
#include "common/value.h"
#include "compiler/compiler.h"
#include "compiler/compiler_factory.h"
#include "compiler/standard_library.h"
#include "eval/public/cel_value.h"
#include "eval/public/containers/container_backed_list_impl.h"
#include "extensions/set_membership_optimization.h"
#include "extensions/sets_functions.h"
#include "internal/benchmark.h"
#include "internal/status_macros.h"
#include "internal/testing.h"
#include "runtime/activation.h"
#include "runtime/constant_folding.h"
#include "runtime/reference_resolver.h"
#include "runtime/runtime.h"
#include "runtime/runtime_builder.h"
#include "runtime/runtime_options.h"
#include "runtime/standard_runtime_builder_factory.h"
#include "google/protobuf/arena.h"

namespace cel::extensions {
namespace {

using ::absl_testing::IsOk;
using ::google::api::expr::runtime::CelList;
using ::google::api::expr::runtime::CelValue;
using ::google::api::expr::runtime::ContainerBackedListImpl;

enum class ListImpl : int {
  kLegacy = 0,
  kModern = 1,
  kRhsConstant = 2,
  kRhsConstantOptimized = 3,
};
int ToNumber(ListImpl impl) { return static_cast<int>(impl); }
ListImpl FromNumber(int number) {
  switch (number) {
    case 0:
      return ListImpl::kLegacy;
    case 1:
      return ListImpl::kModern;
    case 2:
      return ListImpl::kRhsConstant;
    case 3:
      return ListImpl::kRhsConstantOptimized;
    default:
      return ListImpl::kLegacy;
  }
}

struct TestCase {
  std::string test_name;
  std::string expr;
  ListImpl list_impl;
  int size;
  bool result;

  std::string MakeLabel(int len) const {
    std::string list_impl;
    switch (this->list_impl) {
      case ListImpl::kRhsConstantOptimized:
        list_impl = "rhs_constant_opt";
        break;
      case ListImpl::kRhsConstant:
        list_impl = "rhs_constant";
        break;
      case ListImpl::kModern:
        list_impl = "modern";
        break;
      case ListImpl::kLegacy:
        list_impl = "legacy";
        break;
    }

    return absl::StrCat(test_name, "/", list_impl, "/", len);
  }
};

absl::Status RegisterLegacyLists(bool overlap, int len,
                                 google::protobuf::Arena* absl_nonnull arena,
                                 Activation& activation) {
  std::vector<CelValue> x;
  std::vector<CelValue> y;
  x.reserve(len + 1);
  y.reserve(len + 1);
  if (overlap) {
    x.push_back(CelValue::CreateInt64(2));
    y.push_back(CelValue::CreateInt64(1));
  }

  for (int i = 0; i < len; i++) {
    x.push_back(CelValue::CreateInt64(1));
    y.push_back(CelValue::CreateInt64(2));
  }

  const CelList* x_list =
      google::protobuf::Arena::Create<ContainerBackedListImpl>(arena, std::move(x));
  const CelList* y_list =
      google::protobuf::Arena::Create<ContainerBackedListImpl>(arena, std::move(y));

  CEL_ASSIGN_OR_RETURN(Value x_val,
                       cel::ModernValue(arena, CelValue::CreateList(x_list)));
  CEL_ASSIGN_OR_RETURN(Value y_val,
                       cel::ModernValue(arena, CelValue::CreateList(y_list)));

  activation.InsertOrAssignValue("x", std::move(x_val));
  activation.InsertOrAssignValue("y", std::move(y_val));
  return absl::OkStatus();
}

// Constant list literal that has the same elements as the bound test cases.
std::string ConstantList(bool overlap, int len) {
  return absl::StrCat("[", overlap ? "1, " : "",
                      absl::StrJoin(std::vector<std::string>(len, "2"), ", "),
                      "]");
}

absl::Status RegisterModernLists(bool overlap, int len,
                                 google::protobuf::Arena* absl_nonnull arena,
                                 Activation& activation) {
  ListValueBuilderPtr x_builder = cel::NewListValueBuilder(arena);
  ListValueBuilderPtr y_builder = cel::NewListValueBuilder(arena);

  x_builder->Reserve(len + 1);
  y_builder->Reserve(len + 1);

  if (overlap) {
    CEL_RETURN_IF_ERROR(x_builder->Add(cel::IntValue(2)));
    CEL_RETURN_IF_ERROR(y_builder->Add(cel::IntValue(1)));
  }

  for (int i = 0; i < len; i++) {
    CEL_RETURN_IF_ERROR(x_builder->Add(cel::IntValue(1)));
    CEL_RETURN_IF_ERROR(y_builder->Add(cel::IntValue(2)));
  }

  activation.InsertOrAssignValue("x", std::move(*x_builder).Build());
  activation.InsertOrAssignValue("y", std::move(*y_builder).Build());

  return absl::OkStatus();
}

absl::Status RegisterLists(bool overlap, int len, bool use_modern,
                           google::protobuf::Arena* absl_nonnull arena,
                           Activation& activation) {
  if (use_modern) {
    return RegisterModernLists(overlap, len, arena, activation);
  } else {
    return RegisterLegacyLists(overlap, len, arena, activation);
  }
}

absl::StatusOr<std::unique_ptr<Compiler>> CreateBenchmarkCompiler() {
  CEL_ASSIGN_OR_RETURN(std::unique_ptr<CompilerBuilder> builder,
                       NewCompilerBuilder(cel::GetMinimalDescriptorPool()));
  CEL_RETURN_IF_ERROR(builder->AddLibrary(StandardCompilerLibrary()));
  CEL_RETURN_IF_ERROR(builder->AddLibrary(SetsCompilerLibrary()));

  TypeCheckerBuilder& checker = builder->GetCheckerBuilder();
  CEL_RETURN_IF_ERROR(checker.AddVariable(
      MakeVariableDecl("x", ListType(checker.arena(), IntType()))));
  CEL_RETURN_IF_ERROR(checker.AddVariable(
      MakeVariableDecl("y", ListType(checker.arena(), IntType()))));
  CEL_RETURN_IF_ERROR(checker.AddVariable(
      MakeVariableDecl("str_x", ListType(checker.arena(), StringType()))));
  CEL_RETURN_IF_ERROR(checker.AddVariable(
      MakeVariableDecl("str_y", ListType(checker.arena(), StringType()))));
  CEL_RETURN_IF_ERROR(
      checker.AddVariable(MakeVariableDecl("str_elem", StringType())));

  return builder->Build();
}

absl::StatusOr<std::unique_ptr<const Runtime>> CreateBenchmarkRuntime(
    bool enable_set_opt, google::protobuf::Arena* absl_nonnull arena) {
  RuntimeOptions options;
  CEL_ASSIGN_OR_RETURN(
      RuntimeBuilder builder,
      CreateStandardRuntimeBuilder(cel::GetMinimalDescriptorPool(), options));
  CEL_RETURN_IF_ERROR(
      RegisterSetsFunctions(builder.function_registry(), options));
  CEL_RETURN_IF_ERROR(
      EnableReferenceResolver(builder, ReferenceResolverEnabled::kAlways));
  CEL_RETURN_IF_ERROR(EnableConstantFolding(builder, arena));
  if (enable_set_opt) {
    CEL_RETURN_IF_ERROR(EnableSetMembershipOptimization(builder));
  }
  return std::move(builder).Build();
}

void RunBenchmark(const TestCase& test_case, benchmark::State& state) {
  bool lists_overlap = test_case.result;

  std::string expr = test_case.expr;
  if (test_case.list_impl == ListImpl::kRhsConstant ||
      test_case.list_impl == ListImpl::kRhsConstantOptimized) {
    expr = absl::StrReplaceAll(
        expr, {{"y", ConstantList(lists_overlap, test_case.size)}});
  }

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> compiler,
                       CreateBenchmarkCompiler());
  ASSERT_OK_AND_ASSIGN(ValidationResult validation_result,
                       compiler->Compile(expr));
  ASSERT_TRUE(validation_result.IsValid()) << validation_result.FormatError();
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Ast> ast,
                       validation_result.ReleaseAst());

  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const Runtime> runtime,
      CreateBenchmarkRuntime(
          test_case.list_impl == ListImpl::kRhsConstantOptimized, &arena));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Program> program,
                       runtime->CreateProgram(std::move(ast)));

  Activation activation;
  ASSERT_THAT(RegisterLists(test_case.result, test_case.size,
                            test_case.list_impl != ListImpl::kLegacy, &arena,
                            activation),
              IsOk());

  state.SetLabel(test_case.MakeLabel(test_case.size));
  for (auto _ : state) {
    ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));
    ASSERT_TRUE(result.IsBool());
    ASSERT_EQ(result.GetBool().NativeValue(), test_case.result)
        << test_case.test_name;
  }
}

void BM_SetsContainsTrue(benchmark::State& state) {
  ListImpl impl = FromNumber(state.range(0));
  int size = state.range(1);

  RunBenchmark({"sets.contains_true", "sets.contains(x, y)", impl, size, true},
               state);
}

void BM_SetsContainsFalse(benchmark::State& state) {
  ListImpl impl = FromNumber(state.range(0));
  int size = state.range(1);

  RunBenchmark(
      {"sets.contains_false", "sets.contains(x, y)", impl, size, false}, state);
}

void BM_SetsContainsComprehensionTrue(benchmark::State& state) {
  ListImpl impl = FromNumber(state.range(0));
  int size = state.range(1);

  RunBenchmark(
      {"comprehension_contains_true", "x.all(i, i in y)", impl, size, true},
      state);
}

void BM_SetsContainsComprehensionFalse(benchmark::State& state) {
  ListImpl impl = FromNumber(state.range(0));
  int size = state.range(1);

  RunBenchmark(
      {"comprehension_contains_false", "x.all(i, i in y)", impl, size, false},
      state);
}

void BM_SetsIntersectsTrue(benchmark::State& state) {
  ListImpl impl = FromNumber(state.range(0));
  int size = state.range(1);

  RunBenchmark(
      {"sets.intersects_true", "sets.intersects(x, y)", impl, size, true},
      state);
}

void BM_SetsIntersectsFalse(benchmark::State& state) {
  ListImpl impl = FromNumber(state.range(0));
  int size = state.range(1);

  RunBenchmark(
      {"sets.intersects_false", "sets.intersects(x, y)", impl, size, false},
      state);
}

void BM_SetsIntersectsComprehensionTrue(benchmark::State& state) {
  ListImpl impl = FromNumber(state.range(0));
  int size = state.range(1);

  RunBenchmark({"comprehension_intersects_true", "x.exists(i, i in y)", impl,
                size, true},
               state);
}

void BM_SetsIntersectsComprehensionFalse(benchmark::State& state) {
  ListImpl impl = FromNumber(state.range(0));
  int size = state.range(1);

  RunBenchmark({"comprehension_intersects_false", "x.exists(i, i in y)", impl,
                size, false},
               state);
}

void BM_SetsEquivalentTrue(benchmark::State& state) {
  ListImpl impl = FromNumber(state.range(0));
  int size = state.range(1);

  RunBenchmark(
      {"sets.equivalent_true", "sets.equivalent(x, y)", impl, size, true},
      state);
}

void BM_SetsEquivalentFalse(benchmark::State& state) {
  ListImpl impl = FromNumber(state.range(0));
  int size = state.range(1);

  RunBenchmark(
      {"sets.equivalent_false", "sets.equivalent(x, y)", impl, size, false},
      state);
}

void BM_SetsEquivalentComprehensionTrue(benchmark::State& state) {
  ListImpl impl = FromNumber(state.range(0));
  int size = state.range(1);

  RunBenchmark({"comprehension_equivalent_true",
                "x.all(i, i in y) && y.all(j, j in x)", impl, size, true},
               state);
}

void BM_SetsEquivalentComprehensionFalse(benchmark::State& state) {
  ListImpl impl = FromNumber(state.range(0));
  int size = state.range(1);

  RunBenchmark({"comprehension_equivalent_false",
                "x.all(i, i in y) && y.all(j, j in x)", impl, size, false},
               state);
}

template <typename Benchmark>
void BenchArgs(Benchmark* bench) {
  for (ListImpl impl :
       {ListImpl::kLegacy, ListImpl::kModern, ListImpl::kRhsConstant,
        ListImpl::kRhsConstantOptimized}) {
    for (int size : {1, 8, 32, 64, 256}) {
      bench->ArgPair(ToNumber(impl), size);
    }
  }
}

BENCHMARK(BM_SetsContainsComprehensionTrue)->Apply(BenchArgs);
BENCHMARK(BM_SetsContainsComprehensionFalse)->Apply(BenchArgs);
BENCHMARK(BM_SetsContainsTrue)->Apply(BenchArgs);
BENCHMARK(BM_SetsContainsFalse)->Apply(BenchArgs);

BENCHMARK(BM_SetsIntersectsComprehensionTrue)->Apply(BenchArgs);
BENCHMARK(BM_SetsIntersectsComprehensionFalse)->Apply(BenchArgs);
BENCHMARK(BM_SetsIntersectsTrue)->Apply(BenchArgs);
BENCHMARK(BM_SetsIntersectsFalse)->Apply(BenchArgs);

BENCHMARK(BM_SetsEquivalentComprehensionTrue)->Apply(BenchArgs);
BENCHMARK(BM_SetsEquivalentComprehensionFalse)->Apply(BenchArgs);
BENCHMARK(BM_SetsEquivalentTrue)->Apply(BenchArgs);
BENCHMARK(BM_SetsEquivalentFalse)->Apply(BenchArgs);

enum class StringSetBenchOp {
  kIn,
  kIntersects,
  kContains,
  kEquivalent,
};

std::string ConstantStringList(int len) {
  std::vector<std::string> elems;
  elems.reserve(len);
  for (int i = 0; i < len; ++i) {
    elems.push_back(absl::StrCat("'str_", i, "'"));
  }
  return absl::StrCat("[", absl::StrJoin(elems, ", "), "]");
}

absl::Status RegisterStringBenchValues(StringSetBenchOp op, bool expected,
                                       int len,
                                       google::protobuf::Arena* absl_nonnull arena,
                                       Activation& activation) {
  ListValueBuilderPtr y_builder = cel::NewListValueBuilder(arena);
  y_builder->Reserve(len);
  for (int i = 0; i < len; ++i) {
    CEL_RETURN_IF_ERROR(
        y_builder->Add(cel::StringValue::From(absl::StrCat("str_", i), arena)));
  }
  activation.InsertOrAssignValue("str_y", std::move(*y_builder).Build());

  if (op == StringSetBenchOp::kIn) {
    // True: best case / short-circuit (matches first element).
    // False: worst case (scans all elements).
    std::string elem = expected ? "str_0" : "str_missing";
    activation.InsertOrAssignValue("str_elem",
                                   cel::StringValue::From(elem, arena));
    return absl::OkStatus();
  }

  ListValueBuilderPtr x_builder = cel::NewListValueBuilder(arena);
  x_builder->Reserve(len);
  if (op == StringSetBenchOp::kIntersects) {
    // True: best case / short-circuit (matches on the first element).
    // False: worst case (disjoint lists, scans all elements).
    std::string first = expected ? "str_0" : "other_0";
    CEL_RETURN_IF_ERROR(x_builder->Add(cel::StringValue::From(first, arena)));
    for (int i = 1; i < len; ++i) {
      CEL_RETURN_IF_ERROR(x_builder->Add(
          cel::StringValue::From(absl::StrCat("other_", i), arena)));
    }
  } else {
    // For contains and equivalent:
    // True: worst case (str_x has all elements in reverse order, exercising the
    // full loop and matched set allocation).
    // False: best case / short-circuit (disjoint elements starting with
    // "other_0", short-circuiting on the first element for equivalent and
    // avoiding matched set allocation for contains).
    if (expected) {
      for (int i = len - 1; i >= 0; --i) {
        CEL_RETURN_IF_ERROR(x_builder->Add(
            cel::StringValue::From(absl::StrCat("str_", i), arena)));
      }
    } else {
      for (int i = 0; i < len; ++i) {
        CEL_RETURN_IF_ERROR(x_builder->Add(
            cel::StringValue::From(absl::StrCat("other_", i), arena)));
      }
    }
  }
  activation.InsertOrAssignValue("str_x", std::move(*x_builder).Build());
  return absl::OkStatus();
}

void RunStringSetBenchmark(StringSetBenchOp op, const TestCase& test_case,
                           benchmark::State& state) {
  std::string expr = test_case.expr;
  if (test_case.list_impl == ListImpl::kRhsConstant ||
      test_case.list_impl == ListImpl::kRhsConstantOptimized) {
    expr = absl::StrReplaceAll(expr,
                               {{"str_y", ConstantStringList(test_case.size)}});
  }

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Compiler> compiler,
                       CreateBenchmarkCompiler());
  ASSERT_OK_AND_ASSIGN(ValidationResult validation_result,
                       compiler->Compile(expr));
  ASSERT_TRUE(validation_result.IsValid()) << validation_result.FormatError();
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Ast> ast,
                       validation_result.ReleaseAst());

  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const Runtime> runtime,
      CreateBenchmarkRuntime(
          test_case.list_impl == ListImpl::kRhsConstantOptimized, &arena));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Program> program,
                       runtime->CreateProgram(std::move(ast)));

  Activation activation;
  ASSERT_THAT(RegisterStringBenchValues(op, test_case.result, test_case.size,
                                        &arena, activation),
              IsOk());

  state.SetLabel(test_case.MakeLabel(test_case.size));
  for (auto _ : state) {
    ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));
    ASSERT_TRUE(result.IsBool());
    ASSERT_EQ(result.GetBool().NativeValue(), test_case.result)
        << test_case.test_name;
  }
}

void BM_StringSetsInTrue(benchmark::State& state) {
  ListImpl impl = FromNumber(state.range(0));
  int size = state.range(1);
  RunStringSetBenchmark(StringSetBenchOp::kIn,
                        {"str_in_true", "str_elem in str_y", impl, size, true},
                        state);
}

void BM_StringSetsInFalse(benchmark::State& state) {
  ListImpl impl = FromNumber(state.range(0));
  int size = state.range(1);
  RunStringSetBenchmark(
      StringSetBenchOp::kIn,
      {"str_in_false", "str_elem in str_y", impl, size, false}, state);
}

void BM_StringSetsIntersectsTrue(benchmark::State& state) {
  ListImpl impl = FromNumber(state.range(0));
  int size = state.range(1);
  RunStringSetBenchmark(StringSetBenchOp::kIntersects,
                        {"str_sets.intersects_true",
                         "sets.intersects(str_x, str_y)", impl, size, true},
                        state);
}

void BM_StringSetsIntersectsFalse(benchmark::State& state) {
  ListImpl impl = FromNumber(state.range(0));
  int size = state.range(1);
  RunStringSetBenchmark(StringSetBenchOp::kIntersects,
                        {"str_sets.intersects_false",
                         "sets.intersects(str_x, str_y)", impl, size, false},
                        state);
}

void BM_StringSetsContainsTrue(benchmark::State& state) {
  ListImpl impl = FromNumber(state.range(0));
  int size = state.range(1);
  RunStringSetBenchmark(StringSetBenchOp::kContains,
                        {"str_sets.contains_true",
                         "sets.contains(str_x, str_y)", impl, size, true},
                        state);
}

void BM_StringSetsContainsFalse(benchmark::State& state) {
  ListImpl impl = FromNumber(state.range(0));
  int size = state.range(1);
  RunStringSetBenchmark(StringSetBenchOp::kContains,
                        {"str_sets.contains_false",
                         "sets.contains(str_x, str_y)", impl, size, false},
                        state);
}

void BM_StringSetsEquivalentTrue(benchmark::State& state) {
  ListImpl impl = FromNumber(state.range(0));
  int size = state.range(1);
  RunStringSetBenchmark(StringSetBenchOp::kEquivalent,
                        {"str_sets.equivalent_true",
                         "sets.equivalent(str_x, str_y)", impl, size, true},
                        state);
}

void BM_StringSetsEquivalentFalse(benchmark::State& state) {
  ListImpl impl = FromNumber(state.range(0));
  int size = state.range(1);
  RunStringSetBenchmark(StringSetBenchOp::kEquivalent,
                        {"str_sets.equivalent_false",
                         "sets.equivalent(str_x, str_y)", impl, size, false},
                        state);
}

template <typename Benchmark>
void StringBenchArgs(Benchmark* bench) {
  for (ListImpl impl :
       {ListImpl::kRhsConstant, ListImpl::kRhsConstantOptimized}) {
    for (int size : {1, 4, 8, 16, 32, 64, 256}) {
      bench->ArgPair(ToNumber(impl), size);
    }
  }
}

BENCHMARK(BM_StringSetsInTrue)->Apply(StringBenchArgs);
BENCHMARK(BM_StringSetsInFalse)->Apply(StringBenchArgs);
BENCHMARK(BM_StringSetsIntersectsTrue)->Apply(StringBenchArgs);
BENCHMARK(BM_StringSetsIntersectsFalse)->Apply(StringBenchArgs);
BENCHMARK(BM_StringSetsContainsTrue)->Apply(StringBenchArgs);
BENCHMARK(BM_StringSetsContainsFalse)->Apply(StringBenchArgs);
BENCHMARK(BM_StringSetsEquivalentTrue)->Apply(StringBenchArgs);
BENCHMARK(BM_StringSetsEquivalentFalse)->Apply(StringBenchArgs);

}  // namespace
}  // namespace cel::extensions
