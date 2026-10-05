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

#include <any>
#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/absl_log.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "common/typedef/aspect_yaml.h"
#include "common/typedef/typedef.h"
#include "common/typedef/typedef.pb.h"
#include "common/typedef/typedef_proto.h"
#include "common/typedef/yaml_helpers.h"
#include "common/typedef/yaml_test_helpers.h"
#include "internal/proto_matchers.h"
#include "internal/runfiles.h"
#include "internal/status_macros.h"
#include "internal/testing.h"
#include "google/protobuf/text_format.h"
#include "yaml-cpp/emitter.h"
#include "yaml-cpp/emittermanip.h"
#include "yaml-cpp/exceptions.h"
#include "yaml-cpp/mark.h"
#include "yaml-cpp/node/emit.h"
#include "yaml-cpp/node/node.h"
#include "yaml-cpp/node/parse.h"
#include "yaml-cpp/yaml.h"  // IWYU pragma: keep

namespace cel {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::cel::internal::test::EqualsProto;
using ::testing::HasSubstr;

constexpr absl::string_view kTestTypeDefFilePath =
"_main/common/typedef/testdata/";

constexpr absl::string_view kBaselineSeparator =
    "--------------------------------------------------------------------\n";

struct TestPropsData {
  std::optional<std::string> name;
  std::optional<int32_t> id;
};

absl::StatusOr<TestPropsData> DecodeTestProps(absl::string_view aspect_name,
                                              absl::string_view context,
                                              absl::string_view yaml,
                                              const YAML::Node& node) {
  if (!node || !node.IsMap()) {
    return typedef_internal::YamlError(yaml, node,
                                       "Properties node is not a map");
  }
  TestPropsData data;
  for (const auto& kv : node) {
    std::string key = typedef_internal::GetString(kv.first);
    if (key == "name") {
      data.name = typedef_internal::GetString(kv.second);
    } else if (key == "id") {
      CEL_ASSIGN_OR_RETURN(data.id,
                           typedef_internal::GetInt32(yaml, "id", kv.second));
    } else {
      return typedef_internal::YamlError(
          yaml, kv.first,
          absl::StrCat("Unsupported '", aspect_name, "' ", context,
                       " property: '", key, "'"));
    }
  }
  return data;
}

void EncodeTestProps(const TestPropsData& data, YAML::Emitter& out) {
  out << YAML::BeginMap;
  if (data.name.has_value()) {
    out << YAML::Key << "name" << YAML::Value << YAML::DoubleQuoted
        << *data.name;
  }
  if (data.id.has_value()) {
    out << YAML::Key << "id" << YAML::Value << *data.id;
  }
  out << YAML::EndMap;
}

class TestStructProps final {
 public:
  explicit TestStructProps(TestPropsData d) : data_(std::move(d)) {}
  const TestPropsData& data() const { return data_; }

 private:
  TestPropsData data_;
};

class TestFieldProps final {
 public:
  explicit TestFieldProps(TestPropsData d) : data_(std::move(d)) {}
  const TestPropsData& data() const { return data_; }

 private:
  TestPropsData data_;
};

class TestEnumProps final {
 public:
  explicit TestEnumProps(TestPropsData d) : data_(std::move(d)) {}
  const TestPropsData& data() const { return data_; }

 private:
  TestPropsData data_;
};

class TestEnumConstantProps final {
 public:
  explicit TestEnumConstantProps(TestPropsData d) : data_(std::move(d)) {}
  const TestPropsData& data() const { return data_; }

 private:
  TestPropsData data_;
};

class TestAspect final : public AspectYaml {
 public:
  explicit TestAspect(absl::string_view name) : name_(name) {}

  absl::string_view name() const override { return name_; }

  absl::StatusOr<std::any> DecodeStructProperties(
      absl::string_view yaml, const YAML::Node& node) const override {
    CEL_ASSIGN_OR_RETURN(TestPropsData d,
                         DecodeTestProps(name_, "struct", yaml, node));
    return std::make_any<TestStructProps>(std::move(d));
  }

  absl::StatusOr<std::any> DecodeFieldProperties(
      absl::string_view yaml, const YAML::Node& node) const override {
    CEL_ASSIGN_OR_RETURN(TestPropsData d,
                         DecodeTestProps(name_, "field", yaml, node));
    return std::make_any<TestFieldProps>(std::move(d));
  }

