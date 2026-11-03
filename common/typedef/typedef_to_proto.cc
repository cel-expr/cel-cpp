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

#include "common/typedef/typedef_to_proto.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "google/protobuf/descriptor.pb.h"
#include "absl/algorithm/container.h"
#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/types/span.h"
#include "common/ast.h"
#include "common/constant.h"
#include "common/minimal_descriptor_database.h"
#include "common/typedef/proto_to_typedef.h"
#include "common/typedef/proto_type_helpers.h"
#include "common/typedef/typedef.h"
#include "internal/status_macros.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor_database.h"

namespace cel {
namespace {

using ::cel::typedef_internal::DefaultSingularProtoTypeName;
using ::cel::typedef_internal::FindAspect;
using ::cel::typedef_internal::GetOptionalBoolProperty;
using ::cel::typedef_internal::GetOptionalFieldIdProperty;
using ::cel::typedef_internal::GetOptionalStringProperty;
using ::cel::typedef_internal::GetProtoFileNameOverride;
using ::cel::typedef_internal::GetProtoNameOverride;
using ::cel::typedef_internal::GetWellKnownTypeFileName;
using ::cel::typedef_internal::ParseScalarProtoFieldType;
using ::cel::typedef_internal::SnakeToCamelCase;
using ::cel::typedef_internal::ValidateFileDescriptorSet;

std::string ShortTypeName(absl::string_view full_name) {
  size_t pos = full_name.rfind('.');
  return pos == absl::string_view::npos
             ? std::string(full_name)
             : std::string(full_name.substr(pos + 1));
}

absl::StatusOr<std::string> GetProtoShortName(const TypeDef& td) {
  CEL_ASSIGN_OR_RETURN(std::optional<absl::string_view> name_override,
                       GetProtoNameOverride(td));
  return ShortTypeName(name_override.value_or(td.name()));
}

absl::StatusOr<std::string> GetTopLevelProtoPackage(const TypeDef& root_td) {
  CEL_ASSIGN_OR_RETURN(std::optional<absl::string_view> name_override,
                       GetProtoNameOverride(root_td));
  if (name_override.has_value()) {
    if (size_t dot_pos = name_override->rfind('.');
        dot_pos != absl::string_view::npos) {
      return std::string(name_override->substr(0, dot_pos));
    }
  }
  absl::string_view full_name = root_td.name();
  size_t dot_pos = full_name.rfind('.');
  return dot_pos == absl::string_view::npos
             ? ""
             : std::string(full_name.substr(0, dot_pos));
}

struct FileSpec {
  std::string file_name;
  std::string package;
  std::vector<const TypeDef*> top_types;
};

// Partitions a `TypeDefSet` into top-level and nested types, resolves protobuf
// full names under `proto.name` overrides, and groups top-level types into
// planned `.proto` files with validated package assignments.
class ProtoSchemaPlan final {
 public:
  static absl::StatusOr<ProtoSchemaPlan> Create(
      const TypeDefSet& type_def_set) {
    ProtoSchemaPlan plan(&type_def_set);
    CEL_RETURN_IF_ERROR(plan.Initialize());
    return plan;
  }

  const TypeDef* absl_nullable FindTypeDef(absl::string_view name) const {
    if (const TypeDef* td = type_def_set_->FindTypeDef(name); td != nullptr) {
      return td;
    }
    auto it = proto_to_td_.find(name);
    return it != proto_to_td_.end() ? it->second : nullptr;
  }

  std::string GetProtoFullName(absl::string_view cel_name) const {
    auto it = cel_to_proto_full_name_.find(cel_name);
    return it != cel_to_proto_full_name_.end() ? it->second
                                               : std::string(cel_name);
  }

  absl::Span<const TypeDef* const> ChildrenOf(
      absl::string_view parent_cel_name) const {
    auto it = children_by_parent_.find(parent_cel_name);
    if (it == children_by_parent_.end()) {
      return {};
    }
    return it->second;
  }

  absl::Span<const std::string> spec_file_names() const {
    return spec_file_names_;
  }

  const FileSpec& GetFileSpec(absl::string_view file_name) const {
    return file_specs_.at(file_name);
  }

  bool HasGeneratedFile(absl::string_view file_name) const {
    return file_specs_.contains(file_name);
  }

  absl::string_view GetTypeFile(const TypeDef& td ABSL_ATTRIBUTE_LIFETIME_BOUND)
      const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    const TypeDef* root_td = root_by_cel_name_.at(td.name());
    return root_cel_name_to_file_.at(root_td->name());
  }

 private:
  struct RootTypeInfo {
    const TypeDef* root_td = nullptr;
    std::string package;
    std::optional<std::string> explicit_file_name;
  };

  explicit ProtoSchemaPlan(const TypeDefSet* type_def_set)
      : type_def_set_(type_def_set) {}

