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

#include "common/typedef/proto_to_typedef.h"

#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "google/protobuf/descriptor.pb.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/absl_log.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "common/minimal_descriptor_database.h"
#include "common/typedef/proto_schema.h"
#include "common/typedef/proto_schema_yaml.h"
#include "common/typedef/schema_yaml.h"
#include "common/typedef/typedef.h"
#include "common/typedef/typedef_yaml.h"
#include "common/typedef/yaml_test_helpers.h"
#include "internal/runfiles.h"
#include "internal/testing.h"
#include "cel/expr/conformance/proto2/test_all_types.pb.h"
#include "cel/expr/conformance/proto3/test_all_types.pb.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor_database.h"
#include "google/protobuf/text_format.h"

namespace cel {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::NotNull;

constexpr absl::string_view kTestTypeDefFilePath =
"_main/common/typedef/testdata/";

constexpr absl::string_view kBaselineSeparator =
    "--------------------------------------------------------------------\n";

SchemaYamlRegistry MakeYamlRegistry() {
  return SchemaYamlRegistry(std::make_unique<Proto2SchemaYaml>(),
                            std::make_unique<Proto3SchemaYaml>());
}

google::protobuf::FileDescriptorSet BuildTransitiveFileDescriptorSet(
    const google::protobuf::Descriptor* root_descriptor) {
  google::protobuf::FileDescriptorSet file_set;
  absl::flat_hash_set<absl::string_view> visited;
  std::vector<const google::protobuf::FileDescriptor*> stack = {root_descriptor->file()};
  while (!stack.empty()) {
    const google::protobuf::FileDescriptor* file = stack.back();
    stack.pop_back();
    if (!visited.insert(file->name()).second) {
      continue;
    }
    google::protobuf::FileDescriptorProto* file_proto = file_set.add_file();
    file->CopyTo(file_proto);
    file->CopySourceCodeInfoTo(file_proto);
    file->CopyJsonNameTo(file_proto);
    for (int i = file->dependency_count() - 1; i >= 0; --i) {
      stack.push_back(file->dependency(i));
    }
  }
  return file_set;
}

bool IsWellKnownMessage(const google::protobuf::Descriptor& desc) {
  switch (desc.well_known_type()) {
    case google::protobuf::Descriptor::WELLKNOWNTYPE_BOOLVALUE:
    case google::protobuf::Descriptor::WELLKNOWNTYPE_INT32VALUE:
    case google::protobuf::Descriptor::WELLKNOWNTYPE_INT64VALUE:
    case google::protobuf::Descriptor::WELLKNOWNTYPE_UINT32VALUE:
    case google::protobuf::Descriptor::WELLKNOWNTYPE_UINT64VALUE:
    case google::protobuf::Descriptor::WELLKNOWNTYPE_FLOATVALUE:
    case google::protobuf::Descriptor::WELLKNOWNTYPE_DOUBLEVALUE:
    case google::protobuf::Descriptor::WELLKNOWNTYPE_BYTESVALUE:
    case google::protobuf::Descriptor::WELLKNOWNTYPE_STRINGVALUE:
    case google::protobuf::Descriptor::WELLKNOWNTYPE_ANY:
    case google::protobuf::Descriptor::WELLKNOWNTYPE_DURATION:
    case google::protobuf::Descriptor::WELLKNOWNTYPE_TIMESTAMP:
    case google::protobuf::Descriptor::WELLKNOWNTYPE_VALUE:
    case google::protobuf::Descriptor::WELLKNOWNTYPE_LISTVALUE:
    case google::protobuf::Descriptor::WELLKNOWNTYPE_STRUCT:
      return true;
    default:
      return false;
  }
}

bool IsWellKnownEnum(const google::protobuf::EnumDescriptor& enum_desc) {
  return enum_desc.full_name() == "google.protobuf.NullValue";
}

template <typename DescriptorT>
std::string GetTrimmedLeadingComment(const DescriptorT& desc) {
  proto2::SourceLocation loc;
  if (!desc.GetSourceLocation(&loc)) {
    return "";
  }
  return std::string(absl::StripAsciiWhitespace(loc.leading_comments));
}

void VerifyEnumDescriptorEquivalence(const google::protobuf::EnumDescriptor& orig_enum,
                                     const google::protobuf::EnumDescriptor& rt_enum) {
  SCOPED_TRACE(absl::StrCat("Enum: ", orig_enum.full_name()));
  EXPECT_EQ(rt_enum.full_name(), orig_enum.full_name());
  EXPECT_EQ(rt_enum.file()->name(), orig_enum.file()->name());
  EXPECT_EQ(GetTrimmedLeadingComment(rt_enum),
            GetTrimmedLeadingComment(orig_enum));
  EXPECT_EQ(rt_enum.options().has_allow_alias(),
            orig_enum.options().has_allow_alias());
  if (orig_enum.options().has_allow_alias()) {
    EXPECT_EQ(rt_enum.options().allow_alias(),
              orig_enum.options().allow_alias());
  }
  ASSERT_EQ(rt_enum.value_count(), orig_enum.value_count());
  for (int i = 0; i < orig_enum.value_count(); ++i) {
    const google::protobuf::EnumValueDescriptor* orig_val = orig_enum.value(i);
    const google::protobuf::EnumValueDescriptor* rt_val = rt_enum.value(i);
    EXPECT_EQ(rt_val->name(), orig_val->name());
    EXPECT_EQ(rt_val->number(), orig_val->number());
    EXPECT_EQ(GetTrimmedLeadingComment(*rt_val),
              GetTrimmedLeadingComment(*orig_val));
  }
}

void VerifyFieldDefaultEquivalence(const google::protobuf::FieldDescriptor& orig_field,
                                   const google::protobuf::FieldDescriptor& rt_field) {
  ASSERT_EQ(rt_field.has_default_value(), orig_field.has_default_value());
  if (!orig_field.has_default_value()) {
    return;
  }
  switch (orig_field.cpp_type()) {
    case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
      EXPECT_EQ(rt_field.default_value_bool(), orig_field.default_value_bool());
      break;
    case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
      EXPECT_EQ(rt_field.default_value_int32(),
                orig_field.default_value_int32());
      break;
    case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
      EXPECT_EQ(rt_field.default_value_int64(),
                orig_field.default_value_int64());
      break;
    case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
      EXPECT_EQ(rt_field.default_value_uint32(),
                orig_field.default_value_uint32());
      break;
    case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
      EXPECT_EQ(rt_field.default_value_uint64(),
                orig_field.default_value_uint64());
      break;
    case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
      EXPECT_FLOAT_EQ(rt_field.default_value_float(),
                      orig_field.default_value_float());
      break;
    case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
      EXPECT_DOUBLE_EQ(rt_field.default_value_double(),
                       orig_field.default_value_double());
      break;
    case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
      EXPECT_EQ(rt_field.default_value_string(),
                orig_field.default_value_string());
      break;
    case google::protobuf::FieldDescriptor::CPPTYPE_ENUM:
      EXPECT_EQ(rt_field.default_value_enum()->full_name(),
                orig_field.default_value_enum()->full_name());
      break;
    case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
      break;
  }
}

void VerifyFieldDescriptorEquivalence(const google::protobuf::FieldDescriptor& orig_field,
                                      const google::protobuf::FieldDescriptor& rt_field) {
  SCOPED_TRACE(absl::StrCat("Field: ", orig_field.full_name()));
  EXPECT_EQ(rt_field.name(), orig_field.name());
  EXPECT_EQ(rt_field.number(), orig_field.number());
  EXPECT_EQ(rt_field.type(), orig_field.type());
  EXPECT_EQ(rt_field.is_required(), orig_field.is_required());
  EXPECT_EQ(rt_field.is_repeated(), orig_field.is_repeated());
  EXPECT_EQ(rt_field.is_map(), orig_field.is_map());
  EXPECT_EQ(rt_field.has_presence(), orig_field.has_presence());
  EXPECT_EQ(rt_field.json_name(), orig_field.json_name());
  EXPECT_EQ(GetTrimmedLeadingComment(rt_field),
            GetTrimmedLeadingComment(orig_field));

  if (orig_field.real_containing_oneof() != nullptr) {
    ASSERT_THAT(rt_field.real_containing_oneof(), NotNull());
    EXPECT_EQ(rt_field.real_containing_oneof()->name(),
              orig_field.real_containing_oneof()->name());
  } else {
    EXPECT_EQ(rt_field.real_containing_oneof(), nullptr);
  }

  VerifyFieldDefaultEquivalence(orig_field, rt_field);

  if (orig_field.is_map()) {
    const google::protobuf::FieldDescriptor* orig_key =
        orig_field.message_type()->map_key();
    const google::protobuf::FieldDescriptor* rt_key = rt_field.message_type()->map_key();
    ASSERT_THAT(rt_key, NotNull());
    EXPECT_EQ(rt_key->type(), orig_key->type());

    const google::protobuf::FieldDescriptor* orig_val =
        orig_field.message_type()->map_value();
    const google::protobuf::FieldDescriptor* rt_val =
        rt_field.message_type()->map_value();
    ASSERT_THAT(rt_val, NotNull());
    EXPECT_EQ(rt_val->type(), orig_val->type());
    if (orig_val->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
      EXPECT_EQ(rt_val->message_type()->full_name(),
                orig_val->message_type()->full_name());
    } else if (orig_val->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_ENUM) {
      EXPECT_EQ(rt_val->enum_type()->full_name(),
                orig_val->enum_type()->full_name());
    }
  } else if (orig_field.cpp_type() ==
             google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
    ASSERT_THAT(rt_field.message_type(), NotNull());
    EXPECT_EQ(rt_field.message_type()->full_name(),
              orig_field.message_type()->full_name());
  } else if (orig_field.cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_ENUM) {
    ASSERT_THAT(rt_field.enum_type(), NotNull());
    EXPECT_EQ(rt_field.enum_type()->full_name(),
              orig_field.enum_type()->full_name());
  }
}

void VerifyMessageDescriptorEquivalence(const google::protobuf::Descriptor& orig_msg,
                                        const google::protobuf::Descriptor& rt_msg) {
  SCOPED_TRACE(absl::StrCat("Message: ", orig_msg.full_name()));
  EXPECT_EQ(rt_msg.full_name(), orig_msg.full_name());
  EXPECT_EQ(rt_msg.file()->name(), orig_msg.file()->name());
  EXPECT_EQ(GetTrimmedLeadingComment(rt_msg),
            GetTrimmedLeadingComment(orig_msg));

  ASSERT_EQ(rt_msg.extension_range_count(), orig_msg.extension_range_count());
  for (int i = 0; i < orig_msg.extension_range_count(); ++i) {
    EXPECT_EQ(rt_msg.extension_range(i)->start_number(),
              orig_msg.extension_range(i)->start_number());
    EXPECT_EQ(rt_msg.extension_range(i)->end_number(),
              orig_msg.extension_range(i)->end_number());
  }

  ASSERT_EQ(rt_msg.real_oneof_decl_count(), orig_msg.real_oneof_decl_count());
  for (int i = 0; i < orig_msg.real_oneof_decl_count(); ++i) {
    const google::protobuf::OneofDescriptor* orig_oneof = orig_msg.real_oneof_decl(i);
    const google::protobuf::OneofDescriptor* rt_oneof =
        rt_msg.FindOneofByName(orig_oneof->name());
    ASSERT_THAT(rt_oneof, NotNull());
    EXPECT_EQ(rt_oneof->field_count(), orig_oneof->field_count());
  }

  ASSERT_EQ(rt_msg.field_count(), orig_msg.field_count());
  for (int i = 0; i < orig_msg.field_count(); ++i) {
    const google::protobuf::FieldDescriptor* orig_field = orig_msg.field(i);
    const google::protobuf::FieldDescriptor* rt_field =
        rt_msg.FindFieldByName(orig_field->name());
    ASSERT_THAT(rt_field, NotNull()) << orig_field->full_name();
    VerifyFieldDescriptorEquivalence(*orig_field, *rt_field);
  }

  for (int i = 0; i < orig_msg.enum_type_count(); ++i) {
    const google::protobuf::EnumDescriptor* orig_enum = orig_msg.enum_type(i);
    if (IsWellKnownEnum(*orig_enum)) {
      continue;
    }
    const google::protobuf::EnumDescriptor* rt_enum =
        rt_msg.FindEnumTypeByName(orig_enum->name());
    ASSERT_THAT(rt_enum, NotNull()) << orig_enum->full_name();
    VerifyEnumDescriptorEquivalence(*orig_enum, *rt_enum);
  }

  for (int i = 0; i < orig_msg.nested_type_count(); ++i) {
    const google::protobuf::Descriptor* orig_nested = orig_msg.nested_type(i);
    if (orig_nested->options().map_entry() ||
        IsWellKnownMessage(*orig_nested)) {
      continue;
    }
    const google::protobuf::Descriptor* rt_nested =
        rt_msg.FindNestedTypeByName(orig_nested->name());
    ASSERT_THAT(rt_nested, NotNull()) << orig_nested->full_name();
    VerifyMessageDescriptorEquivalence(*orig_nested, *rt_nested);
  }
}

void VerifyFileDescriptorSetEquivalence(
    const google::protobuf::FileDescriptorSet& orig_fds,
    const google::protobuf::FileDescriptorSet& rt_fds) {
  google::protobuf::SimpleDescriptorDatabase orig_ext_db;
  for (const google::protobuf::FileDescriptorProto& f : orig_fds.file()) {
    ASSERT_TRUE(orig_ext_db.Add(f));
  }
  google::protobuf::MergedDescriptorDatabase orig_db(&orig_ext_db,
                                           cel::GetMinimalDescriptorDatabase());
  google::protobuf::DescriptorPool orig_pool(&orig_db);

  google::protobuf::SimpleDescriptorDatabase rt_ext_db;
  for (const google::protobuf::FileDescriptorProto& f : rt_fds.file()) {
    ASSERT_TRUE(rt_ext_db.Add(f));
  }
  google::protobuf::MergedDescriptorDatabase rt_db(&rt_ext_db,
                                         cel::GetMinimalDescriptorDatabase());
  google::protobuf::DescriptorPool rt_pool(&rt_db);

  for (const google::protobuf::FileDescriptorProto& orig_file_proto : orig_fds.file()) {
    const google::protobuf::FileDescriptor* orig_file =
        orig_pool.FindFileByName(orig_file_proto.name());
    ASSERT_THAT(orig_file, NotNull());

    int non_wkt_enums = 0;
    for (int i = 0; i < orig_file->enum_type_count(); ++i) {
      if (!IsWellKnownEnum(*orig_file->enum_type(i))) {
        ++non_wkt_enums;
      }
    }
    int non_wkt_msgs = 0;
    for (int i = 0; i < orig_file->message_type_count(); ++i) {
      if (!IsWellKnownMessage(*orig_file->message_type(i))) {
        ++non_wkt_msgs;
      }
    }
    if (non_wkt_enums == 0 && non_wkt_msgs == 0) {
      // Pure well-known type file (e.g. timestamp.proto, struct.proto).
      continue;
    }

    const google::protobuf::FileDescriptor* rt_file =
        rt_pool.FindFileByName(orig_file->name());
    ASSERT_THAT(rt_file, NotNull()) << "Missing file: " << orig_file->name();
    EXPECT_EQ(rt_file->package(), orig_file->package());
    EXPECT_EQ(rt_file->enum_type_count(), non_wkt_enums);
    EXPECT_EQ(rt_file->message_type_count(), non_wkt_msgs);

    for (int i = 0; i < orig_file->enum_type_count(); ++i) {
      const google::protobuf::EnumDescriptor* orig_enum = orig_file->enum_type(i);
      if (IsWellKnownEnum(*orig_enum)) {
        continue;
      }
      const google::protobuf::EnumDescriptor* rt_enum =
          rt_pool.FindEnumTypeByName(orig_enum->full_name());
      ASSERT_THAT(rt_enum, NotNull());
      VerifyEnumDescriptorEquivalence(*orig_enum, *rt_enum);
    }

    for (int i = 0; i < orig_file->message_type_count(); ++i) {
      const google::protobuf::Descriptor* orig_msg = orig_file->message_type(i);
      if (IsWellKnownMessage(*orig_msg)) {
        continue;
      }
      const google::protobuf::Descriptor* rt_msg =
          rt_pool.FindMessageTypeByName(orig_msg->full_name());
      ASSERT_THAT(rt_msg, NotNull());
      VerifyMessageDescriptorEquivalence(*orig_msg, *rt_msg);
    }
  }
}

struct ProtoToTypeDefBaselineTestCase {
  const google::protobuf::Descriptor* root_descriptor;
  std::string baseline_file;
};

using ProtoToTypeDefBaselineTest =
    testing::TestWithParam<ProtoToTypeDefBaselineTestCase>;

TEST_P(ProtoToTypeDefBaselineTest, ConvertToYamlBaselineAndRoundTrip) {
  std::string baseline;
  std::string baseline_file = cel::internal::ResolveRunfilesPath(
      absl::StrCat(kTestTypeDefFilePath, GetParam().baseline_file));
  ASSERT_THAT(cel::internal::GetFileContents(baseline_file, &baseline), IsOk());
  baseline = absl::StripAsciiWhitespace(baseline);

  google::protobuf::FileDescriptorSet file_set =
      BuildTransitiveFileDescriptorSet(GetParam().root_descriptor);
  ASSERT_OK_AND_ASSIGN(TypeDefSet type_def_set,
                       FileDescriptorSetToTypeDefSet(file_set));

  SchemaYamlRegistry yaml_registry = MakeYamlRegistry();
  std::ostringstream yaml_out;
  ASSERT_THAT(TypeDefSetToYaml(type_def_set, yaml_registry, yaml_out), IsOk());

  // Verify the emitted YAML parses cleanly back into a TypeDefSet.
  ASSERT_OK_AND_ASSIGN(TypeDefSet roundtrip_set,
                       TypeDefSetFromYaml(yaml_out.str(), yaml_registry));
  EXPECT_EQ(roundtrip_set.types().size(), type_def_set.types().size());

  // Verify round-tripping TypeDefSet -> FileDescriptorSet produces an
  // equivalent FileDescriptorSet.
  ASSERT_OK_AND_ASSIGN(google::protobuf::FileDescriptorSet roundtrip_file_set,
                       TypeDefSetToFileDescriptorSet(roundtrip_set));
  VerifyFileDescriptorSetEquivalence(file_set, roundtrip_file_set);

  // Verify idempotence: FileDescriptorSet -> TypeDefSet -> FileDescriptorSet
  // -> TypeDefSet produces the exact same YAML.
  ASSERT_OK_AND_ASSIGN(TypeDefSet second_type_def_set,
                       FileDescriptorSetToTypeDefSet(roundtrip_file_set));
  std::ostringstream second_yaml_out;
  ASSERT_THAT(
      TypeDefSetToYaml(second_type_def_set, yaml_registry, second_yaml_out),
      IsOk());
  EXPECT_EQ(second_yaml_out.str(), yaml_out.str());

  std::ostringstream out;
  out << "PROTO DESCRIPTOR: " << GetParam().root_descriptor->full_name()
      << "\n";
  out << kBaselineSeparator;
  out << "TYPEDEF YAML:\n";
  out << yaml_out.str();

  std::string actual(absl::StripAsciiWhitespace(out.str()));
  if (actual != baseline) {
    // Log the actual result to make it easier to copy/paste into the baseline
    // file when updating the tests.
    ABSL_LOG(INFO) << "Actual:\n" << actual;
    EXPECT_EQ(actual, baseline);
  }
}

INSTANTIATE_TEST_SUITE_P(
    TestAllTypes, ProtoToTypeDefBaselineTest,
    testing::ValuesIn({
        ProtoToTypeDefBaselineTestCase{
            .root_descriptor =
                cel::expr::conformance::proto2::TestAllTypes::descriptor(),
            .baseline_file = "proto2_test_all_types_to_typedef.baseline",
        },
        ProtoToTypeDefBaselineTestCase{
            .root_descriptor =
                cel::expr::conformance::proto3::TestAllTypes::descriptor(),
            .baseline_file = "proto3_test_all_types_to_typedef.baseline",
        },
    }));

TEST(ProtoToTypeDefTest, CommentsCustomJsonNameAndAllowAlias) {
  google::protobuf::FileDescriptorSet file_set;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        name: "custom.proto"
        package: "com.example"
        syntax: "proto2"
        message_type {
          name: "CustomMsg"
          field {
            name: "custom_field"
            number: 1
            label: LABEL_OPTIONAL
            type: TYPE_STRING
            default_value: "hello"
            json_name: "customJson"
          }
          field {
            name: "normal_field"
            number: 2
            label: LABEL_OPTIONAL
            type: TYPE_INT32
            json_name: "normalField"
          }
        }
        enum_type {
          name: "CustomEnum"
          value { name: "UNKNOWN" number: 0 }
          value { name: "FIRST" number: 1 }
          value { name: "ALIAS_FIRST" number: 1 }
          options { allow_alias: true }
        }
        source_code_info {
          location {
            path: [ 4, 0 ]
            span: [ 0, 0, 0 ]
            leading_comments: " Message doc comment.\n"
          }
          location {
            path: [ 4, 0, 2, 0 ]
            span: [ 0, 0, 0 ]
            leading_comments: " Field doc comment.\n"
          }
          location {
            path: [ 5, 0 ]
            span: [ 0, 0, 0 ]
            leading_comments: " Enum doc comment.\n"
          }
          location {
            path: [ 5, 0, 2, 0 ]
            span: [ 0, 0, 0 ]
            leading_comments: " Enum constant doc comment.\n"
          }
        }
      )pb",
      file_set.add_file()));

  ASSERT_OK_AND_ASSIGN(TypeDefSet set, FileDescriptorSetToTypeDefSet(file_set));
  EXPECT_EQ(set.types().size(), 2);

  SchemaYamlRegistry yaml_registry = MakeYamlRegistry();
  std::ostringstream yaml_out;
  ASSERT_THAT(TypeDefSetToYaml(set, yaml_registry, yaml_out), IsOk());
  EXPECT_EQ(yaml_out.str(), Unindent(R"yaml(
              - enum:
                  name: "com.example.CustomEnum"
                  doc: "Enum doc comment."
                  schemas:
                    proto2:
                      allow_alias: true
                      file_name: "custom.proto"
                  constants:
                    - name: "UNKNOWN"
                      id: 0
                      doc: "Enum constant doc comment."
                    - name: "FIRST"
                      id: 1
                    - name: "ALIAS_FIRST"
                      id: 1
              - object:
                  name: "com.example.CustomMsg"
                  doc: "Message doc comment."
                  schemas:
                    proto2:
                      file_name: "custom.proto"
                  fields:
                    - name: "custom_field"
                      type: "string"
                      doc: "Field doc comment."
                      default: "hello"
                      schemas:
                        proto2:
                          id: 1
                          json_name: "customJson"
                    - name: "normal_field"
                      type: "int"
                      schemas:
                        proto2:
                          id: 2
                          type: "int32"
            )yaml"));

  ASSERT_OK_AND_ASSIGN(google::protobuf::FileDescriptorSet rt_file_set,
                       TypeDefSetToFileDescriptorSet(set));
  VerifyFileDescriptorSetEquivalence(file_set, rt_file_set);

  // Disabling comments and defaults omits `doc` and `default`.
  ASSERT_OK_AND_ASSIGN(
      TypeDefSet stripped_set,
      FileDescriptorSetToTypeDefSet(
          file_set, {.include_comments = false, .include_defaults = false}));
  const TypeDef* msg_def = stripped_set.FindTypeDef("com.example.CustomMsg");
  ASSERT_THAT(msg_def, NotNull());
  EXPECT_THAT(msg_def->doc(), Eq(""));
  const ObjectTypeDef::Field* f =
      msg_def->object_type().FindField("custom_field");
  ASSERT_THAT(f, NotNull());
  EXPECT_THAT(f->doc, Eq(""));
  EXPECT_FALSE(f->default_value.has_value());
}

