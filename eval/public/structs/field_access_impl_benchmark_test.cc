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

#include "cel/expr/conformance/proto3/test_all_types.pb.h"
#include "eval/public/cel_options.h"
#include "eval/public/cel_value.h"
#include "eval/public/structs/cel_proto_wrapper.h"
#include "eval/public/structs/field_access_impl.h"
#include "extensions/protobuf/internal/map_reflection.h"
#include "internal/benchmark.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/map_field.h"
#include "google/protobuf/message.h"

namespace google::api::expr::runtime::internal {
namespace {

using ::cel::expr::conformance::proto3::TestAllTypes;

void BM_CreateValueFromSingleField_Int64(benchmark::State& state) {
  google::protobuf::Arena arena;
  TestAllTypes msg;
  msg.set_single_int64(42);
  const google::protobuf::FieldDescriptor* desc =
      TestAllTypes::descriptor()->FindFieldByName("single_int64");

  for (auto _ : state) {
    auto value = CreateValueFromSingleField(
        &msg, desc, ProtoWrapperTypeOptions::kUnsetProtoDefault,
        &CelProtoWrapper::InternalWrapMessage, &arena);
    benchmark::DoNotOptimize(value);
  }
}
BENCHMARK(BM_CreateValueFromSingleField_Int64);

void BM_CreateValueFromSingleField_String(benchmark::State& state) {
  google::protobuf::Arena arena;
  TestAllTypes msg;
  msg.set_single_string("hello world");
  const google::protobuf::FieldDescriptor* desc =
      TestAllTypes::descriptor()->FindFieldByName("single_string");

  for (auto _ : state) {
    auto value = CreateValueFromSingleField(
        &msg, desc, ProtoWrapperTypeOptions::kUnsetProtoDefault,
        &CelProtoWrapper::InternalWrapMessage, &arena);
    benchmark::DoNotOptimize(value);
  }
}
BENCHMARK(BM_CreateValueFromSingleField_String);

void BM_CreateValueFromSingleField_Message(benchmark::State& state) {
  google::protobuf::Arena arena;
  TestAllTypes msg;
  msg.mutable_standalone_message()->set_bb(123);
  const google::protobuf::FieldDescriptor* desc =
      TestAllTypes::descriptor()->FindFieldByName("standalone_message");

  for (auto _ : state) {
    auto value = CreateValueFromSingleField(
        &msg, desc, ProtoWrapperTypeOptions::kUnsetProtoDefault,
        &CelProtoWrapper::InternalWrapMessage, &arena);
    benchmark::DoNotOptimize(value);
  }
}
BENCHMARK(BM_CreateValueFromSingleField_Message);

void BM_CreateValueFromRepeatedField_Int64(benchmark::State& state) {
  google::protobuf::Arena arena;
  TestAllTypes msg;
  msg.add_repeated_int64(42);
  const google::protobuf::FieldDescriptor* desc =
      TestAllTypes::descriptor()->FindFieldByName("repeated_int64");

  for (auto _ : state) {
    auto value = CreateValueFromRepeatedField(
        &msg, desc, 0, &CelProtoWrapper::InternalWrapMessage, &arena);
    benchmark::DoNotOptimize(value);
  }
}
BENCHMARK(BM_CreateValueFromRepeatedField_Int64);

void BM_CreateValueFromRepeatedField_String(benchmark::State& state) {
  google::protobuf::Arena arena;
  TestAllTypes msg;
  msg.add_repeated_string("hello world");
  const google::protobuf::FieldDescriptor* desc =
      TestAllTypes::descriptor()->FindFieldByName("repeated_string");

  for (auto _ : state) {
    auto value = CreateValueFromRepeatedField(
        &msg, desc, 0, &CelProtoWrapper::InternalWrapMessage, &arena);
    benchmark::DoNotOptimize(value);
  }
}
BENCHMARK(BM_CreateValueFromRepeatedField_String);

void BM_CreateValueFromMapValue_Int64(benchmark::State& state) {
  google::protobuf::Arena arena;
  TestAllTypes msg;
  (*msg.mutable_map_int64_int64())[42] = 100;
  const google::protobuf::FieldDescriptor* map_desc =
      TestAllTypes::descriptor()->FindFieldByName("map_int64_int64");
  const google::protobuf::FieldDescriptor* value_desc =
      map_desc->message_type()->FindFieldByName("value");

  google::protobuf::ConstMapIterator iter =
      cel::extensions::protobuf_internal::ConstMapBegin(*msg.GetReflection(),
                                                        msg, *map_desc);
  google::protobuf::MapValueConstRef value_ref = iter.GetValueRef();

  for (auto _ : state) {
    auto value =
        CreateValueFromMapValue(&msg, value_desc, &value_ref,
                                &CelProtoWrapper::InternalWrapMessage, &arena);
    benchmark::DoNotOptimize(value);
  }
}
BENCHMARK(BM_CreateValueFromMapValue_Int64);

void BM_SetValueToSingleField_Int64(benchmark::State& state) {
  google::protobuf::Arena arena;
  TestAllTypes msg;
  const google::protobuf::FieldDescriptor* desc =
      TestAllTypes::descriptor()->FindFieldByName("single_int64");
  CelValue val = CelValue::CreateInt64(42);

  for (auto _ : state) {
    auto status = SetValueToSingleField(val, desc, &msg, &arena);
    benchmark::DoNotOptimize(status);
  }
}
BENCHMARK(BM_SetValueToSingleField_Int64);

void BM_SetValueToSingleField_String(benchmark::State& state) {
  google::protobuf::Arena arena;
  TestAllTypes msg;
  const google::protobuf::FieldDescriptor* desc =
      TestAllTypes::descriptor()->FindFieldByName("single_string");
  CelValue val = CelValue::CreateStringView("hello world");

  for (auto _ : state) {
    auto status = SetValueToSingleField(val, desc, &msg, &arena);
    benchmark::DoNotOptimize(status);
  }
}
BENCHMARK(BM_SetValueToSingleField_String);

void BM_SetValueToSingleField_Message(benchmark::State& state) {
  google::protobuf::Arena arena;
  TestAllTypes msg;
  const google::protobuf::FieldDescriptor* desc =
      TestAllTypes::descriptor()->FindFieldByName("standalone_message");

  TestAllTypes::NestedMessage nested_msg;
  nested_msg.set_bb(123);
  CelValue val = CelProtoWrapper::CreateMessage(&nested_msg, &arena);

  for (auto _ : state) {
    auto status = SetValueToSingleField(val, desc, &msg, &arena);
    benchmark::DoNotOptimize(status);
  }
}
BENCHMARK(BM_SetValueToSingleField_Message);

void BM_AddValueToRepeatedField_Int64(benchmark::State& state) {
  google::protobuf::Arena arena;
  TestAllTypes msg;
  const google::protobuf::FieldDescriptor* desc =
      TestAllTypes::descriptor()->FindFieldByName("repeated_int64");
  CelValue val = CelValue::CreateInt64(42);

  for (auto _ : state) {
    msg.clear_repeated_int64();
    auto status = AddValueToRepeatedField(val, desc, &msg, &arena);
    benchmark::DoNotOptimize(status);
  }
}
BENCHMARK(BM_AddValueToRepeatedField_Int64);

void BM_AddValueToRepeatedField_String(benchmark::State& state) {
  google::protobuf::Arena arena;
  TestAllTypes msg;
  const google::protobuf::FieldDescriptor* desc =
      TestAllTypes::descriptor()->FindFieldByName("repeated_string");
  CelValue val = CelValue::CreateStringView("hello world");

  for (auto _ : state) {
    msg.clear_repeated_string();
    auto status = AddValueToRepeatedField(val, desc, &msg, &arena);
    benchmark::DoNotOptimize(status);
  }
}
BENCHMARK(BM_AddValueToRepeatedField_String);

void BM_CreateValueFromRepeatedField_StringPiece(benchmark::State& state) {
  google::protobuf::Arena arena;
  TestAllTypes msg;
  msg.add_repeated_string_piece("hello world");
  const google::protobuf::FieldDescriptor* desc =
      TestAllTypes::descriptor()->FindFieldByName("repeated_string_piece");

  for (auto _ : state) {
    auto value = CreateValueFromRepeatedField(
        &msg, desc, 0, &CelProtoWrapper::InternalWrapMessage, &arena);
    benchmark::DoNotOptimize(value);
  }
}
BENCHMARK(BM_CreateValueFromRepeatedField_StringPiece);

void BM_AddValueToRepeatedField_StringPiece(benchmark::State& state) {
  google::protobuf::Arena arena;
  TestAllTypes msg;
  const google::protobuf::FieldDescriptor* desc =
      TestAllTypes::descriptor()->FindFieldByName("repeated_string_piece");
  CelValue val = CelValue::CreateStringView("hello world");

  for (auto _ : state) {
    msg.clear_repeated_string_piece();
    auto status = AddValueToRepeatedField(val, desc, &msg, &arena);
    benchmark::DoNotOptimize(status);
  }
}
BENCHMARK(BM_AddValueToRepeatedField_StringPiece);

}  // namespace
}  // namespace google::api::expr::runtime::internal
