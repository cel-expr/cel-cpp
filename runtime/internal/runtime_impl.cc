// Copyright 2023 Google LLC
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
#include "runtime/internal/runtime_impl.h"

#include <memory>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/log/absl_check.h"
#include "absl/status/statusor.h"
#include "base/ast.h"
#include "base/type_provider.h"
#include "common/value.h"
#include "eval/eval/evaluator_core.h"
#include "internal/status_macros.h"
#include "runtime/activation_interface.h"
#include "runtime/runtime.h"
#include "google/protobuf/arena.h"

namespace cel::runtime_internal {
namespace {

using ::google::api::expr::runtime::FlatExpression;

class ProgramImpl final : public TraceableProgram {
 public:
  using EvaluationListener = TraceableProgram::EvaluationListener;
  ProgramImpl(
      const std::shared_ptr<const RuntimeImpl::Environment>& environment,
      FlatExpression impl)
      : environment_(environment), impl_(std::move(impl)) {}

  absl::StatusOr<Value> TraceImpl(
      const ActivationInterface& activation,
      EvaluationListener evaluation_listener, google::protobuf::Arena* absl_nonnull arena,
      const EvaluateOptions& options) const override {
    ABSL_DCHECK(arena != nullptr);
    auto state =
        impl_.MakeEvaluatorState(environment_->descriptor_pool.get(),
                                 options.message_factory != nullptr
                                     ? options.message_factory
                                     : environment_->MutableMessageFactory(),
                                 arena);
    return impl_.EvaluateWithCallback(activation, options.embedder_context,
                                      std::move(evaluation_listener), state);
  }

  const TypeProvider& GetTypeProvider() const override {
    return environment_->type_registry.GetComposedTypeProvider();
  }

 private:
  // Keep the Runtime environment alive while programs reference it.
  std::shared_ptr<const RuntimeImpl::Environment> environment_;
  FlatExpression impl_;
};

}  // namespace

absl::StatusOr<std::unique_ptr<Program>> RuntimeImpl::CreateProgram(
    std::unique_ptr<Ast> ast,
    const Runtime::CreateProgramOptions& options) const {
  return CreateTraceableProgram(std::move(ast), options);
}

absl::StatusOr<std::unique_ptr<TraceableProgram>>
RuntimeImpl::CreateTraceableProgram(
    std::unique_ptr<Ast> ast,
    const Runtime::CreateProgramOptions& options) const {
  CEL_ASSIGN_OR_RETURN(auto flat_expr, expr_builder_.CreateExpressionImpl(
                                           std::move(ast), options.issues));

  return std::make_unique<ProgramImpl>(environment_, std::move(flat_expr));
}

}  // namespace cel::runtime_internal
