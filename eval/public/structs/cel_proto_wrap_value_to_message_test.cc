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

#include "eval/public/structs/cel_proto_wrap_value_to_message.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "google/protobuf/any.pb.h"
#include "google/protobuf/duration.pb.h"
#include "google/protobuf/empty.pb.h"
#include "google/protobuf/field_mask.pb.h"
#include "google/protobuf/struct.pb.h"
#include "google/protobuf/timestamp.pb.h"
#include "google/protobuf/wrappers.pb.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "eval/public/cel_value.h"
#include "eval/public/containers/container_backed_list_impl.h"
#include "eval/public/containers/container_backed_map_impl.h"
#include "eval/public/structs/trivial_legacy_type_info.h"
#include "eval/testutil/test_message.pb.h"
#include "internal/testing.h"
#include "testutil/util.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/dynamic_message.h"
#include "google/protobuf/message.h"
#include "google/protobuf/text_format.h"

namespace google::api::expr::runtime::internal {

namespace {

using google::protobuf::Any;
using google::protobuf::BoolValue;
using google::protobuf::BytesValue;
using google::protobuf::DoubleValue;
using google::protobuf::Duration;
using google::protobuf::FieldMask;
using google::protobuf::FloatValue;
using google::protobuf::Int32Value;
using google::protobuf::Int64Value;
using google::protobuf::ListValue;
using google::protobuf::StringValue;
using google::protobuf::Struct;
using google::protobuf::Timestamp;
using google::protobuf::UInt32Value;
using google::protobuf::UInt64Value;
using google::protobuf::Value;
using google::protobuf::Arena;
using ::google::protobuf::TextFormat;

CelValue MessageToCelValue(const google::protobuf::Message* m) {
  return CelValue::CreateMessageWrapper(
      CelValue::MessageWrapper(m, TrivialTypeInfo::GetInstance()));
}

class CelProtoWrapValueToMessageTest : public ::testing::Test {
 protected:
  CelProtoWrapValueToMessageTest() = default;

  void ExpectWrappedMessage(const CelValue& value,
                            const google::protobuf::Message& message) {
    // Test the input value wraps to the destination message type.
    auto* result = MaybeWrapValueToMessage(
        message.GetDescriptor(), message.GetReflection()->GetMessageFactory(),
        value, arena());
    EXPECT_TRUE(result != nullptr);
    EXPECT_THAT(result, testutil::EqualsProto(message));

    // Ensure that double wrapping results in the object being wrapped once.
    auto* identity = MaybeWrapValueToMessage(
        message.GetDescriptor(), message.GetReflection()->GetMessageFactory(),
        MessageToCelValue(result), arena());
    EXPECT_TRUE(identity == nullptr);

    // Check to make sure that even dynamic messages can be used as input to
    // the wrapping call.
    result = MaybeWrapValueToMessage(
        ReflectedCopy(message)->GetDescriptor(),
        ReflectedCopy(message)->GetReflection()->GetMessageFactory(), value,
        arena());
    EXPECT_TRUE(result != nullptr);
    EXPECT_THAT(result, testutil::EqualsProto(message));
  }

  void ExpectNotWrapped(const CelValue& value, const google::protobuf::Message& message) {
    // Test the input value does not wrap by asserting value == result.
    auto result = MaybeWrapValueToMessage(
        message.GetDescriptor(), message.GetReflection()->GetMessageFactory(),
        value, arena());
    EXPECT_TRUE(result == nullptr);
  }

  std::unique_ptr<google::protobuf::Message> ReflectedCopy(
      const google::protobuf::Message& message) {
    std::unique_ptr<google::protobuf::Message> dynamic_value(
        factory_.GetPrototype(message.GetDescriptor())->New());
    dynamic_value->CopyFrom(message);
    return dynamic_value;
  }

  Arena* arena() { return &arena_; }

