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

package(default_visibility = ["//visibility:public"])

licenses(["notice"])

cc_library(
    name = "cel_proto_wrapper",
    srcs = [
        "cel_proto_wrapper.cc",
    ],
    hdrs = [
        "cel_proto_wrapper.h",
    ],
    deps = [
        ":cel_proto_wrap_value_to_message",
        ":proto_message_type_adapter",
        ":trivial_legacy_type_info_internal",
        "//common:value",
        "//eval/public:cel_value",
        "//eval/public:message_wrapper",
        "//internal:proto_time_encoding",
        "@com_google_absl//absl/base:no_destructor",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/log:absl_check",
        "@com_google_absl//absl/log:absl_log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_protobuf//:duration_cc_proto",
        "@com_google_protobuf//:protobuf",
        "@com_google_protobuf//:timestamp_cc_proto",
    ],
)

cc_library(
    name = "protobuf_value_factory",
    hdrs = [
        "protobuf_value_factory.h",
    ],
    deps = [
        "//eval/public:cel_value",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "cel_proto_wrap_util",
    srcs = [
        "cel_proto_wrap_util.cc",
    ],
    hdrs = [
        "cel_proto_wrap_util.h",
    ],
    deps = [
        ":protobuf_value_factory",
        "//eval/public:cel_value",
        "//internal:proto_time_encoding",
        "//internal:status_macros",
        "//internal:well_known_types",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/functional:overload",
        "@com_google_absl//absl/log:absl_check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:cord",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:optional",
        "@com_google_absl//absl/types:variant",
        "@com_google_protobuf//:any_cc_proto",
        "@com_google_protobuf//:duration_cc_proto",
        "@com_google_protobuf//:protobuf",
        "@com_google_protobuf//:struct_cc_proto",
        "@com_google_protobuf//:timestamp_cc_proto",
        "@com_google_protobuf//:wrappers_cc_proto",
    ],
)

cc_test(
    name = "cel_proto_wrap_util_test",
    size = "small",
    srcs = [
        "cel_proto_wrap_util_test.cc",
    ],
    deps = [
        ":cel_proto_wrap_util",
        ":trivial_legacy_type_info",
        "//eval/public:cel_value",
        "//eval/testutil:test_message_cc_proto",
        "//internal:proto_time_encoding",
        "//internal:testing",
        "//testutil:util",
        "@com_google_absl//absl/status",
        "@com_google_protobuf//:any_cc_proto",
        "@com_google_protobuf//:duration_cc_proto",
        "@com_google_protobuf//:empty_cc_proto",
        "@com_google_protobuf//:field_mask_cc_proto",
        "@com_google_protobuf//:protobuf",
        "@com_google_protobuf//:struct_cc_proto",
        "@com_google_protobuf//:wrappers_cc_proto",
    ],
)

cc_library(
    name = "cel_proto_wrap_value_to_message",
    srcs = [
        "cel_proto_wrap_value_to_message.cc",
    ],
    hdrs = [
        "cel_proto_wrap_value_to_message.h",
    ],
    deps = [
        "//eval/public:cel_value",
        "//internal:overflow",
        "//internal:proto_time_encoding",
        "//internal:status_macros",
        "//internal:time",
        "//internal:well_known_types",
        "@com_google_absl//absl/log:absl_log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:cord",
        "@com_google_absl//absl/time",
        "@com_google_protobuf//:any_cc_proto",
        "@com_google_protobuf//:duration_cc_proto",
        "@com_google_protobuf//:protobuf",
        "@com_google_protobuf//:struct_cc_proto",
        "@com_google_protobuf//:timestamp_cc_proto",
        "@com_google_protobuf//:wrappers_cc_proto",
    ],
)

cc_test(
    name = "cel_proto_wrap_value_to_message_test",
    size = "small",
    srcs = [
        "cel_proto_wrap_value_to_message_test.cc",
    ],
    deps = [
        ":cel_proto_wrap_value_to_message",
        ":trivial_legacy_type_info",
        "//eval/public:cel_value",
        "//eval/public/containers:container_backed_list_impl",
        "//eval/public/containers:container_backed_map_impl",
        "//eval/testutil:test_message_cc_proto",
        "//internal:testing",
        "//testutil:util",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:status_matchers",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:span",
        "@com_google_protobuf//:any_cc_proto",
        "@com_google_protobuf//:duration_cc_proto",
        "@com_google_protobuf//:empty_cc_proto",
        "@com_google_protobuf//:field_mask_cc_proto",
        "@com_google_protobuf//:protobuf",
        "@com_google_protobuf//:struct_cc_proto",
        "@com_google_protobuf//:timestamp_cc_proto",
        "@com_google_protobuf//:wrappers_cc_proto",
    ],
)

cc_library(
    name = "field_access_impl",
    srcs = [
        "field_access_impl.cc",
    ],
    hdrs = [
        "field_access_impl.h",
    ],
    deps = [
        ":cel_proto_wrap_util",
        ":cel_proto_wrap_value_to_message",
        ":protobuf_value_factory",
        "//eval/public:cel_options",
        "//eval/public:cel_value",
        "//internal:casts",
        "//internal:overflow",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_protobuf//:any_cc_proto",
        "@com_google_protobuf//:protobuf",
        "@com_google_protobuf//:struct_cc_proto",
        "@com_google_protobuf//:wrappers_cc_proto",
    ],
)

cc_test(
    name = "field_access_impl_test",
    srcs = ["field_access_impl_test.cc"],
    deps = [
        ":cel_proto_wrapper",
        ":field_access_impl",
        "//eval/public:cel_value",
        "//eval/public/testing:matchers",
        "//eval/testutil:test_message_cc_proto",
        "//internal:testing",
        "//internal:time",
        "//testutil:util",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@com_google_cel_spec//proto/cel/expr/conformance/proto3:test_all_types_cc_proto",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "cel_proto_descriptor_pool_builder",
    srcs = ["cel_proto_descriptor_pool_builder.cc"],
    hdrs = ["cel_proto_descriptor_pool_builder.h"],
    deps = [
        "//internal:proto_util",
        "//internal:status_macros",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_protobuf//:any_cc_proto",
        "@com_google_protobuf//:duration_cc_proto",
        "@com_google_protobuf//:empty_cc_proto",
        "@com_google_protobuf//:field_mask_cc_proto",
        "@com_google_protobuf//:protobuf",
        "@com_google_protobuf//:struct_cc_proto",
        "@com_google_protobuf//:timestamp_cc_proto",
        "@com_google_protobuf//:wrappers_cc_proto",
    ],
)

cc_test(
    name = "cel_proto_descriptor_pool_builder_test",
    srcs = ["cel_proto_descriptor_pool_builder_test.cc"],
    deps = [
        ":cel_proto_descriptor_pool_builder",
        "//eval/testutil:test_message_cc_proto",
        "//internal:testing",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_protobuf//:any_cc_proto",
    ],
)

cc_test(
    name = "cel_proto_wrapper_test",
    size = "small",
    srcs = [
        "cel_proto_wrapper_test.cc",
    ],
    deps = [
        ":cel_proto_wrapper",
        "//eval/public:cel_value",
        "//eval/public/containers:container_backed_list_impl",
        "//eval/public/containers:container_backed_map_impl",
        "//eval/testutil:test_message_cc_proto",
        "//internal:proto_time_encoding",
        "//internal:status_macros",
        "//internal:testing",
        "//testutil:util",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:span",
        "@com_google_protobuf//:any_cc_proto",
        "@com_google_protobuf//:duration_cc_proto",
        "@com_google_protobuf//:empty_cc_proto",
        "@com_google_protobuf//:protobuf",
        "@com_google_protobuf//:struct_cc_proto",
        "@com_google_protobuf//:wrappers_cc_proto",
    ],
)

cc_library(
    name = "legacy_type_provider",
    hdrs = ["legacy_type_provider.h"],
    deps = [
        ":legacy_type_adapter",
        ":legacy_type_info_apis",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/strings:string_view",
    ],
)

cc_library(
    name = "legacy_type_adapter",
    hdrs = ["legacy_type_adapter.h"],
    deps = [
        "//base:attributes",
        "//common:memory",
        "//eval/public:cel_options",
        "//eval/public:cel_value",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
    ],
)

cc_test(
    name = "legacy_type_adapter_test",
    srcs = ["legacy_type_adapter_test.cc"],
    deps = [
        ":legacy_type_adapter",
        ":proto_message_type_adapter",
        "//eval/testutil:test_message_cc_proto",
        "//internal:testing",
    ],
)

cc_library(
    name = "proto_message_type_adapter",
    srcs = ["proto_message_type_adapter.cc"],
    hdrs = ["proto_message_type_adapter.h"],
    deps = [
        ":field_access_impl",
        ":legacy_type_adapter",
        ":legacy_type_info_apis",
        "//base:attributes",
        "//common:memory",
        "//eval/public:cel_options",
        "//eval/public:cel_value",
        "//eval/public:message_wrapper",
        "//eval/public/containers:internal_field_backed_list_impl",
        "//eval/public/containers:internal_field_backed_map_impl",
        "//extensions/protobuf:memory_manager",
        "//extensions/protobuf/internal:qualify",
        "//internal:status_macros",
        "@com_google_absl//absl/base:no_destructor",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/log:absl_check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
        "@com_google_protobuf//:differencer",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "proto_message_type_adapter_test",
    srcs = ["proto_message_type_adapter_test.cc"],
    deps = [
        ":legacy_type_adapter",
        ":legacy_type_info_apis",
        ":proto_message_type_adapter",
        ":trivial_legacy_type_info",
        "//base:attributes",
        "//common:value",
        "//common:value_testing",
        "//eval/public:cel_value",
        "//eval/public:message_wrapper",
        "//eval/public/testing:matchers",
        "//eval/testutil:test_message_cc_proto",
        "//extensions/protobuf:memory_manager",
        "//internal:testing",
        "//runtime:runtime_options",
        "@com_google_absl//absl/status",
        "@com_google_protobuf//:protobuf",
        "@com_google_protobuf//:wrappers_cc_proto",
    ],
)

cc_library(
    name = "protobuf_descriptor_type_provider",
    srcs = ["protobuf_descriptor_type_provider.cc"],
    hdrs = ["protobuf_descriptor_type_provider.h"],
    deps = [
        ":legacy_type_adapter",
        ":legacy_type_info_apis",
        ":legacy_type_provider",
        ":proto_message_type_adapter",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/types:optional",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "protobuf_descriptor_type_provider_test",
    srcs = ["protobuf_descriptor_type_provider_test.cc"],
    deps = [
        ":legacy_type_info_apis",
        ":protobuf_descriptor_type_provider",
        "//eval/public:cel_value",
        "//eval/public/testing:matchers",
        "//extensions/protobuf:memory_manager",
        "//internal:testing",
        "@com_google_absl//absl/status:status_matchers",
        "@com_google_protobuf//:protobuf",
        "@com_google_protobuf//:wrappers_cc_proto",
    ],
)

cc_library(
    name = "legacy_type_info_apis",
    hdrs = ["legacy_type_info_apis.h"],
    deps = [
        "//eval/public:message_wrapper",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "trivial_legacy_type_info_internal",
    hdrs = ["trivial_legacy_type_info_internal.h"],
    deps = [
        ":legacy_type_info_apis",
        "//eval/public:message_wrapper",
        "@com_google_absl//absl/base:no_destructor",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/strings:string_view",
    ],
)

cc_library(
    name = "trivial_legacy_type_info",
    testonly = True,
    hdrs = ["trivial_legacy_type_info.h"],
    deps = [
        ":legacy_type_info_apis",
        ":trivial_legacy_type_info_internal",
    ],
)

cc_test(
    name = "trivial_legacy_type_info_test",
    srcs = ["trivial_legacy_type_info_test.cc"],
    deps = [
        ":trivial_legacy_type_info",
        "//eval/public:message_wrapper",
        "//internal:testing",
    ],
)

cc_test(
    name = "legacy_type_provider_test",
    srcs = ["legacy_type_provider_test.cc"],
    deps = [
        ":legacy_type_adapter",
        ":legacy_type_info_apis",
        ":legacy_type_provider",
        ":trivial_legacy_type_info",
        "//internal:testing",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/types:optional",
    ],
)

cc_test(
    name = "dynamic_descriptor_pool_end_to_end_test",
    srcs = ["dynamic_descriptor_pool_end_to_end_test.cc"],
    deps = [
        ":cel_proto_descriptor_pool_builder",
        ":cel_proto_wrapper",
        "//eval/public:activation",
        "//eval/public:builtin_func_registrar",
        "//eval/public:cel_expr_builder_factory",
        "//eval/public:cel_expression",
        "//eval/public:cel_options",
        "//eval/public/testing:matchers",
        "//internal:testing",
        "//parser",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_cel_spec//proto/cel/expr:syntax_cc_proto",
        "@com_google_cel_spec//proto/cel/expr/conformance/proto3:test_all_types_cc_proto",
        "@com_google_protobuf//:differencer",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "field_access_impl_benchmark_test",
    srcs = ["field_access_impl_benchmark_test.cc"],
    tags = [
        "benchmark",
        "manual",
    ],
    deps = [
        ":cel_proto_wrapper",
        ":field_access_impl",
        "//eval/public:cel_options",
        "//eval/public:cel_value",
        "//extensions/protobuf/internal:map_reflection",
        "//internal:benchmark",
        "//internal:testing",
        "@com_google_cel_spec//proto/cel/expr/conformance/proto3:test_all_types_cc_proto",
        "@com_google_protobuf//:protobuf",
    ],
)
