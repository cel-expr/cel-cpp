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
#include "common/native_type.h"
#include "common/value.h"
#include "eval/eval/attribute_trail.h"
#include "eval/eval/comprehension_slots.h"
#include "eval/eval/direct_expression_step.h"
#include "eval/eval/evaluator_core.h"
#include "internal/casts.h"
#include "internal/status_macros.h"
#include "runtime/activation_interface.h"
#include "runtime/runtime.h"
#include "google/protobuf/arena.h"

namespace cel::runtime_internal {
namespace {

using ::google::api::expr::runtime::AttributeTrail;
using ::google::api::expr::runtime::ComprehensionSlots;
using ::google::api::expr::runtime::DirectExpressionStep;
using ::google::api::expr::runtime::ExecutionFrameBase;
using ::google::api::expr::runtime::FlatExpression;
using ::google::api::expr::runtime::FlatExpressionEvaluatorState;
using ::google::api::expr::runtime::WrappedDirectStep;

class ProcessImpl final : public TraceableProcess {
 public:
  ProcessImpl(std::shared_ptr<const RuntimeImpl::Environment> environment,
              std::shared_ptr<const FlatExpression> impl)
      : environment_(std::move(environment)),
        impl_(std::move(impl)),
        state_(impl_->MakeEvaluatorState(environment_->descriptor_pool.get())) {
  }

  absl::StatusOr<Value> TraceImpl(const ActivationInterface& activation,
                                  EvaluationListener evaluation_listener,
                                  google::protobuf::Arena* absl_nonnull arena,
                                  const EvaluateOptions& options) override {
    ABSL_DCHECK(arena != nullptr);
    state_.Rebind(arena, options.message_factory != nullptr
                             ? options.message_factory
                             : environment_->MutableMessageFactory());
    return impl_->EvaluateWithCallback(activation, options.embedder_context,
                                       std::move(evaluation_listener), state_);
  }

 private:
  std::shared_ptr<const RuntimeImpl::Environment> environment_;
  std::shared_ptr<const FlatExpression> impl_;
  FlatExpressionEvaluatorState state_;
};

class RecursiveProcessImpl final : public TraceableProcess {
 public:
  RecursiveProcessImpl(
      std::shared_ptr<const RuntimeImpl::Environment> environment,
      std::shared_ptr<const FlatExpression> impl,
      const DirectExpressionStep* absl_nonnull root)
      : environment_(std::move(environment)),
        impl_(std::move(impl)),
        root_(root),
        comprehension_slots_(impl_->comprehension_slots_size()) {}

  absl::StatusOr<Value> TraceImpl(const ActivationInterface& activation,
                                  EvaluationListener evaluation_listener,
                                  google::protobuf::Arena* absl_nonnull arena,
                                  const EvaluateOptions& options) override {
    ABSL_DCHECK(arena != nullptr);
    comprehension_slots_.Reset();
    ExecutionFrameBase frame(
        activation, std::move(evaluation_listener), impl_->options(),
        environment_->type_registry.GetComposedTypeProvider(),
        environment_->descriptor_pool.get(),
        options.message_factory != nullptr
            ? options.message_factory
            : environment_->MutableMessageFactory(),
        arena, options.embedder_context, comprehension_slots_);
    Value result;
    AttributeTrail attribute;
    auto status = root_->Evaluate(frame, result, attribute);
    comprehension_slots_.Reset();
    CEL_RETURN_IF_ERROR(status);
    return result;
  }

 private:
  std::shared_ptr<const RuntimeImpl::Environment> environment_;
  std::shared_ptr<const FlatExpression> impl_;
  const DirectExpressionStep* absl_nonnull root_;
  ComprehensionSlots comprehension_slots_;
};

class ProgramImpl final : public TraceableProgram {
 public:
  using EvaluationListener = TraceableProgram::EvaluationListener;
  ProgramImpl(
      const std::shared_ptr<const RuntimeImpl::Environment>& environment,
      std::shared_ptr<const FlatExpression> impl)
      : environment_(environment), impl_(std::move(impl)) {}

