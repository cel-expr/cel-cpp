# This package contains CEL evaluator tests (end-to-end, benchmark etc.)
#
#

load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@com_google_protobuf//bazel:proto_library.bzl", "proto_library")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//visibility:public"])

licenses(["notice"])

exports_files(["LICENSE"])

cc_test(
    name = "benchmark_test",
    srcs = [
        "benchmark_test.cc",
    ],
    tags = [
        "benchmark",
        "manual",
    ],
    deps = [
        ":request_context_cc_proto",
        "//common:ast_proto",
        "//common:decl",
        "//common:native_type",
        "//common:type",
        "//common:value",
        "//compiler",
        "//compiler:compiler_factory",
        "//compiler:standard_library",
        "//extensions/protobuf:runtime_adapter",
        "//internal:benchmark",
        "//internal:status_macros",
        "//internal:testing",
        "//parser",
        "//runtime",
        "//runtime:activation",
        "//runtime:constant_folding",
        "//runtime:runtime_options",
        "//runtime:standard_runtime_builder_factory",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:status_matchers",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_cel_spec//proto/cel/expr:syntax_cc_proto",
        "@com_google_googleapis//google/rpc/context:attribute_context_cc_proto",
        "@com_google_protobuf//:protobuf",
        "@com_google_protobuf//:struct_cc_proto",
    ],
)

cc_test(
    name = "modern_benchmark_test",
    srcs = [
        "modern_benchmark_test.cc",
    ],
    tags = [
        "benchmark",
        "manual",
    ],
    deps = [
        ":request_context_cc_proto",
        "//base:attributes",
        "//checker:validation_result",
        "//common:decl",
        "//common:native_type",
        "//common:type",
        "//common:value",
        "//compiler",
        "//compiler:compiler_factory",
        "//compiler:standard_library",
        "//extensions:comprehensions_v2_functions_modern",
        "//extensions:comprehensions_v2_macros",
        "//extensions/protobuf:runtime_adapter",
        "//internal:benchmark",
        "//internal:testing",
        "//internal:testing_descriptor_pool",
        "//parser",
        "//parser:macro_registry",
        "//runtime",
        "//runtime:activation",
        "//runtime:constant_folding",
        "//runtime:runtime_options",
        "//runtime:standard_runtime_builder_factory",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/container:node_hash_set",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/log:absl_check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:status_matchers",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_cel_spec//proto/cel/expr:syntax_cc_proto",
        "@com_google_googleapis//google/rpc/context:attribute_context_cc_proto",
        "@com_google_protobuf//:protobuf",
        "@com_google_protobuf//:struct_cc_proto",
    ],
)

cc_test(
    name = "modern_unknown_test",
    srcs = [
        "modern_unknown_test.cc",
    ],
    deps = [
        "//base:attributes",
        "//base:function_result",
        "//common:function_descriptor",
        "//common:kind",
        "//common:value",
        "//extensions:bindings_ext",
        "//extensions:comprehensions_v2_macros",
        "//internal:testing",
        "//internal:testing_descriptor_pool",
        "//internal:testing_message_factory",
        "//parser",
        "//parser:options",
        "//parser:parser_interface",
        "//runtime",
        "//runtime:activation",
        "//runtime:constant_folding",
        "//runtime:function",
        "//runtime:optional_types",
        "//runtime:reference_resolver",
        "//runtime:runtime_options",
        "//runtime:standard_runtime_builder_factory",
        "//runtime/internal:errors",
        "@com_google_absl//absl/log:absl_check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:status_matchers",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/types:span",
        "@com_google_protobuf//:protobuf",
        "@com_google_protobuf//:struct_cc_proto",
    ],
)

