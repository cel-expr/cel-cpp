# Copyright 2026 Google LLC
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

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "cel_policy",
    srcs = [
        "cel_policy.cc",
    ],
    hdrs = [
        "cel_policy.h",
    ],
    deps = [
        "//common:source",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/log:absl_check",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "cel_policy_test",
    srcs = ["cel_policy_test.cc"],
    deps = [
        ":cel_policy",
        "//common:source",
        "//internal:testing",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "cel_policy_parser",
    srcs = [
        "cel_policy_parse_context.cc",
        "cel_policy_parse_result.cc",
    ],
    hdrs = [
        "cel_policy_parse_context.h",
        "cel_policy_parse_result.h",
        "cel_policy_parser.h",
    ],
    deps = [
        ":cel_policy",
        "//common:source",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/log:absl_check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/types:optional",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "yaml_policy_parser",
    srcs = [
        "yaml_policy_parser.cc",
    ],
    hdrs = ["yaml_policy_parser.h"],
    copts = ["-fexceptions"],
    features = ["-use_header_modules"],
    deps = [
        ":cel_policy",
        ":cel_policy_parser",
        "//common:source",
        "//internal:status_macros",
        "//internal:utf8",
        "//policy/internal:yaml_string_element_scanner",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@yaml-cpp",
    ],
)

cc_library(
    name = "cel_policy_validation_result",
    srcs = [
        "cel_policy_validation_result.cc",
    ],
    hdrs = [
        "cel_policy_validation_result.h",
    ],
    deps = [
        ":cel_policy",
        ":cel_policy_parser",
        "//common:ast",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "compiler",
    srcs = ["compiler.cc"],
    hdrs = ["compiler.h"],
    deps = [
        ":cel_policy",
        ":cel_policy_parser",
        ":cel_policy_validation_result",
        "//checker:type_check_issue",
        "//checker:validation_result",
        "//common:ast",
        "//common:ast_rewrite",
        "//common:constant",
        "//common:container",
        "//common:decl",
        "//common:expr",
        "//common:format_type_name",
        "//common:navigable_ast",
        "//common:source",
        "//common:type",
        "//common:type_kind",
        "//compiler",
        "//internal:status_macros",
        "//policy/internal:issue_reporter",
        "//policy/internal:optimizer_expr_factory",
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/cleanup",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/log:absl_check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/types:span",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "yaml_policy_parser_test",
    srcs = [
        "test_custom_yaml_policy_parser.cc",
        "yaml_policy_parser_test.cc",
    ],
    data = [
        "//policy/testdata:policy_testdata",
    ],
    deps = [
        ":cel_policy",
        ":cel_policy_parser",
        ":yaml_policy_parser",
        "//common:source",
        "//internal:runfiles",
        "//internal:status_macros",
        "//internal:testing",
        "@com_google_absl//absl/log:absl_log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:status_matchers",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@yaml-cpp",
    ],
)

cc_test(
    name = "compiler_test",
    srcs = ["compiler_test.cc"],
    data = [
        "//policy/testdata:policy_testdata",
    ],
    deps = [
        ":cel_policy",
        ":cel_policy_parser",
        ":cel_policy_validation_result",
        ":compiler",
        ":yaml_policy_parser",
        "//common:ast",
        "//common:ast_proto",
        "//common:decl",
        "//common:navigable_ast",
        "//common:source",
        "//common:type",
        "//common:value",
        "//common:value_testing",
        "//compiler",
        "//compiler:compiler_factory",
        "//compiler:optional",
        "//compiler:standard_library",
        "//extensions:bindings_ext",
        "//internal:runfiles",
        "//internal:status_macros",
        "//internal:testing",
        "//internal:testing_descriptor_pool",
        "//runtime",
        "//runtime:activation",
        "//runtime:optional_types",
        "//runtime:runtime_builder",
        "//runtime:runtime_options",
        "//runtime:standard_runtime_builder_factory",
        "//tools:cel_unparser",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:status_matchers",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "test_util",
    srcs = ["test_util.cc"],
    hdrs = ["test_util.h"],
    copts = ["-fexceptions"],
    features = ["-use_header_modules"],
    deps = [
        "//internal:status_macros",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_cel_spec//proto/cel/expr:value_cc_proto",
        "@com_google_cel_spec//proto/cel/expr/conformance/test:suite_cc_proto",
        "@com_google_protobuf//:struct_cc_proto",
        "@yaml-cpp",
    ],
)