  absl::StatusOr<std::any> DecodeEnumProperties(
      absl::string_view yaml, const YAML::Node& node) const override {
    CEL_ASSIGN_OR_RETURN(TestPropsData d,
                         DecodeTestProps(name_, "enum", yaml, node));
    return std::make_any<TestEnumProps>(std::move(d));
  }

  absl::StatusOr<std::any> DecodeEnumConstantProperties(
      absl::string_view yaml, const YAML::Node& node) const override {
    CEL_ASSIGN_OR_RETURN(TestPropsData d,
                         DecodeTestProps(name_, "enum constant", yaml, node));
    return std::make_any<TestEnumConstantProps>(std::move(d));
  }

  absl::Status EncodeStructProperties(const std::any& properties,
                                      YAML::Emitter& out) const override {
    EncodeTestProps(std::any_cast<const TestStructProps&>(properties).data(),
                    out);
    return absl::OkStatus();
  }

  absl::Status EncodeFieldProperties(const std::any& properties,
                                     YAML::Emitter& out) const override {
    EncodeTestProps(std::any_cast<const TestFieldProps&>(properties).data(),
                    out);
    return absl::OkStatus();
  }

  absl::Status EncodeEnumProperties(const std::any& properties,
                                    YAML::Emitter& out) const override {
    EncodeTestProps(std::any_cast<const TestEnumProps&>(properties).data(),
                    out);
    return absl::OkStatus();
  }

  absl::Status EncodeEnumConstantProperties(const std::any& properties,
                                            YAML::Emitter& out) const override {
    EncodeTestProps(
        std::any_cast<const TestEnumConstantProps&>(properties).data(), out);
    return absl::OkStatus();
  }

 private:
  std::string name_;
};

class StubAspect final : public AspectYaml {
 public:
  explicit StubAspect(absl::string_view name) : name_(name) {}

  absl::string_view name() const override { return name_; }

 private:
  std::string name_;
};

AspectYamlRegistry MakeTestRegistry() {
  return AspectYamlRegistry(std::make_unique<TestAspect>("aspect_a"),
                            std::make_unique<TestAspect>("aspect_b"),
                            std::make_unique<StubAspect>("stub_aspect"));
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

  AspectYamlRegistry registry = MakeTestRegistry();
  ASSERT_OK_AND_ASSIGN(YAML::Node root, typedef_internal::LoadYaml(contents));
  ASSERT_TRUE(root.IsSequence());

  std::vector<TypeDef> type_defs;
  for (const YAML::Node& item : root) {
    ASSERT_OK_AND_ASSIGN(TypeDef type_def, typedef_internal::ParseTypeDefNode(
                                               contents, item, registry));
    type_defs.push_back(std::move(type_def));
  }

  ASSERT_OK_AND_ASSIGN(types::TypeDefSet proto_set, TypeDefsToProto(type_defs));
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
      absl::StrCat(kTestTypeDefFilePath, "cel_type_errors.yaml"));
  ASSERT_THAT(cel::internal::GetFileContents(test_file, &contents), IsOk());

  std::string baseline;
  std::string baseline_file = cel::internal::ResolveRunfilesPath(
      absl::StrCat(kTestTypeDefFilePath, "cel_type_errors.baseline"));
  ASSERT_THAT(cel::internal::GetFileContents(baseline_file, &baseline), IsOk());
  baseline = absl::StripAsciiWhitespace(baseline);

  AspectYamlRegistry registry = MakeTestRegistry();
  ASSERT_OK_AND_ASSIGN(YAML::Node root, typedef_internal::LoadYaml(contents));
  ASSERT_TRUE(root.IsSequence());

  std::ostringstream out;
  out << "YAML SOURCE: cel_type_errors.yaml\n";
  for (const YAML::Node& item : root) {
    out << kBaselineSeparator;
    for (absl::string_view line : absl::StrSplit(YAML::Dump(item), '\n')) {
      out << "#  " << line << "\n";
    }
    out << "\n";
    absl::StatusOr<TypeDef> type_def =
        typedef_internal::ParseTypeDefNode(contents, item, registry);
    ASSERT_THAT(type_def, StatusIs(absl::StatusCode::kInvalidArgument));
    out << type_def.status().message() << "\n";
  }