cc_test(
    name = "allocation_benchmark_test",
    size = "small",
    srcs = [
        "allocation_benchmark_test.cc",
    ],
    tags = [
        "benchmark",
        "manual",
    ],
    deps = [
        ":request_context_cc_proto",
        "//common:value",
        "//extensions/protobuf:runtime_adapter",
        "//internal:benchmark",
        "//internal:testing",
        "//parser",
        "//runtime",
        "//runtime:activation",
        "//runtime:runtime_options",
        "//runtime:standard_runtime_builder_factory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_cel_spec//proto/cel/expr:syntax_cc_proto",
        "@com_google_googleapis//google/rpc/context:attribute_context_cc_proto",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "memory_safety_test",
    srcs = [
        "memory_safety_test.cc",
    ],
    deps = [
        "//common:value",
        "//common:value_testing",
        "//extensions/protobuf:runtime_adapter",
        "//extensions/protobuf:value_testing",
        "//internal:status_macros",
        "//internal:testing",
        "//parser",
        "//runtime",
        "//runtime:activation",
        "//runtime:comprehension_vulnerability_check",
        "//runtime:constant_folding",
        "//runtime:function_adapter",
        "//runtime:reference_resolver",
        "//runtime:regex_precompilation",
        "//runtime:runtime_builder",
        "//runtime:runtime_options",
        "//runtime:standard_runtime_builder_factory",
        "//testutil:util",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_cel_spec//proto/cel/expr:syntax_cc_proto",
        "@com_google_googleapis//google/rpc/context:attribute_context_cc_proto",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "expression_builder_benchmark_test",
    size = "small",
    srcs = [
        "expression_builder_benchmark_test.cc",
    ],
    tags = [
        "benchmark",
        "manual",
    ],
    deps = [
        ":request_context_cc_proto",
        "//common:minimal_descriptor_pool",
        "//extensions/protobuf:runtime_adapter",
        "//internal:benchmark",
        "//internal:status_macros",
        "//internal:testing",
        "//parser",
        "//runtime",
        "//runtime:constant_folding",
        "//runtime:regex_precompilation",
        "//runtime:runtime_options",
        "//runtime:standard_runtime_builder_factory",
        "//runtime:type_registry",
        "@com_google_absl//absl/log:absl_check",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_cel_spec//proto/cel/expr:checked_cc_proto",
        "@com_google_cel_spec//proto/cel/expr:syntax_cc_proto",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "end_to_end_test",
    size = "small",
    srcs = [
        "end_to_end_test.cc",
    ],
    deps = [
        "//common:function_descriptor",
        "//common:kind",
        "//common:value",
        "//eval/testutil:test_message_cc_proto",
        "//extensions/protobuf:runtime_adapter",
        "//internal:testing",
        "//runtime:activation",
        "//runtime:function",
        "//runtime:runtime_options",
        "//runtime:standard_runtime_builder_factory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/types:span",
        "@com_google_cel_spec//proto/cel/expr:syntax_cc_proto",
        "@com_google_protobuf//:protobuf",
        "@com_google_protobuf//:struct_cc_proto",
    ],
)

cc_test(
    name = "unknowns_end_to_end_test",
    size = "small",
    srcs = [
        "unknowns_end_to_end_test.cc",
    ],
    deps = [
        "//base:attributes",
        "//base:function_result",
        "//common:function_descriptor",
        "//common:kind",
        "//common:native_type",
        "//common:unknown",
        "//common:value",
        "//extensions/protobuf:runtime_adapter",
        "//internal:testing",
        "//parser",
        "//runtime",
        "//runtime:activation",
        "//runtime:function",
        "//runtime:runtime_options",
        "//runtime:standard_runtime_builder_factory",
        "//runtime/internal:errors",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:status_matchers",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
        "@com_google_cel_spec//proto/cel/expr:syntax_cc_proto",
        "@com_google_protobuf//:protobuf",
        "@com_google_protobuf//:struct_cc_proto",
    ],
)

proto_library(
    name = "request_context_protos",
    srcs = [
        "request_context.proto",
    ],
)

cc_proto_library(
    name = "request_context_cc_proto",
    deps = [":request_context_protos"],
)

cc_library(
    name = "mock_cel_expression",
    testonly = 1,
    hdrs = ["mock_cel_expression.h"],
    deps = [
        "//eval/public:base_activation",
        "//eval/public:cel_expression",
        "//internal:testing_no_main",
        "@com_google_absl//absl/status:statusor",
    ],
)
