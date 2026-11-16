// Copyright 2022 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//       https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include <memory>
#include <string>
#include <utility>

#include "cel/expr/syntax.pb.h"
#include "google/rpc/context/attribute_context.pb.h"
#include "absl/status/status.h"
#include "absl/strings/substitute.h"
#include "common/value.h"
#include "eval/tests/request_context.pb.h"
#include "extensions/protobuf/runtime_adapter.h"
#include "internal/benchmark.h"
#include "internal/testing.h"
#include "parser/parser.h"
#include "runtime/activation.h"
#include "runtime/runtime.h"
#include "runtime/runtime_options.h"
#include "runtime/standard_runtime_builder_factory.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"

namespace google::api::expr::runtime {
namespace {

using ::absl_testing::StatusIs;
using ::cel::Activation;
using ::cel::CreateStandardRuntimeBuilder;
using ::cel::ErrorValue;
using ::cel::ListValue;
using ::cel::MapValue;
using ::cel::Runtime;
using ::cel::RuntimeOptions;
using ::cel::StringValue;
using ::cel::StructValue;
using ::cel::Value;
using ::cel::extensions::ProtobufRuntimeAdapter;
using ::cel::expr::ParsedExpr;
using ::google::api::expr::parser::Parse;
using ::testing::HasSubstr;

std::unique_ptr<const Runtime> CreateBenchmarkRuntime() {
  auto builder = CreateStandardRuntimeBuilder(
      google::protobuf::DescriptorPool::generated_pool(), RuntimeOptions{});
  if (!builder.ok()) {
    return nullptr;
  }
  auto runtime = std::move(*builder).Build();
  if (!runtime.ok()) {
    return nullptr;
  }
  return *std::move(runtime);
}

// Evaluates cel expression:
// '"1" + "1" + ...'
static void BM_StrCatLocalArena(benchmark::State& state) {
  std::string expr("'1'");
  int len = state.range(0);
  auto runtime = CreateBenchmarkRuntime();
  ASSERT_NE(runtime, nullptr);

  for (int i = 0; i < len; i++) {
    expr = absl::Substitute("($0 + $0)", expr);
  }

  ASSERT_OK_AND_ASSIGN(ParsedExpr parsed_expr, Parse(expr));
  ASSERT_OK_AND_ASSIGN(auto program, ProtobufRuntimeAdapter::CreateProgram(
                                         *runtime, parsed_expr));

  for (auto _ : state) {
    google::protobuf::Arena arena;
    Activation activation;
    ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));
    ASSERT_TRUE(result.Is<StringValue>());
    ASSERT_EQ(result.Get<StringValue>().Size(), 1 << len);
  }
}
BENCHMARK(BM_StrCatLocalArena)->DenseRange(0, 8, 2);

// Evaluates cel expression:
// '("1" + "1") + ...'
static void BM_StrCatSharedArena(benchmark::State& state) {
  google::protobuf::Arena arena;
  std::string expr("'1'");
  int len = state.range(0);
  auto runtime = CreateBenchmarkRuntime();
  ASSERT_NE(runtime, nullptr);

  for (int i = 0; i < len; i++) {
    expr = absl::Substitute("($0 + $0)", expr);
  }

  ASSERT_OK_AND_ASSIGN(ParsedExpr parsed_expr, Parse(expr));
  ASSERT_OK_AND_ASSIGN(auto program, ProtobufRuntimeAdapter::CreateProgram(
                                         *runtime, parsed_expr));

  for (auto _ : state) {
    Activation activation;
    ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));
    ASSERT_TRUE(result.Is<StringValue>());
    ASSERT_EQ(result.Get<StringValue>().Size(), 1 << len);
  }
}

// Expression grows exponentially.
BENCHMARK(BM_StrCatSharedArena)->DenseRange(0, 8, 2);

// Series of simple expressions that are expected to require an allocation.
static void BM_AllocateString(benchmark::State& state) {
  google::protobuf::Arena arena;
  std::string expr("'1' + '1'");
  auto runtime = CreateBenchmarkRuntime();
  ASSERT_NE(runtime, nullptr);

  ASSERT_OK_AND_ASSIGN(ParsedExpr parsed_expr, Parse(expr));
  ASSERT_OK_AND_ASSIGN(auto program, ProtobufRuntimeAdapter::CreateProgram(
                                         *runtime, parsed_expr));

  for (auto _ : state) {
    Activation activation;
    ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));
    ASSERT_TRUE(result.Is<StringValue>());
    ASSERT_TRUE(result.Get<StringValue>().Equals("11"));
  }
}
BENCHMARK(BM_AllocateString);

