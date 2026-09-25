#include "eval/eval/function_step.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/inlined_vector.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "common/casting.h"
#include "common/expr.h"
#include "common/function_descriptor.h"
#include "common/kind.h"
#include "common/value.h"
#include "common/value_kind.h"
#include "eval/eval/attribute_trail.h"
#include "eval/eval/direct_expression_step.h"
#include "eval/eval/evaluator_core.h"
#include "eval/eval/expression_step_logic.h"
#include "eval/internal/errors.h"
#include "internal/status_macros.h"
#include "runtime/activation_interface.h"
#include "runtime/function.h"
#include "runtime/function_overload_reference.h"
#include "runtime/function_provider.h"
#include "runtime/function_registry.h"
#include "runtime/internal/errors.h"

namespace google::api::expr::runtime {

namespace {

using ::cel::ErrorValue;
using ::cel::UnknownValue;
using ::cel::Value;
using ::cel::ValueKindToKind;
using ::cel::runtime_internal::CreateNoMatchingOverloadError;

// Determine if the overload should be considered. Overloads that can consume
// errors or unknown sets must be allowed as a non-strict function.
bool ShouldAcceptOverload(const cel::FunctionDescriptor& descriptor,
                          absl::Span<const cel::Value> arguments) {
  for (size_t i = 0; i < arguments.size(); i++) {
    if (arguments[i]->Is<cel::UnknownValue>() ||
        arguments[i]->Is<cel::ErrorValue>()) {
      return !descriptor.is_strict();
    }
  }
  return true;
}

bool ArgumentKindsMatch(const cel::FunctionDescriptor& descriptor,
                        absl::Span<const cel::Value> arguments) {
  auto types_size = descriptor.types().size();

  if (types_size != arguments.size()) {
    return false;
  }

  for (size_t i = 0; i < types_size; i++) {
    const auto& arg = arguments[i];
    cel::Kind param_kind = descriptor.types()[i];
    if (arg->kind() != param_kind && param_kind != cel::Kind::kAny) {
      return false;
    }
  }

  return true;
}

// Adjust new type names to legacy equivalent. int -> int64.
// Temporary fix to migrate value types without breaking clients.
// TODO(uncreated-issue/46): Update client tests that depend on this value.
std::string ToLegacyKindName(absl::string_view type_name) {
  if (type_name == "int" || type_name == "uint") {
    return absl::StrCat(type_name, "64");
  }

  return std::string(type_name);
}

std::string CallArgTypeString(absl::Span<const cel::Value> args) {
  std::string call_sig_string = "";

  for (size_t i = 0; i < args.size(); i++) {
    const auto& arg = args[i];
    if (!call_sig_string.empty()) {
      absl::StrAppend(&call_sig_string, ", ");
    }
    absl::StrAppend(
        &call_sig_string,
        ToLegacyKindName(cel::KindToString(ValueKindToKind(arg->kind()))));
  }
  return absl::StrCat("(", call_sig_string, ")");
}

// Convert partially unknown arguments to unknowns before passing to the
// function.
// TODO(issues/52): See if this can be refactored to remove the eager
// arguments copy.
// Argument and attribute spans are expected to be equal length.
std::vector<cel::Value> CheckForPartialUnknowns(
    ExecutionFrame* frame, absl::Span<const cel::Value> args,
    absl::Span<const AttributeTrail> attrs) {
  std::vector<cel::Value> result;
  result.reserve(args.size());
  for (size_t i = 0; i < args.size(); i++) {
    const AttributeTrail& trail = attrs.subspan(i, 1)[0];

    if (frame->attribute_utility().CheckForUnknown(trail,
                                                   /*use_partial=*/true)) {
      result.push_back(
          frame->attribute_utility().CreateUnknownSet(trail.attribute()));
    } else {
      result.push_back(args.at(i));
    }
  }

  return result;
}

bool IsUnknownFunctionResultError(const Value& result) {
  if (!result->Is<cel::ErrorValue>()) {
    return false;
  }

  const auto& status = result.GetError().NativeValue();

  if (status.code() != absl::StatusCode::kUnavailable) {
    return false;
  }
  auto payload = status.GetPayload(
      cel::runtime_internal::kPayloadUrlUnknownFunctionResult);
  return payload.has_value() && payload.value() == "true";
}

// Simple wrapper around a function resolution result. A function call should
// resolve to a single function implementation and a descriptor or none.
using ResolveResult = absl::optional<cel::FunctionOverloadReference>;


inline absl::StatusOr<Value> Invoke(
    const cel::FunctionOverloadReference& overload, int64_t expr_id,
    absl::Span<const cel::Value> args, ExecutionFrameBase& frame) {
  cel::Function::InvokeContext context(frame.descriptor_pool(),
                                       frame.message_factory(), frame.arena());
  if (overload.descriptor.is_contextual()) {
    context.set_embedder_context(frame.embedder_context());
  }

  CEL_ASSIGN_OR_RETURN(Value result,
                       overload.implementation.Invoke(args, context));

  if (frame.unknown_function_results_enabled() &&
      IsUnknownFunctionResultError(result)) {
    return frame.attribute_utility().CreateUnknownSet(overload.descriptor,
                                                      expr_id, args);
  }
  return result;
}

Value NoOverloadResult(absl::string_view name,
                       absl::Span<const cel::Value> args, bool receiver_style,
                       ExecutionFrameBase& frame) {
  // No matching overloads.
  // Such absence can be caused by presence of CelError in arguments.
  // To enable behavior of functions that accept CelError( &&, || ), CelErrors
  // should be propagated along execution path.
  for (size_t i = 0; i < args.size(); i++) {
    const auto& arg = args[i];
    if (cel::InstanceOf<cel::ErrorValue>(arg)) {
      return arg;
    }
  }

  if (frame.unknown_processing_enabled()) {
    // Already converted partial unknowns to unknown sets so just merge.
    absl::optional<UnknownValue> unknown_set =
        frame.attribute_utility().MergeUnknowns(args);
    if (unknown_set.has_value()) {
      return *unknown_set;
    }
  }

  // If no errors or unknowns in input args, create new CelError for missing
  // overload.
  std::string signature;
  if (receiver_style) {
    if (args.empty()) {
      // Should not be possible, but return a sensible error in case of logic
      // error.
      return ErrorValue(
          CreateNoMatchingOverloadError(absl::StrCat("().", name, "()")));
    }
    return ErrorValue(CreateNoMatchingOverloadError(absl::StrCat(
        "(",
        ToLegacyKindName(cel::KindToString(ValueKindToKind(args[0].kind()))),
        ").", name, CallArgTypeString(args.subspan(1)))));
  }
  return cel::ErrorValue(CreateNoMatchingOverloadError(
      absl::StrCat(name, CallArgTypeString(args))));
}

ResolveResult ResolveStatic(
    absl::Span<const cel::Value> input_args,
    absl::Span<const cel::FunctionOverloadReference> overloads) {
  for (const auto& overload : overloads) {
    if (ArgumentKindsMatch(overload.descriptor, input_args)) {
      return overload;
    }
  }
  return std::nullopt;
}

absl::StatusOr<ResolveResult> ResolveLazy(
    absl::Span<const cel::Value> input_args, absl::string_view name,
    bool receiver_style,
    absl::Span<const cel::FunctionRegistry::LazyOverload> providers,
    const ExecutionFrameBase& frame) {
  ResolveResult result = std::nullopt;

  std::vector<cel::Kind> arg_types(input_args.size());

  std::transform(
      input_args.begin(), input_args.end(), arg_types.begin(),
      [](const cel::Value& value) { return ValueKindToKind(value->kind()); });

  cel::FunctionDescriptor matcher{name, receiver_style, std::move(arg_types)};

  const cel::ActivationInterface& activation = frame.activation();
  for (auto provider : providers) {
    // The LazyFunctionStep has so far only resolved by function shape, check
    // that the runtime argument kinds agree with the specific descriptor for
    // the provider candidates.
    if (!ArgumentKindsMatch(provider.descriptor, input_args)) {
      continue;
    }

    CEL_ASSIGN_OR_RETURN(auto overload,
                         provider.provider.GetFunction(matcher, activation));
    if (overload.has_value()) {
      // More than one overload matches our arguments.
      if (result.has_value()) {
        return absl::Status(absl::StatusCode::kInternal,
                            "Cannot resolve overloads");
      }

      result.emplace(overload.value());
    }
  }

  return result;
}



class StaticResolver {
 public:
  explicit StaticResolver(std::vector<cel::FunctionOverloadReference> overloads)
      : overloads_(std::move(overloads)) {}

