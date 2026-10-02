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
    name = "config",
    srcs = [
        "config.cc",
    ],
    hdrs = [
        "config.h",
        "type_info.h",
    ],
    deps = [
        "//common:ast",
        "//common:constant",
        "//common:type",
        "//common/typedef:type_ref",
        "//internal:status_macros",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/functional:overload",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "env",
    srcs = ["env.cc"],
    hdrs = ["env.h"],
    deps = [
        ":config",
        "//checker:type_checker_builder",
        "//common:constant",
        "//common:container",
        "//common:decl",
        "//common:signature",
        "//common:type",
        "//common/typedef:type_ref",
        "//compiler",
        "//compiler:compiler_factory",
        "//compiler:standard_library",
        "//env/internal:ext_registry",
        "//internal:status_macros",
        "//parser:macro",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "env_runtime",
    srcs = ["env_runtime.cc"],
    hdrs = ["env_runtime.h"],
    deps = [
        ":config",
        "//env/internal:runtime_ext_registry",
        "//internal:status_macros",
        "//runtime",
        "//runtime:runtime_builder",
        "//runtime:runtime_builder_factory",
        "//runtime:runtime_options",
        "//runtime:standard_functions",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "env_std_extensions",
    srcs = ["env_std_extensions.cc"],
    hdrs = ["env_std_extensions.h"],
    deps = [
        ":env",
        "//checker:optional",
        "//compiler:optional",
        "//extensions:bindings_ext",
        "//extensions:comprehensions_v2",
        "//extensions:encoders",
        "//extensions:lists_functions",
        "//extensions:math_ext_decls",
        "//extensions:proto_ext",
        "//extensions:regex_ext",
        "//extensions:sets_functions",
        "//extensions:strings",
    ],
)

cc_library(
    name = "env_yaml",
    srcs = ["env_yaml.cc"],
    hdrs = ["env_yaml.h"],
    copts = [
        "-fexceptions",
    ],
    features = ["-use_header_modules"],
    deps = [
        ":config",
        "//common:ast",
        "//common:constant",
        "//common:signature",
        "//common/typedef:type_ref",
        "//internal:status_macros",
        "//internal:strings",
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/base:no_destructor",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/log:absl_check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/time",
        "@yaml-cpp",
    ],
)

cc_library(
    name = "runtime_std_extensions",
    srcs = ["runtime_std_extensions.cc"],
    hdrs = ["runtime_std_extensions.h"],
    deps = [
        ":env_runtime",
        "//checker:optional",
        "//env/internal:runtime_ext_registry",
        "//extensions:encoders",
        "//extensions:lists_functions",
        "//extensions:math_ext",
        "//extensions:math_ext_decls",
        "//extensions:regex_ext",
        "//extensions:sets_functions",
        "//extensions:strings",
        "//runtime:optional_types",
        "//runtime:runtime_builder",
        "//runtime:runtime_options",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    deps = [
        ":config",
        "//common:constant",
        "//internal:testing",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:status_matchers",
    ],
)

cc_test(
    name = "env_test",
    srcs = ["env_test.cc"],
    deps = [
        ":config",
        ":env",
        "//checker:type_check_issue",
        "//checker:type_checker_builder",
        "//checker:validation_result",
        "//common:ast",
        "//common:constant",
        "//common:decl",
        "//common:expr",
        "//common:type",
        "//common:value",
        "//compiler",
        "//internal:status_macros",
        "//internal:testing",
        "//internal:testing_descriptor_pool",
        "//parser:macro",
        "//parser:macro_expr_factory",
        "//parser:parser_interface",
        "//runtime",
        "//runtime:activation",
        "//runtime:reference_resolver",
        "//runtime:runtime_builder",
        "//runtime:runtime_options",
        "//runtime:standard_runtime_builder_factory",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:status_matchers",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:span",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "env_runtime_test",
    srcs = ["env_runtime_test.cc"],
    deps = [
        ":config",
        ":env",
        ":env_runtime",
        ":env_std_extensions",
        ":env_yaml",
        ":runtime_std_extensions",
        "//checker:validation_result",
        "//common:ast",
        "//common:source",
        "//common:value",
        "//compiler",
        "//extensions:math_ext",
        "//internal:testing",
        "//internal:testing_descriptor_pool",
        "//runtime",
        "//runtime:activation",
        "//runtime:runtime_builder",
        "//runtime:runtime_options",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "env_std_extensions_test",
    srcs = ["env_std_extensions_test.cc"],
    deps = [
        ":config",
        ":env",
        ":env_std_extensions",
        "//compiler",
        "//internal:testing",
        "//internal:testing_descriptor_pool",
        "@com_google_absl//absl/strings:string_view",
    ],
)

cc_test(
    name = "env_yaml_test",
    srcs = ["env_yaml_test.cc"],
    deps = [
        ":config",
        ":env_yaml",
        "//common:constant",
        "//common/typedef:type_ref",
        "//internal:status_macros",
        "//internal:testing",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:status_matchers",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/time",
    ],
)

cc_test(
    name = "runtime_std_extensions_test",
    srcs = ["runtime_std_extensions_test.cc"],
    deps = [
        ":config",
        ":env",
        ":env_runtime",
        ":env_std_extensions",
        ":runtime_std_extensions",
        "//checker:optional",
        "//checker:validation_result",
        "//common:ast",
        "//common:value",
        "//compiler",
        "//extensions:lists_functions",
        "//extensions:math_ext_decls",
        "//extensions:strings",
        "//internal:testing",
        "//internal:testing_descriptor_pool",
        "//runtime",
        "//runtime:activation",
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_protobuf//:protobuf",
    ],
)
