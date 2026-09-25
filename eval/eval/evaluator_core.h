// Copyright 2017 Google LLC
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

#ifndef THIRD_PARTY_CEL_CPP_EVAL_EVAL_EVALUATOR_CORE_H_
#define THIRD_PARTY_CEL_CPP_EVAL_EVAL_EVALUATOR_CORE_H_

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "base/type_provider.h"
#include "common/native_type.h"
#include "common/value.h"
#include "common/value_kind.h"
#include "common/values/list_value_builder.h"
#include "eval/eval/attribute_utility.h"
#include "eval/eval/comprehension_slots.h"
#include "eval/eval/comprehension_step.h"
#include "eval/eval/direct_expression_step.h"
#include "eval/eval/equality_steps.h"
#include "eval/eval/evaluator_stack.h"
#include "eval/eval/expression_step_logic.h"
#include "eval/eval/function_step.h"
#include "eval/eval/ident_step.h"
#include "eval/eval/iterator_stack.h"
#include "eval/eval/lazy_init_step.h"
#include "eval/eval/logic_step.h"
#include "runtime/activation_interface.h"
#include "runtime/internal/activation_attribute_matcher_access.h"
#include "runtime/internal/errors.h"
#include "runtime/runtime.h"
#include "runtime/runtime_options.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"

namespace cel {
class EmbedderContext;
}  // namespace cel

namespace google::api::expr::runtime {

// Forward declaration of ExecutionFrame, to resolve circular dependency.
class ExecutionFrame;

using EvaluationListener = cel::TraceableProgram::EvaluationListener;

enum class ExpressionStepKind : uint16_t {
  kMovedFrom = 0,
  kGenericLogic = 1,
  kIntConstant = 2,
  kBoolConstant = 3,
  kDoubleConstant = 4,
  kNullConstant = 5,
  kUintConstant = 6,
  // Any constant that can't be inlined.
  kOtherConstant = 7,
  // Lazy subexpressions.
  kLazyInit = 8,
  kAssignSlotAndPop = 9,
  kClearSlots = 10,
  // Core boolean logic.
  kBooleanNot = 11,
  kNotStrictlyFalse = 12,
  kBooleanOr = 13,
  kBooleanAnd = 14,
  // Comprehension steps.
  // Init step doesn't fit inline and is slow anyway, so it uses a generic step.
  kComprehensionFinish = 15,
  kComprehensionNext = 16,
  kComprehensionCond = 17,
  kComprehensionNext2 = 18,
  kComprehensionCond2 = 19,
  kReadSlot = 20,
  // Jump steps.
  kBooleanOrJump = 21,
  kBooleanAndJump = 22,
  kTernaryJump = 23,
  kFixedJump = 24,
  // Identifier
  kIdentifier = 25,
  // Functions calls.
  kEagerFunction = 26,
  kLazyFunction = 27,
  // fast built-ins. These are used if we know they haven't been extended.
  // otherwise we use normal function call steps.
  kFastIn = 28,
  kFastEqual = 29,
  kFastNotEqual = 30,
  // Special built-in steps for mutable lists implementing map/filter.
  kNewMutableList = 31,
  kMutableListAppend = 32,
};

struct BoolJumpStepInfo {
  size_t arg_count : 32;
  bool set : 1;
  int offset : 31;
};

struct TernaryJumpStepInfo {
  bool set : 1;
  int error_offset : 31;
  int jump_to_second_offset : 32;
};

struct FixedJumpStepInfo {
  bool set : 1;
  int32_t reserved : 31;
  int offset : 32;
};

class ExpressionStep {
 public:
  // Move-only.
  ExpressionStep(const ExpressionStep&) = delete;
  ExpressionStep& operator=(const ExpressionStep&) = delete;
  ExpressionStep(ExpressionStep&&);
  ExpressionStep& operator=(ExpressionStep&&);

  ~ExpressionStep();

  // Returns corresponding expression object ID.
  // Requires that the input expression has IDs assigned to sub-expressions,
  // e.g. via a checker. The default value 0 is returned if there is no
  // expression associated (e.g. a jump step), or if there is no ID assigned to
  // the corresponding expression. Useful for error scenarios where information
  // from Expr object is needed to create CelError.
  int64_t id() const {
    return header_.id >= 0 ? static_cast<int64_t>(header_.id) : -1;
  }

  // Returns if the execution step comes from AST.
  bool comes_from_ast() const { return header_.id >= 0; }

  // Evaluates this step on the given execution frame.
  ABSL_ATTRIBUTE_ALWAYS_INLINE inline void Evaluate(
      ExecutionFrame& frame) const;

  const ExpressionStepLogic* GetGenericStep() const;
  bool IsGenericStep() const;

  static ExpressionStep MakeGenericStep(
      std::unique_ptr<ExpressionStepLogic> logic, int64_t id = -1) {
    ExpressionStep step(ExpressionStepKind::kGenericLogic, id);
    step.u_.logic = std::move(logic);
    return step;
  }

  static ExpressionStep MakeConstant(const cel::Value& value, int64_t id = -1);

  static ExpressionStep MakeLazyInitStep(size_t slot_index,
                                         size_t subexpression_index,
                                         int64_t id = -1) {
    ExpressionStep step(ExpressionStepKind::kLazyInit, id);
    ABSL_DCHECK_LT(slot_index, std::numeric_limits<uint32_t>::max());
    ABSL_DCHECK_LT(subexpression_index, std::numeric_limits<uint32_t>::max());
    step.u_.lazy_init = LazyInitStepInfo{slot_index, subexpression_index};
    return step;
  }

  static ExpressionStep MakeAssignSlotAndPopStep(size_t slot_index,
                                                 int64_t id = -1) {
    ExpressionStep step(ExpressionStepKind::kAssignSlotAndPop, id);
    ABSL_DCHECK_LT(slot_index, std::numeric_limits<uint32_t>::max());
    step.u_.slot_index = slot_index;
    return step;
  }

