load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "issue_reporter",
    srcs = ["issue_reporter.cc"],
    hdrs = ["issue_reporter.h"],
    deps = [
        "//common:source",
        "//policy:cel_policy",
        "//policy:cel_policy_parser",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "optimizer_expr_factory",
    srcs = ["optimizer_expr_factory.cc"],
    hdrs = ["optimizer_expr_factory.h"],
    deps = [
        "//common:ast",
        "//common:ast_rewrite",
        "//common:ast_traverse",
        "//common:ast_visitor_base",
        "//common:constant",
        "//common:expr",
        "//common:expr_factory",
        "//common:source",
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/types:optional",
    ],
)

cc_test(
    name = "optimizer_expr_factory_test",
    srcs = ["optimizer_expr_factory_test.cc"],
    deps = [
        ":optimizer_expr_factory",
        "//common:ast",
        "//common:ast_proto",
        "//common:ast_rewrite",
        "//common:decl",
        "//common:expr",
        "//common:expr_factory",
        "//common:expr_printer",
        "//common:source",
        "//common:type",
        "//compiler",
        "//compiler:compiler_factory",
        "//compiler:standard_library",
        "//internal:status_macros",
        "//internal:testing",
        "//internal:testing_descriptor_pool",
        "//tools:cel_unparser",
        "@com_google_absl//absl/status:status_matchers",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "yaml_string_element_scanner",
    srcs = ["yaml_string_element_scanner.cc"],
    hdrs = ["yaml_string_element_scanner.h"],
    deps = [
        "//common:source",
        "//internal:utf8",
        "@com_google_absl//absl/strings:string_view",
    ],
)

cc_test(
    name = "yaml_string_element_scanner_test",
    srcs = ["yaml_string_element_scanner_test.cc"],
    deps = [
        ":yaml_string_element_scanner",
        "//common:source",
        "//internal:testing",
        "@com_google_absl//absl/strings:string_view",
    ],
)
