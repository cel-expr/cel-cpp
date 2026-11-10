# Copyright 2024 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    # Implementation details for the checker library.
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "test_ast_helpers",
    testonly = 1,
    srcs = ["test_ast_helpers.cc"],
    hdrs = ["test_ast_helpers.h"],
    deps = [
        "//common:ast",
        "//internal:status_macros",
        "//parser",
        "//parser:options",
        "//parser:parser_interface",
        "@com_google_absl//absl/log:absl_check",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:string_view",
    ],
)

cc_test(
    name = "test_ast_helpers_test",
    srcs = ["test_ast_helpers_test.cc"],
    deps = [
        ":test_ast_helpers",
        "//common:ast",
        "//internal:testing",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:status_matchers",
    ],
)

cc_library(
    name = "builtins_arena",
    srcs = ["builtins_arena.cc"],
    hdrs = ["builtins_arena.h"],
    deps = [
        "@com_google_absl//absl/base:no_destructor",
        "@com_google_absl//absl/base:nullability",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "type_check_env",
    srcs = ["type_check_env.cc"],
    hdrs = ["type_check_env.h"],
    deps = [
        ":proto_type_mask",
        ":proto_type_mask_registry",
        "//common:constant",
        "//common:container",
        "//common:decl",
        "//common:descriptor_pool_type_introspector",
        "//common:type",
        "//internal:status_macros",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/log:absl_check",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/types:optional",
        "@com_google_absl//absl/types:span",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "namespace_generator",
    srcs = ["namespace_generator.cc"],
    hdrs = ["namespace_generator.h"],
    deps = [
        "//common:container",
        "//internal:lexis",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/functional:function_ref",
        "@com_google_absl//absl/log:absl_check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/types:span",
    ],
)

cc_test(
    name = "namespace_generator_test",
    srcs = ["namespace_generator_test.cc"],
    deps = [
        ":namespace_generator",
        "//common:container",
        "//internal:testing",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings:string_view",
    ],
)

cc_library(
    name = "type_checker_impl",
    srcs = [
        "type_checker_builder_impl.cc",
        "type_checker_impl.cc",
    ],
    hdrs = [
        "type_checker_builder_impl.h",
        "type_checker_impl.h",
    ],
    deps = [
        ":namespace_generator",
        ":proto_type_mask",
        ":type_check_env",
        ":type_inference_context",
        "//checker:checker_options",
        "//checker:type_check_issue",
        "//checker:type_checker",
        "//checker:type_checker_builder",
        "//checker:validation_result",
        "//common:ast",
        "//common:ast_rewrite",
        "//common:ast_traverse",
        "//common:ast_visitor",
        "//common:ast_visitor_base",
        "//common:constant",
        "//common:container",
        "//common:decl",
        "//common:expr",
        "//common:format_type_name",
        "//common:standard_definitions",
        "//common:type",
        "//common:type_kind",
        "//internal:lexis",
        "//internal:status_macros",
        "//parser:macro",
        "@com_google_absl//absl/base:no_destructor",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/cleanup",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/log:absl_check",
        "@com_google_absl//absl/log:absl_log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:optional",
        "@com_google_absl//absl/types:span",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "type_checker_impl_test",
    srcs = ["type_checker_impl_test.cc"],
    deps = [
        ":test_ast_helpers",
        ":type_check_env",
        ":type_checker_impl",
        "//checker:checker_options",
        "//checker:type_check_issue",
        "//checker:type_checker_builder",
        "//checker:validation_result",
        "//common:ast",
        "//common:ast_proto",
        "//common:container",
        "//common:decl",
        "//common:expr",
        "//common:format_type_name",
        "//common:source",
        "//common:type",
        "//internal:status_macros",
        "//internal:testing",
        "//internal:testing_descriptor_pool",
        "//parser",
        "//parser:macro_registry",
        "//testutil:baseline_tests",
        "//testutil:test_macros",
        "@com_google_absl//absl/base:no_destructor",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/log:absl_check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:status_matchers",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_cel_spec//proto/cel/expr/conformance/proto2:test_all_types_cc_proto",
        "@com_google_cel_spec//proto/cel/expr/conformance/proto3:test_all_types_cc_proto",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "type_checker_builder_impl_test",
    srcs = ["type_checker_builder_impl_test.cc"],
    deps = [
        ":test_ast_helpers",
        ":type_checker_impl",
        "//checker:checker_options",
        "//checker:type_checker",
        "//checker:type_checker_builder",
        "//checker:validation_result",
        "//common:ast",
        "//common:decl",
        "//common:type",
        "//internal:testing",
        "//internal:testing_descriptor_pool",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:status_matchers",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/types:optional",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "type_inference_context",
    srcs = ["type_inference_context.cc"],
    hdrs = ["type_inference_context.h"],
    deps = [
        ":builtins_arena",
        "//common:decl",
        "//common:format_type_name",
        "//common:standard_definitions",
        "//common:type",
        "//common:type_kind",
        "@com_google_absl//absl/base:no_destructor",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:node_hash_map",
        "@com_google_absl//absl/functional:function_ref",
        "@com_google_absl//absl/log:absl_check",
        "@com_google_absl//absl/log:absl_log",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/types:optional",
        "@com_google_absl//absl/types:span",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "type_inference_context_test",
    srcs = ["type_inference_context_test.cc"],
    deps = [
        ":type_inference_context",
        "//common:decl",
        "//common:format_type_name",
        "//common:type",
        "//common:type_kind",
        "//internal:testing",
        "@com_google_absl//absl/log:absl_check",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/types:optional",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "field_path",
    srcs = ["field_path.cc"],
    hdrs = ["field_path.h"],
    deps = [
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/types:span",
    ],
)

cc_test(
    name = "field_path_test",
    srcs = ["field_path_test.cc"],
    deps = [
        ":field_path",
        "//internal:testing",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "proto_type_mask",
    srcs = ["proto_type_mask.cc"],
    hdrs = ["proto_type_mask.h"],
    deps = [
        ":field_path",
        "//internal:status_macros",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "proto_type_mask_test",
    srcs = ["proto_type_mask_test.cc"],
    deps = [
        ":field_path",
        ":proto_type_mask",
        "//internal:testing",
        "//internal:testing_descriptor_pool",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "proto_type_mask_registry",
    srcs = ["proto_type_mask_registry.cc"],
    hdrs = ["proto_type_mask_registry.h"],
    deps = [
        ":field_path",
        ":proto_type_mask",
        "//common:type",
        "//internal:status_macros",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/types:optional",
        "@com_google_absl//absl/types:span",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "proto_type_mask_registry_test",
    srcs = ["proto_type_mask_registry_test.cc"],
    deps = [
        ":proto_type_mask",
        ":proto_type_mask_registry",
        "//internal:testing",
        "//internal:testing_descriptor_pool",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:status_matchers",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
    ],
)