  static ExpressionStep MakeClearSlotStep(size_t slot_index, int64_t id = -1) {
    return MakeClearSlotsStep(slot_index, 1, id);
  }

  static ExpressionStep MakeClearSlotsStep(size_t slot_index, size_t slot_count,
                                           int64_t id = -1) {
    ExpressionStep step(ExpressionStepKind::kClearSlots, id);
    ABSL_DCHECK_LT(slot_index, std::numeric_limits<uint32_t>::max());
    ABSL_DCHECK_LT(slot_count, std::numeric_limits<uint32_t>::max());
    step.u_.clear_slots = ClearSlotStepInfo{slot_index, slot_count};
    return step;
  }

  static ExpressionStep MakeBooleanNotStep(int64_t id = -1) {
    return ExpressionStep(ExpressionStepKind::kBooleanNot, id);
  }

  static ExpressionStep MakeNotStrictlyFalseStep(int64_t id = -1) {
    return ExpressionStep(ExpressionStepKind::kNotStrictlyFalse, id);
  }

  static ExpressionStep MakeBooleanOrStep(size_t num_args, int64_t id = -1) {
    ExpressionStep step(ExpressionStepKind::kBooleanOr, id);
    ABSL_DCHECK_LT(num_args, std::numeric_limits<uint32_t>::max());
    step.u_.arg_count = num_args;
    return step;
  }

  static ExpressionStep MakeBooleanAndStep(size_t num_args, int64_t id = -1) {
    ExpressionStep step(ExpressionStepKind::kBooleanAnd, id);
    ABSL_DCHECK_LT(num_args, std::numeric_limits<uint32_t>::max());
    step.u_.arg_count = num_args;
    return step;
  }

  static ExpressionStep MakeComprehensionFinishStep(size_t accu_slot,
                                                    int64_t id = -1) {
    ExpressionStep step(ExpressionStepKind::kComprehensionFinish, id);
    step.u_.slot_index = accu_slot;
    return step;
  }

  static ExpressionStep MakeComprehensionNextStep(int64_t id = -1) {
    ExpressionStep step(ExpressionStepKind::kComprehensionNext, id);
    step.u_.next_step = ComprehensionNextStep();
    return step;
  }

  static ExpressionStep MakeComprehensionNext2Step(int64_t id = -1) {
    ExpressionStep step(ExpressionStepKind::kComprehensionNext2, id);
    step.u_.next_step = ComprehensionNextStep();
    return step;
  }

  static ExpressionStep MakeComprehensionCondStep(int64_t id = -1) {
    ExpressionStep step(ExpressionStepKind::kComprehensionCond, id);
    step.u_.cond_step = ComprehensionCondStep();
    return step;
  }

  static ExpressionStep MakeComprehensionCond2Step(int64_t id = -1) {
    ExpressionStep step(ExpressionStepKind::kComprehensionCond2, id);
    step.u_.cond_step = ComprehensionCondStep();
    return step;
  }

  static ExpressionStep MakeReadSlotStep(size_t slot_index, int64_t id = -1) {
    ExpressionStep step(ExpressionStepKind::kReadSlot, id);
    ABSL_DCHECK_LT(slot_index, std::numeric_limits<uint32_t>::max());
    step.u_.slot_index = slot_index;
    return step;
  }

  static ExpressionStep MakeBooleanOrJumpStep(size_t arg_count,
                                              int64_t id = -1) {
    ExpressionStep step(ExpressionStepKind::kBooleanOrJump, id);
    ABSL_DCHECK_LT(arg_count, std::numeric_limits<uint32_t>::max());
    step.u_.bool_jump_step = BoolJumpStepInfo{arg_count, false, 0};
    return step;
  }

  static ExpressionStep MakeBooleanAndJumpStep(size_t arg_count,
                                               int64_t id = -1) {
    ExpressionStep step(ExpressionStepKind::kBooleanAndJump, id);
    ABSL_DCHECK_LT(arg_count, std::numeric_limits<uint32_t>::max());
    step.u_.bool_jump_step = BoolJumpStepInfo{arg_count, false, 0};
    return step;
  }

  static ExpressionStep MakeTernaryJumpStep(int64_t id = -1) {
    ExpressionStep step(ExpressionStepKind::kTernaryJump, id);
    step.u_.ternary_jump_step = TernaryJumpStepInfo{false, 0, 0};
    return step;
  }

  static ExpressionStep MakeFixedJumpStep(int64_t id = -1) {
    ExpressionStep step(ExpressionStepKind::kFixedJump, id);
    step.u_.fixed_jump_step = FixedJumpStepInfo{false, 0, 0};
    return step;
  }

  static ExpressionStep MakeIdentifierStep(absl::string_view identifier,
                                           int64_t id = -1) {
    ExpressionStep step(ExpressionStepKind::kIdentifier, id);
    step.u_.identifier = std::make_unique<std::string>(identifier);
    return step;
  }

  static ExpressionStep MakeIdentStep(absl::string_view identifier,
                                      int64_t id = -1) {
    return MakeIdentifierStep(identifier, id);
  }

  static ExpressionStep MakeFastInStep(int64_t id = -1) {
    return ExpressionStep(ExpressionStepKind::kFastIn, id);
  }

  static ExpressionStep MakeFastEqualStep(int64_t id = -1) {
    return ExpressionStep(ExpressionStepKind::kFastEqual, id);
  }

  static ExpressionStep MakeFastNotEqualStep(int64_t id = -1) {
    return ExpressionStep(ExpressionStepKind::kFastNotEqual, id);
  }

  static ExpressionStep MakeNewMutableListStep(int64_t id = -1) {
    return ExpressionStep(ExpressionStepKind::kNewMutableList, id);
  }

