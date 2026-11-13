#include "internal/benchmark.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cel/expr/syntax.pb.h"
#include "google/protobuf/struct.pb.h"
#include "google/rpc/context/attribute_context.pb.h"
#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "common/ast_proto.h"
#include "common/decl.h"
#include "common/native_type.h"
#include "common/type.h"
#include "common/value.h"
#include "compiler/compiler.h"
#include "compiler/compiler_factory.h"
#include "compiler/standard_library.h"
#include "eval/tests/request_context.pb.h"
#include "extensions/protobuf/runtime_adapter.h"
#include "internal/status_macros.h"
#include "internal/testing.h"
#include "parser/parser.h"
#include "runtime/activation.h"
#include "runtime/constant_folding.h"
#include "runtime/runtime.h"
#include "runtime/runtime_options.h"
#include "runtime/standard_runtime_builder_factory.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"
#include "google/protobuf/text_format.h"

ABSL_FLAG(bool, enable_optimizations, false, "enable const folding opt");
ABSL_FLAG(bool, enable_typed_field_access, false, "enable typed field access");

namespace google {
namespace api {
namespace expr {
namespace runtime {

namespace {

using ::absl_testing::IsOk;
using ::cel::Activation;
using ::cel::BoolValue;
using ::cel::CreateStandardRuntimeBuilder;
using ::cel::CustomMapValue;
using ::cel::CustomMapValueInterface;
using ::cel::IntValue;
using ::cel::ListValue;
using ::cel::NativeTypeId;
using ::cel::NewListValueBuilder;
using ::cel::NewMapValueBuilder;
using ::cel::Runtime;
using ::cel::RuntimeOptions;
using ::cel::StringValue;
using ::cel::Value;
using ::cel::ValueIteratorPtr;
using ::cel::extensions::EnableConstantFolding;
using ::cel::extensions::ProtobufRuntimeAdapter;
using ::cel::expr::Expr;
using ::cel::expr::ParsedExpr;
using ::cel::expr::SourceInfo;
using ::google::rpc::context::AttributeContext;

RuntimeOptions GetOptions() {
  RuntimeOptions options;

  if (absl::GetFlag(FLAGS_enable_typed_field_access)) {
    options.enable_typed_field_access = true;
  }

  return options;
}

absl::StatusOr<std::unique_ptr<Runtime>> CreateBenchmarkRuntime(
    google::protobuf::Arena& arena, const RuntimeOptions& options = GetOptions(),
    bool force_const_folding = false) {
  CEL_ASSIGN_OR_RETURN(auto builder,
                       CreateStandardRuntimeBuilder(
                           google::protobuf::DescriptorPool::generated_pool(), options));
  if (force_const_folding || absl::GetFlag(FLAGS_enable_optimizations)) {
    CEL_RETURN_IF_ERROR(EnableConstantFolding(
        builder, &arena, google::protobuf::MessageFactory::generated_factory()));
  }
  return std::move(builder).Build();
}

// Benchmark test
// Evaluates cel expression:
// '1 + 1 + 1 .... +1'
static void BM_Eval(benchmark::State& state) {
  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(auto runtime, CreateBenchmarkRuntime(arena));

  int len = state.range(0);

  Expr root_expr;
  Expr* cur_expr = &root_expr;

  for (int i = 0; i < len; i++) {
    Expr::Call* call = cur_expr->mutable_call_expr();
    call->set_function("_+_");
    call->add_args()->mutable_const_expr()->set_int64_value(1);
    cur_expr = call->add_args();
  }

  cur_expr->mutable_const_expr()->set_int64_value(1);

  SourceInfo source_info;
  ASSERT_OK_AND_ASSIGN(auto program, ProtobufRuntimeAdapter::CreateProgram(
                                         *runtime, root_expr, &source_info));

  for (auto _ : state) {
    google::protobuf::Arena eval_arena;
    Activation activation;
    ASSERT_OK_AND_ASSIGN(Value result,
                         program->Evaluate(&eval_arena, activation));
    ASSERT_TRUE(result.Is<IntValue>());
    ASSERT_TRUE(result.Get<IntValue>().NativeValue() == len + 1);
  }
}

BENCHMARK(BM_Eval)->Range(1, 10000);

absl::Status EmptyCallback(int64_t expr_id, const Value&,
                           const google::protobuf::DescriptorPool* absl_nonnull,
                           google::protobuf::MessageFactory* absl_nonnull,
                           google::protobuf::Arena* absl_nonnull) {
  return absl::OkStatus();
}

// Benchmark test
// Traces cel expression with an empty callback:
// '1 + 1 + 1 .... +1'
static void BM_Eval_Trace(benchmark::State& state) {
  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(auto runtime, CreateBenchmarkRuntime(arena));

  int len = state.range(0);

  Expr root_expr;
  Expr* cur_expr = &root_expr;

  for (int i = 0; i < len; i++) {
    Expr::Call* call = cur_expr->mutable_call_expr();
    call->set_function("_+_");
    call->add_args()->mutable_const_expr()->set_int64_value(1);
    cur_expr = call->add_args();
  }

  cur_expr->mutable_const_expr()->set_int64_value(1);

  SourceInfo source_info;
  ASSERT_OK_AND_ASSIGN(auto program, ProtobufRuntimeAdapter::CreateProgram(
                                         *runtime, root_expr, &source_info));

  for (auto _ : state) {
    google::protobuf::Arena eval_arena;
    Activation activation;
    ASSERT_OK_AND_ASSIGN(
        Value result, program->Trace(&eval_arena, activation, EmptyCallback));
    ASSERT_TRUE(result.Is<IntValue>());
    ASSERT_TRUE(result.Get<IntValue>().NativeValue() == len + 1);
  }
}

// A number higher than 10k leads to a stack overflow due to the recursive
// nature of the proto to native type conversion.
BENCHMARK(BM_Eval_Trace)->Range(1, 10000);

// Benchmark test
// Evaluates cel expression:
// '"a" + "a" + "a" .... + "a"'
static void BM_EvalString(benchmark::State& state) {
  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(auto runtime, CreateBenchmarkRuntime(arena));

  int len = state.range(0);

  Expr root_expr;
  Expr* cur_expr = &root_expr;

  for (int i = 0; i < len; i++) {
    Expr::Call* call = cur_expr->mutable_call_expr();
    call->set_function("_+_");
    call->add_args()->mutable_const_expr()->set_string_value("a");
    cur_expr = call->add_args();
  }

  cur_expr->mutable_const_expr()->set_string_value("a");

  SourceInfo source_info;
  ASSERT_OK_AND_ASSIGN(auto program, ProtobufRuntimeAdapter::CreateProgram(
                                         *runtime, root_expr, &source_info));

  for (auto _ : state) {
    google::protobuf::Arena eval_arena;
    Activation activation;
    ASSERT_OK_AND_ASSIGN(Value result,
                         program->Evaluate(&eval_arena, activation));
    ASSERT_TRUE(result.Is<StringValue>());
    ASSERT_TRUE(result.Get<StringValue>().Size() == len + 1);
  }
}

// A number higher than 10k leads to a stack overflow due to the recursive
// nature of the proto to native type conversion.
BENCHMARK(BM_EvalString)->Range(1, 10000);

// Benchmark test
// Traces cel expression with an empty callback:
// '"a" + "a" + "a" .... + "a"'
static void BM_EvalString_Trace(benchmark::State& state) {
  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(auto runtime, CreateBenchmarkRuntime(arena));

  int len = state.range(0);

  Expr root_expr;
  Expr* cur_expr = &root_expr;

  for (int i = 0; i < len; i++) {
    Expr::Call* call = cur_expr->mutable_call_expr();
    call->set_function("_+_");
    call->add_args()->mutable_const_expr()->set_string_value("a");
    cur_expr = call->add_args();
  }

  cur_expr->mutable_const_expr()->set_string_value("a");

  SourceInfo source_info;
  ASSERT_OK_AND_ASSIGN(auto program, ProtobufRuntimeAdapter::CreateProgram(
                                         *runtime, root_expr, &source_info));

  for (auto _ : state) {
    google::protobuf::Arena eval_arena;
    Activation activation;
    ASSERT_OK_AND_ASSIGN(
        Value result, program->Trace(&eval_arena, activation, EmptyCallback));
    ASSERT_TRUE(result.Is<StringValue>());
    ASSERT_TRUE(result.Get<StringValue>().Size() == len + 1);
  }
}

// A number higher than 10k leads to a stack overflow due to the recursive
// nature of the proto to native type conversion.
BENCHMARK(BM_EvalString_Trace)->Range(1, 10000);

const char kIP[] = "10.0.1.2";
const char kPath[] = "/admin/edit";
const char kToken[] = "admin";

ABSL_ATTRIBUTE_NOINLINE
bool NativeCheck(absl::btree_map<std::string, std::string>& attributes,
                 const absl::flat_hash_set<std::string>& denylists,
                 const absl::flat_hash_set<std::string>& allowlists) {
  auto& ip = attributes["ip"];
  auto& path = attributes["path"];
  auto& token = attributes["token"];
  if (denylists.find(ip) != denylists.end()) {
    return false;
  }
  if (absl::StartsWith(path, "v1")) {
    if (token == "v1" || token == "v2" || token == "admin") {
      return true;
    }
  } else if (absl::StartsWith(path, "v2")) {
    if (token == "v2" || token == "admin") {
      return true;
    }
  } else if (absl::StartsWith(path, "/admin")) {
    if (token == "admin") {
      if (allowlists.find(ip) != allowlists.end()) {
        return true;
      }
    }
  }
  return false;
}

void BM_PolicyNative(benchmark::State& state) {
  const auto denylists =
      absl::flat_hash_set<std::string>{"10.0.1.4", "10.0.1.5", "10.0.1.6"};
  const auto allowlists =
      absl::flat_hash_set<std::string>{"10.0.1.1", "10.0.1.2", "10.0.1.3"};
  auto attributes = absl::btree_map<std::string, std::string>{
      {"ip", kIP}, {"token", kToken}, {"path", kPath}};
  for (auto _ : state) {
    auto result = NativeCheck(attributes, denylists, allowlists);
    ASSERT_TRUE(result);
  }
}

BENCHMARK(BM_PolicyNative);

void BM_PolicySymbolic(benchmark::State& state) {
  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(ParsedExpr parsed_expr, parser::Parse(R"cel(
   !(ip in ["10.0.1.4", "10.0.1.5", "10.0.1.6"]) &&
   ((path.startsWith("v1") && token in ["v1", "v2", "admin"]) ||
    (path.startsWith("v2") && token in ["v2", "admin"]) ||
    (path.startsWith("/admin") && token == "admin" && ip in [
       "10.0.1.1",  "10.0.1.2", "10.0.1.3"
    ])
   ))cel"));

  ASSERT_OK_AND_ASSIGN(auto runtime,
                       CreateBenchmarkRuntime(arena, GetOptions(),
                                              /*force_const_folding=*/true));

  ASSERT_OK_AND_ASSIGN(auto program, ProtobufRuntimeAdapter::CreateProgram(
                                         *runtime, parsed_expr));

  Activation activation;
  activation.InsertOrAssignValue("ip", StringValue::WrapUnsafe(kIP));
  activation.InsertOrAssignValue("path", StringValue::WrapUnsafe(kPath));
  activation.InsertOrAssignValue("token", StringValue::WrapUnsafe(kToken));

  for (auto _ : state) {
    ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));
    ASSERT_TRUE(result.Get<BoolValue>().NativeValue());
  }
}

BENCHMARK(BM_PolicySymbolic);

class RequestMap : public CustomMapValueInterface {
 public:
  size_t Size() const override { return 3; }

