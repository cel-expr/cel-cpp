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

#include "common/typedef/typedef_yaml.h"

#include <set>
#include <sstream>
#include <string>
#include <utility>

#include "absl/log/absl_log.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "common/ast.h"
#include "common/constant.h"
#include "common/typedef/typedef.h"
#include "common/typedef/typedef.pb.h"
#include "common/typedef/typedef_proto.h"
#include "common/typedef/yaml_helpers.h"
#include "common/typedef/yaml_test_helpers.h"
#include "internal/runfiles.h"
#include "internal/testing.h"
#include "google/protobuf/text_format.h"
#include "yaml-cpp/mark.h"
#include "yaml-cpp/node/node.h"
#include "yaml-cpp/node/parse.h"
#include "yaml-cpp/yaml.h"  // IWYU pragma: keep

namespace cel {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::HasSubstr;
using ::testing::NotNull;

constexpr absl::string_view kTestTypeDefFilePath =
"_main/common/typedef/testdata/";

constexpr absl::string_view kBaselineSeparator =
    "--------------------------------------------------------------------\n";

Constant MakeInvalidDurationConstant() {
  Constant c;
  // NOLINTNEXTLINE(clang-diagnostic-deprecated-declarations)
  c.set_duration_value(absl::InfiniteDuration());
  return c;
}

TEST(TypeDefYamlTest, CelTypesFromYaml) {
  std::string contents;
  std::string test_file = cel::internal::ResolveRunfilesPath(
      absl::StrCat(kTestTypeDefFilePath, "cel_types.yaml"));
  ASSERT_THAT(cel::internal::GetFileContents(test_file, &contents), IsOk());

  std::string baseline;
  std::string baseline_file = cel::internal::ResolveRunfilesPath(
      absl::StrCat(kTestTypeDefFilePath, "cel_types_from_yaml.baseline"));
  ASSERT_THAT(cel::internal::GetFileContents(baseline_file, &baseline), IsOk());
  baseline = absl::StripAsciiWhitespace(baseline);

  ASSERT_OK_AND_ASSIGN(TypeDefSet type_def_set, TypeDefSetFromYaml(contents));

  types::TypeDefSet proto_set;
  ASSERT_THAT(TypeDefSetToProto(type_def_set, &proto_set), IsOk());
  std::string textproto;
  ASSERT_TRUE(google::protobuf::TextFormat::PrintToString(proto_set, &textproto));

  std::ostringstream out;
  out << "YAML SOURCE: cel_types.yaml\n";
  out << kBaselineSeparator;
  out << "TYPEDEF PROTO:\n";
  out << textproto;

  std::string actual(absl::StripAsciiWhitespace(out.str()));
  if (actual != baseline) {
    ABSL_LOG(INFO) << "Actual:\n" << actual;
    EXPECT_EQ(actual, baseline);
  }
}

TEST(TypeDefYamlTest, CelTypeErrors) {
  std::string contents;
  std::string test_file = cel::internal::ResolveRunfilesPath(
      absl::StrCat(kTestTypeDefFilePath, "cel_type_error_tests.yaml"));
  ASSERT_THAT(cel::internal::GetFileContents(test_file, &contents), IsOk());

  ASSERT_OK_AND_ASSIGN(YAML::Node root, typedef_internal::LoadYaml(contents));
  ASSERT_TRUE(root.IsSequence());

  std::set<std::string> test_names;
  for (const YAML::Node& entry : root) {
    ASSERT_TRUE(entry.IsMap());
    const YAML::Node test_node = entry["test"];
    ASSERT_TRUE(test_node.IsDefined());
    ASSERT_TRUE(test_node.IsMap());

    const YAML::Node name_node = test_node["name"];
    ASSERT_TRUE(name_node.IsScalar());
    std::string test_name = typedef_internal::GetString(name_node);
    ASSERT_FALSE(test_name.empty());
    SCOPED_TRACE(absl::StrCat("test: ", test_name));
    EXPECT_TRUE(test_names.insert(test_name).second)
        << "Duplicate test name: " << test_name;

    const YAML::Node types_node = test_node["types"];
    ASSERT_TRUE(types_node.IsDefined());
    ASSERT_FALSE(types_node.IsNull());
    if (types_node.IsSequence()) {
      ASSERT_GT(types_node.size(), 0);
    }

    const YAML::Node error_node = test_node["error"];
    ASSERT_TRUE(error_node.IsScalar());
    std::string error_raw = typedef_internal::GetString(error_node);
    absl::string_view expected_error =
        absl::StripTrailingAsciiWhitespace(error_raw);
    ASSERT_FALSE(expected_error.empty());

    EXPECT_THAT(typedef_internal::ParseTypeDefSetNode(contents, types_node),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr(expected_error)));
  }
}