  static ExpressionStep MakeMutableListAppendStep(int64_t id = -1) {
    return ExpressionStep(ExpressionStepKind::kMutableListAppend, id);
  }

  static ExpressionStep MakeEagerFunctionStep(
      std::unique_ptr<EagerFunctionStep> step_impl, int64_t id = -1) {
    ExpressionStep step(ExpressionStepKind::kEagerFunction, id);
    step.u_.eager_function_step = std::move(step_impl);
    return step;
  }

  static ExpressionStep MakeLazyFunctionStep(
      std::unique_ptr<LazyFunctionStep> step_impl, int64_t id = -1) {
    ExpressionStep step(ExpressionStepKind::kLazyFunction, id);
    step.u_.lazy_function_step = std::move(step_impl);
    return step;
  }

 private:
  static ABSL_ATTRIBUTE_ALWAYS_INLINE inline void EvaluateReadSlotStep(
      size_t slot_index, ExecutionFrame& frame);
  static ABSL_ATTRIBUTE_ALWAYS_INLINE inline void EvaluateBoolJumpStep(
      const BoolJumpStepInfo& step, bool target, ExecutionFrame& frame);
  static ABSL_ATTRIBUTE_ALWAYS_INLINE inline void EvaluateTernaryJumpStep(
      const TernaryJumpStepInfo& step, ExecutionFrame& frame);
  static void EvaluateMutableListAppendStep(ExecutionFrame& frame);

  struct Header {
    ExpressionStepKind kind;
    uint16_t reserved;
    int32_t id;
  };

  ExpressionStep() : header_{ExpressionStepKind::kMovedFrom, 0, -1} {}
  ExpressionStep(ExpressionStepKind kind, int32_t id) : header_{kind, 0, id} {
    header_ = {kind, 0, id};
  }
  ExpressionStep(ExpressionStepKind kind, int64_t id) : ExpressionStep() {
    if (id < 0 || id > std::numeric_limits<int32_t>::max()) {
      id = -1;
    }
    header_ = {kind, 0, static_cast<int32_t>(id)};
  }
  ExpressionStep(ExpressionStepKind kind, int32_t id,
                 std::unique_ptr<ExpressionStepLogic> logic)
      : ExpressionStep(kind, id) {
    u_.logic = std::move(logic);
  }

  static void SwapToEmpty(ExpressionStep& step, ExpressionStep& empty_step);

  friend void swap(ExpressionStep& lhs, ExpressionStep& rhs) {
    ExpressionStep tmp;
    SwapToEmpty(lhs, tmp);
    SwapToEmpty(rhs, lhs);
    SwapToEmpty(tmp, rhs);
  }

  friend bool GetIfConstant(const ExpressionStep& step, cel::Value& out);
  friend bool IsConstant(const ExpressionStep& step);
  friend ComprehensionCondStep* GetIfComprehensionCondStep(
      ExpressionStep& step);
  friend ComprehensionNextStep* GetIfComprehensionNextStep(
      ExpressionStep& step);
  friend BoolJumpStepInfo* GetIfBoolJumpStep(ExpressionStep& step);
  friend TernaryJumpStepInfo* GetIfTernaryJumpStep(ExpressionStep& step);
  friend FixedJumpStepInfo* GetIfFixedJumpStep(ExpressionStep& step);

  Header header_;
  union Data {
    std::nullptr_t empty;
    std::unique_ptr<ExpressionStepLogic> logic;
    int64_t int_val;
    uint64_t uint_val;
    double double_val;
    bool bool_val;
    std::unique_ptr<cel::Value> other_val;
    LazyInitStepInfo lazy_init;
    size_t slot_index;
    ClearSlotStepInfo clear_slots;
    size_t arg_count;
    ComprehensionCondStep cond_step;
    ComprehensionNextStep next_step;
    BoolJumpStepInfo bool_jump_step;
    TernaryJumpStepInfo ternary_jump_step;
    FixedJumpStepInfo fixed_jump_step;
    std::unique_ptr<EagerFunctionStep> eager_function_step;
    std::unique_ptr<LazyFunctionStep> lazy_function_step;
    std::unique_ptr<std::string> identifier;

    Data() : empty(nullptr) {}
    ~Data() {}
  } u_;
};

#ifndef _MSC_VER
// Keep the core instruction size small to get better memory locality for the
// main program.
//
// MSVC does not support some of the bit-field packing used here so it will be
// larger.
static_assert(sizeof(ExpressionStep) == 16);
#endif

// Wrapper for direct steps to work with the stack machine impl.
class WrappedDirectStep : public ExpressionStepLogic {
 public:
  explicit WrappedDirectStep(std::unique_ptr<DirectExpressionStep> impl,
                             int64_t expr_id = -1)
      : impl_(std::move(impl)) {}

  void Evaluate(ExecutionFrame* frame) const override;

  cel::NativeTypeId GetNativeTypeId() const override {
    return cel::NativeTypeId::For<WrappedDirectStep>();
  }

  const DirectExpressionStep* wrapped() const { return impl_.get(); }

 private:
  std::unique_ptr<DirectExpressionStep> impl_;
};

using ExecutionPath = std::vector<ExpressionStep>;
using ExecutionPathView = absl::Span<const ExpressionStep>;

// Class that wraps the state that needs to be allocated for expression
// evaluation. This can be reused to save on allocations.
class FlatExpressionEvaluatorState {
 public:
  FlatExpressionEvaluatorState(
      size_t value_stack_size, size_t comprehension_slot_count,
      const cel::TypeProvider& type_provider,
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena)
      : value_stack_(value_stack_size),
        // We currently use comprehension_slot_count because it is less of an
        // over estimate than value_stack_size. In future we should just
        // calculate the correct capacity.
        iterator_stack_(comprehension_slot_count),
        comprehension_slots_(comprehension_slot_count),
        type_provider_(type_provider),
        descriptor_pool_(descriptor_pool),
        message_factory_(message_factory),
        arena_(arena) {}