  absl::Status ListKeys(
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena,
      ListValue* absl_nonnull result) const override {
    return absl::UnimplementedError("ListKeys is not implemented");
  }

  absl::StatusOr<absl_nonnull ValueIteratorPtr> NewIterator() const override {
    return absl::UnimplementedError("NewIterator is not implemented");
  }

  std::string DebugString() const override { return "RequestMap"; }

  absl::Status ConvertToJsonObject(
      const google::protobuf::DescriptorPool* absl_nonnull,
      google::protobuf::MessageFactory* absl_nonnull,
      google::protobuf::Message* absl_nonnull) const override {
    return absl::UnimplementedError("ConvertToJsonObject is not implemented");
  }

  CustomMapValue Clone(google::protobuf::Arena* absl_nonnull arena) const override {
    return CustomMapValue(google::protobuf::Arena::Create<RequestMap>(arena), arena);
  }

 protected:
  absl::StatusOr<bool> Find(
      const Value& key,
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena,
      Value* absl_nonnull result) const override {
    auto string_value = key.As<StringValue>();
    if (!string_value) {
      return false;
    }
    if (string_value->Equals("ip")) {
      *result = StringValue::WrapUnsafe(kIP);
    } else if (string_value->Equals("path")) {
      *result = StringValue::WrapUnsafe(kPath);
    } else if (string_value->Equals("token")) {
      *result = StringValue::WrapUnsafe(kToken);
    } else {
      return false;
    }
    return true;
  }

