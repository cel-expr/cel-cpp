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

#include "common/values/legacy_list_value.h"

#include <cstddef>
#include <vector>

#include "google/protobuf/struct.pb.h"
#include "absl/log/absl_check.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/string_view.h"
#include "common/legacy_value.h"
#include "common/memory.h"
#include "common/value.h"
#include "common/value_testing.h"
#include "eval/public/cel_value.h"
#include "eval/public/containers/internal_field_backed_list_impl.h"
#include "eval/public/structs/cel_proto_wrapper.h"
#include "internal/testing.h"
#include "runtime/runtime_options.h"
#include "cel/expr/conformance/proto3/test_all_types.pb.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"

namespace cel {
namespace {

using ::absl_testing::IsOk;
using ::cel::expr::conformance::proto3::TestAllTypes;
using ::cel::test::BoolValueIs;
using ::cel::test::IntValueIs;
using ::google::api::expr::runtime::CelList;
using ::google::api::expr::runtime::CelProtoWrapper;
using ::google::api::expr::runtime::CelValue;
using ::google::api::expr::runtime::internal::FieldBackedListImpl;
using ::testing::SizeIs;

class LegacyListValueTest : public common_internal::ValueTest<> {
 protected:
  void SetUp() override { Populate(message_); }

  static void Populate(TestAllTypes& message) {
    message.add_repeated_bool(true);
    message.add_repeated_bool(false);
    message.add_repeated_int32(-1);
    message.add_repeated_int32(2);
    message.add_repeated_int64(-3);
    message.add_repeated_uint32(4);
    message.add_repeated_uint64(5);
    message.add_repeated_float(6.5);
    message.add_repeated_double(7.5);
    message.add_repeated_string("foo");
    message.add_repeated_string("bar");
    message.add_repeated_bytes("baz");
    message.add_repeated_nested_enum(TestAllTypes::BAR);
    message.add_repeated_nested_message()->set_bb(1);
    message.add_repeated_nested_message()->set_bb(2);
    message.add_repeated_int64_wrapper()->set_value(8);
    message.add_repeated_timestamp()->set_seconds(9);
    message.add_repeated_null_value(google::protobuf::NULL_VALUE);
  }

  const FieldBackedListImpl* MakeList(const TestAllTypes& message,
                                      absl::string_view field_name) {
    const google::protobuf::FieldDescriptor* field =
        message.GetDescriptor()->FindFieldByName(field_name);
    ABSL_CHECK(field != nullptr) << field_name;
    return google::protobuf::Arena::Create<FieldBackedListImpl>(
        arena(), &message, field, &CelProtoWrapper::InternalWrapMessage,
        arena());
  }

  Value ToModern(const CelList* list) {
    Value list_value;
    ABSL_CHECK_OK(ModernValue(arena(), CelValue::CreateList(list), list_value));
    return list_value;
  }

  // Returns the elements produced by iterating `list_value` with `Next1`.
  std::vector<Value> Iterate(const Value& list_value) {
    auto iterator = list_value.GetList().NewIterator();
    ABSL_CHECK_OK(iterator);
    std::vector<Value> elements;
    Value element;
    while (true) {
      auto ok = (*iterator)->Next1(descriptor_pool(), message_factory(),
                                   arena(), &element);
      ABSL_CHECK_OK(ok);
      if (!*ok) {
        break;
      }
      elements.push_back(element);
    }
    return elements;
  }

  // Returns the elements of `list` as converted by `ModernValue`, which is the
  // legacy behavior.
  std::vector<Value> LegacyElements(const CelList* list) {
    std::vector<Value> elements;
    for (int i = 0; i < list->size(); ++i) {
      Value element;
      ABSL_CHECK_OK(ModernValue(arena(), list->Get(arena(), i), element));
      elements.push_back(element);
    }
    return elements;
  }

  TestAllTypes message_;
};

TEST_F(LegacyListValueTest, FieldBackedListBecomesParsedRepeatedField) {
  Value list_value = ToModern(MakeList(message_, "repeated_nested_message"));
  EXPECT_TRUE(list_value.IsParsedRepeatedField());
}

TEST_F(LegacyListValueTest, FieldBackedListMatchesLegacyElements) {
  for (absl::string_view field_name : {
           "repeated_bool",
           "repeated_int32",
           "repeated_int64",
           "repeated_uint32",
           "repeated_uint64",
           "repeated_float",
           "repeated_double",
           "repeated_string",
           "repeated_bytes",
           "repeated_nested_enum",
           "repeated_nested_message",
           "repeated_int64_wrapper",
           "repeated_timestamp",
           "repeated_any",
           "repeated_sint32",
       }) {
    SCOPED_TRACE(field_name);
    const FieldBackedListImpl* list = MakeList(message_, field_name);
    std::vector<Value> actual = Iterate(ToModern(list));
    std::vector<Value> expected = LegacyElements(list);
    ASSERT_THAT(actual, SizeIs(expected.size()));
    for (size_t i = 0; i < actual.size(); ++i) {
      EXPECT_EQ(actual[i].kind(), expected[i].kind());
      Value equal;
      ASSERT_THAT(actual[i].Equal(expected[i], descriptor_pool(),
                                  message_factory(), arena(), &equal),
                  IsOk());
      EXPECT_THAT(equal, BoolValueIs(true))
          << actual[i].DebugString() << " vs " << expected[i].DebugString();
    }
  }
}

TEST_F(LegacyListValueTest, FieldBackedListMessageElementsAreBorrowed) {
  std::vector<Value> actual =
      Iterate(ToModern(MakeList(message_, "repeated_nested_message")));
  ASSERT_THAT(actual, SizeIs(2));
  for (int i = 0; i < 2; ++i) {
    ASSERT_TRUE(actual[i].IsParsedMessage());
    EXPECT_EQ(cel::to_address(actual[i].GetParsedMessage()),
              &message_.repeated_nested_message(i));
    Value bb;
    ASSERT_THAT(actual[i].GetParsedMessage().GetFieldByName(
                    "bb", ProtoWrapperTypeOptions::kUnsetProtoDefault,
                    descriptor_pool(), message_factory(), arena(), &bb),
                IsOk());
    EXPECT_THAT(bb, IntValueIs(i + 1));
  }
}

TEST_F(LegacyListValueTest, FieldBackedListNullValueStaysLegacy) {
  const FieldBackedListImpl* list = MakeList(message_, "repeated_null_value");
  Value list_value = ToModern(list);
  EXPECT_FALSE(list_value.IsParsedRepeatedField());
  std::vector<Value> actual = Iterate(list_value);
  ASSERT_THAT(actual, SizeIs(1));
  EXPECT_THAT(actual[0], IntValueIs(0));
}

TEST_F(LegacyListValueTest, FieldBackedListOnArenaStaysLegacy) {
  auto* arena_message = google::protobuf::Arena::Create<TestAllTypes>(arena());
  Populate(*arena_message);
  const FieldBackedListImpl* list =
      MakeList(*arena_message, "repeated_nested_message");
  Value list_value = ToModern(list);
  EXPECT_FALSE(list_value.IsParsedRepeatedField());
  EXPECT_THAT(Iterate(list_value), SizeIs(2));
}

}  // namespace
}  // namespace cel
