#include "eval/eval/function_step.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "common/expr.h"
#include "common/function_descriptor.h"
#include "common/kind.h"
#include "common/type.h"
#include "common/value.h"
#include "common/value_kind.h"
#include "eval/eval/attribute_trail.h"
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
#include "runtime/internal/function_matching.h"

namespace google::api::expr::runtime {

namespace {

using ::cel::ErrorValue;
using ::cel::UnknownValue;
using ::cel::Value;
using ::cel::ValueKindToKind;
using ::cel::runtime_internal::CreateNoMatchingOverloadError;

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
      result.push_back(frame->attribute_utility().CreateUnknownSet(
          trail.attribute(), frame->arena()));
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

  absl::StatusOr<Value> result = overload.implementation.Invoke(args, context);
  if (!result.ok()) {
    return result;
  }

  if (frame.unknown_function_results_enabled() &&
      IsUnknownFunctionResultError(*result)) {
    return frame.attribute_utility().CreateUnknownSet(
        overload.descriptor, expr_id, args, frame.arena());
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
    if (arg.IsError()) {
      return arg;
    }
  }

  if (frame.unknown_processing_enabled()) {
    // Already converted partial unknowns to unknown sets so just merge.
    absl::optional<UnknownValue> unknown_set =
        frame.attribute_utility().MergeUnknowns(args, frame.arena());
    if (unknown_set.has_value()) {
      return *unknown_set;
    }
  }

  // If no errors or unknowns in input args, create new CelError for missing
  // overload.
  if (receiver_style) {
    if (args.empty()) {
      // Should not be possible, but return a sensible error in case of logic
      // error.
      return ErrorValue::From(
          CreateNoMatchingOverloadError(absl::StrCat("().", name, "()")),
          frame.arena());
    }
    return ErrorValue::From(
        CreateNoMatchingOverloadError(absl::StrCat(
            "(",
            ToLegacyKindName(
                cel::KindToString(ValueKindToKind(args[0].kind()))),
            ").", name, CallArgTypeString(args.subspan(1)))),
        frame.arena());
  }
  return cel::ErrorValue::From(CreateNoMatchingOverloadError(
                                   absl::StrCat(name, CallArgTypeString(args))),
                               frame.arena());
}

absl::StatusOr<ResolveResult> ResolveStatic(
    absl::Span<const cel::Value> input_args, bool receiver_style,
    absl::Span<const cel::FunctionOverloadReference> overloads,
    const ExecutionFrameBase& frame, bool type_level_overload) {
  const cel::Function::InvokeContext context(
      frame.descriptor_pool(), frame.message_factory(), frame.arena());
  // The planner already orders checked IDs before deduplicated fallbacks.
  for (const auto& overload : overloads) {
    if (overload.descriptor.receiver_style() != receiver_style) continue;
    CEL_ASSIGN_OR_RETURN(
        bool accepted,
        cel::runtime_internal::MatchFunctionArguments(
            overload.descriptor, input_args, context, type_level_overload));
    if (accepted) return overload;
  }
  return std::nullopt;
}

absl::StatusOr<ResolveResult> ResolveLazy(
    absl::Span<const cel::Value> input_args, bool receiver_style,
    absl::Span<const cel::FunctionRegistry::LazyOverload> providers,
    const ExecutionFrameBase& frame, bool type_level_overload) {
  const cel::Function::InvokeContext context(
      frame.descriptor_pool(), frame.message_factory(), frame.arena());
  std::vector<cel::Type> kinds;
  kinds.reserve(input_args.size());
  for (const auto& argument : input_args) {
    kinds.emplace_back(cel::ValueKindToKind(argument.kind()));
  }
  for (const auto& provider : providers) {
    if (provider.descriptor.receiver_style() != receiver_style ||
        provider.descriptor.types().size() != input_args.size())
      continue;
    // Namespace resolution is owned by the planner. Use its canonical name,
    // which can differ from the source spelling or retain a leading dot.
    const cel::FunctionDescriptor query(provider.descriptor.name(), "",
                                        receiver_style, kinds);
    CEL_ASSIGN_OR_RETURN(
        auto overload,
        provider.provider.GetFunctionMatching(
            provider.descriptor.has_overload_id() ? provider.descriptor : query,
            frame.activation(),
            [&](const cel::FunctionOverloadReference& candidate) {
              return cel::runtime_internal::MatchFunctionArguments(
                  provider.descriptor, candidate.descriptor, input_args,
                  context, type_level_overload);
            }));
    if (overload.has_value()) return overload;
  }
  return std::nullopt;
}

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

  auto matched_function = step->ResolveFunction(input_args, frame);
  if (!matched_function.ok()) {
    frame.Abort(matched_function.status());
    return;
  }
  if (matched_function->has_value()) {
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

absl::StatusOr<ResolveResult> EagerFunctionStep::ResolveFunction(
    absl::Span<const cel::Value> input_args,
    const ExecutionFrameBase& frame) const {
  return ResolveStatic(input_args, receiver_style_, overloads_, frame,
                       type_level_overload_);
}

absl::StatusOr<ResolveResult> LazyFunctionStep::ResolveFunction(
    absl::Span<const cel::Value> input_args,
    const ExecutionFrameBase& frame) const {
  return ResolveLazy(input_args, receiver_style_, providers_, frame,
                     type_level_overload_);
}

std::unique_ptr<LazyFunctionStep> CreateLazyFunctionStep(
    const cel::CallExpr& call_expr, int64_t expr_id,
    std::vector<cel::FunctionRegistry::LazyOverload> lazy_overloads,
    size_t /*checked_overloads_count*/, bool type_level_overload) {
  bool receiver_style = call_expr.has_target();
  size_t num_args = call_expr.args().size() + (receiver_style ? 1 : 0);
  return std::make_unique<LazyFunctionStep>(
      std::move(lazy_overloads), call_expr.function(), num_args, receiver_style,
      expr_id, type_level_overload);
}

std::unique_ptr<EagerFunctionStep> CreateFunctionStep(
    const cel::CallExpr& call_expr, int64_t expr_id,
    std::vector<cel::FunctionOverloadReference> overloads,
    size_t /*checked_overloads_count*/, bool type_level_overload) {
  bool receiver_style = call_expr.has_target();
  size_t num_args = call_expr.args().size() + (receiver_style ? 1 : 0);
  return std::make_unique<EagerFunctionStep>(
      std::move(overloads), call_expr.function(), num_args, receiver_style,
      expr_id, type_level_overload);
}

}  // namespace google::api::expr::runtime
