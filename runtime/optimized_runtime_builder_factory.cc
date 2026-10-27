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

#include "runtime/optimized_runtime_builder_factory.h"

#include <memory>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/log/absl_check.h"
#include "absl/status/statusor.h"
#include "extensions/select_optimization.h"
#include "extensions/set_membership_optimization.h"
#include "internal/noop_delete.h"
#include "internal/status_macros.h"
#include "runtime/constant_folding.h"
#include "runtime/reference_resolver.h"
#include "runtime/regex_precompilation.h"
#include "runtime/runtime_builder.h"
#include "runtime/runtime_options.h"
#include "runtime/standard_runtime_builder_factory.h"
#include "google/protobuf/descriptor.h"

namespace cel {

absl::StatusOr<RuntimeBuilder> CreateOptimizedRuntimeBuilder(
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    const RuntimeOptions& options) {
  ABSL_DCHECK(descriptor_pool != nullptr);
  return CreateOptimizedRuntimeBuilder(
      std::shared_ptr<const google::protobuf::DescriptorPool>(
          descriptor_pool,
          internal::NoopDeleteFor<const google::protobuf::DescriptorPool>()),
      options);
}

absl::StatusOr<RuntimeBuilder> CreateOptimizedRuntimeBuilder(
    absl_nonnull std::shared_ptr<const google::protobuf::DescriptorPool> descriptor_pool,
    const RuntimeOptions& options) {
  ABSL_DCHECK(descriptor_pool != nullptr);
  CEL_ASSIGN_OR_RETURN(
      RuntimeBuilder builder,
      CreateStandardRuntimeBuilder(std::move(descriptor_pool), options));
  CEL_RETURN_IF_ERROR(EnableReferenceResolver(
      builder, ReferenceResolverEnabled::kCheckedExpressionOnly));
  CEL_RETURN_IF_ERROR(extensions::EnableConstantFolding(builder));
  CEL_RETURN_IF_ERROR(extensions::EnableRegexPrecompilation(builder));
  CEL_RETURN_IF_ERROR(extensions::EnableSelectOptimization(builder));
  CEL_RETURN_IF_ERROR(extensions::EnableSetMembershipOptimization(builder));
  return builder;
}

}  // namespace cel