static void BM_AllocateError(benchmark::State& state) {
  google::protobuf::Arena arena;
  std::string expr("1 / 0");
  auto runtime = CreateBenchmarkRuntime();
  ASSERT_NE(runtime, nullptr);

  ASSERT_OK_AND_ASSIGN(ParsedExpr parsed_expr, Parse(expr));
  ASSERT_OK_AND_ASSIGN(auto program, ProtobufRuntimeAdapter::CreateProgram(
                                         *runtime, parsed_expr));

  for (auto _ : state) {
    Activation activation;
    ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));
    ASSERT_TRUE(result.Is<ErrorValue>());
    ASSERT_THAT(result.Get<ErrorValue>().NativeValue(),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("divide by zero")));
  }
}
BENCHMARK(BM_AllocateError);

static void BM_AllocateMap(benchmark::State& state) {
  google::protobuf::Arena arena;
  std::string expr("{1: 2, 3: 4}");
  auto runtime = CreateBenchmarkRuntime();
  ASSERT_NE(runtime, nullptr);

  ASSERT_OK_AND_ASSIGN(ParsedExpr parsed_expr, Parse(expr));
  ASSERT_OK_AND_ASSIGN(auto program, ProtobufRuntimeAdapter::CreateProgram(
                                         *runtime, parsed_expr));

  for (auto _ : state) {
    Activation activation;
    ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));
    ASSERT_TRUE(result.Is<MapValue>());
  }
}

BENCHMARK(BM_AllocateMap);

static void BM_AllocateMessage(benchmark::State& state) {
  google::protobuf::Arena arena;
  std::string expr(
      "google.api.expr.runtime.RequestContext{"
      "ip: '192.168.0.1',"
      "path: '/root'}");
  // Make sure RequestContext is loaded in the generated descriptor pool.
  RequestContext context;
  static_cast<void>(context);
  auto runtime = CreateBenchmarkRuntime();
  ASSERT_NE(runtime, nullptr);

  ASSERT_OK_AND_ASSIGN(ParsedExpr parsed_expr, Parse(expr));
  ASSERT_OK_AND_ASSIGN(auto program, ProtobufRuntimeAdapter::CreateProgram(
                                         *runtime, parsed_expr));

  for (auto _ : state) {
    Activation activation;
    ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));
    ASSERT_TRUE(result.Is<StructValue>());
  }
}

BENCHMARK(BM_AllocateMessage);

static void BM_AllocateLargeMessage(benchmark::State& state) {
  // Make sure attribute context is loaded in the generated descriptor pool.
  rpc::context::AttributeContext context;
  static_cast<void>(context);

  google::protobuf::Arena arena;
  std::string expr(R"(
  google.rpc.context.AttributeContext{
      source: google.rpc.context.AttributeContext.Peer{
        ip: '192.168.0.1',
        port: 1025,
        labels: {"abc": "123", "def": "456"}
      },
      request: google.rpc.context.AttributeContext.Request{
        method: 'GET',
        path: 'root',
        host: 'www.example.com'
      },
      resource: google.rpc.context.AttributeContext.Resource{
        labels: {"abc": "123", "def": "456"},
      }
  })");
  auto runtime = CreateBenchmarkRuntime();
  ASSERT_NE(runtime, nullptr);

  ASSERT_OK_AND_ASSIGN(ParsedExpr parsed_expr, Parse(expr));
  ASSERT_OK_AND_ASSIGN(auto program, ProtobufRuntimeAdapter::CreateProgram(
                                         *runtime, parsed_expr));

  for (auto _ : state) {
    Activation activation;
    ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));
    ASSERT_TRUE(result.Is<StructValue>());
  }
}

BENCHMARK(BM_AllocateLargeMessage);

static void BM_AllocateList(benchmark::State& state) {
  google::protobuf::Arena arena;
  std::string expr("[1, 2, 3, 4]");
  auto runtime = CreateBenchmarkRuntime();
  ASSERT_NE(runtime, nullptr);

  ASSERT_OK_AND_ASSIGN(ParsedExpr parsed_expr, Parse(expr));
  ASSERT_OK_AND_ASSIGN(auto program, ProtobufRuntimeAdapter::CreateProgram(
                                         *runtime, parsed_expr));

  for (auto _ : state) {
    Activation activation;
    ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));
    ASSERT_TRUE(result.Is<ListValue>());
  }
}
BENCHMARK(BM_AllocateList);

}  // namespace
}  // namespace google::api::expr::runtime