  absl::StatusOr<ResolveResult> Resolve(ExecutionFrameBase& frame,
                                        absl::Span<const Value> input) const {
    return ResolveStatic(input, overloads_);
  }

 private:
  std::vector<cel::FunctionOverloadReference> overloads_;
};

class LazyResolver {
 public:
  explicit LazyResolver(
      std::vector<cel::FunctionRegistry::LazyOverload> providers,
      std::string name, bool receiver_style)
      : providers_(std::move(providers)),
        name_(std::move(name)),
        receiver_style_(receiver_style) {}

  absl::StatusOr<ResolveResult> Resolve(ExecutionFrameBase& frame,
                                        absl::Span<const Value> input) const {
    return ResolveLazy(input, name_, receiver_style_, providers_, frame);
  }

 private:
  std::vector<cel::FunctionRegistry::LazyOverload> providers_;
  std::string name_;
  bool receiver_style_;
};

template <typename Resolver>
class DirectFunctionStepImpl : public DirectExpressionStep {
 public:
  DirectFunctionStepImpl(
      int64_t expr_id, const std::string& name,
      std::vector<std::unique_ptr<DirectExpressionStep>> arg_steps,
      bool receiver_style, Resolver&& resolver)
      : DirectExpressionStep(expr_id),
        name_(name),
        arg_steps_(std::move(arg_steps)),
        receiver_style_(receiver_style),
        resolver_(std::forward<Resolver>(resolver)) {}

