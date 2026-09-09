// Copyright 2026 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
#include "policy/test_util.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "cel/expr/eval.pb.h"
#include "cel/expr/value.pb.h"
#include "google/protobuf/struct.pb.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "internal/status_macros.h"
#include "yaml-cpp/yaml.h"

namespace cel::test {

namespace {

absl::Status YamlToExprValue(const YAML::Node& node,
                             cel::expr::Value* proto) {
  if (node.IsNull()) {
    proto->set_null_value(google::protobuf::NULL_VALUE);
    return absl::OkStatus();
  }
  if (node.IsScalar()) {
    // Try bool
    try {
      proto->set_bool_value(node.as<bool>());
      return absl::OkStatus();
    } catch (...) {
    }
    // Try int64
    try {
      int64_t val;
      if (YAML::convert<int64_t>::decode(node, val)) {
        proto->set_int64_value(val);
        return absl::OkStatus();
      }
    } catch (...) {
    }
    // Try double
    try {
      double val;
      if (YAML::convert<double>::decode(node, val)) {
        proto->set_double_value(val);
        return absl::OkStatus();
      }
    } catch (...) {
    }
    // Fallback to string
    proto->set_string_value(node.as<std::string>());
    return absl::OkStatus();
  }
  if (node.IsSequence()) {
    auto* list = proto->mutable_list_value();
    for (const auto& elem : node) {
      CEL_RETURN_IF_ERROR(YamlToExprValue(elem, list->add_values()));
    }
    return absl::OkStatus();
  }
  if (node.IsMap()) {
    auto* map_val = proto->mutable_map_value();
    for (auto it = node.begin(); it != node.end(); ++it) {
      auto* entry = map_val->add_entries();
      CEL_RETURN_IF_ERROR(YamlToExprValue(it->first, entry->mutable_key()));
      CEL_RETURN_IF_ERROR(YamlToExprValue(it->second, entry->mutable_value()));
    }
    return absl::OkStatus();
  }
  return absl::InvalidArgumentError("Unknown YAML node type");
}

absl::Status ParseInputValue(
    const YAML::Node& node,
    cel::expr::conformance::test::InputValue* input_val) {
  if (node.IsMap() && node["expr"].IsDefined()) {
    input_val->set_expr(node["expr"].as<std::string>());
    return absl::OkStatus();
  }
  if (node.IsMap() && node["value"].IsDefined()) {
    return YamlToExprValue(node["value"], input_val->mutable_value());
  }
  return YamlToExprValue(node, input_val->mutable_value());
}

absl::Status ParseTestOutput(const YAML::Node& node,
                             cel::expr::conformance::test::TestOutput* output) {
  if (!node.IsDefined()) {
    return absl::InvalidArgumentError("Missing output node");
  }
  if (node.IsMap()) {
    if (node["expr"].IsDefined()) {
      output->set_result_expr(node["expr"].as<std::string>());
      return absl::OkStatus();
    }
    if (node["value"].IsDefined()) {
      return YamlToExprValue(node["value"], output->mutable_result_value());
    }
    if (node["error"].IsDefined()) {
      auto* eval_error = output->mutable_eval_error();
      eval_error->add_errors()->set_message(node["error"].as<std::string>());
      return absl::OkStatus();
    }
    if (node["error_set"].IsDefined()) {
      auto* eval_error = output->mutable_eval_error();
      for (const auto& err : node["error_set"]) {
        eval_error->add_errors()->set_message(err.as<std::string>());
      }
      return absl::OkStatus();
    }
    if (node["unknown"].IsDefined()) {
      auto* unknown = output->mutable_unknown();
      for (const auto& expr_id_node : node["unknown"]) {
        unknown->add_exprs(expr_id_node.as<int64_t>());
      }
      return absl::OkStatus();
    }
  }
  return YamlToExprValue(node, output->mutable_result_value());
}

absl::Status ParseTestCaseNode(
    const YAML::Node& test_node,
    cel::expr::conformance::test::TestCase* test_case) {
  if (test_node.IsNull()) {
    return absl::OkStatus();
  }
  if (!test_node.IsMap()) {
    return absl::InvalidArgumentError("Test case must be a YAML mapping");
  }
  if (test_node["name"].IsDefined()) {
    test_case->set_name(test_node["name"].as<std::string>());
  }
  if (test_node["description"].IsDefined()) {
    test_case->set_description(test_node["description"].as<std::string>());
  }
  if (test_node["expr"].IsDefined()) {
    test_case->set_expr(test_node["expr"].as<std::string>());
  }
  if (test_node["context_expr"].IsDefined()) {
    test_case->mutable_input_context()->set_context_expr(
        test_node["context_expr"].as<std::string>());
  } else if (test_node["input_context"].IsDefined() &&
             test_node["input_context"].IsMap() &&
             test_node["input_context"]["context_expr"].IsDefined()) {
    test_case->mutable_input_context()->set_context_expr(
        test_node["input_context"]["context_expr"].as<std::string>());
  }
  if (test_node["disable_check"].IsDefined()) {
    test_case->set_disable_check(test_node["disable_check"].as<bool>());
  }

  YAML::Node input_node = test_node["input"];
  if (input_node.IsDefined() && input_node.IsMap()) {
    auto* input_map = test_case->mutable_input();
    for (auto it = input_node.begin(); it != input_node.end(); ++it) {
      std::string var_name = it->first.as<std::string>();
      cel::expr::conformance::test::InputValue input_val;
      CEL_RETURN_IF_ERROR(ParseInputValue(it->second, &input_val));
      (*input_map)[var_name] = std::move(input_val);
    }
  }

  YAML::Node output_node = test_node["output"];
  if (output_node.IsDefined()) {
    CEL_RETURN_IF_ERROR(
        ParseTestOutput(output_node, test_case->mutable_output()));
  }
  return absl::OkStatus();
}

absl::StatusOr<cel::expr::conformance::test::TestCase> ParseTestCaseYamlImpl(
    absl::string_view yaml_content) {
  if (yaml_content.empty()) {
    return cel::expr::conformance::test::TestCase();
  }
  YAML::Node test_node;
  try {
    test_node = YAML::Load(std::string(yaml_content));
  } catch (const std::exception& e) {
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to parse YAML: ", e.what()));
  }
  cel::expr::conformance::test::TestCase test_case;
  CEL_RETURN_IF_ERROR(ParseTestCaseNode(test_node, &test_case));
  return test_case;
}

absl::StatusOr<cel::expr::conformance::test::TestSuite>
ParsePolicyTestSuiteYamlImpl(absl::string_view yaml_content) {
  YAML::Node tests_node;
  try {
    tests_node = YAML::Load(std::string(yaml_content));
  } catch (const std::exception& e) {
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to parse YAML: ", e.what()));
  }

  cel::expr::conformance::test::TestSuite test_suite;
  if (tests_node["description"].IsDefined()) {
    test_suite.set_description(tests_node["description"].as<std::string>());
  }

  YAML::Node sections = tests_node["sections"];
  if (!sections.IsDefined()) {
    sections = tests_node["section"];  // support singular format
  }
  if (!sections.IsDefined()) {
    return absl::InvalidArgumentError(
        "Missing 'sections' or 'section' in tests YAML");
  }

  for (const auto& section_node : sections) {
    auto* section = test_suite.add_sections();
    if (section_node["name"].IsDefined()) {
      section->set_name(section_node["name"].as<std::string>());
    }
    if (section_node["description"].IsDefined()) {
      section->set_description(section_node["description"].as<std::string>());
    }

    YAML::Node tests = section_node["tests"];
    if (!tests.IsDefined()) {
      tests = section_node["test"];  // support singular format
    }
    if (!tests.IsDefined()) {
      continue;
    }

    for (const auto& test_node : tests) {
      auto* test_case = section->add_tests();
      CEL_RETURN_IF_ERROR(ParseTestCaseNode(test_node, test_case));
    }
  }

  return test_suite;
}

}  // namespace

absl::StatusOr<cel::expr::conformance::test::TestSuite>
ParsePolicyTestSuiteYaml(absl::string_view yaml_content) {
  try {
    return ParsePolicyTestSuiteYamlImpl(yaml_content);
  } catch (...) {
    return absl::InvalidArgumentError("Failed to parse YAML");
  }
}

absl::StatusOr<cel::expr::conformance::test::TestCase> ParseTestCaseYaml(
    absl::string_view yaml_content) {
  try {
    return ParseTestCaseYamlImpl(yaml_content);
  } catch (const std::exception& e) {
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to parse YAML: ", e.what()));
  } catch (...) {
    return absl::InvalidArgumentError("Failed to parse YAML");
  }
}

}  // namespace cel::test