  absl::StatusOr<bool> Has(
      const Value& key,
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena) const override {
    auto string_value = key.As<StringValue>();
    if (!string_value) {
      return false;
    }
    return string_value->Equals("ip") || string_value->Equals("path") ||
           string_value->Equals("token");
  }

 private:
  NativeTypeId GetNativeTypeId() const override {
    return NativeTypeId::For<RequestMap>();
  }
};

// Uses a lazily constructed map container for "ip", "path", and "token".
void BM_PolicySymbolicMap(benchmark::State& state) {
  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(ParsedExpr parsed_expr, parser::Parse(R"cel(
   !(request.ip in ["10.0.1.4", "10.0.1.5", "10.0.1.6"]) &&
   ((request.path.startsWith("v1") && request.token in ["v1", "v2", "admin"]) ||
    (request.path.startsWith("v2") && request.token in ["v2", "admin"]) ||
    (request.path.startsWith("/admin") && request.token == "admin" &&
     request.ip in ["10.0.1.1",  "10.0.1.2", "10.0.1.3"])
   ))cel"));

  ASSERT_OK_AND_ASSIGN(auto runtime, CreateBenchmarkRuntime(arena));

  ASSERT_OK_AND_ASSIGN(auto program, ProtobufRuntimeAdapter::CreateProgram(
                                         *runtime, parsed_expr));

  Activation activation;
  activation.InsertOrAssignValue(
      "request",
      CustomMapValue(google::protobuf::Arena::Create<RequestMap>(&arena), &arena));

  for (auto _ : state) {
    ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));
    ASSERT_TRUE(result.Get<BoolValue>().NativeValue());
  }
}

BENCHMARK(BM_PolicySymbolicMap);

// Uses a protobuf container for "ip", "path", and "token".
void BM_PolicySymbolicProto(benchmark::State& state) {
  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(
      auto compiler_builder,
      cel::NewCompilerBuilder(google::protobuf::DescriptorPool::generated_pool()));
  ASSERT_THAT(compiler_builder->AddLibrary(cel::StandardCompilerLibrary()),
              IsOk());
  ASSERT_THAT(
      compiler_builder->GetCheckerBuilder().AddVariable(cel::MakeVariableDecl(
          "request", cel::MessageType(RequestContext::descriptor()))),
      IsOk());
  ASSERT_OK_AND_ASSIGN(auto compiler, compiler_builder->Build());

  ASSERT_OK_AND_ASSIGN(auto validation_result, compiler->Compile(R"cel(
   !(request.ip in ["10.0.1.4", "10.0.1.5", "10.0.1.6"]) &&
   ((request.path.startsWith("v1") && request.token in ["v1", "v2", "admin"]) ||
    (request.path.startsWith("v2") && request.token in ["v2", "admin"]) ||
    (request.path.startsWith("/admin") && request.token == "admin" &&
     request.ip in ["10.0.1.1",  "10.0.1.2", "10.0.1.3"])
   ))cel"));
  ASSERT_TRUE(validation_result.IsValid());
  ASSERT_OK_AND_ASSIGN(auto ast, validation_result.ReleaseAst());

  cel::expr::CheckedExpr checked_expr;
  ASSERT_THAT(cel::AstToCheckedExpr(*ast, &checked_expr), IsOk());

  ASSERT_OK_AND_ASSIGN(auto runtime, CreateBenchmarkRuntime(arena));

  ASSERT_OK_AND_ASSIGN(auto program, ProtobufRuntimeAdapter::CreateProgram(
                                         *runtime, checked_expr));

  Activation activation;
  RequestContext request;
  request.set_ip(kIP);
  request.set_path(kPath);
  request.set_token(kToken);
  activation.InsertOrAssignValue(
      "request",
      Value::WrapMessage(&request, google::protobuf::DescriptorPool::generated_pool(),
                         google::protobuf::MessageFactory::generated_factory(), &arena));
  for (auto _ : state) {
    ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));
    ASSERT_TRUE(result.Get<BoolValue>().NativeValue());
  }
}

BENCHMARK(BM_PolicySymbolicProto);

// This expression has no equivalent CEL
constexpr char kListSum[] = R"(
id: 1
comprehension_expr: <
  accu_var: "__result__"
  iter_var: "x"
  iter_range: <
    id: 2
    ident_expr: <
      name: "list_var"
    >
  >
  accu_init: <
    id: 3
    const_expr: <
      int64_value: 0
    >
  >
  loop_step: <
    id: 4
    call_expr: <
      function: "_+_"
      args: <
        id: 5
        ident_expr: <
          name: "__result__"
        >
      >
      args: <
        id: 6
        ident_expr: <
          name: "x"
        >
      >
    >
  >
  loop_condition: <
    id: 7
    const_expr: <
      bool_value: true
    >
  >
  result: <
    id: 8
    ident_expr: <
      name: "__result__"
    >
  >
>)";

void BM_Comprehension(benchmark::State& state) {
  google::protobuf::Arena arena;
  Expr expr;
  Activation activation;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(kListSum, &expr));