  void Reset();

  EvaluatorStack& value_stack() { return value_stack_; }

  cel::runtime_internal::IteratorStack& iterator_stack() {
    return iterator_stack_;
  }

  ComprehensionSlots& comprehension_slots() { return comprehension_slots_; }

  const cel::TypeProvider& type_provider() { return type_provider_; }

  const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool() {
    return descriptor_pool_;
  }

  google::protobuf::MessageFactory* absl_nonnull message_factory() {
    return message_factory_;
  }

  google::protobuf::Arena* absl_nonnull arena() { return arena_; }

 private:
  EvaluatorStack value_stack_;
  cel::runtime_internal::IteratorStack iterator_stack_;
  ComprehensionSlots comprehension_slots_;
  const cel::TypeProvider& type_provider_;
  const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool_;
  google::protobuf::MessageFactory* absl_nonnull message_factory_;
  google::protobuf::Arena* absl_nonnull arena_;
};

// Context needed for evaluation. This is sufficient for supporting
// recursive evaluation, but stack machine programs require an
// ExecutionFrame instance for managing a heap-backed stack.
class ExecutionFrameBase {
 public:
  // Overload for test usages.
  ExecutionFrameBase(const cel::ActivationInterface& activation,
                     const cel::RuntimeOptions& options,
                     const cel::TypeProvider& type_provider,
                     const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
                     google::protobuf::MessageFactory* absl_nonnull message_factory,
                     google::protobuf::Arena* absl_nonnull arena)
      : activation_(&activation),
        callback_(),
        options_(&options),
        type_provider_(type_provider),
        descriptor_pool_(descriptor_pool),
        message_factory_(message_factory),
        arena_(arena),
        embedder_context_(nullptr),
        attribute_utility_(activation.GetUnknownAttributes(),
                           activation.GetMissingAttributes()),
        slots_(&ComprehensionSlots::GetEmptyInstance()),
        max_iterations_(options.comprehension_max_iterations),
        iterations_(0),
        attribute_tracking_enabled_(
            options_->unknown_processing !=
                cel::UnknownProcessingOptions::kDisabled ||
            options_->enable_missing_attribute_errors),
        missing_attribute_errors_enabled_(
            options_->enable_missing_attribute_errors),
        unknown_processing_enabled_(options_->unknown_processing !=
                                    cel::UnknownProcessingOptions::kDisabled),
        unknown_function_results_enabled_(
            options_->unknown_processing ==
            cel::UnknownProcessingOptions::kAttributeAndFunction) {
    if (unknown_processing_enabled()) {
      if (auto matcher = cel::runtime_internal::
              ActivationAttributeMatcherAccess::GetAttributeMatcher(activation);
          matcher != nullptr) {
        attribute_utility_.set_matcher(matcher);
      }
    }
  }

  ExecutionFrameBase(const cel::ActivationInterface& activation,
                     EvaluationListener callback,
                     const cel::RuntimeOptions& options,
                     const cel::TypeProvider& type_provider,
                     const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
                     google::protobuf::MessageFactory* absl_nonnull message_factory,
                     google::protobuf::Arena* absl_nonnull arena,
                     const cel::EmbedderContext* absl_nullable embedder_context,
                     ComprehensionSlots& slots)
      : activation_(&activation),
        callback_(std::move(callback)),
        options_(&options),
        type_provider_(type_provider),
        descriptor_pool_(descriptor_pool),
        message_factory_(message_factory),
        arena_(arena),
        embedder_context_(embedder_context),
        attribute_utility_(activation.GetUnknownAttributes(),
                           activation.GetMissingAttributes()),
        slots_(&slots),
        max_iterations_(options.comprehension_max_iterations),
        iterations_(0),
        attribute_tracking_enabled_(
            options_->unknown_processing !=
                cel::UnknownProcessingOptions::kDisabled ||
            options_->enable_missing_attribute_errors),
        missing_attribute_errors_enabled_(
            options_->enable_missing_attribute_errors),
        unknown_processing_enabled_(options_->unknown_processing !=
                                    cel::UnknownProcessingOptions::kDisabled),
        unknown_function_results_enabled_(
            options_->unknown_processing ==
            cel::UnknownProcessingOptions::kAttributeAndFunction) {
    if (unknown_processing_enabled()) {
      if (auto matcher = cel::runtime_internal::
              ActivationAttributeMatcherAccess::GetAttributeMatcher(activation);
          matcher != nullptr) {
        attribute_utility_.set_matcher(matcher);
      }
    }
  }

  const cel::ActivationInterface& activation() const { return *activation_; }

  EvaluationListener& callback() { return callback_; }

  const cel::RuntimeOptions& options() const { return *options_; }

  const cel::TypeProvider& type_provider() { return type_provider_; }

  const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool() const {
    return descriptor_pool_;
  }

  google::protobuf::MessageFactory* absl_nonnull message_factory() const {
    return message_factory_;
  }

  google::protobuf::Arena* absl_nonnull arena() const { return arena_; }

  const cel::EmbedderContext* absl_nullable embedder_context() const {
    return embedder_context_;
  }

  const AttributeUtility& attribute_utility() const {
    return attribute_utility_;
  }

  bool attribute_tracking_enabled() const {
    return attribute_tracking_enabled_;
  }

  bool missing_attribute_errors_enabled() const {
    return missing_attribute_errors_enabled_;
  }

  bool unknown_processing_enabled() const {
    return unknown_processing_enabled_;
  }

  bool unknown_function_results_enabled() const {
    return unknown_function_results_enabled_;
  }

