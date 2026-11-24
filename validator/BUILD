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
    name = "validator",
    srcs = ["validator.cc"],
    hdrs = ["validator.h"],
    deps = [
        "//checker:type_check_issue",
        "//checker:validation_result",
        "//common:ast",
        "//common:navigable_ast",
        "//common:source",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/log:absl_check",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/types:span",
    ],
)

cc_test(
    name = "validator_test",
    srcs = ["validator_test.cc"],
    deps = [
        ":validator",
        "//checker:type_check_issue",
        "//common:ast",
        "//common:expr",
        "//common:source",
        "//internal:testing",
        "@com_google_absl//absl/strings:string_view",
    ],
)

cc_test(
    name = "timestamp_literal_validator_test",
    srcs = ["timestamp_literal_validator_test.cc"],
    deps = [
        ":timestamp_literal_validator",
        ":validator",
        "//checker:validation_result",
        "//compiler",
        "//compiler:compiler_factory",
        "//compiler:standard_library",
        "//internal:testing",
        "//internal:testing_descriptor_pool",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:string_view",
    ],
)

cc_library(
    name = "timestamp_literal_validator",
    srcs = ["timestamp_literal_validator.cc"],
    hdrs = ["timestamp_literal_validator.h"],
    deps = [
        ":validator",
        "//common:constant",
        "//common:navigable_ast",
        "//common:standard_definitions",
        "//internal:time",
        "//tools:navigable_ast",
        "@com_google_absl//absl/base:no_destructor",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/time",
    ],
)

cc_library(
    name = "ast_depth_validator",
    srcs = ["ast_depth_validator.cc"],
    hdrs = ["ast_depth_validator.h"],
    deps = [
        ":validator",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "homogeneous_literal_validator",
    srcs = ["homogeneous_literal_validator.cc"],
    hdrs = ["homogeneous_literal_validator.h"],
    deps = [
        ":validator",
        "//common:ast",
        "//common:expr",
        "//common:navigable_ast",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "regex_validator",
    srcs = ["regex_validator.cc"],
    hdrs = ["regex_validator.h"],
    deps = [
        ":validator",
        "//common:ast",
        "//common:constant",
        "//common:expr",
        "//common:navigable_ast",
        "//common:standard_definitions",
        "//internal:re2_options",
        "@com_google_absl//absl/log:absl_check",
        "@com_google_absl//absl/strings",
        "@com_googlesource_code_re2//:re2",
    ],
)

cc_test(
    name = "homogeneous_literal_validator_test",
    srcs = ["homogeneous_literal_validator_test.cc"],
    deps = [
        ":homogeneous_literal_validator",
        ":validator",
        "//checker:validation_result",
        "//common:decl",
        "//common:type",
        "//compiler",
        "//compiler:compiler_factory",
        "//compiler:optional",
        "//compiler:standard_library",
        "//extensions:strings",
        "//internal:status_macros",
        "//internal:testing",
        "//internal:testing_descriptor_pool",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:string_view",
    ],
)

cc_test(
    name = "ast_depth_validator_test",
    srcs = ["ast_depth_validator_test.cc"],
    deps = [
        ":ast_depth_validator",
        ":validator",
        "//checker:type_check_issue",
        "//compiler",
        "//compiler:compiler_factory",
        "//compiler:standard_library",
        "//internal:testing",
        "//internal:testing_descriptor_pool",
        "@com_google_absl//absl/log:absl_check",
    ],
)

cc_test(
    name = "regex_validator_test",
    srcs = ["regex_validator_test.cc"],
    deps = [
        ":regex_validator",
        ":validator",
        "//common:decl",
        "//common:type",
        "//compiler",
        "//compiler:compiler_factory",
        "//compiler:standard_library",
        "//internal:testing",
        "//internal:testing_descriptor_pool",
        "@com_google_absl//absl/status:statusor",
    ],
)

cc_library(
    name = "comprehension_nesting_validator",
    srcs = ["comprehension_nesting_validator.cc"],
    hdrs = ["comprehension_nesting_validator.h"],
    deps = [
        ":validator",
        "//common:expr",
        "//common:navigable_ast",
        "@com_google_absl//absl/log:absl_check",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "comprehension_nesting_validator_test",
    srcs = ["comprehension_nesting_validator_test.cc"],
    deps = [
        ":comprehension_nesting_validator",
        ":validator",
        "//compiler",
        "//compiler:compiler_factory",
        "//compiler:standard_library",
        "//extensions:bindings_ext",
        "//internal:testing",
        "//internal:testing_descriptor_pool",
        "@com_google_absl//absl/status:statusor",
    ],
)

cc_library(
    name = "bind_nesting_validator",
    srcs = ["bind_nesting_validator.cc"],
    hdrs = ["bind_nesting_validator.h"],
    deps = [
        ":validator",
        "//common:expr",
        "//common:navigable_ast",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
    ],
)

cc_test(
    name = "bind_nesting_validator_test",
    srcs = ["bind_nesting_validator_test.cc"],
    deps = [
        ":bind_nesting_validator",
        ":validator",
        "//checker:validation_result",
        "//compiler",
        "//compiler:compiler_factory",
        "//compiler:standard_library",
        "//extensions:bindings_ext",
        "//internal:status_macros",
        "//internal:testing",
        "//internal:testing_descriptor_pool",
        "@com_google_absl//absl/status:statusor",
    ],
)

licenses(["notice"])
