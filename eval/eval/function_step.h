#ifndef THIRD_PARTY_CEL_CPP_EVAL_EVAL_FUNCTION_STEP_H_
#define THIRD_PARTY_CEL_CPP_EVAL_EVAL_FUNCTION_STEP_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "common/expr.h"
#include "common/value.h"
#include "eval/eval/expression_step_logic.h"
#include "runtime/function_overload_reference.h"
#include "runtime/function_registry.h"

namespace google::api::expr::runtime {

class LazyFunctionStep;
class EagerFunctionStep;
class ExecutionFrameBase;
class ExecutionFrame;

// The adapted factories preserve the feature's trailing count/mode positions.
// The fourth argument is an ignored count: true converts to count=1 and leaves
// kind mode selected. Only the fifth argument selects type mode. Count never
// truncates or reorders candidates.
//
// Factory method for Call-based execution step where the function will be
// resolved at runtime (lazily) from an input Activation.
std::unique_ptr<LazyFunctionStep> CreateLazyFunctionStep(
    const cel::CallExpr& call, int64_t expr_id,
    std::vector<cel::FunctionRegistry::LazyOverload> lazy_overloads,
    size_t checked_overloads_count = 0, bool type_level_overload = false);

// Factory method for Call-based execution step where the function has been
// statically resolved from a set of eagerly functions configured in the
// CelFunctionRegistry.
std::unique_ptr<EagerFunctionStep> CreateFunctionStep(
    const cel::CallExpr& call, int64_t expr_id,
    std::vector<cel::FunctionOverloadReference> overloads,
    size_t checked_overloads_count = 0, bool type_level_overload = false);

// Common base class for EagerFunctionStep and LazyFunctionStep.
class FunctionStepBase {
 public:
  size_t num_arguments() const { return num_arguments_; }

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
                    bool receiver_style, int64_t expr_id,
                    bool type_level_overload = false)
      : FunctionStepBase(name, num_args, receiver_style, expr_id),
        overloads_(std::move(overloads)),
        type_level_overload_(type_level_overload) {}

  template <typename Count, typename = std::enable_if_t<
                                std::is_integral_v<std::decay_t<Count>> &&
                                !std::is_same_v<std::decay_t<Count>, bool>>>
  EagerFunctionStep(std::vector<cel::FunctionOverloadReference>,
                    const std::string&, size_t, bool, int64_t, Count) = delete;

  void Evaluate(ExecutionFrame& frame) const;

 private:
  template <class Step>
  friend void EvaluateFunctionStep(const Step* step, ExecutionFrame& frame);

  absl::StatusOr<std::optional<cel::FunctionOverloadReference>> ResolveFunction(
      absl::Span<const cel::Value> input_args,
      const ExecutionFrameBase& frame) const;

  std::vector<cel::FunctionOverloadReference> overloads_;
  bool type_level_overload_;
};

class LazyFunctionStep : public FunctionStepBase {
 public:
  LazyFunctionStep(std::vector<cel::FunctionRegistry::LazyOverload> providers,
                   const std::string& name, size_t num_args,
                   bool receiver_style, int64_t expr_id,
                   bool type_level_overload = false)
      : FunctionStepBase(name, num_args, receiver_style, expr_id),
        providers_(std::move(providers)),
        type_level_overload_(type_level_overload) {}

  template <typename Count, typename = std::enable_if_t<
                                std::is_integral_v<std::decay_t<Count>> &&
                                !std::is_same_v<std::decay_t<Count>, bool>>>
  LazyFunctionStep(std::vector<cel::FunctionRegistry::LazyOverload>,
                   const std::string&, size_t, bool, int64_t, Count) = delete;

  void Evaluate(ExecutionFrame& frame) const;

 private:
  template <class Step>
  friend void EvaluateFunctionStep(const Step* step, ExecutionFrame& frame);

  absl::StatusOr<std::optional<cel::FunctionOverloadReference>> ResolveFunction(
      absl::Span<const cel::Value> input_args,
      const ExecutionFrameBase& frame) const;

  std::vector<cel::FunctionRegistry::LazyOverload> providers_;
  bool type_level_overload_;
};

}  // namespace google::api::expr::runtime

#endif  // THIRD_PARTY_CEL_CPP_EVAL_EVAL_FUNCTION_STEP_H_