  ComprehensionSlots& comprehension_slots() { return *slots_; }

  // Increment iterations and return an error if the iteration budget is
  // exceeded
  absl::Status IncrementIterations() {
    if (max_iterations_ == 0) {
      return absl::OkStatus();
    }
    iterations_++;
    if (iterations_ >= max_iterations_) {
      return absl::Status(absl::StatusCode::kInternal,
                          "Iteration budget exceeded");
    }
    return absl::OkStatus();
  }

  absl::Status& abort_status() { return abort_status_; }

 protected:
  const cel::ActivationInterface* absl_nonnull activation_;
  EvaluationListener callback_;
  const cel::RuntimeOptions* absl_nonnull options_;
  const cel::TypeProvider& type_provider_;
  const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool_;
  google::protobuf::MessageFactory* absl_nonnull message_factory_;
  google::protobuf::Arena* absl_nonnull arena_;
  const cel::EmbedderContext* absl_nullable embedder_context_;
  AttributeUtility attribute_utility_;
  ComprehensionSlots* absl_nonnull slots_;
  const int max_iterations_;
  int iterations_;
  absl::Status abort_status_;
  const bool attribute_tracking_enabled_;
  const bool missing_attribute_errors_enabled_;
  const bool unknown_processing_enabled_;
  const bool unknown_function_results_enabled_;
};

// ExecutionFrame manages the context needed for expression evaluation.
// The lifecycle of the object is bound to a FlateExpression::Evaluate*(...)
// call.
class ExecutionFrame : public ExecutionFrameBase {
 public:
  // flat is the flattened sequence of execution steps that will be evaluated.
  // activation provides bindings between parameter names and values.
  // state contains the value factory for evaluation and the allocated data
  //   structures needed for evaluation.
  ExecutionFrame(
      ExecutionPathView flat, const cel::ActivationInterface& activation,
      const cel::RuntimeOptions& options, FlatExpressionEvaluatorState& state,
      EvaluationListener callback = EvaluationListener(),
      const cel::EmbedderContext* absl_nullable embedder_context = nullptr)
      : ExecutionFrameBase(activation, std::move(callback), options,
                           state.type_provider(), state.descriptor_pool(),
                           state.message_factory(), state.arena(),
                           embedder_context, state.comprehension_slots()),
        pc_(0UL),
        execution_path_(flat),
        value_stack_(&state.value_stack()),
        iterator_stack_(&state.iterator_stack()),
        subexpressions_(&execution_path_, 1) {}

  ExecutionFrame(
      absl::Span<const ExecutionPathView> subexpressions,
      const cel::ActivationInterface& activation,
      const cel::RuntimeOptions& options, FlatExpressionEvaluatorState& state,
      EvaluationListener callback = EvaluationListener(),
      const cel::EmbedderContext* absl_nullable embedder_context = nullptr)
      : ExecutionFrameBase(activation, std::move(callback), options,
                           state.type_provider(), state.descriptor_pool(),
                           state.message_factory(), state.arena(),
                           embedder_context, state.comprehension_slots()),
        pc_(0UL),
        execution_path_(subexpressions[0]),
        value_stack_(&state.value_stack()),
        iterator_stack_(&state.iterator_stack()),
        subexpressions_(subexpressions) {
    ABSL_DCHECK(!subexpressions.empty());
  }

  // Returns next expression to evaluate.
  ABSL_ATTRIBUTE_ALWAYS_INLINE const ExpressionStep* Next() {
    if (ABSL_PREDICT_TRUE(pc_ < execution_path_.size())) {
      const ExpressionStep* step = &execution_path_[pc_++];
      ABSL_ASSUME(step != nullptr);
      return step;
    }
    return NextSlow();
  }

  // Evaluate the execution frame to completion.
  absl::StatusOr<cel::Value> Evaluate(EvaluationListener& listener);
  // Evaluate the execution frame to completion.
  absl::StatusOr<cel::Value> Evaluate() { return Evaluate(callback()); }

  // Intended for use in builtin shortcutting operations.
  //
  // Offset applies after normal pc increment. For example, JumpTo(0) is a
  // no-op, JumpTo(1) skips the expected next step.
  absl::Status JumpTo(int offset) {
    ABSL_DCHECK_LE(offset, static_cast<int>(execution_path_.size()));
    ABSL_DCHECK_GE(offset, -static_cast<int>(pc_));

    int new_pc = static_cast<int>(pc_) + offset;
    if (new_pc < 0 || new_pc > static_cast<int>(execution_path_.size())) {
      return absl::Status(absl::StatusCode::kInternal,
                          absl::StrCat("Jump address out of range: position: ",
                                       pc_, ", offset: ", offset,
                                       ", range: ", execution_path_.size()));
    }
    pc_ = static_cast<size_t>(new_pc);
    return absl::OkStatus();
  }

  ABSL_ATTRIBUTE_ALWAYS_INLINE void JumpToOrAbort(int offset) {
    ABSL_DCHECK_LE(offset, static_cast<int>(execution_path_.size()));
    ABSL_DCHECK_GE(offset, -static_cast<int>(pc_));

    int new_pc = static_cast<int>(pc_) + offset;
    if (ABSL_PREDICT_FALSE(new_pc < 0 ||
                           new_pc > static_cast<int>(execution_path_.size()))) {
      AbortJumpOutOfRange(offset);
      return;
    }
    pc_ = static_cast<size_t>(new_pc);
  }

