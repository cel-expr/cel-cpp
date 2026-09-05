#include "eval/eval/select_step.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/log/absl_check.h"
#include "absl/log/absl_log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "common/legacy_value.h"
#include "common/memory.h"
#include "common/type.h"
#include "common/value.h"
#include "common/value_kind.h"
#include "eval/eval/attribute_trail.h"
#include "eval/eval/direct_expression_step.h"
#include "eval/eval/evaluator_core.h"
#include "eval/eval/expression_step_base.h"
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

absl::optional<Value> CheckForMarkedAttributes(const AttributeTrail& trail,
                                               ExecutionFrameBase& frame) {
  if (frame.unknown_processing_enabled() &&
      frame.attribute_utility().CheckForUnknownExact(trail)) {
    return frame.attribute_utility().CreateUnknownSet(trail.attribute());
  }

  if (frame.missing_attribute_errors_enabled() &&
      frame.attribute_utility().CheckForMissingAttribute(trail)) {
    auto result = frame.attribute_utility().CreateMissingAttributeError(
        trail.attribute());

    if (result.ok()) {
      return std::move(result).value();
    }
    // Invariant broken (an invalid CEL Attribute shouldn't match anything).
    // Log and return a CelError.
    ABSL_LOG(ERROR) << "Invalid attribute pattern matched select path: "
                    << result.status().ToString();  // NOLINT: OSS compatibility
    return cel::ErrorValue(std::move(result).status());
  }

  return std::nullopt;
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
        result = ErrorValue(std::move(has_field).status());
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
        result = ErrorValue(std::move(status));
      }
      return absl::OkStatus();
    }
    case ValueKind::kStruct: {
      auto status = WrappedStructGet(
          target, field, unboxing_option, descriptor_pool, message_factory,
          arena, enable_use_new_field_select_implementation, &result);
      if (!status.ok()) {
        result = ErrorValue(std::move(status));
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
  SelectStep(StringValue value, bool test_field_presence, int64_t expr_id,
             bool enable_wrapper_type_null_unboxing, bool enable_optional_types)
      : ExpressionStepBase(expr_id),
        field_value_(std::move(value)),
        field_(field_value_.ToString()),
        test_field_presence_(test_field_presence),
        unboxing_option_(enable_wrapper_type_null_unboxing
                             ? ProtoWrapperTypeOptions::kUnsetNull
                             : ProtoWrapperTypeOptions::kUnsetProtoDefault),
        enable_optional_types_(enable_optional_types) {}

  absl::Status Evaluate(ExecutionFrame* frame) const override;

 protected:
  cel::StringValue field_value_;
  std::string field_;
  bool test_field_presence_;
  ProtoWrapperTypeOptions unboxing_option_;
  bool enable_optional_types_;
};

absl::Status SelectStep::Evaluate(ExecutionFrame* frame) const {
  if (!frame->value_stack().HasEnough(1)) {
    return absl::Status(absl::StatusCode::kInternal,
                        "No arguments supplied for Select-type expression");
  }

  const Value& arg = frame->value_stack().Peek();
  const AttributeTrail& trail = frame->value_stack().PeekAttribute();

  if (arg.IsUnknown() || arg.IsError()) {
    // Bubble up unknowns and errors.
    return absl::OkStatus();
  }

  AttributeTrail result_trail;

  // Handle unknown resolution.
  if (frame->attribute_tracking_enabled()) {
    result_trail = trail.Step(&field_);
  }

  absl::optional<OptionalValue> optional_arg;

  if (enable_optional_types_ && arg.IsOptional()) {
    optional_arg = arg.GetOptional();
  }

  if (!(optional_arg || arg.IsMap() || arg.IsStruct())) {
    frame->value_stack().PopAndPush(cel::ErrorValue(InvalidSelectTargetError()),
                                    std::move(result_trail));
    return absl::OkStatus();
  }

  absl::optional<Value> marked_attribute_check =
      CheckForMarkedAttributes(result_trail, *frame);
  if (marked_attribute_check.has_value()) {
    frame->value_stack().PopAndPush(std::move(marked_attribute_check).value(),
                                    std::move(result_trail));
    return absl::OkStatus();
  }

  Value result;
  if (test_field_presence_) {
    const Value* target = &arg;
    if (optional_arg) {
      if (!optional_arg->HasValue()) {
        frame->value_stack().PopAndPush(cel::BoolValue{false},
                                        std::move(result_trail));
        return absl::OkStatus();
      }
      optional_arg->Value(&result);
      target = &result;
    }
    CEL_RETURN_IF_ERROR(
        PerformHas(*target, field_, field_value_, frame->descriptor_pool(),
                   frame->message_factory(), frame->arena(), result));
    frame->value_stack().PopAndPush(std::move(result), std::move(result_trail));
    return absl::OkStatus();
  }

  if (optional_arg) {
    if (!optional_arg->HasValue()) {
      frame->value_stack().PopAndPush(OptionalValue::None(),
                                      std::move(result_trail));
      return absl::OkStatus();
    }
    Value value;
    optional_arg->Value(&value);
    auto status = PerformOptionalGet(
        value, field_, field_value_, unboxing_option_, frame->descriptor_pool(),
        frame->message_factory(), frame->arena(),
        frame->options().enable_use_new_field_select_implementation, result);
    if (!status.ok()) {
      result = ErrorValue(std::move(status));
    }
    frame->value_stack().PopAndPush(std::move(result), std::move(result_trail));
    return absl::OkStatus();
  }

  CEL_RETURN_IF_ERROR(PerformGet(
      arg, field_, field_value_, unboxing_option_, frame->descriptor_pool(),
      frame->message_factory(), frame->arena(),
      frame->options().enable_use_new_field_select_implementation, result));
  frame->value_stack().PopAndPush(std::move(result), std::move(result_trail));
  return absl::OkStatus();
}

class DirectSelectStep : public DirectExpressionStep {
 public:
  DirectSelectStep(int64_t expr_id,
                   std::unique_ptr<DirectExpressionStep> operand,
                   StringValue field, bool test_only,
                   bool enable_wrapper_type_null_unboxing,
                   bool enable_optional_types)
      : DirectExpressionStep(expr_id),
        operand_(std::move(operand)),
        field_value_(std::move(field)),
        field_(field_value_.ToString()),
        test_only_(test_only),
        unboxing_option_(enable_wrapper_type_null_unboxing
                             ? ProtoWrapperTypeOptions::kUnsetNull
                             : ProtoWrapperTypeOptions::kUnsetProtoDefault),
        enable_optional_types_(enable_optional_types) {}

  absl::Status Evaluate(ExecutionFrameBase& frame, Value& result,
                        AttributeTrail& attribute) const override {
    CEL_RETURN_IF_ERROR(operand_->Evaluate(frame, result, attribute));

    if (result.IsError() || result.IsUnknown()) {
      // Just forward.
      return absl::OkStatus();
    }

    if (frame.attribute_tracking_enabled()) {
      attribute = attribute.Step(&field_);
      absl::optional<Value> value = CheckForMarkedAttributes(attribute, frame);
      if (value.has_value()) {
        result = std::move(value).value();
        return absl::OkStatus();
      }
    }

    absl::optional<OptionalValue> optional_arg;

    if (enable_optional_types_ && result.IsOptional()) {
      optional_arg = result.GetOptional();
    }

    switch (result.kind()) {
      case ValueKind::kStruct:
      case ValueKind::kMap:
        break;
      default:
        if (optional_arg) {
          break;
        }
        result = cel::ErrorValue(InvalidSelectTargetError());
        return absl::OkStatus();
    }

    if (test_only_) {
      if (optional_arg) {
        if (!optional_arg->HasValue()) {
          result = cel::BoolValue{false};
          return absl::OkStatus();
        }
        Value value;
        optional_arg->Value(&value);
        return PerformHas(value, field_, field_value_, frame.descriptor_pool(),
                          frame.message_factory(), frame.arena(), result);
      }
      return PerformHas(result, field_, field_value_, frame.descriptor_pool(),
                        frame.message_factory(), frame.arena(), result);
    }

    if (optional_arg) {
      if (!optional_arg->HasValue()) {
        // result is still buffer for the container. just return.
        return absl::OkStatus();
      }
      Value value;
      optional_arg->Value(&value);
      auto status = PerformOptionalGet(
          value, field_, field_value_, unboxing_option_,
          frame.descriptor_pool(), frame.message_factory(), frame.arena(),
          frame.options().enable_use_new_field_select_implementation, result);
      if (!status.ok()) {
        result = ErrorValue(std::move(status));
      }
      return absl::OkStatus();
    }

    return PerformGet(
        result, field_, field_value_, unboxing_option_, frame.descriptor_pool(),
        frame.message_factory(), frame.arena(),
        frame.options().enable_use_new_field_select_implementation, result);
  }

 private:
  std::unique_ptr<DirectExpressionStep> operand_;

  // Field name in formats supported by each of the map and struct field access
  // APIs.
  //
  // ToString or ValueManager::CreateString may force a copy so we do this at
  // plan time.
  StringValue field_value_;
  std::string field_;

  // whether this is a has() expression.
  bool test_only_;
  ProtoWrapperTypeOptions unboxing_option_;
  bool enable_optional_types_;
};

bool CheckAttributeTrail(const std::string& field, ExecutionFrame* frame) {
  if (!frame->attribute_tracking_enabled()) {
    return false;
  }
  AttributeTrail& attr = frame->value_stack().PeekAttribute();
  attr = attr.Step(&field);

  absl::optional<Value> marked_attribute_check =
      CheckForMarkedAttributes(attr, *frame);
  if (marked_attribute_check.has_value()) {
    frame->value_stack().Peek() = std::move(marked_attribute_check).value();
    return true;
  }

  return false;
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
  ProtoSelectStep(StringValue value, int64_t expr_id,
                  bool enable_wrapper_type_null_unboxing,
                  bool enable_optional_types,
                  const google::protobuf::Descriptor* descriptor,
                  const google::protobuf::FieldDescriptor* field_descriptor)
      : SelectStep(std::move(value), /*test_field_presence=*/false, expr_id,
                   enable_wrapper_type_null_unboxing, enable_optional_types),
        descriptor_(descriptor),
        field_descriptor_(field_descriptor) {
    ABSL_DCHECK(descriptor_ != nullptr);
    ABSL_DCHECK(field_descriptor_ != nullptr);
  }

  absl::Status Evaluate(ExecutionFrame* frame) const override {
    if (!frame->value_stack().HasEnough(1)) {
      return absl::InternalError(
          "No arguments supplied for Select-type expression");
    }

    const Value& arg = frame->value_stack().Peek();
    if (auto unwrapped = arg.AsParsedMessage();
        unwrapped.has_value() &&
        SupportsCachedFieldDescriptor(*unwrapped, descriptor_,
                                      field_descriptor_)) {
      return EvaluateMessageFieldGet(frame, *unwrapped);
    } else if (const google::protobuf::Message* legacy_message =
                   cel::interop_internal::GetLegacyMessage(arg);
               frame->options().enable_use_new_field_select_implementation &&
               legacy_message != nullptr) {
      auto parsed_message = cel::UnsafeParsedMessageValue(legacy_message);
      // A little unfortunate, but need to special case for legacy values so we
      // can minimize back and forth interop conversions.
      if (SupportsCachedFieldDescriptor(parsed_message, descriptor_,
                                        field_descriptor_)) {
        return EvaluateMessageFieldGet(frame, legacy_message);
      }
    }
    // If we get an unexpected value type, fall back to the generic
    // implementation.
    return SelectStep::Evaluate(frame);
  }

 private:
  absl::Status EvaluateMessageFieldGet(
      ExecutionFrame* frame,
      const cel::ParsedMessageValue& parsed_message) const;
  absl::Status EvaluateMessageFieldGet(
      ExecutionFrame* frame,
      const google::protobuf::Message* absl_nonnull legacy_message) const;

  const google::protobuf::Descriptor* descriptor_;
  const google::protobuf::FieldDescriptor* field_descriptor_;
};

absl::Status ProtoSelectStep::EvaluateMessageFieldGet(
    ExecutionFrame* frame,
    const cel::ParsedMessageValue& parsed_message) const {
  if (CheckAttributeTrail(field_, frame)) {
    return absl::OkStatus();
  }
  return parsed_message.GetField(
      field_descriptor_, unboxing_option_, frame->descriptor_pool(),
      frame->message_factory(), frame->arena(), &frame->value_stack().Peek());
}

absl::Status ProtoSelectStep::EvaluateMessageFieldGet(
    ExecutionFrame* frame,
    const google::protobuf::Message* absl_nonnull legacy_message) const {
  ABSL_DCHECK(legacy_message != nullptr);
  if (CheckAttributeTrail(field_, frame)) {
    return absl::OkStatus();
  }
  return cel::interop_internal::WrapLegacyMessageField(
      legacy_message, field_descriptor_, unboxing_option_,
      frame->descriptor_pool(), frame->message_factory(), frame->arena(),
      &frame->value_stack().Peek());
}

class ProtoHasStep : public SelectStep {
 public:
  ProtoHasStep(StringValue value, int64_t expr_id,
               bool enable_wrapper_type_null_unboxing,
               bool enable_optional_types, const google::protobuf::Descriptor* descriptor,
               const google::protobuf::FieldDescriptor* field_descriptor)
      : SelectStep(std::move(value), /*test_field_presence=*/true, expr_id,
                   enable_wrapper_type_null_unboxing, enable_optional_types),
        descriptor_(descriptor),
        field_descriptor_(field_descriptor) {
    ABSL_DCHECK(descriptor_ != nullptr);
    ABSL_DCHECK(field_descriptor_ != nullptr);
  }

  absl::Status Evaluate(ExecutionFrame* frame) const override {
    if (!frame->value_stack().HasEnough(1)) {
      return absl::InternalError(
          "No arguments supplied for Select-type expression");
    }

    const Value& arg = frame->value_stack().Peek();
    if (auto unwrapped = arg.AsParsedMessage();
        unwrapped.has_value() &&
        SupportsCachedFieldDescriptor(*unwrapped, descriptor_,
                                      field_descriptor_)) {
      return EvaluateHas(frame, *unwrapped);
    } else if (const google::protobuf::Message* legacy_message =
                   cel::interop_internal::GetLegacyMessage(arg);
               legacy_message != nullptr) {
      cel::ParsedMessageValue parsed_message =
          cel::UnsafeParsedMessageValue(legacy_message);
      if (SupportsCachedFieldDescriptor(parsed_message, descriptor_,
                                        field_descriptor_)) {
        return EvaluateHas(frame, parsed_message);
      }
    }
    // If we get an unexpected value type, fall back to the generic
    // implementation.
    return SelectStep::Evaluate(frame);
  }

 private:
  absl::Status EvaluateHas(ExecutionFrame* frame,
                           const cel::ParsedMessageValue& parsed_message) const;

  const google::protobuf::Descriptor* descriptor_;
  const google::protobuf::FieldDescriptor* field_descriptor_;
};

absl::Status ProtoHasStep::EvaluateHas(
    ExecutionFrame* frame,
    const cel::ParsedMessageValue& parsed_message) const {
  if (CheckAttributeTrail(field_, frame)) {
    return absl::OkStatus();
  }
  frame->value_stack().Peek() =
      BoolValue{parsed_message.HasField(field_descriptor_)};
  return absl::OkStatus();
}

}  // namespace

