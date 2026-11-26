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

#include "env/type_info.h"

#include <cstddef>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "common/ast/metadata.h"
#include "common/type.h"
#include "common/type_proto.h"
#include "env/config.h"
#include "internal/proto_matchers.h"
#include "internal/testing.h"
#include "internal/testing_descriptor_pool.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/text_format.h"

namespace cel {

std::ostream& operator<<(std::ostream& os, const Config::TypeInfo& type_info) {
  if (type_info.is_type_param) {
    os << "?";
  }
  os << type_info.name;
  if (!type_info.params.empty()) {
    os << "<";
    for (size_t i = 0; i < type_info.params.size(); ++i) {
      if (i > 0) os << ", ";
      os << type_info.params[i];
    }
    os << ">";
  }
  return os;
}

namespace {

using absl_testing::IsOk;
using absl_testing::StatusIs;
using testing::ValuesIn;

struct TestCase {
  Config::TypeInfo type_info;
  std::string expected_type_pb;
};

using TypeInfoTest = testing::TestWithParam<TestCase>;

TEST_P(TypeInfoTest, TypeInfo) {
  const TestCase& param = GetParam();
  cel::expr::Type expected_type_pb;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(param.expected_type_pb,
                                                  &expected_type_pb));

  google::protobuf::Arena arena;
  const google::protobuf::DescriptorPool* descriptor_pool =
      cel::internal::GetTestingDescriptorPool();
  ASSERT_OK_AND_ASSIGN(
      cel::Type actual_type,
      cel::TypeInfoToType(param.type_info, descriptor_pool, &arena));