  int len = state.range(0);
  auto list_builder = NewListValueBuilder(&arena);
  list_builder->Reserve(len);
  for (int i = 0; i < len; i++) {
    ASSERT_THAT(list_builder->Add(IntValue(1)), IsOk());
  }
  activation.InsertOrAssignValue("list_var", std::move(*list_builder).Build());

  RuntimeOptions options = GetOptions();
  options.comprehension_max_iterations = 10000000;
  ASSERT_OK_AND_ASSIGN(auto runtime, CreateBenchmarkRuntime(arena, options));

  ASSERT_OK_AND_ASSIGN(auto program,
                       ProtobufRuntimeAdapter::CreateProgram(*runtime, expr));
  for (auto _ : state) {
    ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));
    ASSERT_TRUE(result.Is<IntValue>());
    ASSERT_EQ(result.Get<IntValue>().NativeValue(), len);
  }
}

BENCHMARK(BM_Comprehension)->Range(1, 1 << 20);

void BM_Comprehension_Trace(benchmark::State& state) {
  google::protobuf::Arena arena;
  Expr expr;
  Activation activation;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(kListSum, &expr));

  int len = state.range(0);
  auto list_builder = NewListValueBuilder(&arena);
  list_builder->Reserve(len);
  for (int i = 0; i < len; i++) {
    ASSERT_THAT(list_builder->Add(IntValue(1)), IsOk());
  }
  activation.InsertOrAssignValue("list_var", std::move(*list_builder).Build());

  RuntimeOptions options = GetOptions();
  options.comprehension_max_iterations = 10000000;
  ASSERT_OK_AND_ASSIGN(auto runtime, CreateBenchmarkRuntime(arena, options));

  ASSERT_OK_AND_ASSIGN(auto program,
                       ProtobufRuntimeAdapter::CreateProgram(*runtime, expr));
  for (auto _ : state) {
    ASSERT_OK_AND_ASSIGN(Value result,
                         program->Trace(&arena, activation, EmptyCallback));
    ASSERT_TRUE(result.Is<IntValue>());
    ASSERT_EQ(result.Get<IntValue>().NativeValue(), len);
  }
}

BENCHMARK(BM_Comprehension_Trace)->Range(1, 1 << 20);

void BM_HasMap(benchmark::State& state) {
  google::protobuf::Arena arena;
  Activation activation;
  ASSERT_OK_AND_ASSIGN(ParsedExpr parsed_expr,
                       parser::Parse("has(request.path) && !has(request.ip)"));

  ASSERT_OK_AND_ASSIGN(auto runtime, CreateBenchmarkRuntime(arena));

  ASSERT_OK_AND_ASSIGN(auto program, ProtobufRuntimeAdapter::CreateProgram(
                                         *runtime, parsed_expr));

  auto map_builder = NewMapValueBuilder(&arena);
  ASSERT_THAT(map_builder->Put(StringValue::WrapUnsafe("path"),
                               StringValue::WrapUnsafe("path")),
              IsOk());
  activation.InsertOrAssignValue("request", std::move(*map_builder).Build());

  for (auto _ : state) {
    ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));
    ASSERT_TRUE(result.Is<BoolValue>());
    ASSERT_TRUE(result.Get<BoolValue>().NativeValue());
  }
}

BENCHMARK(BM_HasMap);