  std::string actual(absl::StripAsciiWhitespace(out.str()));
  if (actual != baseline) {
    ABSL_LOG(INFO) << "Actual:\n" << actual;
    EXPECT_EQ(actual, baseline);
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
  ASSERT_OK_AND_ASSIGN(std::vector<TypeDef> type_defs,
                       TypeDefsFromProto(proto_set));

  std::ostringstream out;
  out << "PROTO SOURCE: cel_types.txtproto\n";
  for (const TypeDef& type_def : type_defs) {
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

TEST(TypeDefYamlTest, TypeDefFromYamlTopLevel) {
  AspectYamlRegistry registry = MakeTestRegistry();

  ASSERT_OK_AND_ASSIGN(TypeDef empty_def, TypeDefFromYaml("", registry));
  EXPECT_THAT(TypeDefToProto(empty_def),
              IsOkAndHolds(EqualsProto("struct_type {}")));

  ASSERT_OK_AND_ASSIGN(
      TypeDef struct_def,
      TypeDefFromYaml("struct:\n  name: \"NoFields\"", registry));
  EXPECT_THAT(TypeDefToProto(struct_def),
              IsOkAndHolds(EqualsProto("struct_type { name: \"NoFields\" }")));

  EXPECT_THAT(TypeDefFromYaml("[1]", registry),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Invalid CEL type definition YAML")));
}

TEST(TypeDefYamlTest, EncodeErrorOnUnregisteredAspect) {
  AspectYamlRegistry registry = MakeTestRegistry();
  ASSERT_OK_AND_ASSIGN(TypeDef type_def, TypeDefFromYaml(R"yaml(
                         struct:
                           name: "Item"
                           aspects:
                             aspect_a:
                               name: "Item"
                           fields:
                             - name: "f"
                               type: "int"
                               aspects:
                                 aspect_a:
                                   name: "f"
                       )yaml",
                                                         registry));

  AspectYamlRegistry empty_registry;
  std::stringstream ss;
  EXPECT_THAT(
      TypeDefToYaml(type_def, ss,
                    {.use_type_signatures = true, .registry = &empty_registry}),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Unsupported aspect: 'aspect_a'")));

  ASSERT_OK_AND_ASSIGN(TypeDef enum_def, TypeDefFromYaml(R"yaml(
                         enum:
                           name: "Status"
                           constants:
                             - name: "A"
                               aspects:
                                 aspect_a:
                                   name: "A"
                       )yaml",
                                                         registry));
  std::stringstream ss_enum;
  EXPECT_THAT(
      TypeDefToYaml(enum_def, ss_enum,
                    {.use_type_signatures = true, .registry = &empty_registry}),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Unsupported aspect: 'aspect_a'")));
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

  EXPECT_THAT(typedef_internal::LoadYaml("{ invalid: [ }"),
              StatusIs(absl::StatusCode::kInvalidArgument));

  YAML::Node seq = YAML::Load("[1, 2]");
  EXPECT_EQ(typedef_internal::GetString(seq), "");

  YAML::Node str_node = YAML::Load("hello");
  EXPECT_THAT(typedef_internal::GetBinary("hello", str_node), IsOkAndHolds(""));
  YAML::Node bad_b64 = YAML::Load("!!binary '%%%'");
  EXPECT_THAT(typedef_internal::GetBinary("!!binary '%%%'", bad_b64),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("not a valid Base64 encoded binary")));

  EXPECT_THAT(typedef_internal::GetBool("[1]", "k", seq),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Node 'k' is not a boolean")));
  YAML::Node not_bool = YAML::Load("not_a_bool");
  EXPECT_THAT(typedef_internal::GetBool("not_a_bool", "k", not_bool),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Node 'k' is not a boolean")));

  EXPECT_THAT(typedef_internal::GetInt32("[1]", "k", seq),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Node 'k' is not an integer")));
  YAML::Node not_int = YAML::Load("not_an_int");
  EXPECT_THAT(typedef_internal::GetInt32("not_an_int", "k", not_int),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Node 'k' is not an integer")));

  YAML::Node map_node = YAML::Load("a: 1\nb: 2");
  YAML::Node missing_node = YAML::Load("3");
  YAML::Node ctx =
      typedef_internal::GetContextNodeForKeyValue(map_node, missing_node);
  EXPECT_TRUE(ctx.is(missing_node));

  EXPECT_TRUE(typedef_internal::IsEmptyYamlNode(YAML::Load("\"\"")));
  EXPECT_FALSE(typedef_internal::IsEmptyYamlNode(YAML::Load("'a'")));
  EXPECT_EQ(Unindent("  a\nb"), "a\nb");

  EXPECT_THAT(typedef_internal::ParseTypeDefNode("1", YAML::Load("1")),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Type definition is not a map")));
}

enum class ThrowKind { kYaml, kStd, kInt, kBadEmitter };

class ThrowingAspect final : public AspectYaml {
 public:
  ThrowingAspect(absl::string_view name, ThrowKind kind)
      : name_(name), kind_(kind) {}

  absl::string_view name() const override { return name_; }

  absl::StatusOr<std::any> DecodeStructProperties(
      absl::string_view, const YAML::Node&) const override {
    if (kind_ == ThrowKind::kYaml) {
      throw YAML::Exception(YAML::Mark::null_mark(), "yaml err");
    }
    if (kind_ == ThrowKind::kStd) {
      throw std::runtime_error("std err");
    }
    throw 42;
  }

  absl::Status EncodeStructProperties(const std::any&,
                                      YAML::Emitter& out) const override {
    if (kind_ == ThrowKind::kYaml) {
      throw YAML::Exception(YAML::Mark::null_mark(), "yaml err");
    }
    if (kind_ == ThrowKind::kBadEmitter) {
      out << YAML::EndMap;
      return absl::OkStatus();
    }
    throw 42;
  }

 private:
  std::string name_;
  ThrowKind kind_;
};

TEST(TypeDefYamlTest, EncodePropertiesErrorsAndExceptionHandling) {
  AspectYamlRegistry registry = MakeTestRegistry();

  // Empty std::any in aspects is skipped without emitting.
  StructTypeDef empty_any_struct("Item", "", {{"aspect_a", std::any()}});
  std::stringstream empty_any_ss;
  EXPECT_THAT(
      TypeDefToYaml(TypeDef(empty_any_struct), empty_any_ss,
                    {.use_type_signatures = true, .registry = &registry}),
      IsOk());

  // StubAspect returns UnimplementedError on Encode*Properties.
  StructTypeDef unimplemented_struct(
      "Item", "", {{"stub_aspect", std::make_any<int>(123)}});
  std::stringstream ss1;
  EXPECT_THAT(
      TypeDefToYaml(TypeDef(unimplemented_struct), ss1,
                    {.use_type_signatures = true, .registry = &registry}),
      StatusIs(absl::StatusCode::kUnimplemented,
               HasSubstr("Custom struct properties are not supported "
                         "for aspect 'stub_aspect'")));

  // TestAspect uses std::any_cast<const TestStructProps&>, which throws
  // std::bad_any_cast (a std::exception) if given the wrong std::any payload.
  StructTypeDef wrong_any_struct("Item", "",
                                 {{"aspect_a", std::make_any<int>(123)}});
  std::stringstream ss3;
  EXPECT_THAT(
      TypeDefToYaml(TypeDef(wrong_any_struct), ss3,
                    {.use_type_signatures = true, .registry = &registry}),
      StatusIs(absl::StatusCode::kInternal));

  AspectYamlRegistry throw_reg(
      std::make_unique<ThrowingAspect>("yaml_ex", ThrowKind::kYaml),
      std::make_unique<ThrowingAspect>("std_ex", ThrowKind::kStd),
      std::make_unique<ThrowingAspect>("int_ex", ThrowKind::kInt),
      std::make_unique<ThrowingAspect>("bad_emit", ThrowKind::kBadEmitter));

  EXPECT_THAT(
      TypeDefFromYaml("struct:\n  name: 'A'\n  aspects:\n    yaml_ex: {}",
                      throw_reg),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("yaml err")));
  EXPECT_THAT(
      TypeDefFromYaml("struct:\n  name: 'A'\n  aspects:\n    std_ex: {}",
                      throw_reg),
      StatusIs(absl::StatusCode::kInternal, HasSubstr("std err")));
  EXPECT_THAT(
      TypeDefFromYaml("struct:\n  name: 'A'\n  aspects:\n    int_ex: {}",
                      throw_reg),
      StatusIs(absl::StatusCode::kInternal, HasSubstr("Unknown exception")));

  for (auto [aspect_name, substr] :
       std::vector<std::pair<std::string, std::string>>{
           {"yaml_ex", "yaml err"},
           {"int_ex", "Unknown exception"},
           {"bad_emit", ""}}) {
    StructTypeDef s("Item", "", {{aspect_name, std::make_any<int>(1)}});
    std::stringstream out1;
    EXPECT_THAT(
        TypeDefToYaml(TypeDef(s), out1,
                      {.use_type_signatures = true, .registry = &throw_reg}),
        StatusIs(absl::StatusCode::kInternal, HasSubstr(substr)));
  }
}

}  // namespace
}  // namespace cel