  absl::Status IndexSubtree(const TypeDef& root_td, const TypeDef& td,
                            absl::string_view proto_full_name) {
    root_by_cel_name_[td.name()] = &root_td;
    cel_to_proto_full_name_[td.name()] = std::string(proto_full_name);
    auto [it, inserted] = proto_to_td_.emplace(proto_full_name, &td);
    if (!inserted) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Duplicate protobuf type name '", proto_full_name, "' for types '",
          it->second->name(), "' and '", td.name(), "'."));
    }
    if (!td.is_struct()) {
      return absl::OkStatus();
    }
    for (const TypeDef* child : ChildrenOf(td.name())) {
      CEL_ASSIGN_OR_RETURN(std::string child_short_name,
                           GetProtoShortName(*child));
      std::string child_proto_name =
          absl::StrCat(proto_full_name, ".", child_short_name);
      CEL_RETURN_IF_ERROR(IndexSubtree(root_td, *child, child_proto_name));
    }
    return absl::OkStatus();
  }

  absl::Status Initialize() {
    std::vector<const TypeDef*> top_level_types;
    for (const TypeDef& td : type_def_set_->types()) {
      absl::string_view full_name = td.name();
      const TypeDef* parent_td = nullptr;
      if (size_t dot_pos = full_name.rfind('.');
          dot_pos != absl::string_view::npos) {
        const TypeDef* candidate =
            type_def_set_->FindTypeDef(full_name.substr(0, dot_pos));
        if (candidate != nullptr && candidate->is_struct()) {
          parent_td = candidate;
        }
      }
      if (parent_td != nullptr) {
        children_by_parent_[parent_td->name()].push_back(&td);
      } else {
        top_level_types.push_back(&td);
      }
    }

    std::vector<RootTypeInfo> root_infos;
    root_infos.reserve(top_level_types.size());

    for (const TypeDef* root_td : top_level_types) {
      RootTypeInfo info;
      info.root_td = root_td;
      CEL_ASSIGN_OR_RETURN(info.package, GetTopLevelProtoPackage(*root_td));
      CEL_ASSIGN_OR_RETURN(std::string short_name, GetProtoShortName(*root_td));
      std::string root_proto_name =
          info.package.empty() ? short_name
                               : absl::StrCat(info.package, ".", short_name);
      CEL_RETURN_IF_ERROR(IndexSubtree(*root_td, *root_td, root_proto_name));

      CEL_ASSIGN_OR_RETURN(std::optional<absl::string_view> file_override,
                           GetProtoFileNameOverride(*root_td));
      if (file_override.has_value()) {
        info.explicit_file_name = std::string(*file_override);
      }
      root_infos.push_back(std::move(info));
    }

    for (const RootTypeInfo& info : root_infos) {
      std::string file_name;
      if (info.explicit_file_name.has_value()) {
        file_name = *info.explicit_file_name;
      } else {
        file_name =
            info.package.empty()
                ? "types.proto"
                : absl::StrCat(absl::StrReplaceAll(info.package, {{".", "/"}}),
                               "/types.proto");
      }

      FileSpec initial_spec;
      initial_spec.file_name = file_name;
      initial_spec.package = info.package;
      auto [it, inserted] =
          file_specs_.try_emplace(file_name, std::move(initial_spec));
      if (inserted) {
        spec_file_names_.push_back(file_name);
      } else if (it->second.package != info.package) {
        return absl::InvalidArgumentError(absl::StrCat(
            "Conflicting packages '", it->second.package, "' and '",
            info.package, "' in file '", file_name, "'."));
      }
      it->second.top_types.push_back(info.root_td);
      root_cel_name_to_file_[info.root_td->name()] = file_name;
    }

    return absl::OkStatus();
  }

  const TypeDefSet* type_def_set_;
  absl::flat_hash_map<std::string, std::vector<const TypeDef*>>
      children_by_parent_;
  absl::flat_hash_map<std::string, const TypeDef*> root_by_cel_name_;
  absl::flat_hash_map<std::string, std::string> cel_to_proto_full_name_;
  absl::flat_hash_map<std::string, const TypeDef*> proto_to_td_;
  absl::flat_hash_map<std::string, FileSpec> file_specs_;
  std::vector<std::string> spec_file_names_;
  absl::flat_hash_map<std::string, std::string> root_cel_name_to_file_;
};

// Parses a protobuf map type override string of the form "map<K, V>" into a
// (key_type, value_type) pair of trimmed protobuf type names.
absl::StatusOr<std::pair<std::string, std::string>> ParseMapProtoType(
    absl::string_view map_type_str) {
  absl::string_view inner = map_type_str;
  if (absl::ConsumePrefix(&inner, "map<") && absl::ConsumeSuffix(&inner, ">")) {
    size_t comma_pos = inner.find(',');
    if (comma_pos != absl::string_view::npos &&
        inner.find(',', comma_pos + 1) == absl::string_view::npos) {
      std::string key_type(
          absl::StripAsciiWhitespace(inner.substr(0, comma_pos)));
      std::string val_type(
          absl::StripAsciiWhitespace(inner.substr(comma_pos + 1)));
      if (!key_type.empty() && !val_type.empty()) {
        return std::make_pair(std::move(key_type), std::move(val_type));
      }
    }
  }
  return absl::InvalidArgumentError(absl::StrCat(
      "Invalid protobuf map type override: '", map_type_str, "'."));
}

