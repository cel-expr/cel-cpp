// Copyright 2023 Google LLC
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

#include "eval/eval/cel_expression_flat_impl.h"

#include <cstdint>
#include <memory>

#include "absl/base/nullability.h"
#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/value.h"
#include "eval/eval/evaluator_core.h"
#include "eval/internal/adapter_activation_impl.h"
#include "eval/internal/interop.h"
#include "eval/public/base_activation.h"
#include "eval/public/cel_expression.h"
#include "eval/public/cel_value.h"
#include "internal/casts.h"
#include "internal/status_macros.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"

namespace google::api::expr::runtime {
namespace {

using ::cel::Value;

EvaluationListener AdaptListener(const CelEvaluationListener& listener) {
  if (!listener) return nullptr;
  return [&](int64_t expr_id, const Value& value,
             const google::protobuf::DescriptorPool* absl_nonnull,
             google::protobuf::MessageFactory* absl_nonnull,
             google::protobuf::Arena* absl_nonnull arena) -> absl::Status {
    if (value->Is<cel::OpaqueValue>()) {
      // Opaque types are used to implement some optimized operations.
      // These aren't representable as legacy values and shouldn't be
      // inspectable by clients.
      return absl::OkStatus();
    }
    CelValue legacy_value =
        cel::interop_internal::ModernValueToLegacyValueOrDie(arena, value);
    return listener(expr_id, legacy_value, arena);
  };
}
}  // namespace

CelExpressionFlatEvaluationState::CelExpressionFlatEvaluationState(
    google::protobuf::Arena* arena,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    const FlatExpression& expression)
    : state_(expression.MakeEvaluatorState(descriptor_pool, message_factory,
                                           arena)) {}

CelExpressionFlatEvaluationState::CelExpressionFlatEvaluationState(
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    const FlatExpression& expr)
    : state_(expr.MakeEvaluatorState(descriptor_pool, message_factory)) {}

absl::StatusOr<CelValue> CelExpressionFlatImpl::Trace(
    const BaseActivation& activation, google::protobuf::Arena* arena,
    CelEvaluationListener callback, CelEvaluationState* state) const {
  cel::interop_internal::AdapterActivationImpl modern_activation(activation);
  if (state == nullptr && flat_expression_.value_stack_size() <=
                              FlatExpression::kInlineStackLimit) {
    ABSL_DCHECK(arena != nullptr)
        << "arena must be implicitly provided when using InitializeState() or "
           "explicitly provided when using CreateState()";
    CEL_ASSIGN_OR_RETURN(
        cel::Value value,
        flat_expression_.EvaluateWithCallback(
            modern_activation, /*embedder_context=*/nullptr,
            AdaptListener(callback), env_->descriptor_pool.get(),
            env_->MutableMessageFactory(), arena));
    return cel::interop_internal::ModernValueToLegacyValueOrDie(arena, value);
  }

  std::unique_ptr<CelEvaluationState> inline_state;
  if (state == nullptr) {
    inline_state = CreateState();
    state = inline_state.get();
  }
  auto derived_state =
      ::cel::internal::down_cast<CelExpressionFlatEvaluationState*>(state);
  if (arena != nullptr) {
    derived_state->Rebind(arena);
  } else {
    arena = derived_state->arena();
  }
  ABSL_DCHECK(arena != nullptr)
      << "arena must be implicitly provided when using InitializeState() or "
         "explicitly provided when using CreateState()";

  CEL_ASSIGN_OR_RETURN(cel::Value value,
                       flat_expression_.EvaluateWithCallback(
                           modern_activation,
                           /*embedder_context=*/nullptr,
                           AdaptListener(callback), derived_state->state()));

  return cel::interop_internal::ModernValueToLegacyValueOrDie(arena, value);
}

std::unique_ptr<CelEvaluationState> CelExpressionFlatImpl::InitializeState(
    google::protobuf::Arena* arena) const {
  return std::make_unique<CelExpressionFlatEvaluationState>(
      arena, env_->descriptor_pool.get(), env_->MutableMessageFactory(),
      flat_expression_);
}

std::unique_ptr<CelEvaluationState> CelExpressionFlatImpl::CreateState() const {
  return std::make_unique<CelExpressionFlatEvaluationState>(
      env_->descriptor_pool.get(), env_->MutableMessageFactory(),
      flat_expression_);
}

}  // namespace google::api::expr::runtime
