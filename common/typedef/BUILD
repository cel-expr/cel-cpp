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

load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@com_google_protobuf//bazel:proto_library.bzl", "proto_library")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//:__subpackages__"])

licenses(["notice"])

cc_library(
    name = "typedef",
    srcs = ["typedef.cc"],
    hdrs = ["typedef.h"],
    deps = [
        "//common:ast",
        "//common:constant",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/functional:overload",
        "@com_google_absl//absl/log:absl_check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/types:span",
    ],
)

cc_test(
    name = "typedef_test",
    srcs = ["typedef_test.cc"],
    deps = [
        ":typedef",
        "//common:ast",
        "//common:constant",
        "//internal:testing",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:status_matchers",
        "@com_google_absl//absl/types:span",
    ],
)

proto_library(
    name = "typedef_protos",
    srcs = ["typedef.proto"],
    deps = [
        "@com_google_cel_spec//proto/cel/expr:checked_proto",
        "@com_google_cel_spec//proto/cel/expr:syntax_proto",
    ],
)

cc_proto_library(
    name = "typedef_cc_proto",
    deps = [":typedef_protos"],
)

cc_library(
    name = "typedef_proto",
    srcs = ["typedef_proto.cc"],
    hdrs = ["typedef_proto.h"],
    deps = [
        ":typedef",
        ":typedef_cc_proto",
        "//common:ast",
        "//common:constant",
        "//common/ast:constant_proto",
        "//common/ast:type_proto",
        "//internal:status_macros",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/types:span",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "typedef_proto_test",
    srcs = ["typedef_proto_test.cc"],
    data = [
        "//common/typedef/testdata:typedef_testdata",
    ],
    deps = [
        ":typedef",
        ":typedef_cc_proto",
        ":typedef_proto",
        "//common:ast",
        "//common:constant",
        "//internal:proto_matchers",
        "//internal:runfiles",
        "//internal:testing",
        "@com_google_absl//absl/log:absl_check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:status_matchers",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/time",
        "@com_google_protobuf//:protobuf",
    ],
)

# NOTE: The YAML targets below depend on yaml-cpp (//third_party/yamlcpp),
# which is used for open-source CEL compatibility (and therefore cannot be
# trivially migrated to Crubit/Rust YAML parsers) and requires C++ exceptions.
# The implementation using //third_party/yamlcpp carefully catches and handles
# exceptions, so code using these targets does not need the -fexceptions flag.

cc_library(
    name = "yaml_helpers",
    srcs = ["yaml_helpers.cc"],
    hdrs = ["yaml_helpers.h"],
    copts = ["-fexceptions"],
    features = ["-use_header_modules"],
    deps = [
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
        "@yaml-cpp",
    ],
)

cc_library(
    name = "typedef_yaml",
    srcs = ["typedef_yaml.cc"],
    hdrs = [
        "typedef_yaml.h",
    ],
    copts = ["-fexceptions"],
    features = ["-use_header_modules"],
    deps = [
        ":typedef",
        ":yaml_helpers",
        "//common:ast",
        "//common:constant",
        "//common:signature",
        "//internal:status_macros",
        "//internal:strings",
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/time",
        "@yaml-cpp",
    ],
)

cc_test(
    name = "typedef_yaml_test",
    srcs = ["typedef_yaml_test.cc"],
    copts = ["-fexceptions"],
    data = [
        "//common/typedef/testdata:typedef_testdata",
    ],
    features = ["-use_header_modules"],
    deps = [
        ":typedef",
        ":typedef_cc_proto",
        ":typedef_proto",
        ":typedef_yaml",
        ":yaml_helpers",
        ":yaml_test_helpers",
        "//common:constant",
        "//internal:proto_matchers",
        "//internal:runfiles",
        "//internal:testing",
        "@com_google_absl//absl/log:absl_log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:status_matchers",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/time",
        "@com_google_protobuf//:protobuf",
        "@yaml-cpp",
    ],
)

cc_library(
    name = "yaml_test_helpers",
    testonly = True,
    srcs = ["yaml_test_helpers.cc"],
    hdrs = ["yaml_test_helpers.h"],
    deps = [
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
    ],
)
