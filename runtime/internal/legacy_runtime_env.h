// Copyright 2026 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef THIRD_PARTY_CEL_CPP_RUNTIME_INTERNAL_LEGACY_RUNTIME_ENV_H_
#define THIRD_PARTY_CEL_CPP_RUNTIME_INTERNAL_LEGACY_RUNTIME_ENV_H_

#include <memory>

#include "absl/base/nullability.h"
#include "absl/status/statusor.h"
#include "eval/public/cel_function_registry.h"
#include "eval/public/cel_type_registry.h"
#include "runtime/internal/runtime_env.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"

namespace cel::runtime_internal {

// Legacy runtime environment owning `CelTypeRegistry` and
// `CelFunctionRegistry` alongside the modern `RuntimeEnv` whose references
// alias into them.
//
// Always allocate via `LegacyRuntimeEnv::Create(...)`. `GetRuntimeEnv()`
// returns an aliasing `std::shared_ptr<RuntimeEnv>` that shares ownership of
// this `LegacyRuntimeEnv` so the legacy registries strictly outlive any
// `RuntimeEnv` / `CelExpressionFlatImpl` references.
struct LegacyRuntimeEnv final
    : public std::enable_shared_from_this<LegacyRuntimeEnv> {
 private:
  struct PrivateTag {};

 public:
  static absl::StatusOr<absl_nonnull std::shared_ptr<LegacyRuntimeEnv>> Create(
      absl_nonnull std::shared_ptr<const google::protobuf::DescriptorPool>
          descriptor_pool,
      absl_nullable std::shared_ptr<google::protobuf::MessageFactory> message_factory =
          nullptr);

  LegacyRuntimeEnv(
      PrivateTag,
      absl_nonnull std::shared_ptr<const google::protobuf::DescriptorPool>
          descriptor_pool,
      absl_nullable std::shared_ptr<google::protobuf::MessageFactory> message_factory);

  LegacyRuntimeEnv(const LegacyRuntimeEnv&) = delete;
  LegacyRuntimeEnv(LegacyRuntimeEnv&&) = delete;
  LegacyRuntimeEnv& operator=(const LegacyRuntimeEnv&) = delete;
  LegacyRuntimeEnv& operator=(LegacyRuntimeEnv&&) = delete;

  absl_nonnull std::shared_ptr<RuntimeEnv> GetRuntimeEnv() {
    return std::shared_ptr<RuntimeEnv>(shared_from_this(), &runtime_env);
  }

  absl_nonnull std::shared_ptr<const RuntimeEnv> GetRuntimeEnv() const {
    return std::shared_ptr<const RuntimeEnv>(shared_from_this(), &runtime_env);
  }

  // Declared before `runtime_env` so `runtime_env` (and any `KeepAlive` entries
  // referencing these registries) is destroyed before them.
  google::api::expr::runtime::CelTypeRegistry legacy_type_registry;
  google::api::expr::runtime::CelFunctionRegistry legacy_function_registry;
  RuntimeEnv runtime_env;
};

}  // namespace cel::runtime_internal

#endif  // THIRD_PARTY_CEL_CPP_RUNTIME_INTERNAL_LEGACY_RUNTIME_ENV_H_
