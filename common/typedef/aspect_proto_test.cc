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

#include "common/typedef/aspect_proto.h"

#include <any>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/string_view.h"
#include "common/typedef/typedef.pb.h"
#include "internal/testing.h"

namespace cel {
namespace {

using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::Eq;
using ::testing::IsNull;
using ::testing::NotNull;
using ::testing::Property;

class StubAspectProto final : public AspectProto {
 public:
  explicit StubAspectProto(absl::string_view name) : name_(name) {}

  absl::string_view name() const override { return name_; }

 private:
  std::string name_;
};

TEST(AspectProtoTest, DefaultDecodeReturnsEmptyAny) {
  StubAspectProto aspect("stub");

  types::Struct struct_proto;
  types::Struct::Field field_proto;
  types::Enum enum_proto;
  types::Enum::EnumConstant enum_const_proto;

  EXPECT_THAT(aspect.DecodeStructProperties(struct_proto),
              IsOkAndHolds(Property(&std::any::has_value, false)));
  EXPECT_THAT(aspect.DecodeFieldProperties(field_proto),
              IsOkAndHolds(Property(&std::any::has_value, false)));
  EXPECT_THAT(aspect.DecodeEnumProperties(enum_proto),
              IsOkAndHolds(Property(&std::any::has_value, false)));
  EXPECT_THAT(aspect.DecodeEnumConstantProperties(enum_const_proto),
              IsOkAndHolds(Property(&std::any::has_value, false)));
}

TEST(AspectProtoTest, DefaultEncodeReturnsUnimplementedError) {
  StubAspectProto aspect("stub");
  types::Struct struct_proto;
  types::Struct::Field field_proto;
  types::Enum enum_proto;
  types::Enum::EnumConstant enum_const_proto;

  std::any props = 123;

  EXPECT_THAT(aspect.EncodeStructProperties(props, &struct_proto),
              StatusIs(absl::StatusCode::kUnimplemented,
                       Eq("Custom struct properties are not supported for "
                          "aspect 'stub'")));
  EXPECT_THAT(aspect.EncodeFieldProperties(props, &field_proto),
              StatusIs(absl::StatusCode::kUnimplemented,
                       Eq("Custom field properties are not supported for "
                          "aspect 'stub'")));
  EXPECT_THAT(aspect.EncodeEnumProperties(props, &enum_proto),
              StatusIs(absl::StatusCode::kUnimplemented,
                       Eq("Custom enum properties are not supported for "
                          "aspect 'stub'")));
  EXPECT_THAT(aspect.EncodeEnumConstantProperties(props, &enum_const_proto),
              StatusIs(absl::StatusCode::kUnimplemented,
                       Eq("Custom enum constant properties are not supported "
                          "for aspect 'stub'")));
}

TEST(AspectProtoRegistryTest, ConstructAndFindAspects) {
  AspectProtoRegistry empty_registry;
  EXPECT_THAT(empty_registry.Find("aspect_a"), IsNull());
  EXPECT_TRUE(empty_registry.aspects().empty());

  AspectProtoRegistry registry(std::make_unique<StubAspectProto>("aspect_a"),
                               std::make_unique<StubAspectProto>("aspect_b"));

  const AspectProto* aspect_a = registry.Find("aspect_a");
  ASSERT_THAT(aspect_a, NotNull());
  EXPECT_EQ(aspect_a->name(), "aspect_a");

  const AspectProto* aspect_b = registry.Find("aspect_b");
  ASSERT_THAT(aspect_b, NotNull());
  EXPECT_EQ(aspect_b->name(), "aspect_b");

  EXPECT_THAT(registry.Find("unknown_aspect"), IsNull());
  EXPECT_EQ(registry.aspects().size(), 2);
}

TEST(AspectProtoRegistryTest, ConstructFromVector) {
  std::vector<std::unique_ptr<const AspectProto>> aspects;
  aspects.push_back(std::make_unique<StubAspectProto>("aspect_a"));
  aspects.push_back(nullptr);
  AspectProtoRegistry registry(std::move(aspects));

  EXPECT_THAT(registry.Find("aspect_a"), NotNull());
  EXPECT_THAT(registry.Find("aspect_b"), IsNull());
  EXPECT_EQ(registry.aspects().size(), 1);
}

}  // namespace
}  // namespace cel
