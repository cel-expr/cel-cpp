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
    name = "type_ref",
    srcs = ["type_ref.cc"],
    hdrs = ["type_ref.h"],
    deps = [
        "//common:ast",
        "//common:type",
        "//common:type_kind",
        "//internal:status_macros",
        "@com_google_absl//absl/base:no_destructor",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "type_ref_test",
    srcs = ["type_ref_test.cc"],
    deps = [
        ":type_ref",
        "//common:type",
        "//common:type_proto",
        "//common/ast:metadata",
        "//internal:proto_matchers",
        "//internal:testing",
        "//internal:testing_descriptor_pool",
        "@com_google_absl//absl/status",
        "@com_google_protobuf//:protobuf",
    ],
)