template <typename FloatT>
std::string FormatFloatDefault(FloatT val) {
  if (std::isnan(val)) {
    return "nan";
  }
  if (std::isinf(val)) {
    return val > 0 ? "inf" : "-inf";
  }
  return absl::StrCat(absl::HighPrecision(val));
}

absl::StatusOr<std::string> FormatConstantDefaultValue(
    const Constant& constant, google::protobuf::FieldDescriptorProto::Type proto_type) {
  switch (proto_type) {
    case google::protobuf::FieldDescriptorProto::TYPE_BOOL:
      if (!constant.has_bool_value()) {
        return absl::InvalidArgumentError(
            "Expected bool default value for bool field.");
      }
      return constant.bool_value() ? "true" : "false";
    case google::protobuf::FieldDescriptorProto::TYPE_INT32:
    case google::protobuf::FieldDescriptorProto::TYPE_SINT32:
    case google::protobuf::FieldDescriptorProto::TYPE_SFIXED32:
    case google::protobuf::FieldDescriptorProto::TYPE_INT64:
    case google::protobuf::FieldDescriptorProto::TYPE_SINT64:
    case google::protobuf::FieldDescriptorProto::TYPE_SFIXED64:
      if (!constant.has_int_value()) {
        return absl::InvalidArgumentError(
            "Expected int default value for integer field.");
      }
      if ((proto_type == google::protobuf::FieldDescriptorProto::TYPE_INT32 ||
           proto_type == google::protobuf::FieldDescriptorProto::TYPE_SINT32 ||
           proto_type == google::protobuf::FieldDescriptorProto::TYPE_SFIXED32) &&
          (constant.int_value() < std::numeric_limits<int32_t>::min() ||
           constant.int_value() > std::numeric_limits<int32_t>::max())) {
        return absl::InvalidArgumentError(
            absl::StrCat("Default value ", constant.int_value(),
                         " is out of range for 32-bit signed integer field."));
      }
      return absl::StrCat(constant.int_value());
    case google::protobuf::FieldDescriptorProto::TYPE_UINT32:
    case google::protobuf::FieldDescriptorProto::TYPE_FIXED32:
    case google::protobuf::FieldDescriptorProto::TYPE_UINT64:
    case google::protobuf::FieldDescriptorProto::TYPE_FIXED64:
      if (!constant.has_uint_value()) {
        return absl::InvalidArgumentError(
            "Expected uint default value for unsigned integer field.");
      }
      if ((proto_type == google::protobuf::FieldDescriptorProto::TYPE_UINT32 ||
           proto_type == google::protobuf::FieldDescriptorProto::TYPE_FIXED32) &&
          constant.uint_value() > std::numeric_limits<uint32_t>::max()) {
        return absl::InvalidArgumentError(absl::StrCat(
            "Default value ", constant.uint_value(),
            " is out of range for 32-bit unsigned integer field."));
      }
      return absl::StrCat(constant.uint_value());
    case google::protobuf::FieldDescriptorProto::TYPE_FLOAT:
    case google::protobuf::FieldDescriptorProto::TYPE_DOUBLE:
      if (!constant.has_double_value()) {
        return absl::InvalidArgumentError(
            "Expected double default value for floating-point field.");
      }
      if (proto_type == google::protobuf::FieldDescriptorProto::TYPE_FLOAT) {
        float fval = static_cast<float>(constant.double_value());
        if (std::isfinite(constant.double_value()) && !std::isfinite(fval)) {
          return absl::InvalidArgumentError(
              absl::StrCat("Default value ", constant.double_value(),
                           " is out of range for float field."));
        }
        return FormatFloatDefault(fval);
      }
      return FormatFloatDefault(constant.double_value());
    case google::protobuf::FieldDescriptorProto::TYPE_STRING:
    case google::protobuf::FieldDescriptorProto::TYPE_ENUM:
      if (!constant.has_string_value()) {
        return absl::InvalidArgumentError(
            "Expected string default value for string or enum field.");
      }
      return constant.string_value();
    case google::protobuf::FieldDescriptorProto::TYPE_BYTES:
      if (!constant.has_bytes_value()) {
        return absl::InvalidArgumentError(
            "Expected bytes default value for bytes field.");
      }
      return absl::CEscape(constant.bytes_value());
    default:
      return absl::InvalidArgumentError(
          "Scalar Constant default value is only supported on primitive and "
          "enum fields.");
  }
}

std::vector<int> ChildPath(absl::Span<const int> base_path, int field_number,
                           int index) {
  std::vector<int> path(base_path.begin(), base_path.end());
  path.push_back(field_number);
  path.push_back(index);
  return path;
}

// Emits a single `FileDescriptorProto` (messages, enums, comments, and sorted
// file imports) from a planned `FileSpec`.
class FileProtoBuilder final {
 public:
  FileProtoBuilder(const ProtoSchemaPlan& plan,
                   const TypeDefToProtoOptions& options)
      : plan_(plan), options_(options) {}