 private:
  Arena arena_;
  google::protobuf::DynamicMessageFactory factory_;
};

TEST_F(CelProtoWrapValueToMessageTest, WrapNull) {
  auto cel_value = CelValue::CreateNull();

  Value json;
  json.set_null_value(protobuf::NULL_VALUE);
  ExpectWrappedMessage(cel_value, json);

  Any any;
  any.PackFrom(json);
  ExpectWrappedMessage(cel_value, any);
}

TEST_F(CelProtoWrapValueToMessageTest, WrapBool) {
  auto cel_value = CelValue::CreateBool(true);

  Value json;
  json.set_bool_value(true);
  ExpectWrappedMessage(cel_value, json);

  BoolValue wrapper;
  wrapper.set_value(true);
  ExpectWrappedMessage(cel_value, wrapper);

  Any any;
  any.PackFrom(wrapper);
  ExpectWrappedMessage(cel_value, any);
}

TEST_F(CelProtoWrapValueToMessageTest, WrapBytes) {
  std::string str = "hello world";
  auto cel_value = CelValue::CreateBytes(CelValue::BytesHolder(&str));

  BytesValue wrapper;
  wrapper.set_value(str);
  ExpectWrappedMessage(cel_value, wrapper);

  Any any;
  any.PackFrom(wrapper);
  ExpectWrappedMessage(cel_value, any);
}

TEST_F(CelProtoWrapValueToMessageTest, WrapBytesToValue) {
  std::string str = "hello world";
  auto cel_value = CelValue::CreateBytes(CelValue::BytesHolder(&str));

  Value json;
  json.set_string_value("aGVsbG8gd29ybGQ=");
  ExpectWrappedMessage(cel_value, json);
}

TEST_F(CelProtoWrapValueToMessageTest, WrapDuration) {
  auto cel_value = CelValue::CreateDuration(absl::Seconds(300));

  Duration d;
  d.set_seconds(300);
  ExpectWrappedMessage(cel_value, d);

  Any any;
  any.PackFrom(d);
  ExpectWrappedMessage(cel_value, any);
}

TEST_F(CelProtoWrapValueToMessageTest, WrapDurationToValue) {
  auto cel_value = CelValue::CreateDuration(absl::Seconds(300));

  Value json;
  json.set_string_value("300s");
  ExpectWrappedMessage(cel_value, json);
}

TEST_F(CelProtoWrapValueToMessageTest, WrapDouble) {
  double num = 1.5;
  auto cel_value = CelValue::CreateDouble(num);

  Value json;
  json.set_number_value(num);
  ExpectWrappedMessage(cel_value, json);

  DoubleValue wrapper;
  wrapper.set_value(num);
  ExpectWrappedMessage(cel_value, wrapper);

  Any any;
  any.PackFrom(wrapper);
  ExpectWrappedMessage(cel_value, any);
}

TEST_F(CelProtoWrapValueToMessageTest, WrapDoubleToFloatValue) {
  double num = 1.5;
  auto cel_value = CelValue::CreateDouble(num);

  FloatValue wrapper;
  wrapper.set_value(num);
  ExpectWrappedMessage(cel_value, wrapper);

  // Imprecise double -> float representation results in truncation.
  double small_num = -9.9e-100;
  wrapper.set_value(small_num);
  cel_value = CelValue::CreateDouble(small_num);
  ExpectWrappedMessage(cel_value, wrapper);
}

TEST_F(CelProtoWrapValueToMessageTest, WrapDoubleOverflow) {
  double lowest_double = std::numeric_limits<double>::lowest();
  auto cel_value = CelValue::CreateDouble(lowest_double);

  // Double exceeds float precision, overflow to -infinity.
  FloatValue wrapper;
  wrapper.set_value(-std::numeric_limits<float>::infinity());
  ExpectWrappedMessage(cel_value, wrapper);

  double max_double = std::numeric_limits<double>::max();
  cel_value = CelValue::CreateDouble(max_double);

  wrapper.set_value(std::numeric_limits<float>::infinity());
  ExpectWrappedMessage(cel_value, wrapper);
}

TEST_F(CelProtoWrapValueToMessageTest, WrapInt64) {
  int32_t num = std::numeric_limits<int32_t>::lowest();
  auto cel_value = CelValue::CreateInt64(num);

  Value json;
  json.set_number_value(static_cast<double>(num));
  ExpectWrappedMessage(cel_value, json);

  Int64Value wrapper;
  wrapper.set_value(num);
  ExpectWrappedMessage(cel_value, wrapper);

  Any any;
  any.PackFrom(wrapper);
  ExpectWrappedMessage(cel_value, any);
}

TEST_F(CelProtoWrapValueToMessageTest, WrapInt64ToInt32Value) {
  int32_t num = std::numeric_limits<int32_t>::lowest();
  auto cel_value = CelValue::CreateInt64(num);

  Int32Value wrapper;
  wrapper.set_value(num);
  ExpectWrappedMessage(cel_value, wrapper);
}

TEST_F(CelProtoWrapValueToMessageTest, WrapFailureInt64ToInt32Value) {
  int64_t num = std::numeric_limits<int64_t>::lowest();
  auto cel_value = CelValue::CreateInt64(num);

  Int32Value wrapper;
  ExpectNotWrapped(cel_value, wrapper);
}

TEST_F(CelProtoWrapValueToMessageTest, WrapInt64ToValue) {
  int64_t max = std::numeric_limits<int64_t>::max();
  auto cel_value = CelValue::CreateInt64(max);

  Value json;
  json.set_string_value(absl::StrCat(max));
  ExpectWrappedMessage(cel_value, json);

  int64_t min = std::numeric_limits<int64_t>::min();
  cel_value = CelValue::CreateInt64(min);

  json.set_string_value(absl::StrCat(min));
  ExpectWrappedMessage(cel_value, json);
}

TEST_F(CelProtoWrapValueToMessageTest, WrapUint64) {
  uint32_t num = std::numeric_limits<uint32_t>::max();
  auto cel_value = CelValue::CreateUint64(num);

  Value json;
  json.set_number_value(static_cast<double>(num));
  ExpectWrappedMessage(cel_value, json);

  UInt64Value wrapper;
  wrapper.set_value(num);
  ExpectWrappedMessage(cel_value, wrapper);

  Any any;
  any.PackFrom(wrapper);
  ExpectWrappedMessage(cel_value, any);
}

TEST_F(CelProtoWrapValueToMessageTest, WrapUint64ToUint32Value) {
  uint32_t num = std::numeric_limits<uint32_t>::max();
  auto cel_value = CelValue::CreateUint64(num);

  UInt32Value wrapper;
  wrapper.set_value(num);
  ExpectWrappedMessage(cel_value, wrapper);
}

TEST_F(CelProtoWrapValueToMessageTest, WrapUint64ToValue) {
  uint64_t num = std::numeric_limits<uint64_t>::max();
  auto cel_value = CelValue::CreateUint64(num);

  Value json;
  json.set_string_value(absl::StrCat(num));
  ExpectWrappedMessage(cel_value, json);
}

TEST_F(CelProtoWrapValueToMessageTest, WrapFailureUint64ToUint32Value) {
  uint64_t num = std::numeric_limits<uint64_t>::max();
  auto cel_value = CelValue::CreateUint64(num);

  UInt32Value wrapper;
  ExpectNotWrapped(cel_value, wrapper);
}

TEST_F(CelProtoWrapValueToMessageTest, WrapString) {
  std::string str = "test";
  auto cel_value = CelValue::CreateString(CelValue::StringHolder(&str));

  Value json;
  json.set_string_value(str);
  ExpectWrappedMessage(cel_value, json);

  StringValue wrapper;
  wrapper.set_value(str);
  ExpectWrappedMessage(cel_value, wrapper);

  Any any;
  any.PackFrom(wrapper);
  ExpectWrappedMessage(cel_value, any);
}

TEST_F(CelProtoWrapValueToMessageTest, WrapTimestamp) {
  absl::Time ts = absl::FromUnixSeconds(1615852799);
  auto cel_value = CelValue::CreateTimestamp(ts);

  Timestamp t;
  t.set_seconds(1615852799);
  ExpectWrappedMessage(cel_value, t);

  Any any;
  any.PackFrom(t);
  ExpectWrappedMessage(cel_value, any);
}

TEST_F(CelProtoWrapValueToMessageTest, WrapTimestampToValue) {
  absl::Time ts = absl::FromUnixSeconds(1615852799);
  auto cel_value = CelValue::CreateTimestamp(ts);

  Value json;
  json.set_string_value("2021-03-15T23:59:59Z");
  ExpectWrappedMessage(cel_value, json);
}

TEST_F(CelProtoWrapValueToMessageTest, WrapList) {
  std::vector<CelValue> list_elems = {
      CelValue::CreateDouble(1.5),
      CelValue::CreateInt64(-2L),
  };
  ContainerBackedListImpl list(std::move(list_elems));
  auto cel_value = CelValue::CreateList(&list);

  Value json;
  json.mutable_list_value()->add_values()->set_number_value(1.5);
  json.mutable_list_value()->add_values()->set_number_value(-2.);
  ExpectWrappedMessage(cel_value, json);
  ExpectWrappedMessage(cel_value, json.list_value());

  Any any;
  any.PackFrom(json.list_value());
  ExpectWrappedMessage(cel_value, any);
}

TEST_F(CelProtoWrapValueToMessageTest, WrapFailureListValueBadJSON) {
  TestMessage message;
  std::vector<CelValue> list_elems = {
      CelValue::CreateDouble(1.5),
      MessageToCelValue(&message),
  };
  ContainerBackedListImpl list(std::move(list_elems));
  auto cel_value = CelValue::CreateList(&list);

  Value json;
  ExpectNotWrapped(cel_value, json);
}

TEST_F(CelProtoWrapValueToMessageTest, WrapStruct) {
  const std::string kField1 = "field1";
  std::vector<std::pair<CelValue, CelValue>> args = {
      {CelValue::CreateString(CelValue::StringHolder(&kField1)),
       CelValue::CreateBool(true)}};
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<CelMap> cel_map,
      CreateContainerBackedMap(
          absl::Span<std::pair<CelValue, CelValue>>(args.data(), args.size())));
  auto cel_value = CelValue::CreateMap(cel_map.get());