void BM_HasProto(benchmark::State& state) {
  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(
      auto compiler_builder,
      cel::NewCompilerBuilder(google::protobuf::DescriptorPool::generated_pool()));
  ASSERT_THAT(compiler_builder->AddLibrary(cel::StandardCompilerLibrary()),
              IsOk());
  ASSERT_THAT(
      compiler_builder->GetCheckerBuilder().AddVariable(cel::MakeVariableDecl(
          "request", cel::MessageType(RequestContext::descriptor()))),
      IsOk());
  ASSERT_OK_AND_ASSIGN(auto compiler, compiler_builder->Build());

  ASSERT_OK_AND_ASSIGN(
      auto validation_result,
      compiler->Compile("has(request.path) && !has(request.ip)"));
  ASSERT_TRUE(validation_result.IsValid());
  ASSERT_OK_AND_ASSIGN(auto ast, validation_result.ReleaseAst());

  cel::expr::CheckedExpr checked_expr;
  ASSERT_THAT(cel::AstToCheckedExpr(*ast, &checked_expr), IsOk());

  ASSERT_OK_AND_ASSIGN(auto runtime, CreateBenchmarkRuntime(arena));

  ASSERT_OK_AND_ASSIGN(auto program, ProtobufRuntimeAdapter::CreateProgram(
                                         *runtime, checked_expr));

  Activation activation;
  RequestContext request;
  request.set_path(kPath);
  request.set_token(kToken);
  activation.InsertOrAssignValue(
      "request",
      Value::WrapMessage(&request, google::protobuf::DescriptorPool::generated_pool(),
                         google::protobuf::MessageFactory::generated_factory(), &arena));

  for (auto _ : state) {
    ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));
    ASSERT_TRUE(result.Is<BoolValue>());
    ASSERT_TRUE(result.Get<BoolValue>().NativeValue());
  }
}

BENCHMARK(BM_HasProto);

void BM_HasProtoMap(benchmark::State& state) {
  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(
      auto compiler_builder,
      cel::NewCompilerBuilder(google::protobuf::DescriptorPool::generated_pool()));
  ASSERT_THAT(compiler_builder->AddLibrary(cel::StandardCompilerLibrary()),
              IsOk());
  ASSERT_THAT(
      compiler_builder->GetCheckerBuilder().AddVariable(cel::MakeVariableDecl(
          "request", cel::MessageType(RequestContext::descriptor()))),
      IsOk());
  ASSERT_OK_AND_ASSIGN(auto compiler, compiler_builder->Build());

  ASSERT_OK_AND_ASSIGN(auto validation_result,
                       compiler->Compile("has(request.headers.create_time) && "
                                         "!has(request.headers.update_time)"));
  ASSERT_TRUE(validation_result.IsValid());
  ASSERT_OK_AND_ASSIGN(auto ast, validation_result.ReleaseAst());

  cel::expr::CheckedExpr checked_expr;
  ASSERT_THAT(cel::AstToCheckedExpr(*ast, &checked_expr), IsOk());

  ASSERT_OK_AND_ASSIGN(auto runtime, CreateBenchmarkRuntime(arena));

  ASSERT_OK_AND_ASSIGN(auto program, ProtobufRuntimeAdapter::CreateProgram(
                                         *runtime, checked_expr));

  Activation activation;
  RequestContext request;
  request.mutable_headers()->insert({"create_time", "2021-01-01"});
  activation.InsertOrAssignValue(
      "request",
      Value::WrapMessage(&request, google::protobuf::DescriptorPool::generated_pool(),
                         google::protobuf::MessageFactory::generated_factory(), &arena));

  for (auto _ : state) {
    ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));
    ASSERT_TRUE(result.Is<BoolValue>());
    ASSERT_TRUE(result.Get<BoolValue>().NativeValue());
  }
}

BENCHMARK(BM_HasProtoMap);

void BM_ReadProtoMap(benchmark::State& state) {
  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(
      auto compiler_builder,
      cel::NewCompilerBuilder(google::protobuf::DescriptorPool::generated_pool()));
  ASSERT_THAT(compiler_builder->AddLibrary(cel::StandardCompilerLibrary()),
              IsOk());
  ASSERT_THAT(
      compiler_builder->GetCheckerBuilder().AddVariable(cel::MakeVariableDecl(
          "request", cel::MessageType(RequestContext::descriptor()))),
      IsOk());
  ASSERT_OK_AND_ASSIGN(auto compiler, compiler_builder->Build());

  ASSERT_OK_AND_ASSIGN(auto validation_result, compiler->Compile(R"cel(
     request.headers.create_time == "2021-01-01"
   )cel"));
  ASSERT_TRUE(validation_result.IsValid());
  ASSERT_OK_AND_ASSIGN(auto ast, validation_result.ReleaseAst());

  cel::expr::CheckedExpr checked_expr;
  ASSERT_THAT(cel::AstToCheckedExpr(*ast, &checked_expr), IsOk());

  ASSERT_OK_AND_ASSIGN(auto runtime, CreateBenchmarkRuntime(arena));

  ASSERT_OK_AND_ASSIGN(auto program, ProtobufRuntimeAdapter::CreateProgram(
                                         *runtime, checked_expr));

  Activation activation;
  RequestContext request;
  request.mutable_headers()->insert({"create_time", "2021-01-01"});
  activation.InsertOrAssignValue(
      "request",
      Value::WrapMessage(&request, google::protobuf::DescriptorPool::generated_pool(),
                         google::protobuf::MessageFactory::generated_factory(), &arena));

  for (auto _ : state) {
    ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));
    ASSERT_TRUE(result.Is<BoolValue>());
    ASSERT_TRUE(result.Get<BoolValue>().NativeValue());
  }
}

BENCHMARK(BM_ReadProtoMap);