  // Move pc to a subexpression.
  //
  // Unlike a `Call` in a programming language, the subexpression is evaluated
  // in the same context as the caller (e.g. no stack isolation or scope change)
  //
  // Only intended for use in built-in notion of lazily evaluated
  // subexpressions.
  void Call(size_t slot_index, size_t subexpression_index) {
    ABSL_DCHECK_LT(subexpression_index, subexpressions_.size());
    ExecutionPathView subexpression = subexpressions_[subexpression_index];
    ABSL_DCHECK(subexpression.data() != execution_path_.data());
    size_t return_pc = pc_;
    // return pc == size() is supported (a tail call).
    ABSL_DCHECK_LE(return_pc, execution_path_.size());
    call_stack_.push_back(SubFrame{return_pc, slot_index, execution_path_,
                                   value_stack().size() + 1});
    pc_ = 0UL;
    execution_path_ = subexpression;
  }

  EvaluatorStack& value_stack() { return *value_stack_; }

  cel::runtime_internal::IteratorStack& iterator_stack() {
    return *iterator_stack_;
  }

  bool enable_attribute_tracking() const {
    return attribute_tracking_enabled();
  }

  bool enable_unknowns() const { return unknown_processing_enabled(); }

  bool enable_unknown_function_results() const {
    return unknown_function_results_enabled();
  }

  bool enable_missing_attribute_errors() const {
    return missing_attribute_errors_enabled();
  }

  bool enable_heterogeneous_numeric_lookups() const {
    return options().enable_heterogeneous_equality;
  }

  bool enable_comprehension_list_append() const {
    return options().enable_comprehension_list_append;
  }

  // Returns reference to the modern API activation.
  const cel::ActivationInterface& modern_activation() const {
    return *activation_;
  }

  void Abort(absl::Status status) {
    ABSL_DCHECK(!subexpressions_.empty());
    ABSL_DCHECK(!status.ok());
    abort_status_.Update(std::move(status));
    call_stack_.clear();
    execution_path_ = subexpressions_[0];
    pc_ = execution_path_.size();
  }

 private:
  struct SubFrame {
    size_t return_pc;
    size_t slot_index;
    ExecutionPathView return_expression;
    size_t expected_stack_size;
  };

  // Runs steps until the program completes or is aborted.
  void Interpret();

  // Same as Interpret(), but also calls `listener` with the result of each
  // step that maps to an AST node.
  void InterpretWithCallback(EvaluationListener& listener);

  // Slow path of Next(): returns from a completed subexpression, or returns
  // nullptr at the end of the program.
  ABSL_ATTRIBUTE_NOINLINE const ExpressionStep* NextSlow();

  // Error path of JumpToOrAbort().
  ABSL_ATTRIBUTE_NOINLINE ABSL_ATTRIBUTE_COLD void AbortJumpOutOfRange(
      int offset);

  size_t pc_;  // pc_ - Program Counter. Current position on execution path.
  ExecutionPathView execution_path_;
  EvaluatorStack* absl_nonnull const value_stack_;
  cel::runtime_internal::IteratorStack* absl_nonnull const iterator_stack_;
  absl::Span<const ExecutionPathView> subexpressions_;
  std::vector<SubFrame> call_stack_;
};

// A flattened representation of the input CEL AST.
class FlatExpression {
 public:
  // path is flat execution path that is based upon the flattened AST tree
  // type_provider is the configured type system that should be used for
  //   value creation in evaluation
  FlatExpression(ExecutionPath path, size_t comprehension_slots_size,
                 const cel::TypeProvider& type_provider,
                 const cel::RuntimeOptions& options,
                 absl_nullable std::shared_ptr<google::protobuf::Arena> arena = nullptr)
      : path_(std::move(path)),
        subexpressions_({path_}),
        comprehension_slots_size_(comprehension_slots_size),
        type_provider_(type_provider),
        options_(options),
        arena_(std::move(arena)) {}

  FlatExpression(ExecutionPath path,
                 std::vector<ExecutionPathView> subexpressions,
                 size_t comprehension_slots_size,
                 const cel::TypeProvider& type_provider,
                 const cel::RuntimeOptions& options,
                 absl_nullable std::shared_ptr<google::protobuf::Arena> arena = nullptr)
      : path_(std::move(path)),
        subexpressions_(std::move(subexpressions)),
        comprehension_slots_size_(comprehension_slots_size),
        type_provider_(type_provider),
        options_(options),
        arena_(std::move(arena)) {}

  // Move-only
  FlatExpression(FlatExpression&&) = default;
  FlatExpression& operator=(FlatExpression&&) = delete;

  // Create new evaluator state instance with the configured options and type
  // provider.
  FlatExpressionEvaluatorState MakeEvaluatorState(
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena) const;

  // Evaluate the expression.
  //
  // A status may be returned if an unexpected error occurs. Recoverable errors
  // will be represented as a cel::ErrorValue result.
  //
  // If the listener is not empty, it will be called after each evaluation step
  // that correlates to an AST node. The value passed to the will be the top of
  // the evaluation stack, corresponding to the result of the subexpression.
  absl::StatusOr<cel::Value> EvaluateWithCallback(
      const cel::ActivationInterface& activation,
      const cel::EmbedderContext* absl_nullable embedder_context,
      EvaluationListener listener, FlatExpressionEvaluatorState& state) const;

  const ExecutionPath& path() const { return path_; }

  absl::Span<const ExecutionPathView> subexpressions() const {
    return subexpressions_;
  }

  const cel::RuntimeOptions& options() const { return options_; }

  size_t comprehension_slots_size() const { return comprehension_slots_size_; }

  const cel::TypeProvider& type_provider() const { return type_provider_; }