  absl::StatusOr<google::protobuf::FileDescriptorProto> BuildFile(
      const FileSpec& spec,
      absl::flat_hash_set<std::string>* required_wkt_files) {
    google::protobuf::FileDescriptorProto file_proto;
    file_proto.set_name(spec.file_name);
    if (!spec.package.empty()) {
      file_proto.set_package(spec.package);
    }
    file_proto.set_syntax(options_.syntax == ProtoSyntax::kProto3 ? "proto3"
                                                                  : "proto2");

    for (const TypeDef* top_td : spec.top_types) {
      if (top_td->is_enum()) {
        int enum_idx = file_proto.enum_type_size();
        CEL_RETURN_IF_ERROR(BuildEnum(
            *top_td,
            {google::protobuf::FileDescriptorProto::kEnumTypeFieldNumber, enum_idx},
            file_proto.add_enum_type()));
      } else if (top_td->is_struct()) {
        int msg_idx = file_proto.message_type_size();
        CEL_RETURN_IF_ERROR(BuildMessage(
            *top_td,
            {google::protobuf::FileDescriptorProto::kMessageTypeFieldNumber, msg_idx},
            file_proto.add_message_type()));
      }
    }

    if (source_code_info_.location_size() > 0) {
      *file_proto.mutable_source_code_info() = std::move(source_code_info_);
    }

    for (const std::string& dep_file : referenced_files_) {
      if (dep_file != spec.file_name) {
        file_proto.add_dependency(dep_file);
        if (!plan_.HasGeneratedFile(dep_file)) {
          required_wkt_files->insert(dep_file);
        }
      }
    }

    return file_proto;
  }

 private:
  struct ParsedProtoFieldProps {
    std::optional<absl::string_view> name;
    std::optional<int32_t> id;
    std::optional<absl::string_view> json_name;
    std::optional<absl::string_view> type;
  };

  struct ResolvedField {
    const StructTypeDef::Field* field = nullptr;
    ParsedProtoFieldProps proto_props;
    std::optional<int> real_oneof_index;
  };

  static absl::StatusOr<ParsedProtoFieldProps> ParseProtoFieldProps(
      const StructTypeDef::Field& field ABSL_ATTRIBUTE_LIFETIME_BOUND) {
    ParsedProtoFieldProps props;
    const Aspect* aspect = FindAspect(field, kProtoAspectName);
    CEL_ASSIGN_OR_RETURN(props.name, GetOptionalStringProperty(aspect, "name"));
    CEL_ASSIGN_OR_RETURN(props.id, GetOptionalFieldIdProperty(aspect, "id"));
    CEL_ASSIGN_OR_RETURN(props.json_name,
                         GetOptionalStringProperty(aspect, "json_name"));
    CEL_ASSIGN_OR_RETURN(props.type, GetOptionalStringProperty(aspect, "type"));
    return props;
  }

  void AddComment(absl::Span<const int> path, absl::string_view doc) {
    if (!options_.include_comments || doc.empty()) {
      return;
    }
    google::protobuf::SourceCodeInfo::Location* loc = source_code_info_.add_location();
    for (int p : path) {
      loc->add_path(p);
    }
    loc->add_span(0);
    loc->add_span(0);
    loc->add_span(0);
    loc->set_leading_comments(absl::StrCat(" ", doc, "\n"));
  }

  absl::Status BuildEnum(const TypeDef& enum_td,
                         absl::Span<const int> base_path,
                         google::protobuf::EnumDescriptorProto* enum_proto) {
    const EnumTypeDef& enum_def = enum_td.enum_type();
    CEL_ASSIGN_OR_RETURN(std::string short_name, GetProtoShortName(enum_td));
    enum_proto->set_name(std::move(short_name));
    AddComment(base_path, enum_def.doc());

    const Aspect* enum_aspect = FindAspect(enum_def, kProtoAspectName);
    CEL_ASSIGN_OR_RETURN(std::optional<bool> allow_alias,
                         GetOptionalBoolProperty(enum_aspect, "allow_alias"));
    if (allow_alias.has_value()) {
      enum_proto->mutable_options()->set_allow_alias(*allow_alias);
    }

    for (size_t i = 0; i < enum_def.constants().size(); ++i) {
      const EnumTypeDef::EnumConstant& constant = enum_def.constants()[i];
      google::protobuf::EnumValueDescriptorProto* val_proto = enum_proto->add_value();
      const Aspect* const_aspect = FindAspect(constant, kProtoAspectName);
      CEL_ASSIGN_OR_RETURN(std::optional<absl::string_view> const_name,
                           GetOptionalStringProperty(const_aspect, "name"));
      val_proto->set_name(const_name.value_or(constant.name()));
      val_proto->set_number(constant.id());
      AddComment(
          ChildPath(base_path, google::protobuf::EnumDescriptorProto::kValueFieldNumber,
                    static_cast<int>(i)),
          constant.doc());
    }
    return absl::OkStatus();
  }