TEST(TypeDefYamlTest, CelTypesToYaml) {
  std::string contents;
  std::string test_file = cel::internal::ResolveRunfilesPath(
      absl::StrCat(kTestTypeDefFilePath, "cel_types.txtproto"));
  ASSERT_THAT(cel::internal::GetFileContents(test_file, &contents), IsOk());

  std::string baseline;
  std::string baseline_file = cel::internal::ResolveRunfilesPath(
      absl::StrCat(kTestTypeDefFilePath, "cel_types_to_yaml.baseline"));
  ASSERT_THAT(cel::internal::GetFileContents(baseline_file, &baseline), IsOk());
  baseline = absl::StripAsciiWhitespace(baseline);

  types::TypeDefSet proto_set;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(contents, &proto_set));
  ASSERT_OK_AND_ASSIGN(TypeDefSet type_def_set, TypeDefSetFromProto(proto_set));

  std::ostringstream out;
  out << "PROTO SOURCE: cel_types.txtproto\n";
  for (const TypeDef& type_def : type_def_set.types()) {
    out << kBaselineSeparator;
    std::ostringstream concise;
    ASSERT_THAT(TypeDefToYaml(type_def, concise, {.use_type_signatures = true}),
                IsOk());
    out << concise.str() << "\n";

    std::ostringstream verbose;
    ASSERT_THAT(
        TypeDefToYaml(type_def, verbose, {.use_type_signatures = false}),
        IsOk());
    if (verbose.str() != concise.str()) {
      out << "# use_type_signatures = false\n";
      out << verbose.str() << "\n";
    }
  }

  std::string actual(absl::StripAsciiWhitespace(out.str()));
  if (actual != baseline) {
    ABSL_LOG(INFO) << "Actual:\n" << actual;
    EXPECT_EQ(actual, baseline);
  }
}

