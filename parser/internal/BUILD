# Copyright 2021 Google LLC
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
load("//bazel:antlr.bzl", "antlr_cc_library")

package(default_visibility = ["//visibility:public"])

licenses(["notice"])

cc_library(
    name = "ast_factory_interface",
    hdrs = ["ast_factory_interface.h"],
    deps = [
        "@com_google_absl//absl/functional:function_ref",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "ast_factory",
    srcs = ["ast_factory.cc"],
    hdrs = ["ast_factory.h"],
    deps = [
        ":ast_factory_interface",
        "//common:expr",
        "//common:expr_factory",
        "//internal:status_macros",
        "//parser:macro",
        "//parser:macro_expr_factory",
        "//parser:macro_registry",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/functional:function_ref",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "options",
    hdrs = ["options.h"],
)

antlr_cc_library(
    name = "cel",
    src = "Cel.g4",
    package = "cel_parser_internal",
)

cc_library(
    name = "lexer",
    srcs = ["lexer.cc"],
    hdrs = ["lexer.h"],
    deps = [
        "//common:source",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:no_destructor",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/functional:function_ref",
        "@com_google_absl//absl/log:absl_check",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
    ],
)

cc_library(
    name = "pratt_parser_worker",
    srcs = ["pratt_parser_worker.cc"],
    hdrs = ["pratt_parser_worker.h"],
    deps = [
        ":ast_factory_interface",
        ":lexer",
        "//common:operators",
        "//common:source",
        "//internal:lexis",
        "//internal:strings",
        "//parser:options",
        "//parser:parser_interface",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/types:optional",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "pratt_parser",
    srcs = ["pratt_parser.cc"],
    hdrs = ["pratt_parser.h"],
    deps = [
        ":ast_factory",
        ":pratt_parser_worker",
        "//common:ast",
        "//common:expr",
        "//common:source",
        "//internal:status_macros",
        "//parser:macro",
        "//parser:macro_registry",
        "//parser:options",
        "//parser:parser_interface",
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/cleanup",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/types:span",
    ],
)

cc_test(
    name = "ast_factory_test",
    srcs = ["ast_factory_test.cc"],
    deps = [
        ":ast_factory",
        ":ast_factory_interface",
        "//common:constant",
        "//common:expr",
        "//internal:testing",
        "//parser:macro",
        "//parser:macro_expr_factory",
        "//parser:macro_registry",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:status_matchers",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/types:span",
    ],
)

cc_test(
    name = "lexer_test",
    srcs = ["lexer_test.cc"],
    deps = [
        ":lexer",
        "//common:source",
        "//internal:testing",
    ],
)

cc_test(
    name = "pratt_parser_test",
    srcs = ["pratt_parser_test.cc"],
    deps = [
        ":lexer",
        ":pratt_parser",
        ":pratt_parser_worker",
        "//common:ast",
        "//common:constant",
        "//common:expr",
        "//common:expr_printer",
        "//common:source",
        "//internal:status_macros",
        "//internal:testing",
        "//parser:macro",
        "//parser:macro_expr_factory",
        "//parser:options",
        "//parser:parser_interface",
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:status_matchers",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/types:span",
    ],
)

cc_test(
    name = "pratt_parser_benchmark",
    srcs = ["pratt_parser_benchmark.cc"],
    tags = ["benchmark"],
    deps = [
        ":pratt_parser",
        "//common:source",
        "//internal:benchmark",
        "//internal:testing",
        "//parser",
        "//parser:options",
        "//parser:parser_interface",
        "@com_google_absl//absl/log:absl_check",
        "@com_google_absl//absl/strings",
    ],
)
