#include "eval/eval/function_step.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
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
#include "common/type.h"
#include "common/value.h"
#include "common/value_kind.h"
#include "eval/eval/attribute_trail.h"
#include "eval/eval/direct_expression_step.h"
#include "eval/eval/evaluator_core.h"
#include "eval/eval/expression_step_base.h"
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
    cel::Kind param_kind = descriptor.kinds()[i];
    if (arg->kind() != param_kind && param_kind != cel::Kind::kAny) {
      return false;
    }
  }

  return true;
}

// Forward declarations for recursive functions
bool ValuesMatch(cel::ValueIterator& iterator, const cel::Type& value_expected,
                 const absl::optional<cel::Type>& key_expected,
                 absl::flat_hash_map<std::string, cel::Type>& bindings,
                 const ExecutionFrameBase& frame);

// Check if a single value matches expected type.
// Returns true if value's type is compatible with expected.
bool ValueMatches(const cel::Value& value, const cel::Type& expected,
                  absl::flat_hash_map<std::string, cel::Type>& bindings,
                  const ExecutionFrameBase& frame) {
  // Wildcard: expected accepts any type
  if (expected.IsDyn() || expected.IsAny()) {
    return true;
  }

  // TypeParam handling
  if (expected.IsTypeParam()) {
    std::string name(expected.GetTypeParam().name());
    auto it = bindings.find(name);
    if (it != bindings.end()) {
      // Already bound: verify value matches the bound type
      return ValueMatches(value, it->second, bindings, frame);
    }
    // Not yet bound: bind to value's runtime type
    bindings[name] = value.GetRuntimeType();
    return true;
  }

  // Get actual type from value
  cel::Type actual = value.GetRuntimeType();

  // Container handling - recursive verification
  if (expected.IsList()) {
    if (!actual.IsList()) {
      return false;
    }
    const cel::ListValue& list = value.GetList();
    auto iter_result = list.NewIterator();
    if (!iter_result.ok()) {
      return false;
    }
    cel::Type elem_expected = expected.GetList().GetElement();
    return ValuesMatch(**iter_result, elem_expected, absl::nullopt, bindings,
                       frame);
  }

  if (expected.IsMap()) {
    if (!actual.IsMap()) {
      return false;
    }
    const cel::MapValue& map = value.GetMap();
    auto iter_result = map.NewIterator();
    if (!iter_result.ok()) {
      return false;
    }
    cel::Type key_expected = expected.GetMap().GetKey();
    cel::Type val_expected = expected.GetMap().GetValue();
    return ValuesMatch(**iter_result, val_expected, key_expected, bindings,
                       frame);
  }

  if (expected.IsOptional()) {
    if (!actual.IsOptional()) {
      return false;
    }
    const cel::OptionalValue& opt = value.GetOptional();
    if (!opt.HasValue()) {
      return true;  // Empty optional matches any optional<T>
    }
    cel::OptionalType opt_type = expected.GetOptional();
    cel::TypeParameters params = opt_type.GetParameters();
    if (params.empty()) {
      return true;
    }
    return ValueMatches(opt.Value(), params[0], bindings, frame);
  }

  // Non-container types: direct type comparison
  return expected == actual;
}

// Check if all values from iterator match expected types.
// For lists: key_expected should be nullopt
// For maps: key_expected contains expected key type
bool ValuesMatch(cel::ValueIterator& iterator, const cel::Type& value_expected,
                 const absl::optional<cel::Type>& key_expected,
                 absl::flat_hash_map<std::string, cel::Type>& bindings,
                 const ExecutionFrameBase& frame) {
  while (iterator.HasNext()) {
    cel::Value key, val;
    auto next_result =
        iterator.Next2(frame.descriptor_pool(), frame.message_factory(),
                       frame.arena(), &key, &val);
    if (!next_result.ok() || !*next_result) {
      return false;  // Error during iteration
    }

    // Check key if expected
    if (key_expected.has_value()) {
      if (!ValueMatches(key, *key_expected, bindings, frame)) {
        return false;
      }
    }

    // Check value
    if (!ValueMatches(val, value_expected, bindings, frame)) {
      return false;
    }
  }
  return true;  // All elements match (or empty)
}