  absl::Status Evaluate(ExecutionFrameBase& frame, cel::Value& result,
                        AttributeTrail& trail) const override {
    absl::InlinedVector<Value, 2> args;
    absl::InlinedVector<AttributeTrail, 2> arg_trails;

    args.resize(arg_steps_.size());
    arg_trails.resize(arg_steps_.size());

    for (size_t i = 0; i < arg_steps_.size(); i++) {
      CEL_RETURN_IF_ERROR(
          arg_steps_[i]->Evaluate(frame, args[i], arg_trails[i]));
    }

    if (frame.unknown_processing_enabled()) {
      for (size_t i = 0; i < arg_trails.size(); i++) {
        if (frame.attribute_utility().CheckForUnknown(arg_trails[i],
                                                      /*use_partial=*/true)) {
          args[i] = frame.attribute_utility().CreateUnknownSet(
              arg_trails[i].attribute());
        }
      }
    }

    CEL_ASSIGN_OR_RETURN(ResolveResult resolved_function,
                         resolver_.Resolve(frame, args));

    if (resolved_function.has_value() &&
        ShouldAcceptOverload(resolved_function->descriptor, args)) {
      CEL_ASSIGN_OR_RETURN(result,
                           Invoke(*resolved_function, expr_id_, args, frame));

      return absl::OkStatus();
    }

    result = NoOverloadResult(name_, args, receiver_style_, frame);

    return absl::OkStatus();
  }

  absl::optional<std::vector<const DirectExpressionStep*>> GetDependencies()
      const override {
    std::vector<const DirectExpressionStep*> dependencies;
    dependencies.reserve(arg_steps_.size());
    for (const auto& arg_step : arg_steps_) {
      dependencies.push_back(arg_step.get());
    }
    return dependencies;
  }

  absl::optional<std::vector<std::unique_ptr<DirectExpressionStep>>>
  ExtractDependencies() override {
    return std::move(arg_steps_);
  }