std::unique_ptr<DirectExpressionStep> CreateDirectSelectStep(
    std::unique_ptr<DirectExpressionStep> operand, StringValue field,
    bool test_only, int64_t expr_id, bool enable_wrapper_type_null_unboxing,
    bool enable_optional_types) {
  return std::make_unique<DirectSelectStep>(
      expr_id, std::move(operand), std::move(field), test_only,
      enable_wrapper_type_null_unboxing, enable_optional_types);
}

// Factory method for Select - based Execution step
absl::StatusOr<std::unique_ptr<ExpressionStep>> CreateSelectStep(
    cel::StringValue field, bool test_only, int64_t expr_id,
    bool enable_wrapper_type_null_unboxing, bool enable_optional_types) {
  return std::make_unique<SelectStep>(std::move(field), test_only, expr_id,
                                      enable_wrapper_type_null_unboxing,
                                      enable_optional_types);
}

// Factory method for Select - based Execution step
absl::StatusOr<std::unique_ptr<ExpressionStep>> CreateTypedSelectStep(
    cel::StringValue field, cel::StructType resolved_operand_type,
    cel::StructTypeField resolved_field, bool test_only, int64_t expr_id,
    bool enable_wrapper_type_null_unboxing, bool enable_optional_types) {
  if (!resolved_operand_type.IsMessage()) {
    // The specialization only supports messages. Fallback to the generic
    // implementation for other types.
    // TODO(uncreated-issue/89): support optional select and chaining.
    return CreateSelectStep(std::move(field), test_only, expr_id,
                            enable_wrapper_type_null_unboxing,
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
    return CreateSelectStep(std::move(field), test_only, expr_id,
                            enable_wrapper_type_null_unboxing,
                            enable_optional_types);
  }

  if (test_only) {
    return std::make_unique<ProtoHasStep>(
        std::move(field), expr_id, enable_wrapper_type_null_unboxing,
        enable_optional_types, descriptor, field_descriptor);
  }

  return std::make_unique<ProtoSelectStep>(
      std::move(field), expr_id, enable_wrapper_type_null_unboxing,
      enable_optional_types, descriptor, field_descriptor);
}

}  // namespace google::api::expr::runtime