// Type-level argument matching with value-based verification.
// Directly uses Value.GetRuntimeType() for each argument and compares
// against descriptor.types() with full recursive verification.
// Supports TypeParam bindings and container type checking.
bool ArgumentTypesMatch(const cel::FunctionDescriptor& descriptor,
                        absl::Span<const cel::Value> arguments,
                        const ExecutionFrameBase& frame) {
  const auto& expected_types = descriptor.types();

  if (expected_types.size() != arguments.size()) {
    return false;
  }

  // Shared binding context for TypeParam consistency across all arguments
  absl::flat_hash_map<std::string, cel::Type> bindings;

  for (size_t i = 0; i < expected_types.size(); i++) {
    if (!ValueMatches(arguments[i], expected_types[i], bindings, frame)) {
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

// Implementation of ExpressionStep that finds suitable CelFunction overload and
// invokes it. Abstract base class standardizes behavior between lazy and eager
// function bindings. Derived classes provide ResolveFunction behavior.
class AbstractFunctionStep : public ExpressionStepBase {
 public:
  // Constructs FunctionStep that uses overloads specified.
  AbstractFunctionStep(const std::string& name, size_t num_arguments,
                       bool receiver_style, int64_t expr_id)
      : ExpressionStepBase(expr_id),
        name_(name),
        num_arguments_(num_arguments),
        receiver_style_(receiver_style) {}

  absl::Status Evaluate(ExecutionFrame* frame) const override;

  // Handles overload resolution and updating result appropriately.
  // Shouldn't update frame state.
  //
  // A non-ok result is an unrecoverable error, either from an illegal
  // evaluation state or forwarded from an extension function. Errors where
  // evaluation can reasonably condition are returned in the result as a
  // cel::ErrorValue.
  absl::StatusOr<Value> DoEvaluate(ExecutionFrame* frame) const;

  virtual absl::StatusOr<ResolveResult> ResolveFunction(
      absl::Span<const cel::Value> args, const ExecutionFrame* frame) const = 0;

 protected:
  std::string name_;
  size_t num_arguments_;
  bool receiver_style_;
};

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

absl::StatusOr<Value> AbstractFunctionStep::DoEvaluate(
    ExecutionFrame* frame) const {
  // Create Span object that contains input arguments to the function.
  auto input_args = frame->value_stack().GetSpan(num_arguments_);

  std::vector<cel::Value> unknowns_args;
  // Preprocess args. If an argument is partially unknown, convert it to an
  // unknown attribute set.
  if (frame->enable_unknowns()) {
    auto input_attrs = frame->value_stack().GetAttributeSpan(num_arguments_);
    unknowns_args = CheckForPartialUnknowns(frame, input_args, input_attrs);
    input_args = absl::MakeConstSpan(unknowns_args);
  }

  // Derived class resolves to a single function overload or none.
  CEL_ASSIGN_OR_RETURN(ResolveResult matched_function,
                       ResolveFunction(input_args, frame));

  // Overload found and is allowed to consume the arguments.
  if (matched_function.has_value() &&
      ShouldAcceptOverload(matched_function->descriptor, input_args)) {
    return Invoke(*matched_function, id(), input_args, *frame);
  }

  return NoOverloadResult(name_, input_args, receiver_style_, *frame);
}

absl::Status AbstractFunctionStep::Evaluate(ExecutionFrame* frame) const {
  if (!frame->value_stack().HasEnough(num_arguments_)) {
    return absl::Status(absl::StatusCode::kInternal, "Value stack underflow");
  }

  // DoEvaluate may return a status for non-recoverable errors  (e.g.
  // unexpected typing, illegal expression state). Application errors that can
  // reasonably be handled as a cel error will appear in the result value.
  CEL_ASSIGN_OR_RETURN(auto result, DoEvaluate(frame));

  frame->value_stack().PopAndPush(num_arguments_, std::move(result));

  return absl::OkStatus();
}

absl::StatusOr<ResolveResult> ResolveStatic(
    absl::Span<const cel::Value> input_args,
    absl::Span<const cel::FunctionOverloadReference> overloads,
    const ExecutionFrameBase& frame, size_t checked_overloads_count,
    bool type_level_overload) {
  // Fast path: Checked expression with a single overload resolved by
  // overload_id. Still verify runtime arguments because checked expressions can
  // contain dyn values or be evaluated with activation values that differ from
  // the checker environment.
  if (checked_overloads_count == 1) {
    bool matches =
        type_level_overload
            ? ArgumentTypesMatch(overloads[0].descriptor, input_args, frame)
            : ArgumentKindsMatch(overloads[0].descriptor, input_args);
    if (matches) {
      return overloads[0];
    }
    // Runtime mismatch -- fall through to full resolution which includes both
    // checked overloads and arity-matched fallbacks.
  }

  // Full resolution path over all candidates.
  for (const auto& overload : overloads) {
    bool matches =
        type_level_overload
            ? ArgumentTypesMatch(overload.descriptor, input_args, frame)
            : ArgumentKindsMatch(overload.descriptor, input_args);

    if (matches) {
      return overload;
    }
  }

  return std::nullopt;
}

absl::StatusOr<ResolveResult> ResolveLazy(
    absl::Span<const cel::Value> input_args, absl::string_view name,
    bool receiver_style,
    absl::Span<const cel::FunctionRegistry::LazyOverload> providers,
    const ExecutionFrameBase& frame, size_t checked_overloads_count,
    bool type_level_overload) {
  const cel::ActivationInterface& activation = frame.activation();

  // Phase A: Collect overloads from providers.
  std::vector<cel::FunctionOverloadReference> overloads;

  if (checked_overloads_count > 0) {
    // Checked path: use provider.descriptor (which carries overload_id) for
    // precise lookup.
    for (const auto& provider : providers) {
      CEL_ASSIGN_OR_RETURN(auto overload, provider.provider.GetFunction(
                                              provider.descriptor, activation));
      if (overload.has_value()) {
        overloads.push_back(overload.value());
      }
    }
  } else {
    // Parse-only: pre-filter by ArgumentKindsMatch, then collect overloads.
    for (const auto& provider : providers) {
      if (!ArgumentKindsMatch(provider.descriptor, input_args)) {
        continue;
      }

      CEL_ASSIGN_OR_RETURN(auto overload, provider.provider.GetFunction(
                                              provider.descriptor, activation));

      if (overload.has_value()) {
        overloads.push_back(overload.value());
      }
    }
  }

  // Phase B: Match arguments and select overload.
  for (const auto& overload : overloads) {
    bool matches =
        type_level_overload
            ? ArgumentTypesMatch(overload.descriptor, input_args, frame)
            : ArgumentKindsMatch(overload.descriptor, input_args);

    if (matches) {
      return overload;
    }
  }

  return std::nullopt;
}

class EagerFunctionStep : public AbstractFunctionStep {
 public:
  EagerFunctionStep(std::vector<cel::FunctionOverloadReference> overloads,
                    const std::string& name, size_t num_args,
                    bool receiver_style, int64_t expr_id,
                    size_t checked_overloads_count, bool type_level_overload)
      : AbstractFunctionStep(name, num_args, receiver_style, expr_id),
        overloads_(std::move(overloads)),
        checked_overloads_count_(checked_overloads_count),
        type_level_overload_(type_level_overload) {}

  absl::StatusOr<ResolveResult> ResolveFunction(
      absl::Span<const cel::Value> input_args,
      const ExecutionFrame* frame) const override {
    return ResolveStatic(input_args, overloads_, *frame,
                         checked_overloads_count_, type_level_overload_);
  }

 private:
  std::vector<cel::FunctionOverloadReference> overloads_;
  size_t checked_overloads_count_;
  bool type_level_overload_;
};

class LazyFunctionStep : public AbstractFunctionStep {
 public:
  LazyFunctionStep(const std::string& name, size_t num_args,
                   bool receiver_style,
                   std::vector<cel::FunctionRegistry::LazyOverload> providers,
                   int64_t expr_id, size_t checked_overloads_count,
                   bool type_level_overload)
      : AbstractFunctionStep(name, num_args, receiver_style, expr_id),
        providers_(std::move(providers)),
        checked_overloads_count_(checked_overloads_count),
        type_level_overload_(type_level_overload) {}

  absl::StatusOr<ResolveResult> ResolveFunction(
      absl::Span<const cel::Value> input_args,
      const ExecutionFrame* frame) const override;

 private:
  std::vector<cel::FunctionRegistry::LazyOverload> providers_;
  size_t checked_overloads_count_;
  bool type_level_overload_;
};

absl::StatusOr<ResolveResult> LazyFunctionStep::ResolveFunction(
    absl::Span<const cel::Value> input_args,
    const ExecutionFrame* frame) const {
  return ResolveLazy(input_args, name_, receiver_style_, providers_, *frame,
                     checked_overloads_count_, type_level_overload_);
}

class StaticResolver {
 public:
  using ResolveResult = absl::optional<cel::FunctionOverloadReference>;

  StaticResolver(std::vector<cel::FunctionOverloadReference> overloads,
                 size_t checked_overloads_count, bool type_level_overload)
      : overloads_(std::move(overloads)),
        checked_overloads_count_(checked_overloads_count),
        type_level_overload_(type_level_overload) {}

  absl::StatusOr<ResolveResult> Resolve(ExecutionFrameBase& frame,
                                        absl::Span<const Value> input) const {
    return ResolveStatic(input, overloads_, frame, checked_overloads_count_,
                         type_level_overload_);
  }

 private:
  std::vector<cel::FunctionOverloadReference> overloads_;
  size_t checked_overloads_count_;
  bool type_level_overload_;
};

class LazyResolver {
 public:
  using ResolveResult = absl::optional<cel::FunctionOverloadReference>;

  LazyResolver(std::vector<cel::FunctionRegistry::LazyOverload> providers,
               std::string name, bool receiver_style,
               size_t checked_overloads_count, bool type_level_overload)
      : providers_(std::move(providers)),
        name_(std::move(name)),
        receiver_style_(receiver_style),
        checked_overloads_count_(checked_overloads_count),
        type_level_overload_(type_level_overload) {}

  absl::StatusOr<ResolveResult> Resolve(ExecutionFrameBase& frame,
                                        absl::Span<const Value> input) const {
    return ResolveLazy(input, name_, receiver_style_, providers_, frame,
                       checked_overloads_count_, type_level_overload_);
  }

 private:
  std::vector<cel::FunctionRegistry::LazyOverload> providers_;
  std::string name_;
  bool receiver_style_;
  size_t checked_overloads_count_;
  bool type_level_overload_;
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

std::unique_ptr<DirectExpressionStep> CreateDirectFunctionStep(
    int64_t expr_id, const cel::CallExpr& call,
    std::vector<std::unique_ptr<DirectExpressionStep>> deps,
    std::vector<cel::FunctionOverloadReference> overloads,
    size_t checked_overloads_count, bool type_level_overload) {
  return std::make_unique<DirectFunctionStepImpl<StaticResolver>>(
      expr_id, call.function(), std::move(deps), call.has_target(),
      StaticResolver(std::move(overloads), checked_overloads_count,
                     type_level_overload));
}

std::unique_ptr<DirectExpressionStep> CreateDirectLazyFunctionStep(
    int64_t expr_id, const cel::CallExpr& call,
    std::vector<std::unique_ptr<DirectExpressionStep>> deps,
    std::vector<cel::FunctionRegistry::LazyOverload> providers,
    size_t checked_overloads_count, bool type_level_overload) {
  return std::make_unique<DirectFunctionStepImpl<LazyResolver>>(
      expr_id, call.function(), std::move(deps), call.has_target(),
      LazyResolver(std::move(providers), call.function(), call.has_target(),
                   checked_overloads_count, type_level_overload));
}

absl::StatusOr<std::unique_ptr<ExpressionStep>> CreateFunctionStep(
    const cel::CallExpr& call_expr, int64_t expr_id,
    std::vector<cel::FunctionRegistry::LazyOverload> lazy_overloads,
    size_t checked_overloads_count, bool type_level_overload) {
  bool receiver_style = call_expr.has_target();
  size_t num_args = call_expr.args().size() + (receiver_style ? 1 : 0);
  const std::string& name = call_expr.function();
  return std::make_unique<LazyFunctionStep>(
      name, num_args, receiver_style, std::move(lazy_overloads), expr_id,
      checked_overloads_count, type_level_overload);
}

absl::StatusOr<std::unique_ptr<ExpressionStep>> CreateFunctionStep(
    const cel::CallExpr& call_expr, int64_t expr_id,
    std::vector<cel::FunctionOverloadReference> overloads,
    size_t checked_overloads_count, bool type_level_overload) {
  bool receiver_style = call_expr.has_target();
  size_t num_args = call_expr.args().size() + (receiver_style ? 1 : 0);
  const std::string& name = call_expr.function();
  return std::make_unique<EagerFunctionStep>(
      std::move(overloads), name, num_args, receiver_style, expr_id,
      checked_overloads_count, type_level_overload);
}

}  // namespace google::api::expr::runtime