 private:
  ExecutionPath path_;
  std::vector<ExecutionPathView> subexpressions_;
  size_t comprehension_slots_size_;
  const cel::TypeProvider& type_provider_;
  cel::RuntimeOptions options_;
  // Arena used during planning phase, may hold constant values so should be
  // kept alive.
  absl_nullable std::shared_ptr<google::protobuf::Arena> arena_;
};

// Helper functions for checking ExpressionStep kinds. Used for program
// optimization.

// Checks if the step is a constant and if so, writes the value into `out`.
// Returns true if the step is a constant, false otherwise.
bool GetIfConstant(const ExpressionStep& step, cel::Value& out);

// Checks if the step is a constant.
bool IsConstant(const ExpressionStep& step);

ComprehensionCondStep* GetIfComprehensionCondStep(ExpressionStep& step);
ComprehensionNextStep* GetIfComprehensionNextStep(ExpressionStep& step);

BoolJumpStepInfo* GetIfBoolJumpStep(ExpressionStep& step);
inline BoolJumpStepInfo* GetIfBoolJumpStep(ExpressionStep* step) {
  if (step == nullptr) {
    return nullptr;
  }
  return GetIfBoolJumpStep(*step);
}

TernaryJumpStepInfo* GetIfTernaryJumpStep(ExpressionStep& step);
inline TernaryJumpStepInfo* GetIfTernaryJumpStep(ExpressionStep* step) {
  if (step == nullptr) {
    return nullptr;
  }
  return GetIfTernaryJumpStep(*step);
}

FixedJumpStepInfo* GetIfFixedJumpStep(ExpressionStep& step);
inline FixedJumpStepInfo* GetIfFixedJumpStep(ExpressionStep* step) {
  if (step == nullptr) {
    return nullptr;
  }
  return GetIfFixedJumpStep(*step);
}

// Implementation details.

inline ExpressionStep::~ExpressionStep() {
  switch (header_.kind) {
    case ExpressionStepKind::kGenericLogic:
      u_.logic.reset();
      break;
    case ExpressionStepKind::kOtherConstant:
      u_.other_val.reset();
      break;
    case ExpressionStepKind::kIdentifier:
      u_.identifier.reset();
      break;
    case ExpressionStepKind::kEagerFunction:
      u_.eager_function_step.reset();
      break;
    case ExpressionStepKind::kLazyFunction:
      u_.lazy_function_step.reset();
      break;
    case ExpressionStepKind::kMovedFrom:
    case ExpressionStepKind::kIntConstant:
    case ExpressionStepKind::kBoolConstant:
    case ExpressionStepKind::kDoubleConstant:
    case ExpressionStepKind::kNullConstant:
    case ExpressionStepKind::kUintConstant:
    case ExpressionStepKind::kLazyInit:
    case ExpressionStepKind::kAssignSlotAndPop:
    case ExpressionStepKind::kClearSlots:
    case ExpressionStepKind::kBooleanNot:
    case ExpressionStepKind::kNotStrictlyFalse:
    case ExpressionStepKind::kBooleanOr:
    case ExpressionStepKind::kBooleanAnd:
    case ExpressionStepKind::kComprehensionFinish:
    case ExpressionStepKind::kComprehensionCond:
    case ExpressionStepKind::kComprehensionNext:
    case ExpressionStepKind::kComprehensionCond2:
    case ExpressionStepKind::kComprehensionNext2:
    case ExpressionStepKind::kReadSlot:
    case ExpressionStepKind::kBooleanOrJump:
    case ExpressionStepKind::kBooleanAndJump:
    case ExpressionStepKind::kTernaryJump:
    case ExpressionStepKind::kFixedJump:
    case ExpressionStepKind::kFastIn:
    case ExpressionStepKind::kFastEqual:
    case ExpressionStepKind::kFastNotEqual:
    case ExpressionStepKind::kNewMutableList:
    case ExpressionStepKind::kMutableListAppend:
      break;
    default:
      ABSL_UNREACHABLE();
  }
  header_.kind = ExpressionStepKind::kMovedFrom;
  u_.empty = nullptr;
}

inline const ExpressionStepLogic* ExpressionStep::GetGenericStep() const {
  ABSL_DCHECK_EQ(header_.kind, ExpressionStepKind::kGenericLogic);
  return u_.logic.get();
}

inline bool ExpressionStep::IsGenericStep() const {
  return header_.kind == ExpressionStepKind::kGenericLogic;
}

inline ExpressionStep::ExpressionStep(ExpressionStep&& other)
    : ExpressionStep() {
  SwapToEmpty(other, *this);
}

inline ExpressionStep& ExpressionStep::operator=(ExpressionStep&& other) {
  ExpressionStep temp;
  SwapToEmpty(*this, temp);
  SwapToEmpty(other, *this);
  return *this;
}

inline void ExpressionStep::EvaluateReadSlotStep(size_t slot_index,
                                                 ExecutionFrame& frame) {
  const ComprehensionSlots::Slot* slot =
      frame.comprehension_slots().Get(slot_index);
  if (!slot->Has()) {
    frame.Abort(absl::InternalError(absl::StrCat(
        "Comprehension variable read out of scope: ", slot_index)));
    return;
  }
  frame.value_stack().Push(slot->value(), slot->attribute());
}

inline void ExpressionStep::EvaluateBoolJumpStep(const BoolJumpStepInfo& step,
                                                 bool target,
                                                 ExecutionFrame& frame) {
  ABSL_DCHECK(step.set) << "BoolJumpStep did not have a value set.";
  if (!frame.value_stack().HasEnough(step.arg_count)) {
    frame.Abort(
        absl::Status(absl::StatusCode::kInternal, "Value stack underflow"));
    return;
  }
  const cel::Value& value = frame.value_stack().Peek();
  if (value.IsBool() && value.GetBool().NativeValue() == target) {
    frame.value_stack().SwapAndPop(step.arg_count, step.arg_count - 1);
    frame.JumpToOrAbort(step.offset);
  }
  // No-op if the value is not a bool or the value is not the target.
  // Cleanup will happen if we hit a later jump or we fall-through.
}

inline void ExpressionStep::EvaluateTernaryJumpStep(
    const TernaryJumpStepInfo& step, ExecutionFrame& frame) {
  ABSL_DCHECK(step.set) << "TernaryJumpStep did not have a value set.";
  if (!frame.value_stack().HasEnough(1)) {
    frame.Abort(absl::InternalError("TernaryJumpStep: value stack underflow"));
    return;
  }
  const cel::Value& condition = frame.value_stack().Peek();
  switch (condition.kind()) {
    case cel::ValueKind::kBool:
      if (!condition.GetBool().NativeValue()) {
        frame.JumpToOrAbort(step.jump_to_second_offset);
      }
      frame.value_stack().Pop();
      break;
    default:
      frame.value_stack().PopAndPush(
          cel::ErrorValue(cel::runtime_internal::CreateNoMatchingOverloadError(
              "<ternary_condition>")));
      ABSL_FALLTHROUGH_INTENDED;
    case cel::ValueKind::kError:
    case cel::ValueKind::kUnknown:
      // Propagate the error or unknown set.
      frame.JumpToOrAbort(step.error_offset);
      break;
  }
}

inline void ExpressionStep::Evaluate(ExecutionFrame& frame) const {
  switch (header_.kind) {
    case ExpressionStepKind::kGenericLogic:
      u_.logic->Evaluate(&frame);
      break;
    case ExpressionStepKind::kIntConstant:
      frame.value_stack().Push(cel::IntValue(u_.int_val));
      break;
    case ExpressionStepKind::kBoolConstant:
      frame.value_stack().Push(cel::BoolValue(u_.bool_val));
      break;
    case ExpressionStepKind::kDoubleConstant:
      frame.value_stack().Push(cel::DoubleValue(u_.double_val));
      break;
    case ExpressionStepKind::kNullConstant:
      frame.value_stack().Push(cel::NullValue());
      break;
    case ExpressionStepKind::kUintConstant:
      frame.value_stack().Push(cel::UintValue(u_.uint_val));
      break;
    case ExpressionStepKind::kOtherConstant:
      frame.value_stack().Push(*u_.other_val);
      break;
    case ExpressionStepKind::kLazyInit:
      EvaluateLazyInitStep(u_.lazy_init, frame);
      break;
    case ExpressionStepKind::kAssignSlotAndPop:
      EvaluateAssignSlotAndPop(u_.slot_index, frame);
      break;
    case ExpressionStepKind::kClearSlots:
      EvaluateClearSlotStep(u_.clear_slots, frame);
      break;
    case ExpressionStepKind::kBooleanNot:
      EvaluateNotStep(frame);
      break;
    case ExpressionStepKind::kNotStrictlyFalse:
      EvaluateNotStrictlyFalseStep(frame);
      break;
    case ExpressionStepKind::kBooleanOr:
      EvaluateBoolLogicStep(BoolLogicKind::kOr, u_.arg_count, frame);
      break;
    case ExpressionStepKind::kBooleanAnd:
      EvaluateBoolLogicStep(BoolLogicKind::kAnd, u_.arg_count, frame);
      break;
    case ExpressionStepKind::kComprehensionFinish:
      EvaluateComprehensionFinishStep(u_.slot_index, frame);
      break;
    case ExpressionStepKind::kComprehensionNext:
      u_.next_step.Evaluate1(&frame);
      break;
    case ExpressionStepKind::kComprehensionNext2:
      u_.next_step.Evaluate2(&frame);
      break;
    case ExpressionStepKind::kComprehensionCond:
      u_.cond_step.Evaluate1(&frame);
      break;
    case ExpressionStepKind::kComprehensionCond2:
      u_.cond_step.Evaluate2(&frame);
      break;
    case ExpressionStepKind::kReadSlot:
      EvaluateReadSlotStep(u_.slot_index, frame);
      break;
    case ExpressionStepKind::kBooleanOrJump:
      EvaluateBoolJumpStep(u_.bool_jump_step, true, frame);
      break;
    case ExpressionStepKind::kBooleanAndJump:
      EvaluateBoolJumpStep(u_.bool_jump_step, false, frame);
      break;
    case ExpressionStepKind::kTernaryJump:
      EvaluateTernaryJumpStep(u_.ternary_jump_step, frame);
      break;
    case ExpressionStepKind::kFixedJump:
      ABSL_DCHECK(u_.fixed_jump_step.set)
          << "FixedJumpStep did not have a value set.";
      frame.JumpToOrAbort(u_.fixed_jump_step.offset);
      break;
    case ExpressionStepKind::kIdentifier:
      EvaluateIdentifierStep(*u_.identifier, frame);
      break;
    case ExpressionStepKind::kEagerFunction:
      u_.eager_function_step->Evaluate(frame);
      break;
    case ExpressionStepKind::kLazyFunction:
      u_.lazy_function_step->Evaluate(frame);
      break;
    case ExpressionStepKind::kFastIn:
      EvaluateFastInStep(frame);
      break;
    case ExpressionStepKind::kFastEqual:
      EvaluateFastEqualStep(/*negation=*/false, frame);
      break;
    case ExpressionStepKind::kFastNotEqual:
      EvaluateFastEqualStep(/*negation=*/true, frame);
      break;
    case ExpressionStepKind::kNewMutableList:
      frame.value_stack().Push(cel::CustomListValue(
          cel::common_internal::NewMutableListValue(frame.arena()),
          frame.arena()));
      break;
    case ExpressionStepKind::kMutableListAppend:
      EvaluateMutableListAppendStep(frame);
      break;
    case ExpressionStepKind::kMovedFrom:
      frame.Abort(absl::InternalError(
          "ExpressionStep::Evaluate called on moved-from step"));
      break;
    default:
      ABSL_UNREACHABLE();
  }
}

}  // namespace google::api::expr::runtime

#endif  // THIRD_PARTY_CEL_CPP_EVAL_EVAL_EVALUATOR_CORE_H_
