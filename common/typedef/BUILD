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

licenses(["notice"])

cc_library(
    name = "schema",
    hdrs = ["schema.h"],
)

cc_library(
    name = "typedef",
    srcs = ["typedef.cc"],
    hdrs = ["typedef.h"],
    deps = [
        ":schema",
        "//common:ast",
        "//common:constant",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/functional:overload",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
    ],
)

cc_test(
    name = "typedef_test",
    srcs = ["typedef_test.cc"],
    deps = [
        ":schema",
        ":typedef",
        "//common:ast",
        "//common:constant",
        "//internal:testing",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:status_matchers",
    ],
)

cc_library(
    name = "schema_yaml",
    srcs = ["schema_yaml.cc"],
    hdrs = ["schema_yaml.h"],
    deps = [
        ":schema",
        ":yaml_helpers",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
    ],
)

cc_test(
    name = "schema_yaml_test",
    srcs = ["schema_yaml_test.cc"],
    copts = ["-fexceptions"],
    features = ["-use_header_modules"],
    deps = [
        ":schema",
        ":schema_yaml",
        "//internal:testing",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:status_matchers",
        "@com_google_absl//absl/strings:string_view",
        "@yaml-cpp",
    ],
)

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
        "typedef_yaml_internal.h",
    ],
    copts = ["-fexceptions"],
    features = ["-use_header_modules"],
    deps = [
        ":schema",
        ":schema_yaml",
        ":typedef",
        ":yaml_helpers",
        "//common:ast",
        "//common:constant",
        "//common:signature",
        "//internal:status_macros",
        "//internal:strings",
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:span",
        "@yaml-cpp",
    ],
)

cc_test(
    name = "typedef_yaml_test",
    srcs = ["typedef_yaml_test.cc"],
    copts = ["-fexceptions"],
    features = ["-use_header_modules"],
    deps = [
        ":schema",
        ":schema_yaml",
        ":typedef",
        ":typedef_yaml",
        ":yaml_helpers",
        ":yaml_test_helpers",
        "//common:ast",
        "//common:constant",
        "//internal:status_macros",
        "//internal:testing",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:status_matchers",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/time",
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

cc_library(
    name = "json_schema",
    hdrs = ["json_schema.h"],
    deps = [
        ":schema",
        "@com_google_absl//absl/strings:string_view",
    ],
)

cc_library(
    name = "json_schema_yaml",
    srcs = ["json_schema_yaml.cc"],
    hdrs = ["json_schema_yaml.h"],
    copts = ["-fexceptions"],
    features = ["-use_header_modules"],
    deps = [
        ":json_schema",
        ":schema",
        ":schema_yaml",
        ":yaml_helpers",
        "//internal:status_macros",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
        "@yaml-cpp",
    ],
)

cc_test(
    name = "json_schema_yaml_test",
    srcs = ["json_schema_yaml_test.cc"],
    copts = ["-fexceptions"],
    features = ["-use_header_modules"],
    deps = [
        ":json_schema",
        ":json_schema_yaml",
        ":schema_yaml",
        ":typedef",
        ":typedef_yaml",
        ":yaml_helpers",
        ":yaml_test_helpers",
        "//internal:status_macros",
        "//internal:testing",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:status_matchers",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:string_view",
        "@yaml-cpp",
    ],
)

cc_library(
    name = "proto_schema",
    hdrs = ["proto_schema.h"],
    deps = [
        ":schema",
        "@com_google_absl//absl/strings:string_view",
    ],
)

cc_library(
    name = "proto_schema_yaml",
    srcs = ["proto_schema_yaml.cc"],
    hdrs = ["proto_schema_yaml.h"],
    copts = ["-fexceptions"],
    features = ["-use_header_modules"],
    deps = [
        ":proto_schema",
        ":schema",
        ":schema_yaml",
        ":yaml_helpers",
        "//internal:status_macros",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
        "@yaml-cpp",
    ],
)

cc_test(
    name = "proto_schema_yaml_test",
    srcs = ["proto_schema_yaml_test.cc"],
    copts = ["-fexceptions"],
    features = ["-use_header_modules"],
    deps = [
        ":proto_schema",
        ":proto_schema_yaml",
        ":schema_yaml",
        ":typedef",
        ":typedef_yaml",
        ":yaml_helpers",
        ":yaml_test_helpers",
        "//internal:status_macros",
        "//internal:testing",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:status_matchers",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:string_view",
        "@yaml-cpp",
    ],
)
