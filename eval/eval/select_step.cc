#include "eval/eval/select_step.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "base/attribute.h"
#include "common/legacy_value.h"
#include "common/memory.h"
#include "common/type.h"
#include "common/value.h"
#include "common/value_kind.h"
#include "eval/eval/attribute_trail.h"
#include "eval/eval/evaluator_core.h"
#include "eval/eval/expression_step_base.h"
#include "eval/eval/expression_step_logic.h"
#include "eval/public/cel_value.h"
#include "eval/public/structs/proto_message_type_adapter.h"
#include "internal/status_macros.h"
#include "runtime/runtime_options.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"

namespace google::api::expr::runtime {

namespace {

using ::cel::BoolValue;
using ::cel::ErrorValue;
using ::cel::OptionalValue;
using ::cel::ProtoWrapperTypeOptions;
using ::cel::StringValue;
using ::cel::Value;
using ::cel::ValueKind;

// Common error for cases where evaluation attempts to perform select operations
// on an unsupported type.
//
// This should not happen under normal usage of the evaluator, but useful for
// troubleshooting broken invariants.
absl::Status InvalidSelectTargetError() {
  return absl::Status(absl::StatusCode::kInvalidArgument,
                      "Applying SELECT to non-message type");
}

// Helper for StructValue::GetFieldByName. Used for opting out of old reflection
// implementation.
absl::Status WrappedStructGet(
    const Value& target, absl::string_view field,
    ProtoWrapperTypeOptions unboxing_option,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena,
    bool enable_use_new_field_select_implementation,
    Value* absl_nonnull result) {
  if (!enable_use_new_field_select_implementation) {
    if (const google::protobuf::Message* message =
            cel::interop_internal::GetLegacyMessage(target);
        message != nullptr) {
      CelValue::MessageWrapper message_wrapper(
          message, &GetGenericProtoTypeInfoInstance());
      CEL_ASSIGN_OR_RETURN(
          CelValue cel_value,
          internal::GetGenericProtoAccessApisInstance().GetField(
              field, message_wrapper, unboxing_option,
              cel::MemoryManagerRef::Pooling(arena)));
      return cel::ModernValue(arena, cel_value, *result);
    }
  }
  return target.GetStruct().GetFieldByName(
      field, unboxing_option, descriptor_pool, message_factory, arena, result);
}

absl::Status PerformHas(const Value& target, absl::string_view field,
                        const StringValue& field_value,
                        const google::protobuf::DescriptorPool* descriptor_pool,
                        google::protobuf::MessageFactory* message_factory,
                        google::protobuf::Arena* arena, Value& result) {
  switch (target.kind()) {
    case ValueKind::kMap: {
      CEL_RETURN_IF_ERROR(target.GetMap().Has(field_value, descriptor_pool,
                                              message_factory, arena, &result));
      return absl::OkStatus();
    }
    case ValueKind::kStruct: {
      auto has_field = target.GetStruct().HasFieldByName(field);
      if (!has_field.ok()) {
        result = ErrorValue::From(std::move(has_field).status(), arena);
      } else {
        result = BoolValue{*has_field};
      }
      return absl::OkStatus();
    }
    default:
      return InvalidSelectTargetError();
  }
}

absl::Status PerformGet(const Value& target, absl::string_view field,
                        const StringValue& field_value,
                        ProtoWrapperTypeOptions unboxing_option,
                        const google::protobuf::DescriptorPool* descriptor_pool,
                        google::protobuf::MessageFactory* message_factory,
                        google::protobuf::Arena* arena,
                        bool enable_use_new_field_select_implementation,
                        Value& result) {
  switch (target.kind()) {
    case ValueKind::kMap: {
      auto status = target.GetMap().Get(field_value, descriptor_pool,
                                        message_factory, arena, &result);
      if (!status.ok()) {
        result = ErrorValue::From(std::move(status), arena);
      }
      return absl::OkStatus();
    }
    case ValueKind::kStruct: {
      auto status = WrappedStructGet(
          target, field, unboxing_option, descriptor_pool, message_factory,
          arena, enable_use_new_field_select_implementation, &result);
      if (!status.ok()) {
        result = ErrorValue::From(std::move(status), arena);
      }
      return absl::OkStatus();
    }
    default:
      return InvalidSelectTargetError();
  }
}

absl::Status PerformOptionalGet(const Value& target, absl::string_view field,
                                const StringValue& field_value,
                                ProtoWrapperTypeOptions unboxing_option,
                                const google::protobuf::DescriptorPool* descriptor_pool,
                                google::protobuf::MessageFactory* message_factory,
                                google::protobuf::Arena* arena,
                                bool enable_use_new_field_select_implementation,
                                Value& result) {
  switch (target.kind()) {
    case ValueKind::kMap: {
      CEL_ASSIGN_OR_RETURN(
          bool found, target.GetMap().Find(field_value, descriptor_pool,
                                           message_factory, arena, &result));
      if (!found) {
        result = OptionalValue::None();
        return absl::OkStatus();
      }
      ABSL_DCHECK(!result.IsUnknown());
      result = OptionalValue::Of(std::move(result), arena);
      return absl::OkStatus();
    }
    case ValueKind::kStruct: {
      CEL_ASSIGN_OR_RETURN(bool found,
                           target.GetStruct().HasFieldByName(field));
      if (!found) {
        result = OptionalValue::None();
        return absl::OkStatus();
      }
      CEL_RETURN_IF_ERROR(WrappedStructGet(
          target, field, unboxing_option, descriptor_pool, message_factory,
          arena, enable_use_new_field_select_implementation, &result));

      ABSL_DCHECK(!result.IsUnknown());
      result = OptionalValue::Of(std::move(result), arena);
      return absl::OkStatus();
    }
    default:
      return InvalidSelectTargetError();
  }
}

// SelectStep performs message field access specified by Expr::Select
// message.
class SelectStep : public ExpressionStepBase {
 public:
  SelectStep(absl::string_view field, bool test_field_presence,
             bool enable_wrapper_type_null_unboxing, bool enable_optional_types)
      : field_(field),
        unboxing_option_(enable_wrapper_type_null_unboxing
                             ? ProtoWrapperTypeOptions::kUnsetNull
                             : ProtoWrapperTypeOptions::kUnsetProtoDefault),
        test_field_presence_(test_field_presence),
        enable_optional_types_(enable_optional_types) {}