  Value json;
  (*json.mutable_struct_value()->mutable_fields())[kField1].set_bool_value(
      true);
  ExpectWrappedMessage(cel_value, json);
  ExpectWrappedMessage(cel_value, json.struct_value());

  Any any;
  any.PackFrom(json.struct_value());
  ExpectWrappedMessage(cel_value, any);
}

TEST_F(CelProtoWrapValueToMessageTest, WrapFailureStructBadKeyType) {
  std::vector<std::pair<CelValue, CelValue>> args = {
      {CelValue::CreateInt64(1L), CelValue::CreateBool(true)}};
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<CelMap> cel_map,
      CreateContainerBackedMap(
          absl::Span<std::pair<CelValue, CelValue>>(args.data(), args.size())));
  auto cel_value = CelValue::CreateMap(cel_map.get());

  Value json;
  ExpectNotWrapped(cel_value, json);
}

TEST_F(CelProtoWrapValueToMessageTest, WrapFailureStructBadValueType) {
  const std::string kField1 = "field1";
  TestMessage bad_value;
  std::vector<std::pair<CelValue, CelValue>> args = {
      {CelValue::CreateString(CelValue::StringHolder(&kField1)),
       MessageToCelValue(&bad_value)}};
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<CelMap> cel_map,
      CreateContainerBackedMap(
          absl::Span<std::pair<CelValue, CelValue>>(args.data(), args.size())));
  auto cel_value = CelValue::CreateMap(cel_map.get());
  Value json;
  ExpectNotWrapped(cel_value, json);
}