void BM_NestedProtoFieldRead(benchmark::State& state) {
  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(
      auto compiler_builder,
      cel::NewCompilerBuilder(google::protobuf::DescriptorPool::generated_pool()));
  ASSERT_THAT(compiler_builder->AddLibrary(cel::StandardCompilerLibrary()),
              IsOk());
  ASSERT_THAT(
      compiler_builder->GetCheckerBuilder().AddVariable(cel::MakeVariableDecl(
          "request", cel::MessageType(RequestContext::descriptor()))),
      IsOk());
  ASSERT_OK_AND_ASSIGN(auto compiler, compiler_builder->Build());

  ASSERT_OK_AND_ASSIGN(auto validation_result, compiler->Compile(R"cel(
      !request.a.b.c.d.e
   )cel"));
  ASSERT_TRUE(validation_result.IsValid());
  ASSERT_OK_AND_ASSIGN(auto ast, validation_result.ReleaseAst());

  cel::expr::CheckedExpr checked_expr;
  ASSERT_THAT(cel::AstToCheckedExpr(*ast, &checked_expr), IsOk());

  ASSERT_OK_AND_ASSIGN(auto runtime, CreateBenchmarkRuntime(arena));

  ASSERT_OK_AND_ASSIGN(auto program, ProtobufRuntimeAdapter::CreateProgram(
                                         *runtime, checked_expr));

  Activation activation;
  RequestContext request;
  request.mutable_a()->mutable_b()->mutable_c()->mutable_d()->set_e(false);
  activation.InsertOrAssignValue(
      "request",
      Value::WrapMessage(&request, google::protobuf::DescriptorPool::generated_pool(),
                         google::protobuf::MessageFactory::generated_factory(), &arena));

  for (auto _ : state) {
    ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));
    ASSERT_TRUE(result.Is<BoolValue>());
    ASSERT_TRUE(result.Get<BoolValue>().NativeValue());
  }
}

BENCHMARK(BM_NestedProtoFieldRead);

void BM_NestedProtoFieldReadDefaults(benchmark::State& state) {
  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(
      auto compiler_builder,
      cel::NewCompilerBuilder(google::protobuf::DescriptorPool::generated_pool()));
  ASSERT_THAT(compiler_builder->AddLibrary(cel::StandardCompilerLibrary()),
              IsOk());
  ASSERT_THAT(
      compiler_builder->GetCheckerBuilder().AddVariable(cel::MakeVariableDecl(
          "request", cel::MessageType(RequestContext::descriptor()))),
      IsOk());
  ASSERT_OK_AND_ASSIGN(auto compiler, compiler_builder->Build());

  ASSERT_OK_AND_ASSIGN(auto validation_result, compiler->Compile(R"cel(
      !request.a.b.c.d.e
   )cel"));
  ASSERT_TRUE(validation_result.IsValid());
  ASSERT_OK_AND_ASSIGN(auto ast, validation_result.ReleaseAst());

  cel::expr::CheckedExpr checked_expr;
  ASSERT_THAT(cel::AstToCheckedExpr(*ast, &checked_expr), IsOk());

  ASSERT_OK_AND_ASSIGN(auto runtime, CreateBenchmarkRuntime(arena));

  ASSERT_OK_AND_ASSIGN(auto program, ProtobufRuntimeAdapter::CreateProgram(
                                         *runtime, checked_expr));

  Activation activation;
  RequestContext request;
  activation.InsertOrAssignValue(
      "request",
      Value::WrapMessage(&request, google::protobuf::DescriptorPool::generated_pool(),
                         google::protobuf::MessageFactory::generated_factory(), &arena));

  for (auto _ : state) {
    ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));
    ASSERT_TRUE(result.Is<BoolValue>());
    ASSERT_TRUE(result.Get<BoolValue>().NativeValue());
  }
}

BENCHMARK(BM_NestedProtoFieldReadDefaults);

void BM_ProtoStructAccess(benchmark::State& state) {
  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(
      auto compiler_builder,
      cel::NewCompilerBuilder(google::protobuf::DescriptorPool::generated_pool()));
  ASSERT_THAT(compiler_builder->AddLibrary(cel::StandardCompilerLibrary()),
              IsOk());
  ASSERT_THAT(
      compiler_builder->GetCheckerBuilder().AddVariable(cel::MakeVariableDecl(
          "request",
          cel::MessageType(AttributeContext::Request::descriptor()))),
      IsOk());
  ASSERT_OK_AND_ASSIGN(auto compiler, compiler_builder->Build());

  ASSERT_OK_AND_ASSIGN(auto validation_result, compiler->Compile(R"cel(
      has(request.auth.claims.iss) && request.auth.claims.iss == 'accounts.google.com'
   )cel"));
  ASSERT_TRUE(validation_result.IsValid());
  ASSERT_OK_AND_ASSIGN(auto ast, validation_result.ReleaseAst());

  cel::expr::CheckedExpr checked_expr;
  ASSERT_THAT(cel::AstToCheckedExpr(*ast, &checked_expr), IsOk());

  ASSERT_OK_AND_ASSIGN(auto runtime, CreateBenchmarkRuntime(arena));

  ASSERT_OK_AND_ASSIGN(auto program, ProtobufRuntimeAdapter::CreateProgram(
                                         *runtime, checked_expr));

  Activation activation;
  AttributeContext::Request request;
  auto* auth = request.mutable_auth();
  (*auth->mutable_claims()->mutable_fields())["iss"].set_string_value(
      "accounts.google.com");
  activation.InsertOrAssignValue(
      "request",
      Value::WrapMessage(&request, google::protobuf::DescriptorPool::generated_pool(),
                         google::protobuf::MessageFactory::generated_factory(), &arena));

  for (auto _ : state) {
    ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));
    ASSERT_TRUE(result.Is<BoolValue>());
    ASSERT_TRUE(result.Get<BoolValue>().NativeValue());
  }
}

BENCHMARK(BM_ProtoStructAccess);