  cel::expr::Type actual_type_pb;
  ASSERT_THAT(cel::TypeToProto(actual_type, &actual_type_pb), IsOk());
  EXPECT_THAT(actual_type_pb,
              cel::internal::test::EqualsProto(expected_type_pb));
}

std::vector<TestCase> GetTestCases() {
  return {
      TestCase{
          .type_info = {.name = "null"},
          .expected_type_pb = "null: NULL_VALUE",
      },
      TestCase{
          .type_info = {.name = "null_type"},
          .expected_type_pb = "null: NULL_VALUE",
      },
      TestCase{
          .type_info = {.name = "google.protobuf.NullValue"},
          .expected_type_pb = "null: NULL_VALUE",
      },
      TestCase{
          .type_info = {.name = "int"},
          .expected_type_pb = "primitive: INT64",
      },
      TestCase{
          .type_info = {.name = "list",
                        .params = {Config::TypeInfo{.name = "int"}}},
          .expected_type_pb = "list_type { elem_type { primitive: INT64 } }",
      },
      TestCase{
          .type_info = {.name = "list"},
          .expected_type_pb = "list_type { elem_type { dyn {} }}",
      },
      TestCase{
          .type_info = {.name = "map",
                        .params = {Config::TypeInfo{.name = "string"},
                                   Config::TypeInfo{.name = "int"}}},
          .expected_type_pb = "map_type { key_type { primitive: STRING } "
                              "value_type { primitive: INT64 }}",
      },
      TestCase{
          .type_info = {.name = "cel.expr.conformance.proto2.TestAllTypes"},
          .expected_type_pb =
              "message_type: 'cel.expr.conformance.proto2.TestAllTypes'",
      },
      TestCase{
          .type_info = {.name = "A",
                        .params = {Config::TypeInfo{.name = "B",
                                                    .is_type_param = true}}},
          .expected_type_pb =
              "abstract_type { name: 'A' parameter_types { type_param: 'B' } }",
      },
      TestCase{
          .type_info = {.name = "any"},
          .expected_type_pb = "well_known: ANY",
      },
      TestCase{
          .type_info = {.name = "timestamp"},
          .expected_type_pb = "well_known: TIMESTAMP",
      },
      TestCase{
          .type_info = {.name = "google.protobuf.DoubleValue"},
          .expected_type_pb = "wrapper: DOUBLE",
      },
      TestCase{
          .type_info = {.name = "double_wrapper"},
          .expected_type_pb = "wrapper: DOUBLE",
      },
      TestCase{
          .type_info = {.name = "type",
                        .params = {Config::TypeInfo{.name = "duration"}}},
          .expected_type_pb = "type: { well_known: DURATION }",
      },
      TestCase{
          .type_info = {.name = "parameterized",
                        .params = {{.name = "A", .is_type_param = true},
                                   {.name = "double"}}},
          .expected_type_pb = "abstract_type { name: 'parameterized' "
                              "parameter_types { type_param: 'A' } "
                              "parameter_types { primitive: DOUBLE } }",
      },
  };
}

INSTANTIATE_TEST_SUITE_P(TypeInfoTest, TypeInfoTest, ValuesIn(GetTestCases()));

bool TypeInfoEqImpl(const Config::TypeInfo& actual,
                    const Config::TypeInfo& expected) {
  if (actual.name != expected.name) return false;
  if (actual.is_type_param != expected.is_type_param) return false;
  if (actual.params.size() != expected.params.size()) return false;
  for (size_t i = 0; i < actual.params.size(); ++i) {
    if (!TypeInfoEqImpl(actual.params[i], expected.params[i])) return false;
  }
  return true;
}

MATCHER_P(TypeInfoEq, expected, "") { return TypeInfoEqImpl(arg, expected); }

struct TypeSpecTestCase {
  TypeSpec type_spec;
  Config::TypeInfo expected_type_info;
};

using TypeSpecToTypeInfoTest = testing::TestWithParam<TypeSpecTestCase>;

TEST_P(TypeSpecToTypeInfoTest, Convert) {
  const TypeSpecTestCase& param = GetParam();
  ASSERT_OK_AND_ASSIGN(Config::TypeInfo actual_type_info,
                       TypeSpecToTypeInfo(param.type_spec));
  EXPECT_THAT(actual_type_info, TypeInfoEq(param.expected_type_info));
}

std::vector<TypeSpecTestCase> GetTypeSpecTestCases() {
  return {
      TypeSpecTestCase{
          .type_spec = TypeSpec(PrimitiveType::kInt64),
          .expected_type_info = {.name = "int"},
      },
      TypeSpecTestCase{
          .type_spec = TypeSpec(
              ListTypeSpec(std::make_unique<TypeSpec>(PrimitiveType::kInt64))),
          .expected_type_info = {.name = "list",
                                 .params = {Config::TypeInfo{.name = "int"}}},
      },
      TypeSpecTestCase{
          .type_spec = TypeSpec(ListTypeSpec()),
          .expected_type_info = {.name = "list"},
      },
      TypeSpecTestCase{
          .type_spec = TypeSpec(
              MapTypeSpec(std::make_unique<TypeSpec>(PrimitiveType::kString),
                          std::make_unique<TypeSpec>(PrimitiveType::kInt64))),
          .expected_type_info = {.name = "map",
                                 .params = {Config::TypeInfo{.name = "string"},
                                            Config::TypeInfo{.name = "int"}}},
      },
      TypeSpecTestCase{
          .type_spec = TypeSpec(MapTypeSpec()),
          .expected_type_info = {.name = "map"},
      },
      TypeSpecTestCase{
          .type_spec = TypeSpec(
              MessageTypeSpec("cel.expr.conformance.proto2.TestAllTypes")),
          .expected_type_info =
              {.name = "cel.expr.conformance.proto2.TestAllTypes"},
      },
      TypeSpecTestCase{
          .type_spec =
              TypeSpec(AbstractType("A", {TypeSpec(ParamTypeSpec("B"))})),
          .expected_type_info = {.name = "A",
                                 .params = {Config::TypeInfo{
                                     .name = "B", .is_type_param = true}}},
      },
      TypeSpecTestCase{
          .type_spec = TypeSpec(WellKnownTypeSpec::kAny),
          .expected_type_info = {.name = "any"},
      },
      TypeSpecTestCase{
          .type_spec = TypeSpec(WellKnownTypeSpec::kTimestamp),
          .expected_type_info = {.name = "timestamp"},
      },
      TypeSpecTestCase{
          .type_spec = TypeSpec(PrimitiveTypeWrapper(PrimitiveType::kDouble)),
          .expected_type_info = {.name = "double_wrapper"},
      },
      TypeSpecTestCase{
          .type_spec = TypeSpec(
              std::make_unique<TypeSpec>(WellKnownTypeSpec::kDuration)),
          .expected_type_info = {.name = "type",
                                 .params = {Config::TypeInfo{.name =
                                                                 "duration"}}},
      },
      TypeSpecTestCase{
          .type_spec = TypeSpec(std::make_unique<TypeSpec>(DynTypeSpec())),
          .expected_type_info = {.name = "type",
                                 .params = {Config::TypeInfo{.name = "dyn"}}},
      },
      TypeSpecTestCase{
          .type_spec = TypeSpec(DynTypeSpec{}),
          .expected_type_info = {.name = "dyn"},
      },
      TypeSpecTestCase{
          .type_spec = TypeSpec(NullTypeSpec{}),
          .expected_type_info = {.name = "null"},
      },
      TypeSpecTestCase{
          .type_spec = TypeSpec(
              MapTypeSpec(std::make_unique<TypeSpec>(PrimitiveType::kString),
                          std::make_unique<TypeSpec>(DynTypeSpec()))),
          .expected_type_info = {.name = "map",
                                 .params = {Config::TypeInfo{.name = "string"},
                                            Config::TypeInfo{.name = "dyn"}}},
      },
      TypeSpecTestCase{
          .type_spec = TypeSpec(
              MapTypeSpec(std::make_unique<TypeSpec>(DynTypeSpec()),
                          std::make_unique<TypeSpec>(PrimitiveType::kInt64))),
          .expected_type_info = {.name = "map",
                                 .params = {Config::TypeInfo{.name = "dyn"},
                                            Config::TypeInfo{.name = "int"}}},
      },
  };
}

INSTANTIATE_TEST_SUITE_P(TypeSpecToTypeInfoTest, TypeSpecToTypeInfoTest,
                         ValuesIn(GetTypeSpecTestCases()));

using TypeInfoToTypeSpecTest = testing::TestWithParam<TypeSpecTestCase>;

TEST_P(TypeInfoToTypeSpecTest, Convert) {
  const TypeSpecTestCase& param = GetParam();
  ASSERT_OK_AND_ASSIGN(TypeSpec actual_type_spec,
                       TypeInfoToTypeSpec(param.expected_type_info));
  EXPECT_EQ(actual_type_spec, param.type_spec);
}

INSTANTIATE_TEST_SUITE_P(TypeInfoToTypeSpecTest, TypeInfoToTypeSpecTest,
                         ValuesIn(GetTypeSpecTestCases()));

TEST(TypeSpecToTypeInfoTest, ErrorConversions) {
  EXPECT_THAT(TypeSpecToTypeInfo(TypeSpec(ErrorTypeSpec::kValue)),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "ErrorType cannot be converted to TypeInfo"));
  EXPECT_THAT(TypeSpecToTypeInfo(TypeSpec(FunctionTypeSpec())),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "FunctionType cannot be converted to TypeInfo"));
  EXPECT_THAT(
      TypeSpecToTypeInfo(TypeSpec(UnsetTypeSpec())),
      StatusIs(absl::StatusCode::kInvalidArgument, "Unknown TypeSpec kind"));
}

}  // namespace
}  // namespace cel
