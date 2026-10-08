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

#include "common/ast/type_proto.h"

#include "cel/expr/checked.pb.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "common/ast/metadata.h"
#include "internal/proto_matchers.h"
#include "internal/testing.h"
#include "google/protobuf/text_format.h"

namespace cel::ast_internal {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::cel::internal::test::EqualsProto;
using ::testing::HasSubstr;

class TypeProtoRoundTripTest : public testing::TestWithParam<const char*> {};

TEST_P(TypeProtoRoundTripTest, RoundTrip) {
  cel::expr::Type input;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(GetParam(), &input));
  ASSERT_OK_AND_ASSIGN(TypeSpec native, TypeSpecFromProto(input));
  cel::expr::Type roundtrip;
  ASSERT_THAT(TypeSpecToProto(native, &roundtrip), IsOk());
  EXPECT_THAT(roundtrip, EqualsProto(input));
}

INSTANTIATE_TEST_SUITE_P(
    TypeProtoRoundTripTest, TypeProtoRoundTripTest,
    testing::Values(R"pb()pb", R"pb(dyn {})pb", R"pb(null: NULL_VALUE)pb",
                    R"pb(primitive: BOOL)pb", R"pb(wrapper: STRING)pb",
                    R"pb(well_known: TIMESTAMP)pb",
                    R"pb(list_type { elem_type { primitive: INT64 } })pb",
                    R"pb(map_type {
                           key_type { primitive: STRING }
                           value_type { primitive: DOUBLE }
                         })pb",
                    R"pb(function {
                           result_type { primitive: BOOL }
                           arg_types { primitive: INT64 }
                           arg_types { primitive: BYTES }
                         })pb",
                    R"pb(message_type: "com.example.Message")pb",
                    R"pb(type_param: "T")pb",
                    R"pb(type { primitive: UINT64 })pb", R"pb(type {})pb",
                    R"pb(error {})pb",
                    R"pb(abstract_type {
                           name: "MyType"
                           parameter_types { primitive: STRING }
                         })pb"));

TEST(TypeProtoTest, InvalidEnumErrors) {
  cel::expr::Type invalid_prim;
  invalid_prim.set_primitive(
      static_cast<cel::expr::Type::PrimitiveType>(999));
  EXPECT_THAT(
      TypeSpecFromProto(invalid_prim),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("PrimitiveType")));

  cel::expr::Type invalid_wk;
  invalid_wk.set_well_known(
      static_cast<cel::expr::Type::WellKnownType>(999));
  EXPECT_THAT(
      TypeSpecFromProto(invalid_wk),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("WellKnownType")));

  cel::expr::Type out;
  EXPECT_THAT(TypeSpecToProto(TypeSpec(static_cast<PrimitiveType>(999)), &out),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Unsupported primitive type")));
  EXPECT_THAT(
      TypeSpecToProto(TypeSpec(static_cast<WellKnownTypeSpec>(999)), &out),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Unsupported well-known type")));
}

TEST(TypeProtoTest, ToProtoClearsExistingOutput) {
  cel::expr::Type out;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        abstract_type {
          name: "StaleType"
          parameter_types { primitive: INT64 }
        }
      )pb",
      &out));

  ASSERT_THAT(TypeSpecToProto(TypeSpec(AbstractType("FreshType", {})), &out),
              IsOk());

  cel::expr::Type expected;
  expected.mutable_abstract_type()->set_name("FreshType");
  EXPECT_THAT(out, EqualsProto(expected));

  ASSERT_THAT(TypeSpecToProto(TypeSpec(), &out), IsOk());
  EXPECT_THAT(out, EqualsProto(cel::expr::Type()));
}

}  // namespace
}  // namespace cel::ast_internal