TEST(ProtoToTypeDefTest, MinimalWktFallbackDatabaseResolvesWktImports) {
  // Verify that a FileDescriptorSet importing google/protobuf/timestamp.proto
  // without bundling timestamp.proto in the FileDescriptorSet still builds via
  // cel::GetMinimalDescriptorDatabase(), and round-trips back to a
  // FileDescriptorSet with or without bundling WKT files.
  google::protobuf::FileDescriptorSet file_set;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        name: "event.proto"
        package: "com.example"
        syntax: "proto3"
        dependency: "google/protobuf/timestamp.proto"
        message_type {
          name: "Event"
          field {
            name: "created_at"
            number: 1
            label: LABEL_OPTIONAL
            type: TYPE_MESSAGE
            type_name: ".google.protobuf.Timestamp"
          }
        }
      )pb",
      file_set.add_file()));

  ASSERT_OK_AND_ASSIGN(TypeDefSet set, FileDescriptorSetToTypeDefSet(file_set));
  EXPECT_EQ(set.types().size(), 1);
  const TypeDef* event_def = set.FindTypeDef("com.example.Event");
  ASSERT_THAT(event_def, NotNull());
  const ObjectTypeDef::Field* created_at =
      event_def->object_type().FindField("created_at");
  ASSERT_THAT(created_at, NotNull());
  EXPECT_TRUE(created_at->type.has_well_known());

  ASSERT_OK_AND_ASSIGN(google::protobuf::FileDescriptorSet rt_with_wkt,
                       TypeDefSetToFileDescriptorSet(
                           set, {.include_well_known_type_files = true}));
  EXPECT_EQ(rt_with_wkt.file_size(), 2);
  VerifyFileDescriptorSetEquivalence(file_set, rt_with_wkt);

  ASSERT_OK_AND_ASSIGN(google::protobuf::FileDescriptorSet rt_without_wkt,
                       TypeDefSetToFileDescriptorSet(
                           set, {.include_well_known_type_files = false}));
  EXPECT_EQ(rt_without_wkt.file_size(), 1);
  EXPECT_EQ(rt_without_wkt.file(0).name(), "event.proto");
  VerifyFileDescriptorSetEquivalence(file_set, rt_without_wkt);
}

