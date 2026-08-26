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

#include "common/values/legacy_struct_value.h"

#include <cstdint>
#include <string>
#include <vector>

#include "google/protobuf/struct.pb.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/string_view.h"
#include "common/legacy_value.h"
#include "common/value.h"
#include "common/value_testing.h"
#include "common/values/legacy_list_value.h"
#include "common/values/legacy_map_value.h"
#include "eval/public/cel_value.h"
#include "eval/public/structs/proto_message_type_adapter.h"
#include "internal/testing.h"
#include "runtime/runtime_options.h"
#include "cel/expr/conformance/proto3/test_all_types.pb.h"

namespace cel {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::cel::expr::conformance::proto3::TestAllTypes;
using ::cel::test::BoolValueIs;
using ::cel::test::ErrorValueIs;
using ::cel::test::IntValueIs;
using ::cel::test::StringValueIs;
using ::google::api::expr::runtime::CelValue;
using ::testing::NotNull;

using LegacyStructValueTest = common_internal::ValueTest<>;

TEST_F(LegacyStructValueTest, RepeatedFieldAccess) {
  TestAllTypes message;
  message.add_repeated_int32(10);
  message.add_repeated_int32(20);

  common_internal::LegacyStructValue struct_value(
      &message, &google::api::expr::runtime::GetGenericProtoTypeInfoInstance());

  Value field_value;
  ASSERT_THAT(struct_value.GetFieldByName(
                  "repeated_int32", ProtoWrapperTypeOptions::kUnsetProtoDefault,
                  descriptor_pool(), message_factory(), arena(), &field_value),
              IsOk());

  EXPECT_TRUE(field_value.IsList());
  auto list_value = field_value.GetList();
  EXPECT_THAT(list_value.Size(), IsOkAndHolds(2));

  // Verify legacy CelList interface
  auto legacy_list = common_internal::AsLegacyListValue(field_value);
  ASSERT_TRUE(legacy_list.has_value());
  const auto* cel_list = legacy_list->cel_list();
  ASSERT_THAT(cel_list, NotNull());
  EXPECT_EQ(cel_list->size(), 2);

  CelValue elem0 = cel_list->Get(arena(), 0);
  ASSERT_TRUE(elem0.IsInt64());
  EXPECT_EQ(elem0.Int64OrDie(), 10);

  CelValue elem1 = cel_list->Get(arena(), 1);
  ASSERT_TRUE(elem1.IsInt64());
  EXPECT_EQ(elem1.Int64OrDie(), 20);
}

TEST_F(LegacyStructValueTest, RepeatedMessageFieldAccess) {
  TestAllTypes message;
  auto* elem0 = message.add_repeated_nested_message();
  elem0->set_bb(42);

  common_internal::LegacyStructValue struct_value(
      &message, &google::api::expr::runtime::GetGenericProtoTypeInfoInstance());

  Value field_value;
  ASSERT_THAT(struct_value.GetFieldByName(
                  "repeated_nested_message",
                  ProtoWrapperTypeOptions::kUnsetProtoDefault,
                  descriptor_pool(), message_factory(), arena(), &field_value),
              IsOk());

  EXPECT_TRUE(field_value.IsList());
  auto list_value = field_value.GetList();
  EXPECT_THAT(list_value.Size(), IsOkAndHolds(1));

  Value first_elem;
  ASSERT_THAT(list_value.Get(0, descriptor_pool(), message_factory(), arena(),
                             &first_elem),
              IsOk());
  EXPECT_TRUE(common_internal::IsLegacyStructValue(first_elem));

  // Verify via CelList
  auto legacy_list = common_internal::AsLegacyListValue(field_value);
  ASSERT_TRUE(legacy_list.has_value());
  const auto* cel_list = legacy_list->cel_list();
  ASSERT_THAT(cel_list, NotNull());

  CelValue cel_elem = cel_list->Get(arena(), 0);
  ASSERT_TRUE(cel_elem.IsMessage());
  EXPECT_EQ(cel_elem.MessageOrDie()->GetDescriptor(), elem0->GetDescriptor());
  EXPECT_EQ(
      static_cast<const TestAllTypes::NestedMessage*>(cel_elem.MessageOrDie())
          ->bb(),
      42);
}

TEST_F(LegacyStructValueTest, MapFieldAccess) {
  TestAllTypes message;
  (*message.mutable_map_string_string())["hello"] = "world";

  common_internal::LegacyStructValue struct_value(
      &message, &google::api::expr::runtime::GetGenericProtoTypeInfoInstance());

  Value field_value;
  ASSERT_THAT(
      struct_value.GetFieldByName(
          "map_string_string", ProtoWrapperTypeOptions::kUnsetProtoDefault,
          descriptor_pool(), message_factory(), arena(), &field_value),
      IsOk());

  EXPECT_TRUE(field_value.IsMap());
  auto map_value = field_value.GetMap();
  EXPECT_THAT(map_value.Size(), IsOkAndHolds(1));

  // Verify legacy CelMap interface
  auto legacy_map = common_internal::AsLegacyMapValue(field_value);
  ASSERT_TRUE(legacy_map.has_value());
  const auto* cel_map = legacy_map->cel_map();
  ASSERT_THAT(cel_map, NotNull());
  EXPECT_EQ(cel_map->size(), 1);

  std::string key_str = "hello";
  CelValue cel_key = CelValue::CreateString(&key_str);
  auto cel_result = cel_map->Get(arena(), cel_key);
  ASSERT_TRUE(cel_result.has_value());
  ASSERT_TRUE(cel_result->IsString());
  EXPECT_EQ(cel_result->StringOrDie().value(), "world");

  auto has_res = cel_map->Has(cel_key);
  ASSERT_THAT(has_res, IsOk());
  EXPECT_TRUE(*has_res);
}

TEST_F(LegacyStructValueTest, MapFieldKeyTypeValidation) {
  TestAllTypes message;
  (*message.mutable_map_int32_int32())[1] = 2;

  common_internal::LegacyStructValue struct_value(
      &message, &google::api::expr::runtime::GetGenericProtoTypeInfoInstance());

  Value field_value;
  ASSERT_THAT(
      struct_value.GetFieldByName(
          "map_int32_int32", ProtoWrapperTypeOptions::kUnsetProtoDefault,
          descriptor_pool(), message_factory(), arena(), &field_value),
      IsOk());

  auto legacy_map = common_internal::AsLegacyMapValue(field_value);
  ASSERT_TRUE(legacy_map.has_value());
  const auto* cel_map = legacy_map->cel_map();
  ASSERT_THAT(cel_map, NotNull());

  // Valid key
  CelValue int_key = CelValue::CreateInt64(1);
  auto has_res = cel_map->Has(int_key);
  ASSERT_THAT(has_res, IsOk());
  EXPECT_TRUE(*has_res);

  // Invalid key type (string key on int32 map)
  std::string str_key_val = "1";
  CelValue str_key = CelValue::CreateString(&str_key_val);
  auto invalid_has_res = cel_map->Has(str_key);
  EXPECT_THAT(invalid_has_res, StatusIs(absl::StatusCode::kInvalidArgument));

  auto invalid_get_res = cel_map->Get(arena(), str_key);
  ASSERT_TRUE(invalid_get_res.has_value());
  ASSERT_TRUE(invalid_get_res->IsError());
  EXPECT_THAT(*invalid_get_res->ErrorOrDie(),
              StatusIs(absl::StatusCode::kInvalidArgument));

  auto invalid_subscript_res = (*cel_map)[str_key];
  ASSERT_TRUE(invalid_subscript_res.has_value());
  ASSERT_TRUE(invalid_subscript_res->IsError());
  EXPECT_THAT(*invalid_subscript_res->ErrorOrDie(),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(LegacyStructValueTest, JsonStructAccess) {
  TestAllTypes message;
  auto* struct_field = message.mutable_single_struct();
  (*struct_field->mutable_fields())["key"].set_string_value("value");

  common_internal::LegacyStructValue struct_value(
      &message, &google::api::expr::runtime::GetGenericProtoTypeInfoInstance());

  Value field_value;
  ASSERT_THAT(struct_value.GetFieldByName(
                  "single_struct", ProtoWrapperTypeOptions::kUnsetProtoDefault,
                  descriptor_pool(), message_factory(), arena(), &field_value),
              IsOk());

  EXPECT_TRUE(field_value.IsMap());
  auto map_value = field_value.GetMap();
  EXPECT_THAT(map_value.Size(), IsOkAndHolds(1));

  // Verify legacy CelMap interface
  auto legacy_map = common_internal::AsLegacyMapValue(field_value);
  ASSERT_TRUE(legacy_map.has_value());
  const auto* cel_map = legacy_map->cel_map();
  ASSERT_THAT(cel_map, NotNull());
  EXPECT_EQ(cel_map->size(), 1);

  std::string key_str = "key";
  CelValue cel_key = CelValue::CreateString(&key_str);
  auto cel_result = cel_map->Get(arena(), cel_key);
  ASSERT_TRUE(cel_result.has_value());
  ASSERT_TRUE(cel_result->IsString());
  EXPECT_EQ(cel_result->StringOrDie().value(), "value");
}

TEST_F(LegacyStructValueTest, JsonListAccess) {
  TestAllTypes message;
  auto* list_field = message.mutable_single_value()->mutable_list_value();
  list_field->add_values()->set_string_value("item");

  common_internal::LegacyStructValue struct_value(
      &message, &google::api::expr::runtime::GetGenericProtoTypeInfoInstance());

  Value field_value;
  ASSERT_THAT(struct_value.GetFieldByName(
                  "single_value", ProtoWrapperTypeOptions::kUnsetProtoDefault,
                  descriptor_pool(), message_factory(), arena(), &field_value),
              IsOk());

  EXPECT_TRUE(field_value.IsList());
  auto list_value = field_value.GetList();
  EXPECT_THAT(list_value.Size(), IsOkAndHolds(1));

  // Verify legacy CelList interface
  auto legacy_list = common_internal::AsLegacyListValue(field_value);
  ASSERT_TRUE(legacy_list.has_value());
  const auto* cel_list = legacy_list->cel_list();
  ASSERT_THAT(cel_list, NotNull());
  EXPECT_EQ(cel_list->size(), 1);

  CelValue elem = cel_list->Get(arena(), 0);
  ASSERT_TRUE(elem.IsString());
  EXPECT_EQ(elem.StringOrDie().value(), "item");
}

TEST_F(LegacyStructValueTest, SingularMessageAccess) {
  TestAllTypes message;
  message.mutable_single_nested_message()->set_bb(100);

  common_internal::LegacyStructValue struct_value(
      &message, &google::api::expr::runtime::GetGenericProtoTypeInfoInstance());

  Value field_value;
  ASSERT_THAT(
      struct_value.GetFieldByName(
          "single_nested_message", ProtoWrapperTypeOptions::kUnsetProtoDefault,
          descriptor_pool(), message_factory(), arena(), &field_value),
      IsOk());

  EXPECT_TRUE(common_internal::IsLegacyStructValue(field_value));
  auto nested_struct = common_internal::GetLegacyStructValue(field_value);

  Value bb_value;
  ASSERT_THAT(nested_struct.GetFieldByName(
                  "bb", ProtoWrapperTypeOptions::kUnsetProtoDefault,
                  descriptor_pool(), message_factory(), arena(), &bb_value),
              IsOk());
  EXPECT_TRUE(bb_value.IsInt());
  EXPECT_EQ(bb_value.GetInt().NativeValue(), 100);
}

TEST_F(LegacyStructValueTest, WrapLegacyFieldAccessResultParsedRepeatedField) {
  TestAllTypes message;
  message.add_repeated_int32(10);
  message.add_repeated_int32(20);

  const auto* field_desc =
      message.GetDescriptor()->FindFieldByName("repeated_int32");
  Value val = ParsedRepeatedFieldValue(&message, field_desc, arena());
  interop_internal::WrapLegacyFieldAccessResult(arena(), &val);

  EXPECT_TRUE(val.IsList());
  auto list_val = val.GetList();
  EXPECT_THAT(list_val.IsEmpty(), IsOkAndHolds(false));
  EXPECT_FALSE(list_val.IsZeroValue());
  EXPECT_THAT(list_val.Size(), IsOkAndHolds(2));
  EXPECT_THAT(list_val.Contains(IntValue(10), descriptor_pool(),
                                message_factory(), arena()),
              IsOkAndHolds(BoolValueIs(true)));

  Value elem;
  ASSERT_THAT(
      list_val.Get(0, descriptor_pool(), message_factory(), arena(), &elem),
      IsOk());
  EXPECT_THAT(elem, IntValueIs(10));

  std::vector<int64_t> elements;
  ASSERT_THAT(list_val.ForEach(
                  [&](const Value& v) -> absl::StatusOr<bool> {
                    elements.push_back(v.GetInt().NativeValue());
                    return true;
                  },
                  descriptor_pool(), message_factory(), arena()),
              IsOk());
  EXPECT_THAT(elements, testing::ElementsAre(10, 20));

  auto legacy_list = common_internal::AsLegacyListValue(val);
  ASSERT_TRUE(legacy_list.has_value());
  const auto* cel_list = legacy_list->cel_list();
  ASSERT_THAT(cel_list, NotNull());
  EXPECT_EQ(cel_list->size(), 2);
  EXPECT_FALSE(cel_list->empty());
  EXPECT_EQ(cel_list->Get(arena(), 0).Int64OrDie(), 10);
  EXPECT_EQ((*cel_list)[1].Int64OrDie(), 20);

  Value cloned = val.Clone(arena());
  EXPECT_TRUE(cloned.IsList());
  EXPECT_THAT(cloned.GetList().Size(), IsOkAndHolds(2));
}

TEST_F(LegacyStructValueTest, WrapLegacyFieldAccessResultParsedJsonList) {
  google::protobuf::ListValue list_proto;
  list_proto.add_values()->set_string_value("item1");
  list_proto.add_values()->set_string_value("item2");

  Value val = ParsedJsonListValue(&list_proto, arena());
  interop_internal::WrapLegacyFieldAccessResult(arena(), &val);

  EXPECT_TRUE(val.IsList());
  auto list_val = val.GetList();
  EXPECT_THAT(list_val.IsEmpty(), IsOkAndHolds(false));
  EXPECT_THAT(list_val.Size(), IsOkAndHolds(2));
  EXPECT_THAT(list_val.Contains(StringValue("item1"), descriptor_pool(),
                                message_factory(), arena()),
              IsOkAndHolds(BoolValueIs(true)));

  Value elem;
  ASSERT_THAT(
      list_val.Get(0, descriptor_pool(), message_factory(), arena(), &elem),
      IsOk());
  EXPECT_THAT(elem, StringValueIs("item1"));

  std::vector<std::string> elements;
  ASSERT_THAT(list_val.ForEach(
                  [&](const Value& v) -> absl::StatusOr<bool> {
                    elements.push_back(v.GetString().ToString());
                    return true;
                  },
                  descriptor_pool(), message_factory(), arena()),
              IsOk());
  EXPECT_THAT(elements, testing::ElementsAre("item1", "item2"));

  auto legacy_list = common_internal::AsLegacyListValue(val);
  ASSERT_TRUE(legacy_list.has_value());
  const auto* cel_list = legacy_list->cel_list();
  ASSERT_THAT(cel_list, NotNull());
  EXPECT_EQ(cel_list->size(), 2);
  EXPECT_FALSE(cel_list->empty());
  EXPECT_EQ(cel_list->Get(arena(), 0).StringOrDie().value(), "item1");
  EXPECT_EQ((*cel_list)[1].StringOrDie().value(), "item2");

  Value cloned = val.Clone(arena());
  EXPECT_TRUE(cloned.IsList());
  EXPECT_THAT(cloned.GetList().Size(), IsOkAndHolds(2));
}

TEST_F(LegacyStructValueTest, WrapLegacyFieldAccessResultParsedMapField) {
  TestAllTypes message;
  (*message.mutable_map_string_string())["key1"] = "val1";
  (*message.mutable_map_string_string())["key2"] = "val2";

  const auto* field_desc =
      message.GetDescriptor()->FindFieldByName("map_string_string");
  Value val = ParsedMapFieldValue(&message, field_desc, arena());
  interop_internal::WrapLegacyFieldAccessResult(arena(), &val);

  EXPECT_TRUE(val.IsMap());
  auto map_val = val.GetMap();
  EXPECT_THAT(map_val.IsEmpty(), IsOkAndHolds(false));
  EXPECT_FALSE(map_val.IsZeroValue());
  EXPECT_THAT(map_val.Size(), IsOkAndHolds(2));
  EXPECT_THAT(map_val.Has(StringValue("key1"), descriptor_pool(),
                          message_factory(), arena()),
              IsOkAndHolds(BoolValueIs(true)));
  EXPECT_THAT(map_val.Has(StringValue("missing"), descriptor_pool(),
                          message_factory(), arena()),
              IsOkAndHolds(BoolValueIs(false)));
  EXPECT_THAT(
      map_val.Has(IntValue(1), descriptor_pool(), message_factory(), arena()),
      IsOkAndHolds(ErrorValueIs(StatusIs(absl::StatusCode::kInvalidArgument))));

  Value found_val;
  ASSERT_THAT(map_val.Find(StringValue("key1"), descriptor_pool(),
                           message_factory(), arena(), &found_val),
              IsOkAndHolds(true));
  EXPECT_THAT(found_val, StringValueIs("val1"));

  Value get_val;
  ASSERT_THAT(map_val.Get(StringValue("key2"), descriptor_pool(),
                          message_factory(), arena(), &get_val),
              IsOk());
  EXPECT_THAT(get_val, StringValueIs("val2"));

  ListValue keys;
  ASSERT_THAT(
      map_val.ListKeys(descriptor_pool(), message_factory(), arena(), &keys),
      IsOk());
  EXPECT_THAT(keys.Size(), IsOkAndHolds(2));

  auto legacy_map = common_internal::AsLegacyMapValue(val);
  ASSERT_TRUE(legacy_map.has_value());
  const auto* cel_map = legacy_map->cel_map();
  ASSERT_THAT(cel_map, NotNull());
  EXPECT_EQ(cel_map->size(), 2);
  EXPECT_FALSE(cel_map->empty());

  std::string k1 = "key1";
  CelValue cel_k1 = CelValue::CreateString(&k1);
  auto cel_find = cel_map->Get(arena(), cel_k1);
  ASSERT_TRUE(cel_find.has_value());
  EXPECT_EQ(cel_find->StringOrDie().value(), "val1");

  auto has_res = cel_map->Has(cel_k1);
  ASSERT_THAT(has_res, IsOk());
  EXPECT_TRUE(*has_res);

  Value cloned = val.Clone(arena());
  EXPECT_TRUE(cloned.IsMap());
  EXPECT_THAT(cloned.GetMap().Size(), IsOkAndHolds(2));
}

TEST_F(LegacyStructValueTest, WrapLegacyFieldAccessResultParsedJsonMap) {
  google::protobuf::Struct struct_proto;
  (*struct_proto.mutable_fields())["k1"].set_string_value("v1");
  (*struct_proto.mutable_fields())["k2"].set_string_value("v2");

  Value val = ParsedJsonMapValue(&struct_proto, arena());
  interop_internal::WrapLegacyFieldAccessResult(arena(), &val);

  EXPECT_TRUE(val.IsMap());
  auto map_val = val.GetMap();
  EXPECT_THAT(map_val.IsEmpty(), IsOkAndHolds(false));
  EXPECT_FALSE(map_val.IsZeroValue());
  EXPECT_THAT(map_val.Size(), IsOkAndHolds(2));
  EXPECT_THAT(map_val.Has(StringValue("k1"), descriptor_pool(),
                          message_factory(), arena()),
              IsOkAndHolds(BoolValueIs(true)));
  EXPECT_THAT(map_val.Has(StringValue("missing"), descriptor_pool(),
                          message_factory(), arena()),
              IsOkAndHolds(BoolValueIs(false)));
  EXPECT_THAT(
      map_val.Has(IntValue(1), descriptor_pool(), message_factory(), arena()),
      IsOkAndHolds(ErrorValueIs(StatusIs(absl::StatusCode::kInvalidArgument))));

  Value found_val;
  ASSERT_THAT(map_val.Find(StringValue("k1"), descriptor_pool(),
                           message_factory(), arena(), &found_val),
              IsOkAndHolds(true));
  EXPECT_THAT(found_val, StringValueIs("v1"));

  Value get_val;
  ASSERT_THAT(map_val.Get(StringValue("k2"), descriptor_pool(),
                          message_factory(), arena(), &get_val),
              IsOk());
  EXPECT_THAT(get_val, StringValueIs("v2"));

  ListValue keys;
  ASSERT_THAT(
      map_val.ListKeys(descriptor_pool(), message_factory(), arena(), &keys),
      IsOk());
  EXPECT_THAT(keys.Size(), IsOkAndHolds(2));

  auto legacy_map = common_internal::AsLegacyMapValue(val);
  ASSERT_TRUE(legacy_map.has_value());
  const auto* cel_map = legacy_map->cel_map();
  ASSERT_THAT(cel_map, NotNull());
  EXPECT_EQ(cel_map->size(), 2);
  EXPECT_FALSE(cel_map->empty());

  std::string k1 = "k1";
  CelValue cel_k1 = CelValue::CreateString(&k1);
  auto cel_find = cel_map->Get(arena(), cel_k1);
  ASSERT_TRUE(cel_find.has_value());
  EXPECT_EQ(cel_find->StringOrDie().value(), "v1");

  auto has_res = cel_map->Has(cel_k1);
  ASSERT_THAT(has_res, IsOk());
  EXPECT_TRUE(*has_res);

  Value cloned = val.Clone(arena());
  EXPECT_TRUE(cloned.IsMap());
  EXPECT_THAT(cloned.GetMap().Size(), IsOkAndHolds(2));
}

TEST_F(LegacyStructValueTest, WrapLegacyFieldAccessResultEmptyContainers) {
  Value empty_list = ListValue();
  interop_internal::WrapLegacyFieldAccessResult(arena(), &empty_list);
  EXPECT_TRUE(empty_list.IsList());
  EXPECT_THAT(empty_list.GetList().Size(), IsOkAndHolds(0));
  EXPECT_THAT(empty_list.GetList().IsEmpty(), IsOkAndHolds(true));
  auto legacy_list = common_internal::AsLegacyListValue(empty_list);
  ASSERT_TRUE(legacy_list.has_value());
  EXPECT_EQ(legacy_list->cel_list()->size(), 0);

  Value empty_map = MapValue();
  interop_internal::WrapLegacyFieldAccessResult(arena(), &empty_map);
  EXPECT_TRUE(empty_map.IsMap());
  EXPECT_THAT(empty_map.GetMap().Size(), IsOkAndHolds(0));
  EXPECT_THAT(empty_map.GetMap().IsEmpty(), IsOkAndHolds(true));
  auto legacy_map = common_internal::AsLegacyMapValue(empty_map);
  ASSERT_TRUE(legacy_map.has_value());
  EXPECT_EQ(legacy_map->cel_map()->size(), 0);
}

TEST_F(LegacyStructValueTest, ToLegacyValueParsedRepeatedField) {
  TestAllTypes message;
  message.add_repeated_int32(10);
  message.add_repeated_int32(20);

  const auto* field_desc =
      message.GetDescriptor()->FindFieldByName("repeated_int32");
  Value val = ParsedRepeatedFieldValue(&message, field_desc, arena());

  ASSERT_OK_AND_ASSIGN(CelValue cel_val,
                       interop_internal::ToLegacyValue(arena(), val));
  ASSERT_TRUE(cel_val.IsList());
  EXPECT_EQ(cel_val.ListOrDie()->size(), 2);
  EXPECT_EQ(cel_val.ListOrDie()->Get(arena(), 0).Int64OrDie(), 10);
  EXPECT_EQ(cel_val.ListOrDie()->Get(arena(), 1).Int64OrDie(), 20);

  CelValue unsafe_cel_val =
      common_internal::UnsafeLegacyValue(val, /*stable=*/false, arena());
  ASSERT_TRUE(unsafe_cel_val.IsList());
  EXPECT_EQ(unsafe_cel_val.ListOrDie()->size(), 2);
}

TEST_F(LegacyStructValueTest, ToLegacyValueParsedJsonList) {
  google::protobuf::ListValue list_proto;
  list_proto.add_values()->set_string_value("item1");
  list_proto.add_values()->set_string_value("item2");

  Value val = ParsedJsonListValue(&list_proto, arena());

  ASSERT_OK_AND_ASSIGN(CelValue cel_val,
                       interop_internal::ToLegacyValue(arena(), val));
  ASSERT_TRUE(cel_val.IsList());
  EXPECT_EQ(cel_val.ListOrDie()->size(), 2);
  EXPECT_EQ(cel_val.ListOrDie()->Get(arena(), 0).StringOrDie().value(),
            "item1");
  EXPECT_EQ(cel_val.ListOrDie()->Get(arena(), 1).StringOrDie().value(),
            "item2");

  CelValue unsafe_cel_val =
      common_internal::UnsafeLegacyValue(val, /*stable=*/false, arena());
  ASSERT_TRUE(unsafe_cel_val.IsList());
  EXPECT_EQ(unsafe_cel_val.ListOrDie()->size(), 2);
}

TEST_F(LegacyStructValueTest, ToLegacyValueParsedMapField) {
  TestAllTypes message;
  (*message.mutable_map_string_string())["k1"] = "v1";

  const auto* field_desc =
      message.GetDescriptor()->FindFieldByName("map_string_string");
  Value val = ParsedMapFieldValue(&message, field_desc, arena());

  ASSERT_OK_AND_ASSIGN(CelValue cel_val,
                       interop_internal::ToLegacyValue(arena(), val));
  ASSERT_TRUE(cel_val.IsMap());
  EXPECT_EQ(cel_val.MapOrDie()->size(), 1);
  std::string k1 = "k1";
  CelValue cel_k1 = CelValue::CreateString(&k1);
  auto res = cel_val.MapOrDie()->Get(arena(), cel_k1);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->StringOrDie().value(), "v1");

  CelValue unsafe_cel_val =
      common_internal::UnsafeLegacyValue(val, /*stable=*/false, arena());
  ASSERT_TRUE(unsafe_cel_val.IsMap());
  EXPECT_EQ(unsafe_cel_val.MapOrDie()->size(), 1);
}

TEST_F(LegacyStructValueTest, ToLegacyValueParsedJsonMap) {
  google::protobuf::Struct struct_proto;
  (*struct_proto.mutable_fields())["k1"].set_string_value("v1");

  Value val = ParsedJsonMapValue(&struct_proto, arena());

  ASSERT_OK_AND_ASSIGN(CelValue cel_val,
                       interop_internal::ToLegacyValue(arena(), val));
  ASSERT_TRUE(cel_val.IsMap());
  EXPECT_EQ(cel_val.MapOrDie()->size(), 1);
  std::string k1 = "k1";
  CelValue cel_k1 = CelValue::CreateString(&k1);
  auto res = cel_val.MapOrDie()->Get(arena(), cel_k1);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->StringOrDie().value(), "v1");

  CelValue unsafe_cel_val =
      common_internal::UnsafeLegacyValue(val, /*stable=*/false, arena());
  ASSERT_TRUE(unsafe_cel_val.IsMap());
  EXPECT_EQ(unsafe_cel_val.MapOrDie()->size(), 1);
}

}  // namespace
}  // namespace cel
