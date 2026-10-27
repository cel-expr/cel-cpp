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

#include "common/typedef/proto_schema_yaml.h"

#include <any>
#include <sstream>
#include <string>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "common/typedef/aspect_yaml.h"
#include "common/typedef/proto_schema.h"
#include "common/typedef/yaml_helpers.h"
#include "internal/status_macros.h"
#include "internal/testing.h"
#include "yaml-cpp/emitter.h"
#include "yaml-cpp/node/node.h"
#include "yaml-cpp/node/parse.h"

namespace cel {
namespace {

using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::HasSubstr;
using ::testing::Property;

absl::StatusOr<std::string> RoundTripStruct(const AspectYaml& aspect,
                                            absl::string_view yaml) {
  CEL_ASSIGN_OR_RETURN(YAML::Node node, typedef_internal::LoadYaml(yaml));
  CEL_ASSIGN_OR_RETURN(auto props, aspect.DecodeStructProperties(yaml, node));
  std::stringstream ss;
  YAML::Emitter out(ss);
  out.SetIndent(2);
  CEL_RETURN_IF_ERROR(aspect.EncodeStructProperties(props, out));
  return ss.str();
}

absl::StatusOr<std::string> RoundTripField(const AspectYaml& aspect,
                                           absl::string_view yaml) {
  CEL_ASSIGN_OR_RETURN(YAML::Node node, typedef_internal::LoadYaml(yaml));
  CEL_ASSIGN_OR_RETURN(auto props, aspect.DecodeFieldProperties(yaml, node));
  std::stringstream ss;
  YAML::Emitter out(ss);
  out.SetIndent(2);
  CEL_RETURN_IF_ERROR(aspect.EncodeFieldProperties(props, out));
  return ss.str();
}

absl::StatusOr<std::string> RoundTripEnum(const AspectYaml& aspect,
                                          absl::string_view yaml) {
  CEL_ASSIGN_OR_RETURN(YAML::Node node, typedef_internal::LoadYaml(yaml));
  CEL_ASSIGN_OR_RETURN(auto props, aspect.DecodeEnumProperties(yaml, node));
  std::stringstream ss;
  YAML::Emitter out(ss);
  out.SetIndent(2);
  CEL_RETURN_IF_ERROR(aspect.EncodeEnumProperties(props, out));
  return ss.str();
}

absl::StatusOr<std::string> RoundTripEnumConstant(const AspectYaml& aspect,
                                                  absl::string_view yaml) {
  CEL_ASSIGN_OR_RETURN(YAML::Node node, typedef_internal::LoadYaml(yaml));
  CEL_ASSIGN_OR_RETURN(auto props,
                       aspect.DecodeEnumConstantProperties(yaml, node));
  std::stringstream ss;
  YAML::Emitter out(ss);
  out.SetIndent(2);
  CEL_RETURN_IF_ERROR(aspect.EncodeEnumConstantProperties(props, out));
  return ss.str();
}

TEST(ProtoSchemaYamlTest, AspectNames) {
  ProtoSchemaYaml p;
  Proto2SchemaYaml p2;
  Proto3SchemaYaml p3;
  EXPECT_EQ(p.name(), kProtoAspectName);
  EXPECT_EQ(p2.name(), kProto2AspectName);
  EXPECT_EQ(p3.name(), kProto3AspectName);
}

TEST(ProtoSchemaYamlTest, DecodeAndEncodeRoundTrip) {
  ProtoSchemaYaml p;
  EXPECT_THAT(RoundTripStruct(p, "name: \"com.example.Item\""),
              IsOkAndHolds("name: \"com.example.Item\""));
  EXPECT_THAT(RoundTripField(p,
                             "name: \"com.example.store.Price\"\n"
                             "id: 12\n"
                             "json_name: \"priceVal\""),
              IsOkAndHolds("name: \"com.example.store.Price\"\n"
                           "id: 12\n"
                           "json_name: \"priceVal\""));
  EXPECT_THAT(RoundTripField(p, "id: 1"), IsOkAndHolds("id: 1"));
  EXPECT_THAT(RoundTripField(p, "id: 18999"), IsOkAndHolds("id: 18999"));
  EXPECT_THAT(RoundTripField(p, "id: 20000"), IsOkAndHolds("id: 20000"));
  EXPECT_THAT(RoundTripField(p, "id: 536870911"),
              IsOkAndHolds("id: 536870911"));
  EXPECT_THAT(
      RoundTripEnum(p, "name: \"com.example.Status\"\nallow_alias: true"),
      IsOkAndHolds("name: \"com.example.Status\"\nallow_alias: true"));
  EXPECT_THAT(RoundTripEnumConstant(p, "name: \"SHIPPED\""),
              IsOkAndHolds("name: \"SHIPPED\""));
  EXPECT_THAT(RoundTripStruct(p, "{}"), IsOkAndHolds("{}"));
  EXPECT_THAT(RoundTripField(p, "{}"), IsOkAndHolds("{}"));
  EXPECT_THAT(RoundTripEnum(p, "{}"), IsOkAndHolds("{}"));
  EXPECT_THAT(RoundTripEnumConstant(p, "{}"), IsOkAndHolds("{}"));

  Proto2SchemaYaml p2;
  EXPECT_THAT(
      RoundTripStruct(p2, "extension_ranges:\n  - start: 100\n    end: 200"),
      IsOkAndHolds("extension_ranges:\n  - start: 100\n    end: 200"));
  EXPECT_THAT(RoundTripField(p2, "required: true\npacked: false"),
              IsOkAndHolds("required: true\npacked: false"));
  EXPECT_THAT(RoundTripStruct(p2, "{}"), IsOkAndHolds("{}"));
  EXPECT_THAT(RoundTripField(p2, "{}"), IsOkAndHolds("{}"));
  EXPECT_THAT(p2.DecodeEnumProperties("{}", YAML::Load("{}")),
              IsOkAndHolds(Property(&std::any::has_value, false)));
  EXPECT_THAT(p2.DecodeEnumConstantProperties("{}", YAML::Load("{}")),
              IsOkAndHolds(Property(&std::any::has_value, false)));

  Proto3SchemaYaml p3;
  EXPECT_THAT(RoundTripField(p3, "optional: true\npacked: true"),
              IsOkAndHolds("optional: true\npacked: true"));
  EXPECT_THAT(RoundTripField(p3, "{}"), IsOkAndHolds("{}"));
  EXPECT_THAT(p3.DecodeStructProperties("{}", YAML::Load("{}")),
              IsOkAndHolds(Property(&std::any::has_value, false)));
  EXPECT_THAT(p3.DecodeEnumProperties("{}", YAML::Load("{}")),
              IsOkAndHolds(Property(&std::any::has_value, false)));
  EXPECT_THAT(p3.DecodeEnumConstantProperties("{}", YAML::Load("{}")),
              IsOkAndHolds(Property(&std::any::has_value, false)));
}

struct DummyProperties {};

TEST(ProtoSchemaYamlTest, EncodeTypeMismatchErrors) {
  ProtoSchemaYaml p;
  Proto2SchemaYaml p2;
  Proto3SchemaYaml p3;
  std::stringstream ss;
  YAML::Emitter out(ss);

  DummyProperties dummy;
  EXPECT_THAT(
      p.EncodeStructProperties(dummy, out),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Invalid struct properties type for 'proto'")));
  EXPECT_THAT(p.EncodeFieldProperties(dummy, out),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Invalid field properties type for 'proto'")));
  EXPECT_THAT(p.EncodeEnumProperties(dummy, out),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Invalid enum properties type for 'proto'")));
  EXPECT_THAT(
      p.EncodeEnumConstantProperties(dummy, out),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Invalid enum constant properties type for 'proto'")));

  EXPECT_THAT(
      p2.EncodeStructProperties(dummy, out),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Invalid struct properties type for 'proto2'")));
  EXPECT_THAT(
      p2.EncodeFieldProperties(dummy, out),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Invalid field properties type for 'proto2'")));

  EXPECT_THAT(
      p3.EncodeFieldProperties(dummy, out),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Invalid field properties type for 'proto3'")));
}

}  // namespace
}  // namespace cel