void BM_ProtoListAccess(benchmark::State& state) {
  google::protobuf::Arena arena;
  ASSERT_OK_AND_ASSIGN(
      auto compiler_builder,
      cel::NewCompilerBuilder(google::protobuf::DescriptorPool::generated_pool()));
  ASSERT_THAT(compiler_builder->AddLibrary(cel::StandardCompilerLibrary()),
              IsOk());
  ASSERT_THAT(
      compiler_builder->GetCheckerBuilder().AddVariable(cel::MakeVariableDecl(
          "request",
          cel::MessageType(AttributeContext::Request::descriptor()))),
      IsOk());
  ASSERT_OK_AND_ASSIGN(auto compiler, compiler_builder->Build());

  ASSERT_OK_AND_ASSIGN(auto validation_result, compiler->Compile(R"cel(
      "//.../accessLevels/MY_LEVEL_4" in request.auth.access_levels
   )cel"));
  ASSERT_TRUE(validation_result.IsValid());
  ASSERT_OK_AND_ASSIGN(auto ast, validation_result.ReleaseAst());

  cel::expr::CheckedExpr checked_expr;
  ASSERT_THAT(cel::AstToCheckedExpr(*ast, &checked_expr), IsOk());

  ASSERT_OK_AND_ASSIGN(auto runtime, CreateBenchmarkRuntime(arena));

  ASSERT_OK_AND_ASSIGN(auto program, ProtobufRuntimeAdapter::CreateProgram(
                                         *runtime, checked_expr));

  Activation activation;
  AttributeContext::Request request;
  auto* auth = request.mutable_auth();
  auth->add_access_levels("//.../accessLevels/MY_LEVEL_0");
  auth->add_access_levels("//.../accessLevels/MY_LEVEL_1");
  auth->add_access_levels("//.../accessLevels/MY_LEVEL_2");
  auth->add_access_levels("//.../accessLevels/MY_LEVEL_3");
  auth->add_access_levels("//.../accessLevels/MY_LEVEL_4");
  activation.InsertOrAssignValue(
      "request",
      Value::WrapMessage(&request, google::protobuf::DescriptorPool::generated_pool(),
                         google::protobuf::MessageFactory::generated_factory(), &arena));

  for (auto _ : state) {
    ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));
    ASSERT_TRUE(result.Is<BoolValue>());
    ASSERT_TRUE(result.Get<BoolValue>().NativeValue());
  }
}

BENCHMARK(BM_ProtoListAccess);

// This expression has no equivalent CEL expression.
// Sum a square with a nested comprehension
constexpr char kNestedListSum[] = R"(
id: 1
comprehension_expr: <
  accu_var: "__result__"
  iter_var: "x"
  iter_range: <
    id: 2
    ident_expr: <
      name: "list_var"
    >
  >
  accu_init: <
    id: 3
    const_expr: <
      int64_value: 0
    >
  >
  loop_step: <
    id: 4
    call_expr: <
      function: "_+_"
      args: <
        id: 5
        ident_expr: <
          name: "__result__"
        >
      >
      args: <
        id: 6
        comprehension_expr: <
          accu_var: "__result__"
          iter_var: "x"
          iter_range: <
            id: 9
            ident_expr: <
              name: "list_var"
            >
          >
          accu_init: <
            id: 10
            const_expr: <
              int64_value: 0
            >
          >
          loop_step: <
            id: 11
            call_expr: <
              function: "_+_"
              args: <
                id: 12
                ident_expr: <
                  name: "__result__"
                >
              >
              args: <
                id: 13
                ident_expr: <
                  name: "x"
                >
              >
            >
          >
          loop_condition: <
            id: 14
            const_expr: <
              bool_value: true
            >
          >
          result: <
            id: 15
            ident_expr: <
              name: "__result__"
            >
          >
        >
      >
    >
  >
  loop_condition: <
    id: 7
    const_expr: <
      bool_value: true
    >
  >
  result: <
    id: 8
    ident_expr: <
      name: "__result__"
    >
  >
>)";

void BM_NestedComprehension(benchmark::State& state) {
  google::protobuf::Arena arena;
  Expr expr;
  Activation activation;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(kNestedListSum, &expr));

  int len = state.range(0);
  auto list_builder = NewListValueBuilder(&arena);
  list_builder->Reserve(len);
  for (int i = 0; i < len; i++) {
    ASSERT_THAT(list_builder->Add(IntValue(1)), IsOk());
  }
  activation.InsertOrAssignValue("list_var", std::move(*list_builder).Build());

  RuntimeOptions options = GetOptions();
  options.comprehension_max_iterations = 10000000;
  ASSERT_OK_AND_ASSIGN(auto runtime, CreateBenchmarkRuntime(arena, options));

  ASSERT_OK_AND_ASSIGN(auto program,
                       ProtobufRuntimeAdapter::CreateProgram(*runtime, expr));

  for (auto _ : state) {
    ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));
    ASSERT_TRUE(result.Is<IntValue>());
    ASSERT_EQ(result.Get<IntValue>().NativeValue(), len * len);
  }
}

BENCHMARK(BM_NestedComprehension)->Range(1, 1 << 10);

void BM_NestedComprehension_Trace(benchmark::State& state) {
  google::protobuf::Arena arena;
  Expr expr;
  Activation activation;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(kNestedListSum, &expr));

  int len = state.range(0);
  auto list_builder = NewListValueBuilder(&arena);
  list_builder->Reserve(len);
  for (int i = 0; i < len; i++) {
    ASSERT_THAT(list_builder->Add(IntValue(1)), IsOk());
  }
  activation.InsertOrAssignValue("list_var", std::move(*list_builder).Build());

  RuntimeOptions options = GetOptions();
  options.comprehension_max_iterations = 10000000;
  options.enable_comprehension_list_append = true;
  ASSERT_OK_AND_ASSIGN(auto runtime, CreateBenchmarkRuntime(arena, options));

  ASSERT_OK_AND_ASSIGN(auto program,
                       ProtobufRuntimeAdapter::CreateProgram(*runtime, expr));

  for (auto _ : state) {
    ASSERT_OK_AND_ASSIGN(Value result,
                         program->Trace(&arena, activation, EmptyCallback));
    ASSERT_TRUE(result.Is<IntValue>());
    ASSERT_EQ(result.Get<IntValue>().NativeValue(), len * len);
  }
}

