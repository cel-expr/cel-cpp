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

#include "common/typedef/type_ref.h"

#include <cstddef>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "common/ast/metadata.h"
#include "common/type.h"
#include "common/type_proto.h"
#include "internal/proto_matchers.h"
#include "internal/testing.h"
#include "internal/testing_descriptor_pool.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/text_format.h"

namespace cel {

std::ostream& operator<<(std::ostream& os, const TypeRef& type_ref) {
  if (type_ref.is_type_param) {
    os << "?";
  }
  os << type_ref.name;
  if (!type_ref.params.empty()) {
    os << "<";
    for (size_t i = 0; i < type_ref.params.size(); ++i) {
      if (i > 0) os << ", ";
      os << type_ref.params[i];
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
  TypeRef type_ref;
  std::string expected_type_pb;
};

using TypeRefTest = testing::TestWithParam<TestCase>;

TEST_P(TypeRefTest, TypeRef) {
  const TestCase& param = GetParam();
  cel::expr::Type expected_type_pb;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(param.expected_type_pb,
                                                  &expected_type_pb));

  google::protobuf::Arena arena;
  const google::protobuf::DescriptorPool* descriptor_pool =
      cel::internal::GetTestingDescriptorPool();
  ASSERT_OK_AND_ASSIGN(
      cel::Type actual_type,
      cel::TypeRefToType(param.type_ref, descriptor_pool, &arena));

  cel::expr::Type actual_type_pb;
  ASSERT_THAT(cel::TypeToProto(actual_type, &actual_type_pb), IsOk());
  EXPECT_THAT(actual_type_pb,
              cel::internal::test::EqualsProto(expected_type_pb));
}

std::vector<TestCase> GetTestCases() {
  return {
      TestCase{
          .type_ref = {.name = "int"},
          .expected_type_pb = "primitive: INT64",
      },
      TestCase{
          .type_ref = {.name = "list", .params = {TypeRef{.name = "int"}}},
          .expected_type_pb = "list_type { elem_type { primitive: INT64 } }",
      },
      TestCase{
          .type_ref = {.name = "list"},
          .expected_type_pb = "list_type { elem_type { dyn {} }}",
      },
      TestCase{
          .type_ref = {.name = "map",
                       .params = {TypeRef{.name = "string"},
                                  TypeRef{.name = "int"}}},
          .expected_type_pb = "map_type { key_type { primitive: STRING } "
                              "value_type { primitive: INT64 }}",
      },
      TestCase{
          .type_ref = {.name = "cel.expr.conformance.proto2.TestAllTypes"},
          .expected_type_pb =
              "message_type: 'cel.expr.conformance.proto2.TestAllTypes'",
      },
      TestCase{
          .type_ref = {.name = "A",
                       .params = {TypeRef{.name = "B", .is_type_param = true}}},
          .expected_type_pb =
              "abstract_type { name: 'A' parameter_types { type_param: 'B' } }",
      },
      TestCase{
          .type_ref = {.name = "any"},
          .expected_type_pb = "well_known: ANY",
      },
      TestCase{
          .type_ref = {.name = "timestamp"},
          .expected_type_pb = "well_known: TIMESTAMP",
      },
      TestCase{
          .type_ref = {.name = "google.protobuf.DoubleValue"},
          .expected_type_pb = "wrapper: DOUBLE",
      },
      TestCase{
          .type_ref = {.name = "double_wrapper"},
          .expected_type_pb = "wrapper: DOUBLE",
      },
      TestCase{
          .type_ref = {.name = "type", .params = {TypeRef{.name = "duration"}}},
          .expected_type_pb = "type: { well_known: DURATION }",
      },
      TestCase{
          .type_ref = {.name = "parameterized",
                       .params = {{.name = "A", .is_type_param = true},
                                  {.name = "double"}}},
          .expected_type_pb = "abstract_type { name: 'parameterized' "
                              "parameter_types { type_param: 'A' } "
                              "parameter_types { primitive: DOUBLE } }",
      },
  };
}

INSTANTIATE_TEST_SUITE_P(TypeRefTest, TypeRefTest, ValuesIn(GetTestCases()));

bool TypeRefEqImpl(const TypeRef& actual, const TypeRef& expected) {
  if (actual.name != expected.name) return false;
  if (actual.is_type_param != expected.is_type_param) return false;
  if (actual.params.size() != expected.params.size()) return false;
  for (size_t i = 0; i < actual.params.size(); ++i) {
    if (!TypeRefEqImpl(actual.params[i], expected.params[i])) return false;
  }
  return true;
}

MATCHER_P(TypeRefEq, expected, "") { return TypeRefEqImpl(arg, expected); }

struct TypeSpecTestCase {
  TypeSpec type_spec;
  TypeRef expected_type_ref;
};

using TypeSpecToTypeRefTest = testing::TestWithParam<TypeSpecTestCase>;

TEST_P(TypeSpecToTypeRefTest, Convert) {
  const TypeSpecTestCase& param = GetParam();
  ASSERT_OK_AND_ASSIGN(TypeRef actual_type_ref,
                       TypeSpecToTypeRef(param.type_spec));
  EXPECT_THAT(actual_type_ref, TypeRefEq(param.expected_type_ref));
}

std::vector<TypeSpecTestCase> GetTypeSpecTestCases() {
  return {
      TypeSpecTestCase{
          .type_spec = TypeSpec(PrimitiveType::kInt64),
          .expected_type_ref = {.name = "int"},
      },
      TypeSpecTestCase{
          .type_spec = TypeSpec(
              ListTypeSpec(std::make_unique<TypeSpec>(PrimitiveType::kInt64))),
          .expected_type_ref = {.name = "list",
                                .params = {TypeRef{.name = "int"}}},
      },
      TypeSpecTestCase{
          .type_spec = TypeSpec(ListTypeSpec()),
          .expected_type_ref = {.name = "list"},
      },
      TypeSpecTestCase{
          .type_spec = TypeSpec(
              MapTypeSpec(std::make_unique<TypeSpec>(PrimitiveType::kString),
                          std::make_unique<TypeSpec>(PrimitiveType::kInt64))),
          .expected_type_ref = {.name = "map",
                                .params = {TypeRef{.name = "string"},
                                           TypeRef{.name = "int"}}},
      },
      TypeSpecTestCase{
          .type_spec = TypeSpec(MapTypeSpec()),
          .expected_type_ref = {.name = "map"},
      },
      TypeSpecTestCase{
          .type_spec = TypeSpec(
              MessageTypeSpec("cel.expr.conformance.proto2.TestAllTypes")),
          .expected_type_ref = {.name =
                                    "cel.expr.conformance.proto2.TestAllTypes"},
      },
      TypeSpecTestCase{
          .type_spec =
              TypeSpec(AbstractType("A", {TypeSpec(ParamTypeSpec("B"))})),
          .expected_type_ref = {.name = "A",
                                .params = {TypeRef{.name = "B",
                                                   .is_type_param = true}}},
      },
      TypeSpecTestCase{
          .type_spec = TypeSpec(WellKnownTypeSpec::kAny),
          .expected_type_ref = {.name = "any"},
      },
      TypeSpecTestCase{
          .type_spec = TypeSpec(WellKnownTypeSpec::kTimestamp),
          .expected_type_ref = {.name = "timestamp"},
      },
      TypeSpecTestCase{
          .type_spec = TypeSpec(PrimitiveTypeWrapper(PrimitiveType::kDouble)),
          .expected_type_ref = {.name = "double_wrapper"},
      },
      TypeSpecTestCase{
          .type_spec = TypeSpec(
              std::make_unique<TypeSpec>(WellKnownTypeSpec::kDuration)),
          .expected_type_ref = {.name = "type",
                                .params = {TypeRef{.name = "duration"}}},
      },
      TypeSpecTestCase{
          .type_spec = TypeSpec(std::make_unique<TypeSpec>(DynTypeSpec())),
          .expected_type_ref = {.name = "type",
                                .params = {TypeRef{.name = "dyn"}}},
      },
      TypeSpecTestCase{
          .type_spec = TypeSpec(DynTypeSpec{}),
          .expected_type_ref = {.name = "dyn"},
      },
      TypeSpecTestCase{
          .type_spec = TypeSpec(NullTypeSpec{}),
          .expected_type_ref = {.name = "null"},
      },
      TypeSpecTestCase{
          .type_spec = TypeSpec(
              MapTypeSpec(std::make_unique<TypeSpec>(PrimitiveType::kString),
                          std::make_unique<TypeSpec>(DynTypeSpec()))),
          .expected_type_ref = {.name = "map",
                                .params = {TypeRef{.name = "string"},
                                           TypeRef{.name = "dyn"}}},
      },
      TypeSpecTestCase{
          .type_spec = TypeSpec(
              MapTypeSpec(std::make_unique<TypeSpec>(DynTypeSpec()),
                          std::make_unique<TypeSpec>(PrimitiveType::kInt64))),
          .expected_type_ref = {.name = "map",
                                .params = {TypeRef{.name = "dyn"},
                                           TypeRef{.name = "int"}}},
      },
  };
}

INSTANTIATE_TEST_SUITE_P(TypeSpecToTypeRefTest, TypeSpecToTypeRefTest,
                         ValuesIn(GetTypeSpecTestCases()));

using TypeRefToTypeSpecTest = testing::TestWithParam<TypeSpecTestCase>;

TEST_P(TypeRefToTypeSpecTest, Convert) {
  const TypeSpecTestCase& param = GetParam();
  ASSERT_OK_AND_ASSIGN(TypeSpec actual_type_spec,
                       TypeRefToTypeSpec(param.expected_type_ref));
  EXPECT_EQ(actual_type_spec, param.type_spec);
}

INSTANTIATE_TEST_SUITE_P(TypeRefToTypeSpecTest, TypeRefToTypeSpecTest,
                         ValuesIn(GetTypeSpecTestCases()));

TEST(TypeSpecToTypeRefTest, ErrorConversions) {
  EXPECT_THAT(TypeSpecToTypeRef(TypeSpec(ErrorTypeSpec::kValue)),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "ErrorType cannot be converted to TypeRef"));
  EXPECT_THAT(TypeSpecToTypeRef(TypeSpec(FunctionTypeSpec())),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "FunctionType cannot be converted to TypeRef"));
  EXPECT_THAT(
      TypeSpecToTypeRef(TypeSpec(UnsetTypeSpec())),
      StatusIs(absl::StatusCode::kInvalidArgument, "Unknown TypeSpec kind"));
}

}  // namespace
}  // namespace cel