class TestMap : public CelMapBuilder {
 public:
  absl::StatusOr<const CelList*> ListKeys() const override {
    return absl::UnimplementedError("test");
  }
};

TEST_F(CelProtoWrapValueToMessageTest, WrapFailureStructListKeysUnimplemented) {
  const std::string kField1 = "field1";
  TestMap map;
  ASSERT_THAT(map.Add(CelValue::CreateString(CelValue::StringHolder(&kField1)),
                      CelValue::CreateString(CelValue::StringHolder(&kField1))),
              absl_testing::IsOk());

  auto cel_value = CelValue::CreateMap(&map);
  Value json;
  ExpectNotWrapped(cel_value, json);
}

TEST_F(CelProtoWrapValueToMessageTest, WrapFailureWrongType) {
  auto cel_value = CelValue::CreateNull();
  std::vector<const google::protobuf::Message*> wrong_types = {
      &BoolValue::default_instance(),   &BytesValue::default_instance(),
      &DoubleValue::default_instance(), &Duration::default_instance(),
      &FloatValue::default_instance(),  &Int32Value::default_instance(),
      &Int64Value::default_instance(),  &ListValue::default_instance(),
      &StringValue::default_instance(), &Struct::default_instance(),
      &Timestamp::default_instance(),   &UInt32Value::default_instance(),
      &UInt64Value::default_instance(),
  };
  for (const auto* wrong_type : wrong_types) {
    ExpectNotWrapped(cel_value, *wrong_type);
  }
}

TEST_F(CelProtoWrapValueToMessageTest, WrapFailureErrorToAny) {
  auto cel_value = CreateNoSuchFieldError(arena(), "error_field");
  ExpectNotWrapped(cel_value, Any::default_instance());
}