BENCHMARK(BM_NestedComprehension_Trace)->Range(1, 1 << 10);

void BM_ListComprehension(benchmark::State& state) {
  google::protobuf::Arena arena;
  Activation activation;
  ASSERT_OK_AND_ASSIGN(ParsedExpr parsed_expr,
                       parser::Parse("list_var.map(x, x * 2)"));

  int len = state.range(0);
  auto list_builder = NewListValueBuilder(&arena);
  list_builder->Reserve(len);
  for (int i = 0; i < len; i++) {
    ASSERT_THAT(list_builder->Add(IntValue(1)), IsOk());
  }
  activation.InsertOrAssignValue("list_var", std::move(*list_builder).Build());

  RuntimeOptions options = GetOptions();
  options.comprehension_max_iterations = 10000000;
  options.enable_comprehension_list_append = true;
  ASSERT_OK_AND_ASSIGN(auto runtime, CreateBenchmarkRuntime(arena, options));

  ASSERT_OK_AND_ASSIGN(auto program, ProtobufRuntimeAdapter::CreateProgram(
                                         *runtime, parsed_expr));

  for (auto _ : state) {
    ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));
    ASSERT_TRUE(result.Is<ListValue>());
    ASSERT_THAT(result.Get<ListValue>().Size(),
                ::absl_testing::IsOkAndHolds(len));
  }
}

BENCHMARK(BM_ListComprehension)->Range(1, 1 << 16);

void BM_ListComprehension_Trace(benchmark::State& state) {
  google::protobuf::Arena arena;
  Activation activation;
  ASSERT_OK_AND_ASSIGN(ParsedExpr parsed_expr,
                       parser::Parse("list_var.map(x, x * 2)"));

  int len = state.range(0);
  auto list_builder = NewListValueBuilder(&arena);
  list_builder->Reserve(len);
  for (int i = 0; i < len; i++) {
    ASSERT_THAT(list_builder->Add(IntValue(1)), IsOk());
  }
  activation.InsertOrAssignValue("list_var", std::move(*list_builder).Build());

  RuntimeOptions options = GetOptions();
  options.comprehension_max_iterations = 10000000;
  options.enable_comprehension_list_append = true;
  ASSERT_OK_AND_ASSIGN(auto runtime, CreateBenchmarkRuntime(arena, options));

  ASSERT_OK_AND_ASSIGN(auto program, ProtobufRuntimeAdapter::CreateProgram(
                                         *runtime, parsed_expr));

  for (auto _ : state) {
    ASSERT_OK_AND_ASSIGN(Value result,
                         program->Trace(&arena, activation, EmptyCallback));
    ASSERT_TRUE(result.Is<ListValue>());
    ASSERT_THAT(result.Get<ListValue>().Size(),
                ::absl_testing::IsOkAndHolds(len));
  }
}

BENCHMARK(BM_ListComprehension_Trace)->Range(1, 1 << 16);

void BM_ListComprehension_Opt(benchmark::State& state) {
  google::protobuf::Arena arena;
  Activation activation;
  ASSERT_OK_AND_ASSIGN(ParsedExpr parsed_expr,
                       parser::Parse("list_var.map(x, x * 2)"));

  int len = state.range(0);
  auto list_builder = NewListValueBuilder(&arena);
  list_builder->Reserve(len);
  for (int i = 0; i < len; i++) {
    ASSERT_THAT(list_builder->Add(IntValue(1)), IsOk());
  }
  activation.InsertOrAssignValue("list_var", std::move(*list_builder).Build());

  RuntimeOptions options;
  options.comprehension_max_iterations = 10000000;
  options.enable_comprehension_list_append = true;
  ASSERT_OK_AND_ASSIGN(
      auto runtime,
      CreateBenchmarkRuntime(arena, options, /*force_const_folding=*/true));

  ASSERT_OK_AND_ASSIGN(auto program, ProtobufRuntimeAdapter::CreateProgram(
                                         *runtime, parsed_expr));

  for (auto _ : state) {
    ASSERT_OK_AND_ASSIGN(Value result, program->Evaluate(&arena, activation));
    ASSERT_TRUE(result.Is<ListValue>());
    ASSERT_THAT(result.Get<ListValue>().Size(),
                ::absl_testing::IsOkAndHolds(len));
  }
}

BENCHMARK(BM_ListComprehension_Opt)->Range(1, 1 << 16);

void BM_ComprehensionCpp(benchmark::State& state) {
  int len = state.range(0);
  std::vector<Value> list;
  list.reserve(len);
  for (int i = 0; i < len; i++) {
    list.push_back(IntValue(1));
  }

  auto op = [&list]() {
    int sum = 0;
    for (const auto& value : list) {
      sum += value.Get<IntValue>().NativeValue();
    }
    return sum;
  };
  for (auto _ : state) {
    int result = op();
    ASSERT_EQ(result, len);
  }
}

BENCHMARK(BM_ComprehensionCpp)->Range(1, 1 << 20);

}  // namespace

}  // namespace runtime
}  // namespace expr
}  // namespace api
}  // namespace google
