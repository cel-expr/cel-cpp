# Copyright 2026 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Fuzz targets for the CEL parser and evaluator, built and run by OSS-Fuzz
# (https://github.com/google/oss-fuzz).  Build the OSS-Fuzz variants with:
#
#   bazel build -c opt --config=oss-fuzz //fuzz:fuzz_parse_oss_fuzz //fuzz:fuzz_eval_oss_fuzz

load("@rules_fuzzing//fuzzing:cc_defs.bzl", "cc_fuzz_test")

cc_fuzz_test(
    name = "fuzz_parse",
    srcs = ["fuzz_parse.cc"],
    deps = ["//parser"],
)

cc_fuzz_test(
    name = "fuzz_eval",
    srcs = ["fuzz_eval.cc"],
    deps = [
        "//eval/public:activation",
        "//eval/public:builtin_func_registrar",
        "//eval/public:cel_expr_builder_factory",
        "//eval/public:cel_expression",
        "//parser",
    ],
)