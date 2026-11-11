// Copyright 2026 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef THIRD_PARTY_CEL_CPP_ENV_ENV_YAML_H_
#define THIRD_PARTY_CEL_CPP_ENV_ENV_YAML_H_

#include <ostream>
#include <string>

#include "absl/status/statusor.h"
#include "env/config.h"

namespace cel {

struct EnvConfigFromYamlOptions {
  // Whether to treat an omitted extension `version` as
  // `Config::ExtensionConfig::kLatest` instead of version 0.
  // Temporary rollout option; will be removed once callers migrate.
  bool legacy_default_extension_version_latest = false;
};

// EnvConfigFromYaml creates an environment configuration from a YAML string.
//
// To ensure safety, only pass trusted YAML input. yaml-cpp has some fuzz
// coverage, but its security model is unclear. Additionally, callers should be
// aware that improper CEL configuration can lead to unsafe or unpredictably
// expensive expressions.
absl::StatusOr<Config> EnvConfigFromYaml(
    const std::string& yaml, const EnvConfigFromYamlOptions& options = {});

struct EnvConfigToYamlOptions {
  // Whether to use type and overload signatures instead of arg/return types in
  // the output YAML.
  // Example of type signature: "map<int,A<~B>>" vs
  //   type_name: "map"
  //   params:
  //     - type_name: "int"
  //     - type_name: "A"
  //       params:
  //         - type_name: "B"
  //           is_type_param: true
  //
  // Example of overload signature config:
  //   name: "foo"
  //   overloads:
  //     - signature: "timestamp.foo(A<~B>)"
  //       return: "int"
  // vs
  //   name: "foo"
  //   overloads:
  //     - id: "foo_id"
  //       target:
  //         type_name: "timestamp"
  //       args:
  //         - type_name: "A"
  //           params:
  //             - type_name: "B"
  //               is_type_param: true
  //       return:
  //         type_name: "int"
  // TODO(uncreated-issue/91): default to true after all dependencies are updated
  bool use_type_signatures = false;

  // Whether to omit `version` when it is `Config::ExtensionConfig::kLatest` and
  // emit `version: 0` explicitly instead of emitting `version: latest` and
  // omitting version 0.
  // Temporary rollout option; will be removed once callers migrate.
  bool legacy_omit_latest_extension_version = false;
};

// EnvConfigToYaml serializes an environment configuration as a YAML string.
void EnvConfigToYaml(const Config& env_config, std::ostream& os,
                     const EnvConfigToYamlOptions& options = {});

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_ENV_ENV_YAML_H_