  absl::Status PopulateSingularFieldProtoType(
      absl::string_view proto_type_str,
      google::protobuf::FieldDescriptorProto* field_proto) {
    if (std::optional<google::protobuf::FieldDescriptorProto::Type> scalar_type =
            ParseScalarProtoFieldType(proto_type_str);
        scalar_type.has_value()) {
      field_proto->set_type(*scalar_type);
      return absl::OkStatus();
    }
    if (proto_type_str == "google.protobuf.NullValue") {
      field_proto->set_type(google::protobuf::FieldDescriptorProto::TYPE_ENUM);
      field_proto->set_type_name(".google.protobuf.NullValue");
      referenced_files_.emplace("google/protobuf/struct.proto");
      return absl::OkStatus();
    }
    if (const TypeDef* target_td = plan_.FindTypeDef(proto_type_str);
        target_td != nullptr) {
      field_proto->set_type(target_td->is_enum()
                                ? google::protobuf::FieldDescriptorProto::TYPE_ENUM
                                : google::protobuf::FieldDescriptorProto::TYPE_MESSAGE);
      std::string target_proto_full_name =
          plan_.GetProtoFullName(target_td->name());
      field_proto->set_type_name(absl::StrCat(".", target_proto_full_name));
      referenced_files_.emplace(plan_.GetTypeFile(*target_td));
      return absl::OkStatus();
    }
    if (std::optional<absl::string_view> wkt_file =
            GetWellKnownTypeFileName(proto_type_str);
        wkt_file.has_value()) {
      field_proto->set_type(google::protobuf::FieldDescriptorProto::TYPE_MESSAGE);
      field_proto->set_type_name(absl::StrCat(".", proto_type_str));
      referenced_files_.emplace(*wkt_file);
      return absl::OkStatus();
    }
    return absl::InvalidArgumentError(absl::StrCat(
        "Unknown referenced protobuf type '", proto_type_str, "'."));
  }

  absl::StatusOr<std::vector<ResolvedField>> ResolveMessageMembers(
      const StructTypeDef& struct_def, google::protobuf::DescriptorProto* msg_proto,
      absl::flat_hash_set<int32_t>* used_field_numbers) {
    // Pre-collect explicit proto oneof names across all oneof groups so
    // synthesized oneof names never collide with explicit ones.
    absl::flat_hash_set<std::string> used_oneof_names;
    std::vector<std::optional<absl::string_view>> explicit_oneof_names;
    explicit_oneof_names.reserve(struct_def.members().size());
    for (const StructTypeDef::Member& member : struct_def.members()) {
      std::optional<absl::string_view> oneof_name;
      if (member.is_oneof() && !member.oneof().fields().empty()) {
        const Aspect* oneof_aspect =
            FindAspect(member.oneof(), kProtoAspectName);
        CEL_ASSIGN_OR_RETURN(oneof_name,
                             GetOptionalStringProperty(oneof_aspect, "name"));
        if (oneof_name.has_value() &&
            !used_oneof_names.insert(std::string(*oneof_name)).second) {
          return absl::InvalidArgumentError(
              absl::StrCat("Duplicate proto oneof name '", *oneof_name,
                           "' in struct '", struct_def.name(), "'."));
        }
      }
      explicit_oneof_names.push_back(oneof_name);
    }

    // Register oneof declarations, collect explicit field numbers, and flatten
    // members into ordered fields in a single pass.
    std::vector<ResolvedField> resolved_fields;
    int next_synth_oneof_id = 0;
    for (size_t i = 0; i < struct_def.members().size(); ++i) {
      const StructTypeDef::Member& member = struct_def.members()[i];
      if (member.is_field()) {
        CEL_ASSIGN_OR_RETURN(ParsedProtoFieldProps fp,
                             ParseProtoFieldProps(member.field()));
        if (fp.id.has_value()) {
          used_field_numbers->insert(*fp.id);
        }
        resolved_fields.push_back(
            ResolvedField{&member.field(), fp, std::nullopt});
      } else if (member.is_oneof()) {
        const StructTypeDef::Oneof& oneof_def = member.oneof();
        if (oneof_def.fields().empty()) {
          continue;
        }
        std::string resolved_oneof_name;
        if (explicit_oneof_names[i].has_value()) {
          resolved_oneof_name = std::string(*explicit_oneof_names[i]);
        } else {
          do {
            resolved_oneof_name = absl::StrCat("oneof_", next_synth_oneof_id);
            ++next_synth_oneof_id;
          } while (!used_oneof_names.insert(resolved_oneof_name).second);
        }
        int oneof_idx = msg_proto->oneof_decl_size();
        msg_proto->add_oneof_decl()->set_name(std::move(resolved_oneof_name));
        for (const StructTypeDef::Field& f : oneof_def.fields()) {
          CEL_ASSIGN_OR_RETURN(ParsedProtoFieldProps fp,
                               ParseProtoFieldProps(f));
          if (fp.id.has_value()) {
            used_field_numbers->insert(*fp.id);
          }
          resolved_fields.push_back(ResolvedField{&f, fp, oneof_idx});
        }
      }
    }
    return resolved_fields;
  }

