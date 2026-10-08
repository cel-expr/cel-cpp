# Copyright 2024 Google LLC
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

"""
This module contains build rules for generating the conformance test targets.
"""

load("@rules_cc//cc:cc_test.bzl", "cc_test")

_TESTS_TO_SKIP_WINDOWS = [
    # These tests depend on configuring a timezone database which isn't available in our windows
    # test environment.
    "timestamps/timestamp_selectors_tz/getDate",
    "timestamps/timestamp_selectors_tz/getDayOfMonth_name_pos",
    "timestamps/timestamp_selectors_tz/getDayOfMonth_name_neg",
    "timestamps/timestamp_selectors_tz/getDayOfYear",
    "timestamps/timestamp_selectors_tz/getMinutes",
]

# Converts the list of tests to skip from the format used by the original Go test runner to a single
# flag value where each test is separated by a comma. It also performs expansion, for example
# `foo/bar,baz` becomes two entries which are `foo/bar` and `foo/baz`.
def _expand_tests_to_skip(tests_to_skip):
    result = []
    for test_to_skip in tests_to_skip:
        comma = test_to_skip.find(",")
        if comma == -1:
            result.append(test_to_skip)
            continue
        slash = test_to_skip.rfind("/", 0, comma)
        if slash == -1:
            slash = 0
        else:
            slash = slash + 1
        for part in test_to_skip[slash:].split(","):
            result.append(test_to_skip[0:slash] + part)
    return result

def _conformance_test_name(name, pratt, optimize):
    return "_".join(
        [
            name,
            "pratt" if pratt else "antlr",
            "optimized" if optimize else "unoptimized",
        ],
    )

def _conformance_test_args(modern, optimize, select_opt, skip_check, dashboard, enable_variadic_logical_operators, pratt):
    args = []
    if modern:
        args.append("--modern")
    if optimize:
        args.append("--opt")
    if select_opt:
        args.append("--select_optimization")
    if skip_check:
        args.append("--skip_check")
    else:
        args.append("--noskip_check")
    if dashboard:
        args.append("--dashboard")
    if enable_variadic_logical_operators:
        args.append("--enable_variadic_logical_operators")
    if pratt:
        args.append("--enable_pratt_parser")
    else:
        args.append("--noenable_pratt_parser")
    return args

def _conformance_test(name, data, modern, optimize, select_opt, skip_check, skip_tests, tags, dashboard, enable_variadic_logical_operators, pratt):
    cc_test(
        name = _conformance_test_name(name, pratt, optimize),
        args = _conformance_test_args(modern, optimize, select_opt, skip_check, dashboard, enable_variadic_logical_operators, pratt) + ["$(rlocationpath {})".format(test) for test in data],
        env = select(
            {
                "@platforms//os:windows": {"CEL_SKIP_TESTS": ",".join(skip_tests + _TESTS_TO_SKIP_WINDOWS)},
                "//conditions:default": {"CEL_SKIP_TESTS": ",".join(skip_tests)},
            },
        ),
        data = data,
        deps = ["//conformance:run"],
        tags = tags,
    )

def gen_conformance_tests(name, data, modern = False, checked = False, select_opt = False, dashboard = False, skip_tests = [], tags = [], enable_variadic_logical_operators = False):
    """Generates conformance tests.

    Args:
        name: prefix for all tests
        data: textproto targets describing conformance tests
        modern: run using modern APIs
        checked: whether to apply type checking
        select_opt: enable select optimization
        dashboard: enable dashboard mode
        skip_tests: tests to skip in the format of the cel-spec test runner. See documentation
            in github.com/google/cel-spec/tests/simple/simple_test.go
        tags: tags added to the generated targets
        enable_variadic_logical_operators: enable variadic logical operators
    """
    skip_check = not checked
    tests = []
    for pratt in (True, False):
        for optimize in (True, False):
            test_name = _conformance_test_name(name, pratt, optimize)
            tests.append(test_name)
            _conformance_test(
                name,
                data,
                modern = modern,
                optimize = optimize,
                select_opt = select_opt,
                skip_check = skip_check,
                skip_tests = _expand_tests_to_skip(skip_tests),
                tags = tags,
                dashboard = dashboard,
                enable_variadic_logical_operators = enable_variadic_logical_operators,
                pratt = pratt,
            )
    native.test_suite(
        name = name,
        tests = tests,
        tags = tags,
    )