TEST(ProtoToTypeDefTest, HandConstructedTypeDefSetRoundTrip) {
  SchemaYamlRegistry yaml_registry = MakeYamlRegistry();
  std::string yaml = Unindent(R"yaml(
    - object:
        name: "com.example.Order"
        doc: "Order message."
        fields:
          - name: "order_id"
            type: "string"
          - name: "count"
            type: "int"
  )yaml");
  ASSERT_OK_AND_ASSIGN(TypeDefSet set, TypeDefSetFromYaml(yaml, yaml_registry));
  ASSERT_OK_AND_ASSIGN(google::protobuf::FileDescriptorSet fds,
                       TypeDefSetToFileDescriptorSet(set));
  ASSERT_EQ(fds.file_size(), 1);
  EXPECT_EQ(fds.file(0).name(), "com/example/proto3_types.proto");
  EXPECT_EQ(fds.file(0).package(), "com.example");
  EXPECT_EQ(fds.file(0).syntax(), "proto3");

  ASSERT_OK_AND_ASSIGN(TypeDefSet empty_fds_check,
                       FileDescriptorSetToTypeDefSet(fds));
  EXPECT_NE(empty_fds_check.FindTypeDef("com.example.Order"), nullptr);

  TypeDefSet empty_set;
  ASSERT_OK_AND_ASSIGN(google::protobuf::FileDescriptorSet empty_fds,
                       TypeDefSetToFileDescriptorSet(empty_set));
  EXPECT_EQ(empty_fds.file_size(), 0);
}

