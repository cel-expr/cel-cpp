#ifndef THIRD_PARTY_CEL_CPP_EVAL_EVAL_FUNCTION_STEP_H_
#define THIRD_PARTY_CEL_CPP_EVAL_EVAL_FUNCTION_STEP_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "common/expr.h"
#include "common/value.h"
#include "eval/eval/direct_expression_step.h"
#include "eval/eval/expression_step_logic.h"
#include "runtime/function_overload_reference.h"
#include "runtime/function_registry.h"

namespace google::api::expr::runtime {

// Factory method for Call-based execution step where the function has been
// statically resolved from a set of eagerly functions configured in the
// CelFunctionRegistry.
std::unique_ptr<DirectExpressionStep> CreateDirectFunctionStep(
    int64_t expr_id, const cel::CallExpr& call,
    std::vector<std::unique_ptr<DirectExpressionStep>> deps,
    std::vector<cel::FunctionOverloadReference> overloads);

// Factory method for Call-based execution step where the function has been
// statically resolved from a set of lazy functions configured in the
// CelFunctionRegistry.
std::unique_ptr<DirectExpressionStep> CreateDirectLazyFunctionStep(
    int64_t expr_id, const cel::CallExpr& call,
    std::vector<std::unique_ptr<DirectExpressionStep>> deps,
    std::vector<cel::FunctionRegistry::LazyOverload> providers);

class LazyFunctionStep;
class EagerFunctionStep;
class ExecutionFrameBase;
class ExecutionFrame;

// Factory method for Call-based execution step where the function will be
// resolved at runtime (lazily) from an input Activation.
std::unique_ptr<LazyFunctionStep> CreateLazyFunctionStep(
    const cel::CallExpr& call, int64_t expr_id,
    std::vector<cel::FunctionRegistry::LazyOverload> lazy_overloads);

// Factory method for Call-based execution step where the function has been
// statically resolved from a set of eagerly functions configured in the
// CelFunctionRegistry.
std::unique_ptr<EagerFunctionStep> CreateFunctionStep(
    const cel::CallExpr& call, int64_t expr_id,
    std::vector<cel::FunctionOverloadReference> overloads);

// Common base class for EagerFunctionStep and LazyFunctionStep.
class FunctionStepBase {
 private:
  friend class EagerFunctionStep;
  friend class LazyFunctionStep;
  template <class Step>
  friend void EvaluateFunctionStep(const Step* step, ExecutionFrame& frame);

  // Constructs FunctionStep that uses overloads specified.
  FunctionStepBase(const std::string& name, size_t num_arguments,
                   bool receiver_style, int64_t expr_id)
      : name_(name),
        num_arguments_(num_arguments),
        receiver_style_(receiver_style),
        expr_id_(expr_id) {}

  std::string name_;
  size_t num_arguments_;
  bool receiver_style_;
  int64_t expr_id_;
};

class EagerFunctionStep : public FunctionStepBase {
 public:
  EagerFunctionStep(std::vector<cel::FunctionOverloadReference> overloads,
                    const std::string& name, size_t num_args,
                    bool receiver_style, int64_t expr_id)
      : FunctionStepBase(name, num_args, receiver_style, expr_id),
        overloads_(std::move(overloads)) {}

  void Evaluate(ExecutionFrame& frame) const;

 private:
  template <class Step>
  friend void EvaluateFunctionStep(const Step* step, ExecutionFrame& frame);

  std::optional<cel::FunctionOverloadReference> ResolveFunction(
      absl::Span<const cel::Value> input_args,
      const ExecutionFrameBase& frame) const;

  std::vector<cel::FunctionOverloadReference> overloads_;
};

class LazyFunctionStep : public FunctionStepBase {
 public:
  LazyFunctionStep(std::vector<cel::FunctionRegistry::LazyOverload> providers,
                   const std::string& name, size_t num_args,
                   bool receiver_style, int64_t expr_id)
      : FunctionStepBase(name, num_args, receiver_style, expr_id),
        providers_(std::move(providers)) {}

  void Evaluate(ExecutionFrame& frame) const;

 private:
  template <class Step>
  friend void EvaluateFunctionStep(const Step* step, ExecutionFrame& frame);

  absl::StatusOr<std::optional<cel::FunctionOverloadReference>> ResolveFunction(
      absl::Span<const cel::Value> input_args,
      const ExecutionFrameBase& frame) const;

  std::vector<cel::FunctionRegistry::LazyOverload> providers_;
};

}  // namespace google::api::expr::runtime

#endif  // THIRD_PARTY_CEL_CPP_EVAL_EVAL_FUNCTION_STEP_H_