  absl::Status BuildNestedTypes(
      const StructTypeDef& struct_def, absl::Span<const int> base_path,
      google::protobuf::DescriptorProto* msg_proto,
      absl::flat_hash_set<std::string>* used_nested_type_names) {
    for (const TypeDef* child : plan_.ChildrenOf(struct_def.name())) {
      CEL_ASSIGN_OR_RETURN(std::string child_short_name,
                           GetProtoShortName(*child));
      used_nested_type_names->insert(std::move(child_short_name));
      if (child->is_enum()) {
        int enum_idx = msg_proto->enum_type_size();
        CEL_RETURN_IF_ERROR(BuildEnum(
            *child,
            ChildPath(base_path, google::protobuf::DescriptorProto::kEnumTypeFieldNumber,
                      enum_idx),
            msg_proto->add_enum_type()));
      } else if (child->is_struct()) {
        int nested_idx = msg_proto->nested_type_size();
        CEL_RETURN_IF_ERROR(BuildMessage(
            *child,
            ChildPath(base_path,
                      google::protobuf::DescriptorProto::kNestedTypeFieldNumber,
                      nested_idx),
            msg_proto->add_nested_type()));
      }
    }
    return absl::OkStatus();
  }

  absl::Status BuildMapField(
      const StructTypeDef& struct_def, const StructTypeDef::Field& field_def,
      const ParsedProtoFieldProps& field_props,
      std::optional<int> real_oneof_index, google::protobuf::DescriptorProto* msg_proto,
      google::protobuf::FieldDescriptorProto* field_proto,
      absl::flat_hash_set<std::string>* used_nested_type_names) {
    if (real_oneof_index.has_value()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Protobuf oneof field '", field_def.name(), "' in struct '",
          struct_def.name(), "' cannot be a map field."));
    }
    CEL_ASSIGN_OR_RETURN(
        std::string key_proto_type,
        DefaultSingularProtoTypeName(field_def.type().map_type().key_type()));
    CEL_ASSIGN_OR_RETURN(
        std::string val_proto_type,
        DefaultSingularProtoTypeName(field_def.type().map_type().value_type()));
    if (field_props.type.has_value()) {
      std::pair<std::string, std::string> parsed_types;
      CEL_ASSIGN_OR_RETURN(parsed_types, ParseMapProtoType(*field_props.type));
      key_proto_type = std::move(parsed_types.first);
      val_proto_type = std::move(parsed_types.second);
    }

    std::string entry_name = absl::StrCat(
        SnakeToCamelCase(field_proto->name(), /*upper_first=*/true), "Entry");
    if (!used_nested_type_names->insert(entry_name).second) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Map entry type '", entry_name, "' for field '", field_proto->name(),
          "' collides with a nested type in struct '", struct_def.name(),
          "'."));
    }

    google::protobuf::DescriptorProto* entry_proto = msg_proto->add_nested_type();
    entry_proto->set_name(entry_name);
    entry_proto->mutable_options()->set_map_entry(true);

    google::protobuf::FieldDescriptorProto* key_field_proto = entry_proto->add_field();
    key_field_proto->set_name("key");
    key_field_proto->set_number(1);
    key_field_proto->set_label(google::protobuf::FieldDescriptorProto::LABEL_OPTIONAL);
    CEL_RETURN_IF_ERROR(
        PopulateSingularFieldProtoType(key_proto_type, key_field_proto));

    google::protobuf::FieldDescriptorProto* val_field_proto = entry_proto->add_field();
    val_field_proto->set_name("value");
    val_field_proto->set_number(2);
    val_field_proto->set_label(google::protobuf::FieldDescriptorProto::LABEL_OPTIONAL);
    CEL_RETURN_IF_ERROR(
        PopulateSingularFieldProtoType(val_proto_type, val_field_proto));

    field_proto->set_label(google::protobuf::FieldDescriptorProto::LABEL_REPEATED);
    field_proto->set_type(google::protobuf::FieldDescriptorProto::TYPE_MESSAGE);
    field_proto->set_type_name(absl::StrCat(
        ".", plan_.GetProtoFullName(struct_def.name()), ".", entry_name));
    return absl::OkStatus();
  }

  absl::Status BuildRepeatedField(const StructTypeDef& struct_def,
                                  const StructTypeDef::Field& field_def,
                                  const ParsedProtoFieldProps& field_props,
                                  std::optional<int> real_oneof_index,
                                  google::protobuf::FieldDescriptorProto* field_proto) {
    if (real_oneof_index.has_value()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Protobuf oneof field '", field_def.name(), "' in struct '",
          struct_def.name(), "' cannot be a repeated field."));
    }
    field_proto->set_label(google::protobuf::FieldDescriptorProto::LABEL_REPEATED);
    const TypeSpec& elem_spec = field_def.type().list_type().elem_type();
    std::string elem_proto_type;
    if (field_props.type.has_value()) {
      elem_proto_type = std::string(*field_props.type);
    } else {
      CEL_ASSIGN_OR_RETURN(elem_proto_type,
                           DefaultSingularProtoTypeName(elem_spec));
    }
    return PopulateSingularFieldProtoType(elem_proto_type, field_proto);
  }

  absl::Status BuildSingularField(const StructTypeDef::Field& field_def,
                                  const ParsedProtoFieldProps& field_props,
                                  std::optional<int> real_oneof_index,
                                  google::protobuf::FieldDescriptorProto* field_proto) {
    field_proto->set_label(google::protobuf::FieldDescriptorProto::LABEL_OPTIONAL);

    std::string singular_proto_type;
    if (field_props.type.has_value()) {
      singular_proto_type = std::string(*field_props.type);
    } else {
      CEL_ASSIGN_OR_RETURN(singular_proto_type,
                           DefaultSingularProtoTypeName(field_def.type()));
    }
    CEL_RETURN_IF_ERROR(
        PopulateSingularFieldProtoType(singular_proto_type, field_proto));

    if (real_oneof_index.has_value()) {
      field_proto->set_oneof_index(*real_oneof_index);
    }

    if (options_.syntax == ProtoSyntax::kProto2 &&
        field_def.default_value().has_value()) {
      CEL_ASSIGN_OR_RETURN(std::string default_str,
                           FormatConstantDefaultValue(field_def.default_value(),
                                                      field_proto->type()));
      if (field_proto->type() == google::protobuf::FieldDescriptorProto::TYPE_ENUM) {
        if (const TypeDef* enum_td = plan_.FindTypeDef(singular_proto_type);
            enum_td != nullptr && enum_td->is_enum()) {
          if (const EnumTypeDef::EnumConstant* constant =
                  enum_td->enum_type().FindConstant(default_str);
              constant != nullptr) {
            const Aspect* const_aspect =
                FindAspect(*constant, kProtoAspectName);
            CEL_ASSIGN_OR_RETURN(
                std::optional<absl::string_view> const_name,
                GetOptionalStringProperty(const_aspect, "name"));
            if (const_name.has_value()) {
              default_str = std::string(*const_name);
            }
          }
        }
      }
      field_proto->set_default_value(default_str);
    }
    return absl::OkStatus();
  }

  absl::Status BuildMessage(const TypeDef& struct_td,
                            absl::Span<const int> base_path,
                            google::protobuf::DescriptorProto* msg_proto) {
    const StructTypeDef& struct_def = struct_td.struct_type();
    CEL_ASSIGN_OR_RETURN(std::string short_name, GetProtoShortName(struct_td));
    msg_proto->set_name(std::move(short_name));
    AddComment(base_path, struct_def.doc());

    absl::flat_hash_set<int32_t> used_field_numbers;
    CEL_ASSIGN_OR_RETURN(
        std::vector<ResolvedField> resolved_fields,
        ResolveMessageMembers(struct_def, msg_proto, &used_field_numbers));

    // Build nested enums and explicit nested messages first so we know their
    // names and can detect collisions with synthesized map-entry messages.
    absl::flat_hash_set<std::string> used_nested_type_names;
    CEL_RETURN_IF_ERROR(BuildNestedTypes(struct_def, base_path, msg_proto,
                                         &used_nested_type_names));

    int32_t next_auto_field_number = 1;
    auto allocate_field_number = [&]() -> int32_t {
      while (used_field_numbers.contains(next_auto_field_number) ||
             (next_auto_field_number >=
                  google::protobuf::FieldDescriptor::kFirstReservedNumber &&
              next_auto_field_number <=
                  google::protobuf::FieldDescriptor::kLastReservedNumber)) {
        ++next_auto_field_number;
      }
      const int32_t allocated = next_auto_field_number;
      used_field_numbers.insert(allocated);
      ++next_auto_field_number;
      return allocated;
    };

    for (size_t i = 0; i < resolved_fields.size(); ++i) {
      const StructTypeDef::Field& field_def = *resolved_fields[i].field;
      const ParsedProtoFieldProps& field_props = resolved_fields[i].proto_props;
      const std::optional<int> real_oneof_index =
          resolved_fields[i].real_oneof_index;

      google::protobuf::FieldDescriptorProto* field_proto = msg_proto->add_field();
      field_proto->set_name(field_props.name.value_or(field_def.name()));
      field_proto->set_number(field_props.id.has_value()
                                  ? *field_props.id
                                  : allocate_field_number());
      if (field_props.json_name.has_value()) {
        field_proto->set_json_name(*field_props.json_name);
      }

      AddComment(
          ChildPath(base_path, google::protobuf::DescriptorProto::kFieldFieldNumber,
                    static_cast<int>(i)),
          field_def.doc());

      const bool is_singular_struct_override =
          field_props.type == "google.protobuf.Struct";
      const bool is_singular_list_value_override =
          field_props.type == "google.protobuf.ListValue";

      if (field_def.type().has_map_type() && !is_singular_struct_override) {
        CEL_RETURN_IF_ERROR(
            BuildMapField(struct_def, field_def, field_props, real_oneof_index,
                          msg_proto, field_proto, &used_nested_type_names));
      } else if (field_def.type().has_list_type() &&
                 !is_singular_list_value_override) {
        CEL_RETURN_IF_ERROR(BuildRepeatedField(
            struct_def, field_def, field_props, real_oneof_index, field_proto));
      } else {
        CEL_RETURN_IF_ERROR(BuildSingularField(field_def, field_props,
                                               real_oneof_index, field_proto));
      }
    }
    return absl::OkStatus();
  }

  const ProtoSchemaPlan& plan_;
  const TypeDefToProtoOptions& options_;
  google::protobuf::SourceCodeInfo source_code_info_;
  // Use btree_set so file_proto.dependency() is unique and deterministically
  // sorted.
  absl::btree_set<std::string> referenced_files_;
};