TEST(ProtoToTypeDefTest, ValidationErrors) {
  // Empty file name.
  {
    google::protobuf::FileDescriptorSet file_set;
    file_set.add_file();
    EXPECT_THAT(FileDescriptorSetToTypeDefSet(file_set),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("File descriptor name cannot be empty.")));
  }

  // Duplicate file name.
  {
    google::protobuf::FileDescriptorSet file_set;
    file_set.add_file()->set_name("dup.proto");
    file_set.add_file()->set_name("dup.proto");
    EXPECT_THAT(FileDescriptorSetToTypeDefSet(file_set),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("Duplicate file descriptor: 'dup.proto'.")));
  }

  // Unsupported syntax (e.g. editions).
  {
    google::protobuf::FileDescriptorSet file_set;
    google::protobuf::FileDescriptorProto* f = file_set.add_file();
    f->set_name("editions.proto");
    f->set_syntax("editions");
    EXPECT_THAT(FileDescriptorSetToTypeDefSet(file_set),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("Unsupported proto syntax 'editions' in "
                                   "file 'editions.proto'.")));
  }

  // Unresolved dependency.
  {
    google::protobuf::FileDescriptorSet file_set;
    google::protobuf::FileDescriptorProto* f = file_set.add_file();
    f->set_name("broken.proto");
    f->add_dependency("nonexistent.proto");
    EXPECT_THAT(
        FileDescriptorSetToTypeDefSet(file_set),
        StatusIs(absl::StatusCode::kInvalidArgument,
                 HasSubstr("Failed to build file descriptor 'broken.proto'")));
  }

  // Unknown referenced type in TypeDefSetToFileDescriptorSet.
  {
    SchemaYamlRegistry yaml_registry = MakeYamlRegistry();
    ASSERT_OK_AND_ASSIGN(
        TypeDefSet set,
        TypeDefSetFromYaml("- object:\n"
                           "    name: \"com.example.Bad\"\n"
                           "    fields:\n"
                           "      - name: \"ref\"\n"
                           "        type: \"com.example.Missing\"\n",
                           yaml_registry));
    EXPECT_THAT(TypeDefSetToFileDescriptorSet(set),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("Unknown referenced protobuf type "
                                   "'com.example.Missing'")));
  }

  // Conflicting proto2 and proto3 schemas on a single TypeDef.
  {
    ObjectTypeDef obj;
    obj.name = "com.example.Mixed";
    obj.schema_specific_properties["proto2"] =
        std::make_unique<ProtoObjectProperties>();
    obj.schema_specific_properties["proto3"] =
        std::make_unique<ProtoObjectProperties>();
    TypeDefSet set;
    ASSERT_THAT(set.AddTypeDef(TypeDef(std::move(obj))), IsOk());
    EXPECT_THAT(TypeDefSetToFileDescriptorSet(set),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("mixes 'proto2' and 'proto3'")));
  }
}

}  // namespace
}  // namespace cel
