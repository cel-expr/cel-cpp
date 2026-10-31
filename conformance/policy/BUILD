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
load(
    "//conformance/policy:policy_conformance_test.bzl",
    "cel_policy_conformance_test",
)

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "policy_conformance_test_lib",
    testonly = True,
    srcs = ["policy_conformance_test.cc"],
    deps = [
        "//common:ast",
        "//common:source",
        "//common:value",
        "//common/internal:value_conversion",
        "//compiler",
        "//env",
        "//env:config",
        "//env:env_runtime",
        "//env:env_std_extensions",
        "//env:env_yaml",
        "//env:runtime_std_extensions",
        "//extensions/protobuf:enum_adapter",
        "//internal:runfiles",
        "//internal:status_macros",
        "//internal:testing_descriptor_pool",
        "//internal:testing_no_main",
        "//policy:cel_policy",
        "//policy:cel_policy_parser",
        "//policy:cel_policy_validation_result",
        "//policy:compiler",
        "//policy:test_util",
        "//policy:yaml_policy_parser",
        "//runtime",
        "//runtime:activation",
        "//runtime:bind_proto_to_activation",
        "//runtime:function_adapter",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/log:absl_check",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_cel_spec//proto/cel/expr:value_cc_proto",
        "@com_google_cel_spec//proto/cel/expr/conformance/test:suite_cc_proto",
        "@com_google_protobuf//:protobuf",
    ],
    alwayslink = True,
)

_ALL_TESTS = [
    "@cel_policy//conformance:agent_tool_execution_governance",
    "@cel_policy//conformance:aggregate",
    "@cel_policy//conformance:aggregate_explicit_list_output",
    "@cel_policy//conformance:aggregate_explicit_optional_none",
    "@cel_policy//conformance:aggregate_nested_all_rules_fall_through_to_empty",
    "@cel_policy//conformance:aggregate_nested_explicit_list_double_wrapping",
    "@cel_policy//conformance:aggregate_nested_multi_branch_match",
    "@cel_policy//conformance:aggregate_nested_single_conditional_match",
    "@cel_policy//conformance:aggregate_shadowed_variables",
    "@cel_policy//conformance:first_match_nested_aggregate",
    "@cel_policy//conformance:k8s",
    "@cel_policy//conformance:limits",
    "@cel_policy//conformance:nested_rule",
    "@cel_policy//conformance:nested_rule2",
    "@cel_policy//conformance:nested_rule3",
    "@cel_policy//conformance:nested_rule4",
    "@cel_policy//conformance:nested_rule5",
    "@cel_policy//conformance:nested_rule6",
    "@cel_policy//conformance:nested_rule7",
    "@cel_policy//conformance:nested_rules_variable_shadowing",
    "@cel_policy//conformance:pb",
    "@cel_policy//conformance:required_labels",
    "@cel_policy//conformance:restricted_destinations",
    "@cel_policy//conformance:unconditional_rules",
    "@cel_policy//conformance:unnest",
    "@cel_policy//conformance:variable_type_propagation",
    "@cel_policy//conformance:compile_error_aggregate_false_condition",
    "@cel_policy//conformance:compile_error_aggregate_heterogeneous_outputs",
    "@cel_policy//conformance:compile_error_aggregate_nested_mixed_semantics",
    "@cel_policy//conformance:compile_error_aggregate_subrule_heterogeneous_outputs",
    "@cel_policy//conformance:compile_error_aggregate_unreachable_in_nested_first_match",
    "@cel_policy//conformance:compile_error_compose_conflicting_output",
    "@cel_policy//conformance:compile_error_compose_conflicting_subrule",
    "@cel_policy//conformance:compile_error_unreachable",
    "@cel_policy//conformance:compile_error_unreachable_under_unconditional_aggregate_subrule",
    "@cel_policy//conformance:compile_error_duplicate_variable",
    "@cel_policy//conformance:compile_error_import",
    "@cel_policy//conformance:compile_error_incompatible_outputs",
    "@cel_policy//conformance:compile_error_syntax",
    "@cel_policy//conformance:compile_error_undeclared_reference",
]

cel_policy_conformance_test(
    name = "policy_conformance_test",
    skip_tests = [
        # TODO(b/506179116): Fix these.
        # Need to add k8s custom yaml parser and mock runtime.
        "k8s",
        # Expects Java reverse-order type mismatch error message.
        "compile_error_aggregate_heterogeneous_outputs",
        # Expects "rule creates unreachable outputs" on the exhaustive subrule,
        # whereas C++ reports "match creates unreachable outputs" on the
        # subsequent unreachable match.
        "compile_error_unreachable_under_unconditional_aggregate_subrule",
    ],
    test_files = _ALL_TESTS,
)