TEST_F(CelProtoWrapValueToMessageTest, WrapFieldMaskToValue) {
  FieldMask field_mask;
  ASSERT_TRUE(TextFormat::ParseFromString(R"pb(
                                            paths: "foo.bar" paths: "baz"
                                          )pb",
                                          &field_mask));
  CelValue value = MessageToCelValue(&field_mask);

  Value expected_message;
  ASSERT_TRUE(TextFormat::ParseFromString(R"pb(string_value: "foo.bar,baz")pb",
                                          &expected_message));

  ExpectWrappedMessage(value, expected_message);
}

TEST_F(CelProtoWrapValueToMessageTest, WrapMapWithFieldMaskToAny) {
  const std::string kField = "field_mask";
  FieldMask field_mask;
  ASSERT_TRUE(TextFormat::ParseFromString(R"pb(
                                            paths: "foo.bar" paths: "baz"
                                          )pb",
                                          &field_mask));
  CelValue value = MessageToCelValue(&field_mask);

  std::vector<std::pair<CelValue, CelValue>> args = {
      {CelValue::CreateString(CelValue::StringHolder(&kField)), value}};
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<CelMap> cel_map,
      CreateContainerBackedMap(
          absl::Span<std::pair<CelValue, CelValue>>(args.data(), args.size())));
  CelValue cel_value = CelValue::CreateMap(cel_map.get());

  Struct expected_struct;
  ASSERT_TRUE(
      TextFormat::ParseFromString(R"pb(
                                    fields {
                                      key: "field_mask"
                                      value { string_value: "foo.bar,baz" }
                                    }
                                  )pb",
                                  &expected_struct));
  Any expected_message;
  ASSERT_TRUE(expected_message.PackFrom(expected_struct));

  ExpectWrappedMessage(cel_value, expected_message);
}

TEST_F(CelProtoWrapValueToMessageTest, WrapListWithFieldMaskToAny) {
  FieldMask field_mask;
  ASSERT_TRUE(TextFormat::ParseFromString(R"pb(
                                            paths: "foo.bar" paths: "baz"
                                          )pb",
                                          &field_mask));
  CelValue value = MessageToCelValue(&field_mask);

  std::vector<CelValue> list_entries = {value};
  ContainerBackedListImpl cel_list(list_entries);
  CelValue list_value = CelValue::CreateList(&cel_list);

  ListValue expected_list;
  ASSERT_TRUE(TextFormat::ParseFromString(
      R"pb(values { string_value: "foo.bar,baz" })pb", &expected_list));
  Any expected_message;
  ASSERT_TRUE(expected_message.PackFrom(expected_list));

  ExpectWrappedMessage(list_value, expected_message);
}

TEST_F(CelProtoWrapValueToMessageTest, WrapEmptyToValue) {
  google::protobuf::Empty empty;
  CelValue value = MessageToCelValue(&empty);

  Value expected_message;
  expected_message.mutable_struct_value();

  ExpectWrappedMessage(value, expected_message);
}

TEST_F(CelProtoWrapValueToMessageTest, WrapMapWithEmptyToAny) {
  const std::string kField = "empty";
  google::protobuf::Empty empty;
  CelValue value = MessageToCelValue(&empty);

  std::vector<std::pair<CelValue, CelValue>> args = {
      {CelValue::CreateString(CelValue::StringHolder(&kField)), value}};
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<CelMap> cel_map,
      CreateContainerBackedMap(
          absl::Span<std::pair<CelValue, CelValue>>(args.data(), args.size())));
  auto cel_value = CelValue::CreateMap(cel_map.get());

  Struct expected_struct;
  (*expected_struct.mutable_fields())[kField].mutable_struct_value();
  Any expected_message;
  ASSERT_TRUE(expected_message.PackFrom(expected_struct));

  ExpectWrappedMessage(cel_value, expected_message);
}

TEST_F(CelProtoWrapValueToMessageTest, WrapListWithEmptyToAny) {
  google::protobuf::Empty empty;
  CelValue value = MessageToCelValue(&empty);

  std::vector<CelValue> list_entries = {value};
  ContainerBackedListImpl cel_list(list_entries);
  CelValue list_value = CelValue::CreateList(&cel_list);

  ListValue expected_list;
  expected_list.add_values()->mutable_struct_value();
  Any expected_message;
  ASSERT_TRUE(expected_message.PackFrom(expected_list));

  ExpectWrappedMessage(list_value, expected_message);
}

}  // namespace

}  // namespace google::api::expr::runtime::internal
