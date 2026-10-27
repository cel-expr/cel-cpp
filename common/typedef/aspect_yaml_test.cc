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

#include "common/typedef/aspect_yaml.h"

#include <any>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/string_view.h"
#include "internal/testing.h"
#include "yaml-cpp/emitter.h"
#include "yaml-cpp/node/node.h"
#include "yaml-cpp/node/parse.h"
#include "yaml-cpp/node/type.h"
#include "yaml-cpp/yaml.h"  // IWYU pragma: keep

namespace cel {
namespace {

using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::Eq;
using ::testing::IsNull;
using ::testing::NotNull;
using ::testing::Property;

class StubAspectYaml final : public AspectYaml {
 public:
  explicit StubAspectYaml(absl::string_view name) : name_(name) {}

  absl::string_view name() const override { return name_; }

 private:
  std::string name_;
};

TEST(AspectYamlTest, DefaultDecodeSucceedsOnEmptyNodes) {
  StubAspectYaml aspect("stub");

  std::vector<YAML::Node> empty_nodes = {
      YAML::Node(YAML::NodeType::Undefined),
      YAML::Load("~"),
      YAML::Load("\"\""),
      YAML::Load("{}"),
      YAML::Load("[]"),
  };

  for (const YAML::Node& node : empty_nodes) {
    EXPECT_THAT(aspect.DecodeStructProperties("", node),
                IsOkAndHolds(Property(&std::any::has_value, false)));
    EXPECT_THAT(aspect.DecodeFieldProperties("", node),
                IsOkAndHolds(Property(&std::any::has_value, false)));
    EXPECT_THAT(aspect.DecodeEnumProperties("", node),
                IsOkAndHolds(Property(&std::any::has_value, false)));
    EXPECT_THAT(aspect.DecodeEnumConstantProperties("", node),
                IsOkAndHolds(Property(&std::any::has_value, false)));
  }
}

TEST(AspectYamlTest, DefaultDecodeFailsWithYamlErrorOnNonEmptyNode) {
  StubAspectYaml aspect("stub");
  absl::string_view yaml = "prop: 123";
  YAML::Node node = YAML::Load(std::string(yaml));

  EXPECT_THAT(aspect.DecodeStructProperties(yaml, node),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       Eq("1:1: Custom struct properties are not supported for "
                          "aspect 'stub'\n|prop: 123\n|^")));
  EXPECT_THAT(aspect.DecodeFieldProperties(yaml, node),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       Eq("1:1: Custom field properties are not supported for "
                          "aspect 'stub'\n|prop: 123\n|^")));
  EXPECT_THAT(aspect.DecodeEnumProperties(yaml, node),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       Eq("1:1: Custom enum properties are not supported for "
                          "aspect 'stub'\n|prop: 123\n|^")));
  EXPECT_THAT(
      aspect.DecodeEnumConstantProperties(yaml, node),
      StatusIs(absl::StatusCode::kInvalidArgument,
               Eq("1:1: Custom enum constant properties are not supported for "
                  "aspect 'stub'\n|prop: 123\n|^")));
}

TEST(AspectYamlTest, DefaultEncodeReturnsUnimplementedError) {
  StubAspectYaml aspect("stub");
  YAML::Emitter emitter;
  std::any props = 123;

  EXPECT_THAT(aspect.EncodeStructProperties(props, emitter),
              StatusIs(absl::StatusCode::kUnimplemented,
                       Eq("Custom struct properties are not supported for "
                          "aspect 'stub'")));
  EXPECT_THAT(aspect.EncodeFieldProperties(props, emitter),
              StatusIs(absl::StatusCode::kUnimplemented,
                       Eq("Custom field properties are not supported for "
                          "aspect 'stub'")));
  EXPECT_THAT(aspect.EncodeEnumProperties(props, emitter),
              StatusIs(absl::StatusCode::kUnimplemented,
                       Eq("Custom enum properties are not supported for "
                          "aspect 'stub'")));
  EXPECT_THAT(
      aspect.EncodeEnumConstantProperties(props, emitter),
      StatusIs(absl::StatusCode::kUnimplemented,
               Eq("Custom enum constant properties are not supported for "
                  "aspect 'stub'")));
}

TEST(AspectYamlRegistryTest, ConstructAndFindAspects) {
  AspectYamlRegistry empty_registry;
  EXPECT_THAT(empty_registry.Find("aspect_a"), IsNull());

  AspectYamlRegistry registry(std::make_unique<StubAspectYaml>("aspect_a"),
                              std::make_unique<StubAspectYaml>("aspect_b"));

  const AspectYaml* aspect_a = registry.Find("aspect_a");
  ASSERT_THAT(aspect_a, NotNull());
  EXPECT_EQ(aspect_a->name(), "aspect_a");

  const AspectYaml* aspect_b = registry.Find("aspect_b");
  ASSERT_THAT(aspect_b, NotNull());
  EXPECT_EQ(aspect_b->name(), "aspect_b");

  EXPECT_THAT(registry.Find("unknown_aspect"), IsNull());
}

TEST(AspectYamlRegistryTest, ConstructFromVector) {
  std::vector<std::unique_ptr<const AspectYaml>> aspects;
  aspects.push_back(std::make_unique<StubAspectYaml>("aspect_a"));
  aspects.push_back(nullptr);
  AspectYamlRegistry registry(std::move(aspects));

  EXPECT_THAT(registry.Find("aspect_a"), NotNull());
  EXPECT_THAT(registry.Find("aspect_b"), IsNull());
}

}  // namespace
}  // namespace cel
