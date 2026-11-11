load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//visibility:public"])

licenses(["notice"])

cc_library(
    name = "cel_field_extractor",
    srcs = ["cel_field_extractor.cc"],
    hdrs = ["cel_field_extractor.h"],
    deps = [
        ":navigable_ast",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/strings",
        "@com_google_cel_spec//proto/cel/expr:syntax_cc_proto",
    ],
)

cc_test(
    name = "cel_field_extractor_test",
    srcs = ["cel_field_extractor_test.cc"],
    deps = [
        ":cel_field_extractor",
        "//internal:testing",
        "//parser",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/log:absl_check",
        "@com_google_absl//absl/status:statusor",
        "@com_google_cel_spec//proto/cel/expr:syntax_cc_proto",
    ],
)

cc_library(
    name = "cel_unparser",
    srcs = [
        "cel_unparser.cc",
    ],
    hdrs = [
        "cel_unparser.h",
    ],
    deps = [
        "//common:operators",
        "//internal:status_macros",
        "//internal:strings",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:no_destructor",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:optional",
        "@com_google_cel_spec//proto/cel/expr:checked_cc_proto",
        "@com_google_cel_spec//proto/cel/expr:syntax_cc_proto",
        "@com_google_protobuf//:duration_cc_proto",
        "@com_google_protobuf//:timestamp_cc_proto",
        "@com_googlesource_code_re2//:re2",
    ],
)

cc_test(
    name = "cel_unparser_test",
    srcs = ["cel_unparser_test.cc"],
    deps = [
        ":cel_unparser",
        "//internal:proto_matchers",
        "//internal:testing",
        "//parser",
        "//parser:options",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_cel_spec//proto/cel/expr:syntax_cc_proto",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "navigable_ast",
    srcs = ["navigable_ast.cc"],
    hdrs = ["navigable_ast.h"],
    deps = [
        "//common/ast:navigable_ast_internal",
        "//eval/public:ast_traverse",
        "//eval/public:ast_visitor",
        "//eval/public:ast_visitor_base",
        "//eval/public:source_position",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/memory",
        "@com_google_cel_spec//proto/cel/expr:checked_cc_proto",
        "@com_google_cel_spec//proto/cel/expr:syntax_cc_proto",
    ],
)

cc_test(
    name = "navigable_ast_test",
    srcs = ["navigable_ast_test.cc"],
    deps = [
        ":navigable_ast",
        "//base:builtins",
        "//internal:testing",
        "//parser",
        "@com_google_cel_spec//proto/cel/expr:syntax_cc_proto",
    ],
)

cc_library(
    name = "branch_coverage",
    srcs = ["branch_coverage.cc"],
    hdrs = ["branch_coverage.h"],
    deps = [
        ":navigable_ast",
        "//common:value",
        "//eval/internal:interop",
        "//eval/public:cel_value",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:no_destructor",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/functional:overload",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/types:variant",
        "@com_google_cel_spec//proto/cel/expr:checked_cc_proto",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "branch_coverage_test",
    srcs = ["branch_coverage_test.cc"],
    data = [
        "//tools/testdata:coverage_testdata",
    ],
    deps = [
        ":branch_coverage",
        ":navigable_ast",
        "//base:builtins",
        "//common:value",
        "//eval/public:activation",
        "//eval/public:builtin_func_registrar",
        "//eval/public:cel_expr_builder_factory",
        "//eval/public:cel_expression",
        "//eval/public:cel_value",
        "//internal:proto_file_util",
        "//internal:testing",
        "@com_google_absl//absl/base:no_destructor",
        "@com_google_absl//absl/log:absl_check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "descriptor_pool_builder",
    srcs = ["descriptor_pool_builder.cc"],
    hdrs = ["descriptor_pool_builder.h"],
    deps = [
        "//common:minimal_descriptor_database",
        "//internal:status_macros",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "proto_to_predicate",
    srcs = ["proto_to_predicate.cc"],
    hdrs = ["proto_to_predicate.h"],
    deps = [
        "//common:ast",
        "//common:expr",
        "//common:expr_factory",
        "//common:operators",
        "//internal:status_macros",
        "@com_google_absl//absl/log:absl_log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "proto_to_predicate_test",
    srcs = ["proto_to_predicate_test.cc"],
    deps = [
        ":cel_unparser",
        ":proto_to_predicate",
        "//common:ast",
        "//common:ast_proto",
        "//common:value",
        "//env:config",
        "//env:env_runtime",
        "//env:env_yaml",
        "//env:runtime_std_extensions",
        "//eval/testutil:test_message_cc_proto",
        "//extensions/protobuf:value",
        "//internal:status_macros",
        "//internal:testing",
        "//internal:testing_descriptor_pool",
        "//parser",
        "//runtime",
        "//runtime:activation",
        "//tools/testdata:test_policy_cc_proto",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:status_matchers",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/types:span",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "descriptor_pool_builder_test",
    srcs = ["descriptor_pool_builder_test.cc"],
    deps = [
        ":descriptor_pool_builder",
        "//internal:testing",
        "@cel_conformance//proto/cel/expr/conformance/proto2:test_all_types_cc_proto",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:status_matchers",
        "@com_google_protobuf//:protobuf",
    ],
)