  void Evaluate(ExecutionFrame* frame) const override;

 protected:
  std::string field_;
  ProtoWrapperTypeOptions unboxing_option_;
  bool test_field_presence_;
  bool enable_optional_types_;
};

void SelectStep::Evaluate(ExecutionFrame* frame) const {
  if (!frame->value_stack().HasEnough(1)) {
    frame->Abort(absl::InternalError(
        "No arguments supplied for Select-type expression"));
    return;
  }

  Value& arg_and_result = frame->value_stack().Peek();

  if (arg_and_result.IsUnknown() || arg_and_result.IsError()) {
    // Bubble up unknowns and errors.
    return;
  }

  absl::optional<OptionalValue> optional_arg;

  if (enable_optional_types_ && arg_and_result.IsOptional()) {
    optional_arg = arg_and_result.GetOptional();
  }

  if (!(optional_arg || arg_and_result.IsMap() || arg_and_result.IsStruct())) {
    // Do not bother stepping the attribute trail if the target is incorrect, we
    // would not have encountered it anyway.
    arg_and_result =
        cel::ErrorValue::From(InvalidSelectTargetError(), frame->arena());
    return;
  }

  // Handle unknown resolution.
  if (frame->attribute_tracking_enabled() &&
      frame->value_stack().PeekAttribute().Match<AttributeTrail::kFull>(
          cel::AttributeQualifierView::OfString(field_), arg_and_result,
          frame->unknown_tree())) {
    return;
  }

  Value result;
  if (test_field_presence_) {
    const Value* target = &arg_and_result;
    if (optional_arg) {
      if (!optional_arg->HasValue()) {
        arg_and_result = cel::FalseValue();
        return;
      }
      optional_arg->Value(&result);
      target = &result;
    }
    if (absl::Status status =
            PerformHas(*target, field_, cel::StringValue::WrapUnsafe(field_),
                       frame->descriptor_pool(), frame->message_factory(),
                       frame->arena(), result);
        !status.ok()) {
      frame->Abort(std::move(status));
      return;
    }
    arg_and_result = result;
    return;
  }

  if (optional_arg) {
    if (!optional_arg->HasValue()) {
      arg_and_result = OptionalValue::None();
      return;
    }
    Value value;
    optional_arg->Value(&value);
    absl::Status status = PerformOptionalGet(
        value, field_, cel::StringValue::WrapUnsafe(field_), unboxing_option_,
        frame->descriptor_pool(), frame->message_factory(), frame->arena(),
        frame->options().enable_use_new_field_select_implementation, result);
    if (!status.ok()) {
      result = ErrorValue::From(std::move(status), frame->arena());
    }
    arg_and_result = result;
    return;
  }

  if (absl::Status status = PerformGet(
          arg_and_result, field_, cel::StringValue::WrapUnsafe(field_),
          unboxing_option_, frame->descriptor_pool(), frame->message_factory(),
          frame->arena(),
          frame->options().enable_use_new_field_select_implementation, result);
      !status.ok()) {
    frame->Abort(std::move(status));
    return;
  }
  arg_and_result = result;
}

bool CheckAttributeTrail(const std::string& field, ExecutionFrame* frame) {
  return frame->attribute_tracking_enabled() &&
         frame->value_stack().PeekAttribute().Match<AttributeTrail::kFull>(
             cel::AttributeQualifierView::OfString(field),
             frame->value_stack().Peek(), frame->unknown_tree());
}

bool SupportsCachedFieldDescriptor(
    const cel::ParsedMessageValue& parsed_message,
    const google::protobuf::Descriptor* descriptor,
    const google::protobuf::FieldDescriptor* field_descriptor) {
  ABSL_DCHECK_EQ(field_descriptor->containing_type(), descriptor);
  const google::protobuf::Descriptor* rt_descriptor = parsed_message.GetDescriptor();

  if (rt_descriptor != descriptor) {
    return false;
  }

  // Caller should have already checked this. Crash here if not instead of
  // making proto crash.
  ABSL_DCHECK_EQ(rt_descriptor->file()->pool(),
                 field_descriptor->file()->pool());

  return true;
}

class ProtoSelectStep : public SelectStep {
 public:
  ProtoSelectStep(absl::string_view value,
                  bool enable_wrapper_type_null_unboxing,
                  bool enable_optional_types,
                  const google::protobuf::Descriptor* descriptor,
                  const google::protobuf::FieldDescriptor* field_descriptor)
      : SelectStep(value, /*test_field_presence=*/false,
                   enable_wrapper_type_null_unboxing, enable_optional_types),
        descriptor_(descriptor),
        field_descriptor_(field_descriptor) {
    ABSL_DCHECK(descriptor_ != nullptr);
    ABSL_DCHECK(field_descriptor_ != nullptr);
  }

