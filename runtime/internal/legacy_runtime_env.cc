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

#include "runtime/internal/legacy_runtime_env.h"

#include <memory>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/status/statusor.h"
#include "internal/status_macros.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"

namespace cel::runtime_internal {

absl::StatusOr<absl_nonnull std::shared_ptr<LegacyRuntimeEnv>>
LegacyRuntimeEnv::Create(
    absl_nonnull std::shared_ptr<const google::protobuf::DescriptorPool> descriptor_pool,
    absl_nullable std::shared_ptr<google::protobuf::MessageFactory> message_factory) {
  auto env = std::make_shared<LegacyRuntimeEnv>(
      PrivateTag{}, std::move(descriptor_pool), std::move(message_factory));
  CEL_RETURN_IF_ERROR(env->runtime_env.Initialize());
  return env;
}

LegacyRuntimeEnv::LegacyRuntimeEnv(
    PrivateTag,
    absl_nonnull std::shared_ptr<const google::protobuf::DescriptorPool> descriptor_pool,
    absl_nullable std::shared_ptr<google::protobuf::MessageFactory> message_factory)
    : legacy_type_registry(descriptor_pool.get(), message_factory.get()),
      runtime_env(std::move(descriptor_pool), std::move(message_factory),
                  legacy_type_registry.InternalGetModernRegistry(),
                  legacy_function_registry.InternalGetRegistry()) {}

}  // namespace cel::runtime_internal