void AddWellKnownTypeFiles(
    const absl::flat_hash_set<std::string>& required_wkt_files,
    absl::flat_hash_map<std::string, google::protobuf::FileDescriptorProto>*
        files_by_name) {
  if (required_wkt_files.empty()) {
    return;
  }
  google::protobuf::DescriptorDatabase* wkt_db = cel::GetMinimalDescriptorDatabase();
  for (const std::string& wkt_file : required_wkt_files) {
    google::protobuf::FileDescriptorProto wkt_proto;
    if (wkt_db->FindFileByName(wkt_file, &wkt_proto)) {
      (*files_by_name)[wkt_file] = std::move(wkt_proto);
    }
  }
}

enum class TopoVisitState { kVisiting, kVisited };

absl::Status TopoSortFile(
    absl::string_view file_name,
    absl::flat_hash_map<std::string, google::protobuf::FileDescriptorProto>&
        files_by_name,
    absl::flat_hash_map<std::string, TopoVisitState>* visit_state,
    google::protobuf::FileDescriptorSet* output_set) {
  if (auto state_it = visit_state->find(file_name);
      state_it != visit_state->end()) {
    if (state_it->second == TopoVisitState::kVisited) {
      return absl::OkStatus();
    }
    return absl::InvalidArgumentError(
        absl::StrCat("Cyclic file dependency involving '", file_name, "'."));
  }
  (*visit_state)[file_name] = TopoVisitState::kVisiting;
  if (auto it = files_by_name.find(file_name); it != files_by_name.end()) {
    for (const std::string& dep : it->second.dependency()) {
      if (files_by_name.contains(dep)) {
        CEL_RETURN_IF_ERROR(
            TopoSortFile(dep, files_by_name, visit_state, output_set));
      }
    }
    *output_set->add_file() = std::move(it->second);
  }
  (*visit_state)[file_name] = TopoVisitState::kVisited;
  return absl::OkStatus();
}

