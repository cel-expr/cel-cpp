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
load(
    "//conformance/env:env_conformance_test.bzl",
    "cel_env_conformance_test",
)

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "normalize_yaml",
    testonly = True,
    srcs = ["normalize_yaml.cc"],
    hdrs = ["normalize_yaml.h"],
    deps = [
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "normalize_yaml_test",
    srcs = ["normalize_yaml_test.cc"],
    deps = [
        ":normalize_yaml",
        "//internal:testing",
    ],
)

cc_library(
    name = "env_conformance_test_lib",
    testonly = True,
    srcs = ["env_conformance_test.cc"],
    deps = [
        ":normalize_yaml",
        "//common:ast",
        "//common:value",
        "//common/internal:value_conversion",
        "//compiler",
        "//conformance:utils",
        "//env",
        "//env:config",
        "//env:env_runtime",
        "//env:env_std_extensions",
        "//env:env_yaml",
        "//env:runtime_std_extensions",
        "//internal:runfiles",
        "//internal:status_macros",
        "//internal:testing_descriptor_pool",
        "//internal:testing_no_main",
        "//runtime",
        "//runtime:activation",
        "@cel_conformance//proto/cel/expr/conformance/test:suite_cc_proto",
        "@cel_conformance//proto/cel/expr/conformance/test/env:suite_cc_proto",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/log:absl_check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:status_matchers",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
        "@com_google_cel_spec//proto/cel/expr:value_cc_proto",
        "@com_google_protobuf//:protobuf",
        "@com_google_protobuf//src/google/protobuf/io",
    ],
    alwayslink = True,
)

_ALL_TESTS = [
    "@cel_conformance//tests/env:testdata/type_signatures.textproto",
]

cel_env_conformance_test(
    name = "env_conformance_test",
    test_files = _ALL_TESTS,
)

cel_env_conformance_test(
    name = "env_conformance_skip_test",
    skip_tests = [
        "type_signatures/primitives/bool",
        "type_signatures/timestamp",
    ],
    test_files = _ALL_TESTS,
)