TEST(TypeDefYamlTest, TypeDefFromYamlInvalidYaml) {
  EXPECT_THAT(TypeDefFromYaml("[1]"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Invalid CEL type definition YAML")));
  EXPECT_THAT(TypeDefFromYaml("{ invalid: [ }"),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(TypeDefYamlTest, TypeDefSetFromYaml) {
  ASSERT_OK_AND_ASSIGN(TypeDefSet empty_set, TypeDefSetFromYaml(""));
  EXPECT_TRUE(empty_set.types().empty());

  absl::string_view map_yaml = R"yaml(
    types:
      - struct:
          name: "com.example.B"
      - enum:
          name: "com.example.A"
  )yaml";
  ASSERT_OK_AND_ASSIGN(TypeDefSet set, TypeDefSetFromYaml(map_yaml));
  EXPECT_EQ(set.types().size(), 2);
  EXPECT_THAT(set.FindTypeDef("com.example.A"), NotNull());
  EXPECT_THAT(set.FindTypeDef("com.example.B"), NotNull());

  absl::string_view dup_yaml = R"yaml(
    - struct:
        name: "com.example.Dup"
    - struct:
        name: "com.example.Dup"
  )yaml";
  EXPECT_THAT(
      TypeDefSetFromYaml(dup_yaml),
      StatusIs(absl::StatusCode::kAlreadyExists,
               HasSubstr("Type 'com.example.Dup' is already defined.")));

  EXPECT_THAT(TypeDefSetFromYaml("123"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Invalid CEL type definitions YAML")));
  EXPECT_THAT(TypeDefSetFromYaml("struct:\n  name: \"OnlyOne\""),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Invalid CEL type definitions YAML")));
  ASSERT_OK_AND_ASSIGN(TypeDefSet extra_key_set,
                       TypeDefSetFromYaml("types: []\nextra: 1"));
  EXPECT_TRUE(extra_key_set.types().empty());
  EXPECT_THAT(TypeDefSetFromYaml("{ invalid: [ }"),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(TypeDefYamlTest, TypeDefSetToYaml) {
  TypeDefSet empty_set;
  std::ostringstream empty_out;
  ASSERT_THAT(TypeDefSetToYaml(empty_set, empty_out), IsOk());
  EXPECT_EQ(empty_out.str(), "");

  TypeDefSet set;
  ASSERT_THAT(set.AddTypeDef(TypeDef(StructTypeDef("com.example.B"))), IsOk());
  ASSERT_THAT(set.AddTypeDef(TypeDef(EnumTypeDef("com.example.A"))), IsOk());

  // TypeDefSetToYaml emits definitions sorted deterministically by type name.
  std::ostringstream emitted;
  ASSERT_THAT(TypeDefSetToYaml(set, emitted), IsOk());
  EXPECT_EQ(emitted.str(), Unindent(R"yaml(
    - enum:
        name: "com.example.A"
    - struct:
        name: "com.example.B"
  )yaml"));
}

TEST(TypeDefYamlTest, NanDoubleToYaml) {
  // Tested outside cel_types.txtproto because MessageDifferencer::Equals treats
  // NaN != NaN.
  types::TypeDef proto_def;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        struct_type {
          name: "NanFloats"
          members {
            field {
              name: "nan_val"
              type { primitive: DOUBLE }
              default_value { double_value: nan }
            }
          }
        }
      )pb",
      &proto_def));
  ASSERT_OK_AND_ASSIGN(TypeDef type_def, TypeDefFromProto(proto_def));

  std::ostringstream out;
  ASSERT_THAT(TypeDefToYaml(type_def, out), IsOk());
  EXPECT_EQ(out.str(), Unindent(R"yaml(
    struct:
      name: "NanFloats"
      fields:
        - name: "nan_val"
          type: "double"
          default: nan
  )yaml"));
}

TEST(TypeDefYamlTest, InvalidAspectConstantReturnsError) {
  StructTypeDef bad_struct(
      "com.example.BadAspect", "",
      {{"proto", Aspect({{"d", MakeInvalidDurationConstant()}})}});
  std::ostringstream out;
  EXPECT_THAT(TypeDefToYaml(TypeDef(bad_struct), out),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Infinite duration")));

  StructTypeDef bad_oneof_struct("com.example.BadOneofFieldAspect");
  StructTypeDef::Oneof bad_oneof;
  ASSERT_THAT(bad_oneof.AddField(StructTypeDef::Field(
                  "f", TypeSpec(PrimitiveType::kString), "", {},
                  {{"proto", Aspect({{"d", MakeInvalidDurationConstant()}})}})),
              IsOk());
  ASSERT_THAT(bad_oneof_struct.AddOneof(std::move(bad_oneof)), IsOk());
  std::ostringstream oneof_out;
  EXPECT_THAT(TypeDefToYaml(TypeDef(bad_oneof_struct), oneof_out),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Infinite duration")));

  StructTypeDef bad_oneof_group_struct("com.example.BadOneofGroupAspect");
  StructTypeDef::Oneof bad_oneof_group(
      {{"proto", Aspect({{"d", MakeInvalidDurationConstant()}})}});
  ASSERT_THAT(bad_oneof_group_struct.AddOneof(std::move(bad_oneof_group)),
              IsOk());
  std::ostringstream oneof_group_out;
  EXPECT_THAT(TypeDefToYaml(TypeDef(bad_oneof_group_struct), oneof_group_out),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Infinite duration")));

  TypeDefSet set;
  ASSERT_THAT(set.AddTypeDef(TypeDef(std::move(bad_struct))), IsOk());
  std::ostringstream set_out;
  EXPECT_THAT(TypeDefSetToYaml(set, set_out),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Infinite duration")));
}

TEST(TypeDefYamlTest, YamlHelpersCoverage) {
  EXPECT_EQ(typedef_internal::FormatYamlErrorMessage("yaml", "error",
                                                     YAML::Mark::null_mark()),
            "error");

  YAML::Mark mark;
  mark.pos = 7;
  mark.line = 0;
  mark.column = 7;
  EXPECT_THAT(
      typedef_internal::FormatYamlErrorMessage("single line", "err", mark),
      HasSubstr("single line\n|       ^"));
  EXPECT_EQ(typedef_internal::FormatYamlErrorMessage("", "err", mark), "err");

  YAML::Node seq = YAML::Load("[1, 2]");
  EXPECT_EQ(typedef_internal::GetString(seq), "");

  YAML::Node str_node = YAML::Load("hello");
  EXPECT_THAT(typedef_internal::GetBinary("hello", str_node), IsOkAndHolds(""));

  YAML::Node map_node = YAML::Load("a: 1\nb: 2");
  YAML::Node missing_node = YAML::Load("3");
  YAML::Node ctx =
      typedef_internal::GetContextNodeForKeyValue(map_node, missing_node);
  EXPECT_TRUE(ctx.is(missing_node));

  EXPECT_TRUE(typedef_internal::IsEmptyYamlNode(YAML::Load("\"\"")));
  EXPECT_FALSE(typedef_internal::IsEmptyYamlNode(YAML::Load("'a'")));
  EXPECT_EQ(Unindent("  a\nb"), "a\nb");
}

}  // namespace
}  // namespace cel
