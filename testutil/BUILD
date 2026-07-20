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

load("@com_google_protobuf//bazel:proto_library.bzl", "proto_library")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//visibility:public"])

licenses(["notice"])

cc_library(
    name = "util",
    testonly = True,
    hdrs = [
        "util.h",
    ],
    deps = ["//internal:proto_matchers"],
)

cc_library(
    name = "test_macros",
    testonly = True,
    srcs = ["test_macros.cc"],
    hdrs = ["test_macros.h"],
    deps = [
        "//common:expr",
        "//internal:status_macros",
        "//parser:macro",
        "//parser:macro_expr_factory",
        "//parser:macro_registry",
        "@com_google_absl//absl/base:no_destructor",
        "@com_google_absl//absl/log:absl_check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:optional",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "baseline_tests",
    testonly = True,
    srcs = ["baseline_tests.cc"],
    hdrs = ["baseline_tests.h"],
    deps = [
        "//common:ast",
        "//common:expr",
        "//common:expr_printer",
        "//extensions/protobuf:ast_converters",
        "@com_google_absl//absl/strings",
        "@com_google_cel_spec//proto/cel/expr:checked_cc_proto",
    ],
)

cc_test(
    name = "baseline_tests_test",
    srcs = ["baseline_tests_test.cc"],
    deps = [
        ":baseline_tests",
        "//common:ast",
        "//internal:testing",
        "@com_google_protobuf//:protobuf",
    ],
)

proto_library(
    name = "test_json_names_proto",
    srcs = ["test_json_names.proto"],
)