  void Evaluate(ExecutionFrame* frame) const override {
    if (!frame->value_stack().HasEnough(1)) {
      frame->Abort(absl::InternalError(
          "No arguments supplied for Select-type expression"));
      return;
    }

    const Value& arg = frame->value_stack().Peek();
    if (auto unwrapped = arg.AsParsedMessage();
        unwrapped.has_value() &&
        SupportsCachedFieldDescriptor(*unwrapped, descriptor_,
                                      field_descriptor_)) {
      EvaluateMessageFieldGet(frame, *unwrapped);
      return;
    } else if (const google::protobuf::Message* legacy_message =
                   cel::interop_internal::GetLegacyMessage(arg);
               frame->options().enable_use_new_field_select_implementation &&
               legacy_message != nullptr) {
      auto parsed_message = cel::UnsafeParsedMessageValue(legacy_message);
      // A little unfortunate, but need to special case for legacy values so we
      // can minimize back and forth interop conversions.
      if (SupportsCachedFieldDescriptor(parsed_message, descriptor_,
                                        field_descriptor_)) {
        EvaluateMessageFieldGet(frame, legacy_message);
        return;
      }
    }
    // If we get an unexpected value type, fall back to the generic
    // implementation.
    SelectStep::Evaluate(frame);
  }

 private:
  void EvaluateMessageFieldGet(
      ExecutionFrame* frame,
      const cel::ParsedMessageValue& parsed_message) const;
  void EvaluateMessageFieldGet(
      ExecutionFrame* frame,
      const google::protobuf::Message* absl_nonnull legacy_message) const;

  const google::protobuf::Descriptor* descriptor_;
  const google::protobuf::FieldDescriptor* field_descriptor_;
};

void ProtoSelectStep::EvaluateMessageFieldGet(
    ExecutionFrame* frame,
    const cel::ParsedMessageValue& parsed_message) const {
  if (CheckAttributeTrail(field_, frame)) {
    return;
  }
  if (absl::Status status = parsed_message.GetField(
          field_descriptor_, unboxing_option_, frame->descriptor_pool(),
          frame->message_factory(), frame->arena(),
          &frame->value_stack().Peek());
      !status.ok()) {
    frame->Abort(std::move(status));
  }
}

void ProtoSelectStep::EvaluateMessageFieldGet(
    ExecutionFrame* frame,
    const google::protobuf::Message* absl_nonnull legacy_message) const {
  ABSL_DCHECK(legacy_message != nullptr);
  if (CheckAttributeTrail(field_, frame)) {
    return;
  }
  if (absl::Status status = cel::interop_internal::WrapLegacyMessageField(
          legacy_message, field_descriptor_, unboxing_option_,
          frame->descriptor_pool(), frame->message_factory(), frame->arena(),
          &frame->value_stack().Peek());
      !status.ok()) {
    frame->Abort(std::move(status));
  }
}

class ProtoHasStep : public SelectStep {
 public:
  ProtoHasStep(absl::string_view field, bool enable_wrapper_type_null_unboxing,
               bool enable_optional_types, const google::protobuf::Descriptor* descriptor,
               const google::protobuf::FieldDescriptor* field_descriptor)
      : SelectStep(field, /*test_field_presence=*/true,
                   enable_wrapper_type_null_unboxing, enable_optional_types),
        descriptor_(descriptor),
        field_descriptor_(field_descriptor) {
    ABSL_DCHECK(descriptor_ != nullptr);
    ABSL_DCHECK(field_descriptor_ != nullptr);
  }