absl::StatusOr<google::protobuf::FileDescriptorSet> TopoSortFiles(
    absl::flat_hash_map<std::string, google::protobuf::FileDescriptorProto>&
        files_by_name) {
  // Sort file names lexicographically so independent files are emitted in a
  // deterministic order.
  std::vector<absl::string_view> sorted_file_names;
  sorted_file_names.reserve(files_by_name.size());
  for (const auto& [file_name, _] : files_by_name) {
    sorted_file_names.push_back(file_name);
  }
  absl::c_sort(sorted_file_names);

  google::protobuf::FileDescriptorSet result;
  absl::flat_hash_map<std::string, TopoVisitState> visit_state;
  for (absl::string_view file_name : sorted_file_names) {
    CEL_RETURN_IF_ERROR(
        TopoSortFile(file_name, files_by_name, &visit_state, &result));
  }
  return result;
}

}  // namespace

absl::StatusOr<google::protobuf::FileDescriptorSet> TypeDefSetToFileDescriptorSet(
    const TypeDefSet& type_def_set, const TypeDefToProtoOptions& options) {
  if (type_def_set.types().empty()) {
    return google::protobuf::FileDescriptorSet{};
  }

  CEL_ASSIGN_OR_RETURN(ProtoSchemaPlan plan,
                       ProtoSchemaPlan::Create(type_def_set));

  absl::flat_hash_map<std::string, google::protobuf::FileDescriptorProto> files_by_name;
  absl::flat_hash_set<std::string> required_wkt_files;

  for (const std::string& spec_file_name : plan.spec_file_names()) {
    const FileSpec& spec = plan.GetFileSpec(spec_file_name);
    FileProtoBuilder builder(plan, options);
    CEL_ASSIGN_OR_RETURN(google::protobuf::FileDescriptorProto file_proto,
                         builder.BuildFile(spec, &required_wkt_files));
    files_by_name[spec.file_name] = std::move(file_proto);
  }

  if (options.include_well_known_type_files) {
    AddWellKnownTypeFiles(required_wkt_files, &files_by_name);
  }

  CEL_ASSIGN_OR_RETURN(google::protobuf::FileDescriptorSet result,
                       TopoSortFiles(files_by_name));
  CEL_RETURN_IF_ERROR(ValidateFileDescriptorSet(result));
  return result;
}

}  // namespace cel