  absl::StatusOr<std::unique_ptr<Process>> CreateProcess() override {
    return std::make_unique<ProcessImpl>(environment_, impl_);
  }

  absl::StatusOr<std::unique_ptr<TraceableProcess>> CreateTraceableProcess()
      override {
    return std::make_unique<ProcessImpl>(environment_, impl_);
  }

  absl::StatusOr<Value> TraceImpl(
      const ActivationInterface& activation,
      EvaluationListener evaluation_listener, google::protobuf::Arena* absl_nonnull arena,
      const EvaluateOptions& options) const override {
    ABSL_DCHECK(arena != nullptr);
    ProcessImpl process(environment_, impl_);
    return process.TraceImpl(activation, std::move(evaluation_listener), arena,
                             options);
  }

  const TypeProvider& GetTypeProvider() const override {
    return environment_->type_registry.GetComposedTypeProvider();
  }

 private:
  // Keep the Runtime environment alive while programs reference it.
  std::shared_ptr<const RuntimeImpl::Environment> environment_;
  std::shared_ptr<const FlatExpression> impl_;
};

class RecursiveProgramImpl final : public TraceableProgram {
 public:
  using EvaluationListener = TraceableProgram::EvaluationListener;
  RecursiveProgramImpl(
      const std::shared_ptr<const RuntimeImpl::Environment>& environment,
      std::shared_ptr<const FlatExpression> impl,
      const DirectExpressionStep* absl_nonnull root)
      : environment_(environment), impl_(std::move(impl)), root_(root) {}

  absl::StatusOr<std::unique_ptr<Process>> CreateProcess() override {
    return std::make_unique<RecursiveProcessImpl>(environment_, impl_, root_);
  }

  absl::StatusOr<std::unique_ptr<TraceableProcess>> CreateTraceableProcess()
      override {
    return std::make_unique<RecursiveProcessImpl>(environment_, impl_, root_);
  }

  absl::StatusOr<Value> TraceImpl(
      const ActivationInterface& activation,
      EvaluationListener evaluation_listener, google::protobuf::Arena* absl_nonnull arena,
      const EvaluateOptions& options) const override {
    ABSL_DCHECK(arena != nullptr);
    RecursiveProcessImpl process(environment_, impl_, root_);
    return process.TraceImpl(activation, std::move(evaluation_listener), arena,
                             options);
  }

  const TypeProvider& GetTypeProvider() const override {
    return environment_->type_registry.GetComposedTypeProvider();
  }

 private:
  // Keep the Runtime environment alive while programs reference it.
  std::shared_ptr<const RuntimeImpl::Environment> environment_;
  std::shared_ptr<const FlatExpression> impl_;
  const DirectExpressionStep* absl_nonnull root_;
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
  auto shared_flat_expr =
      std::make_shared<FlatExpression>(std::move(flat_expr));

  // Special case if the program is fully recursive.
  //
  // This implementation avoids unnecessary allocs at evaluation time which
  // improves performance notably for small expressions.
  if (expr_builder_.options().max_recursion_depth != 0 &&
      !flat_expr.subexpressions().empty() &&
      // mainline expression is exactly one recursive step.
      flat_expr.subexpressions().front().size() == 1 &&
      flat_expr.subexpressions().front().front().IsGenericStep() &&
      flat_expr.subexpressions()
              .front()
              .front()
              .GetGenericStep()
              ->GetNativeTypeId() == NativeTypeId::For<WrappedDirectStep>()) {
    const DirectExpressionStep* root =
        internal::down_cast<const WrappedDirectStep*>(
            flat_expr.subexpressions().front().front().GetGenericStep())
            ->wrapped();
    return std::make_unique<RecursiveProgramImpl>(
        environment_, std::move(shared_flat_expr), root);
  }

  return std::make_unique<ProgramImpl>(environment_,
                                       std::move(shared_flat_expr));
}

bool TestOnly_IsRecursiveImpl(const Program* program) {
  return dynamic_cast<const RecursiveProgramImpl*>(program) != nullptr;
}

}  // namespace cel::runtime_internal