  void Evaluate(ExecutionFrame* frame) const override {
    if (!frame->value_stack().HasEnough(1)) {
      frame->Abort(absl::InternalError(
          "No arguments supplied for Select-type expression"));
      return;
    }

    const Value& arg = frame->value_stack().Peek();
    if (auto unwrapped = arg.AsParsedMessage();
        unwrapped.has_value() &&
        SupportsCachedFieldDescriptor(*unwrapped, descriptor_,
                                      field_descriptor_)) {
      EvaluateHas(frame, *unwrapped);
      return;
    } else if (const google::protobuf::Message* legacy_message =
                   cel::interop_internal::GetLegacyMessage(arg);
               legacy_message != nullptr) {
      cel::ParsedMessageValue parsed_message =
          cel::UnsafeParsedMessageValue(legacy_message);
      if (SupportsCachedFieldDescriptor(parsed_message, descriptor_,
                                        field_descriptor_)) {
        EvaluateHas(frame, parsed_message);
        return;
      }
    }
    // If we get an unexpected value type, fall back to the generic
    // implementation.
    SelectStep::Evaluate(frame);
  }

 private:
  void EvaluateHas(ExecutionFrame* frame,
                   const cel::ParsedMessageValue& parsed_message) const;

  const google::protobuf::Descriptor* descriptor_;
  const google::protobuf::FieldDescriptor* field_descriptor_;
};

void ProtoHasStep::EvaluateHas(
    ExecutionFrame* frame,
    const cel::ParsedMessageValue& parsed_message) const {
  if (CheckAttributeTrail(field_, frame)) {
    return;
  }
  frame->value_stack().Peek() =
      BoolValue{parsed_message.HasField(field_descriptor_)};
}

}  // namespace

// Factory method for Select - based Execution step
absl::StatusOr<std::unique_ptr<ExpressionStepLogic>> CreateSelectStep(
    absl::string_view field, bool test_only,
    bool enable_wrapper_type_null_unboxing, bool enable_optional_types) {
  return std::make_unique<SelectStep>(field, test_only,
                                      enable_wrapper_type_null_unboxing,
                                      enable_optional_types);
}

// Factory method for Select - based Execution step
absl::StatusOr<std::unique_ptr<ExpressionStepLogic>> CreateTypedSelectStep(
    absl::string_view field, cel::StructType resolved_operand_type,
    cel::StructTypeField resolved_field, bool test_only,
    bool enable_wrapper_type_null_unboxing, bool enable_optional_types) {
  if (!resolved_operand_type.IsMessage()) {
    // The specialization only supports messages. Fallback to the generic
    // implementation for other types.
    // TODO(uncreated-issue/89): support optional select and chaining.
    return CreateSelectStep(field, test_only, enable_wrapper_type_null_unboxing,
                            enable_optional_types);
  }
  const google::protobuf::Descriptor* descriptor =
      resolved_operand_type.GetMessage().descriptor();

  ABSL_DCHECK(resolved_field.IsMessage());
  const google::protobuf::FieldDescriptor* field_descriptor =
      resolved_field.GetMessage().descriptor();

  if (field_descriptor->file()->pool() != descriptor->file()->pool()) {
    // The field descriptor is not in the same pool as the operand type.
    // (this should only happen if an overlay extends the type).
    //
    // We don't have a way to determine if a runtime message is compatible
    // with the resolved extension and proto's reflection implementation may
    // crash.
    //
    // Fallback to the generic implementation.
    return CreateSelectStep(field, test_only, enable_wrapper_type_null_unboxing,
                            enable_optional_types);
  }

  if (test_only) {
    return std::make_unique<ProtoHasStep>(
        field, enable_wrapper_type_null_unboxing, enable_optional_types,
        descriptor, field_descriptor);
  }

  return std::make_unique<ProtoSelectStep>(
      field, enable_wrapper_type_null_unboxing, enable_optional_types,
      descriptor, field_descriptor);
}

}  // namespace google::api::expr::runtime