 private:
  friend Resolver;
  std::string name_;
  std::vector<std::unique_ptr<DirectExpressionStep>> arg_steps_;
  bool receiver_style_;
  Resolver resolver_;
};

}  // namespace

template <class Step>
void EvaluateFunctionStep(const Step* step, ExecutionFrame& frame) {
  if (!frame.value_stack().HasEnough(step->num_arguments_)) {
    frame.Abort(
        absl::Status(absl::StatusCode::kInternal, "Value stack underflow"));
    return;
  }

  // Create Span object that contains input arguments to the function.
  absl::Span<const cel::Value> input_args =
      frame.value_stack().GetSpan(step->num_arguments_);

  std::vector<cel::Value> unknowns_args;
  // Preprocess args. If an argument is partially unknown, convert it to an
  // unknown attribute set.
  if (frame.enable_unknowns()) {
    absl::Span<const AttributeTrail> input_attrs =
        frame.value_stack().GetAttributeSpan(step->num_arguments_);
    unknowns_args = CheckForPartialUnknowns(&frame, input_args, input_attrs);
    input_args = absl::MakeConstSpan(unknowns_args);
  }

  // Derived class resolves to a single function overload or none.
  absl::StatusOr<ResolveResult> matched_function =
      step->ResolveFunction(input_args, frame);
  if (!matched_function.ok()) {
    frame.Abort(std::move(matched_function).status());
    return;
  }

  // Overload found and is allowed to consume the arguments.
  if (matched_function->has_value() &&
      ShouldAcceptOverload((*matched_function)->descriptor, input_args)) {
    absl::StatusOr<Value> result =
        Invoke(**matched_function, step->expr_id_, input_args, frame);
    if (!result.ok()) {
      frame.Abort(std::move(result).status());
      return;
    }
    frame.value_stack().PopAndPush(step->num_arguments_, *std::move(result));
    return;
  }

  frame.value_stack().PopAndPush(
      step->num_arguments_,
      NoOverloadResult(step->name_, input_args, step->receiver_style_, frame));
}

void EagerFunctionStep::Evaluate(ExecutionFrame& frame) const {
  EvaluateFunctionStep(this, frame);
}

void LazyFunctionStep::Evaluate(ExecutionFrame& frame) const {
  EvaluateFunctionStep(this, frame);
}

ResolveResult EagerFunctionStep::ResolveFunction(
    absl::Span<const cel::Value> input_args,
    const ExecutionFrameBase& frame) const {
  return ResolveStatic(input_args, overloads_);
}

absl::StatusOr<ResolveResult> LazyFunctionStep::ResolveFunction(
    absl::Span<const cel::Value> input_args,
    const ExecutionFrameBase& frame) const {
  return ResolveLazy(input_args, name_, receiver_style_, providers_, frame);
}

std::unique_ptr<DirectExpressionStep> CreateDirectFunctionStep(
    int64_t expr_id, const cel::CallExpr& call,
    std::vector<std::unique_ptr<DirectExpressionStep>> deps,
    std::vector<cel::FunctionOverloadReference> overloads) {
  return std::make_unique<DirectFunctionStepImpl<StaticResolver>>(
      expr_id, call.function(), std::move(deps), call.has_target(),
      StaticResolver(std::move(overloads)));
}

std::unique_ptr<DirectExpressionStep> CreateDirectLazyFunctionStep(
    int64_t expr_id, const cel::CallExpr& call,
    std::vector<std::unique_ptr<DirectExpressionStep>> deps,
    std::vector<cel::FunctionRegistry::LazyOverload> providers) {
  return std::make_unique<DirectFunctionStepImpl<LazyResolver>>(
      expr_id, call.function(), std::move(deps), call.has_target(),
      LazyResolver(std::move(providers), call.function(), call.has_target()));
}

std::unique_ptr<LazyFunctionStep> CreateLazyFunctionStep(
    const cel::CallExpr& call_expr, int64_t expr_id,
    std::vector<cel::FunctionRegistry::LazyOverload> lazy_overloads) {
  bool receiver_style = call_expr.has_target();
  size_t num_args = call_expr.args().size() + (receiver_style ? 1 : 0);
  return std::make_unique<LazyFunctionStep>(std::move(lazy_overloads),
                                            call_expr.function(), num_args,
                                            receiver_style, expr_id);
}

std::unique_ptr<EagerFunctionStep> CreateFunctionStep(
    const cel::CallExpr& call_expr, int64_t expr_id,
    std::vector<cel::FunctionOverloadReference> overloads) {
  bool receiver_style = call_expr.has_target();
  size_t num_args = call_expr.args().size() + (receiver_style ? 1 : 0);
  return std::make_unique<EagerFunctionStep>(std::move(overloads),
                                             call_expr.function(), num_args,
                                             receiver_style, expr_id);
}

}  // namespace google::api::expr::runtime
