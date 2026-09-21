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

#include <sstream>
#include <string>
#include <utility>

#include "google/protobuf/struct.pb.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/cord.h"
#include "absl/strings/cord_test_helpers.h"
#include "common/native_type.h"
#include "common/value.h"
#include "common/value_testing.h"
#include "internal/testing.h"

namespace cel {
namespace {

using ::absl_testing::IsOk;
using ::testing::An;
using ::testing::Eq;
using ::testing::NotNull;
using ::testing::Optional;

using BytesValueTest = common_internal::ValueTest<>;

TEST_F(BytesValueTest, Kind) {
  EXPECT_EQ(BytesValue::WrapUnsafe("foo").kind(), BytesValue::kKind);
  EXPECT_EQ(Value(BytesValue::From(absl::Cord("foo"), arena())).kind(),
            BytesValue::kKind);
}

TEST_F(BytesValueTest, DebugString) {
  {
    std::ostringstream out;
    out << BytesValue::WrapUnsafe("foo");
    EXPECT_EQ(out.str(), "b\"foo\"");
  }
  {
    std::ostringstream out;
    out << BytesValue::From(absl::MakeFragmentedCord({"f", "o", "o"}), arena());
    EXPECT_EQ(out.str(), "b\"foo\"");
  }
  {
    std::ostringstream out;
    out << Value(BytesValue::From(absl::Cord("foo"), arena()));
    EXPECT_EQ(out.str(), "b\"foo\"");
  }
}

TEST_F(BytesValueTest, ConvertToJson) {
  auto* message = NewArenaValueMessage();
  EXPECT_THAT(BytesValue::WrapUnsafe("foo").ConvertToJson(
                  descriptor_pool(), message_factory(), message),
              IsOk());
  EXPECT_THAT(*message, EqualsValueTextProto(R"pb(string_value: "Zm9v")pb"));
}

TEST_F(BytesValueTest, NativeValue) {
  std::string scratch;
  EXPECT_EQ(BytesValue::WrapUnsafe("foo").NativeString(), "foo");
  EXPECT_EQ(BytesValue::WrapUnsafe("foo").NativeString(scratch), "foo");
  EXPECT_EQ(BytesValue::WrapUnsafe("foo").NativeCord(), "foo");
}

TEST_F(BytesValueTest, TryFlat) {
  EXPECT_THAT(BytesValue::WrapUnsafe("foo").TryFlat(), Optional(Eq("foo")));
  EXPECT_THAT(
      BytesValue::From(
          absl::MakeFragmentedCord({"Hello, World!", "World, Hello!"}), arena())
          .TryFlat(),
      Eq(std::nullopt));
}

TEST_F(BytesValueTest, ToString) {
  EXPECT_EQ(BytesValue::WrapUnsafe("foo").ToString(), "foo");
  EXPECT_EQ(BytesValue::From(absl::MakeFragmentedCord({"f", "o", "o"}), arena())
                .ToString(),
            "foo");
}

TEST_F(BytesValueTest, CopyToString) {
  std::string out;
  BytesValue::WrapUnsafe("foo").CopyToString(&out);
  EXPECT_EQ(out, "foo");
  BytesValue::From(absl::MakeFragmentedCord({"f", "o", "o"}), arena())
      .CopyToString(&out);
  EXPECT_EQ(out, "foo");
}

TEST_F(BytesValueTest, AppendToString) {
  std::string out;
  BytesValue::WrapUnsafe("foo").AppendToString(&out);
  EXPECT_EQ(out, "foo");
  BytesValue::From(absl::MakeFragmentedCord({"f", "o", "o"}), arena())
      .AppendToString(&out);
  EXPECT_EQ(out, "foofoo");
}

TEST_F(BytesValueTest, ToCord) {
  EXPECT_EQ(BytesValue::WrapUnsafe("foo").ToCord(), "foo");
  EXPECT_EQ(BytesValue::From(absl::MakeFragmentedCord({"f", "o", "o"}), arena())
                .ToCord(),
            "foo");
}

TEST_F(BytesValueTest, CopyToCord) {
  absl::Cord out;
  BytesValue::WrapUnsafe("foo").CopyToCord(&out);
  EXPECT_EQ(out, "foo");
  BytesValue::From(absl::MakeFragmentedCord({"f", "o", "o"}), arena())
      .CopyToCord(&out);
  EXPECT_EQ(out, "foo");
}

TEST_F(BytesValueTest, AppendToCord) {
  absl::Cord out;
  BytesValue::WrapUnsafe("foo").AppendToCord(&out);
  EXPECT_EQ(out, "foo");
  BytesValue::From(absl::MakeFragmentedCord({"f", "o", "o"}), arena())
      .AppendToCord(&out);
  EXPECT_EQ(out, "foofoo");
}

TEST_F(BytesValueTest, NativeTypeId) {
  EXPECT_EQ(NativeTypeId::Of(BytesValue::WrapUnsafe("foo")),
            NativeTypeId::For<BytesValue>());
  EXPECT_EQ(
      NativeTypeId::Of(Value(BytesValue::From(absl::Cord("foo"), arena()))),
      NativeTypeId::For<BytesValue>());
}

TEST_F(BytesValueTest, StringViewEquality) {
  // NOLINTBEGIN(readability/check)
  EXPECT_TRUE(BytesValue::WrapUnsafe("foo") == "foo");
  EXPECT_FALSE(BytesValue::WrapUnsafe("foo") == "bar");

  EXPECT_TRUE("foo" == BytesValue::WrapUnsafe("foo"));
  EXPECT_FALSE("bar" == BytesValue::WrapUnsafe("foo"));
  // NOLINTEND(readability/check)
}

TEST_F(BytesValueTest, StringViewInequality) {
  // NOLINTBEGIN(readability/check)
  EXPECT_FALSE(BytesValue::WrapUnsafe("foo") != "foo");
  EXPECT_TRUE(BytesValue::WrapUnsafe("foo") != "bar");

  EXPECT_FALSE("foo" != BytesValue::WrapUnsafe("foo"));
  EXPECT_TRUE("bar" != BytesValue::WrapUnsafe("foo"));
  // NOLINTEND(readability/check)
}

TEST_F(BytesValueTest, Comparison) {
  EXPECT_LT(BytesValue::WrapUnsafe("bar"), BytesValue::WrapUnsafe("foo"));
  EXPECT_FALSE(BytesValue::WrapUnsafe("foo") < BytesValue::WrapUnsafe("foo"));
  EXPECT_FALSE(BytesValue::WrapUnsafe("foo") < BytesValue::WrapUnsafe("bar"));
}

TEST_F(BytesValueTest, StringInputStream) {
  BytesValue value = BytesValue::WrapUnsafe("foo");
  BytesValueInputStream stream(&value);
  const void* data;
  int size;
  absl::Cord cord;
  ASSERT_TRUE(stream.Next(&data, &size));
  EXPECT_THAT(data, NotNull());
  EXPECT_EQ(size, 3);
  EXPECT_EQ(stream.ByteCount(), 3);
  stream.BackUp(size);
  ASSERT_TRUE(stream.Skip(3));
  EXPECT_FALSE(stream.ReadCord(&cord, 3));
  EXPECT_FALSE(stream.Next(&data, &size));
}

TEST_F(BytesValueTest, CordInputStream) {
  BytesValue value = BytesValue::From(absl::Cord("foo"), arena());
  BytesValueInputStream stream(&value);
  const void* data;
  int size;
  absl::Cord cord;
  ASSERT_TRUE(stream.Next(&data, &size));
  EXPECT_THAT(data, NotNull());
  EXPECT_EQ(size, 3);
  EXPECT_EQ(stream.ByteCount(), 3);
  stream.BackUp(size);
  ASSERT_TRUE(stream.Skip(3));
  EXPECT_FALSE(stream.ReadCord(&cord, 3));
  EXPECT_FALSE(stream.Next(&data, &size));
}

TEST_F(BytesValueTest, ArenaStringOutputStream) {
  BytesValue value = BytesValue();
  {
    BytesValueOutputStream stream(value);
    EXPECT_THAT(stream.AllowsAliasing(), An<bool>());
    EXPECT_EQ(stream.ByteCount(), 0);
    google::protobuf::Value value_proto;
    auto* struct_proto = value_proto.mutable_struct_value();
    (*struct_proto->mutable_fields())["foo"].set_string_value("bar");
    (*struct_proto->mutable_fields())["baz"].set_number_value(3.14159);
    ASSERT_TRUE(value_proto.SerializePartialToZeroCopyStream(&stream));
    EXPECT_EQ(std::move(stream).Consume(arena()),
              value_proto.SerializePartialAsString());
  }
  {
    BytesValueOutputStream stream(value);
    EXPECT_EQ(std::move(stream).Consume(arena()), "");
  }
}

TEST_F(BytesValueTest, StringOutputStream) {
  BytesValue value = BytesValue();
  {
    BytesValueOutputStream stream(value);
    EXPECT_THAT(stream.AllowsAliasing(), An<bool>());
    EXPECT_EQ(stream.ByteCount(), 0);
    google::protobuf::Value value_proto;
    auto* struct_proto = value_proto.mutable_struct_value();
    (*struct_proto->mutable_fields())["foo"].set_string_value("bar");
    (*struct_proto->mutable_fields())["baz"].set_number_value(3.14159);
    ASSERT_TRUE(value_proto.SerializePartialToZeroCopyStream(&stream));
    EXPECT_EQ(std::move(stream).Consume(arena()),
              value_proto.SerializePartialAsString());
  }
  {
    BytesValueOutputStream stream(value);
    EXPECT_EQ(std::move(stream).Consume(arena()), "");
  }
}

TEST_F(BytesValueTest, CordOutputStream) {
  absl::Cord cord;
  BytesValue value = BytesValue::WrapUnsafe(&cord);
  {
    BytesValueOutputStream stream(value);
    EXPECT_THAT(stream.AllowsAliasing(), An<bool>());
    EXPECT_EQ(stream.ByteCount(), 0);
    google::protobuf::Value value_proto;
    auto* struct_proto = value_proto.mutable_struct_value();
    (*struct_proto->mutable_fields())["foo"].set_string_value("bar");
    (*struct_proto->mutable_fields())["baz"].set_number_value(3.14159);
    ASSERT_TRUE(value_proto.SerializePartialToZeroCopyStream(&stream));
    EXPECT_EQ(std::move(stream).Consume(arena()),
              value_proto.SerializePartialAsString());
  }
  {
    BytesValueOutputStream stream(value);
    EXPECT_EQ(std::move(stream).Consume(arena()), "");
  }
}

}  // namespace
}  // namespace cel
