#include "eval/public/activation_bind_helper.h"

#include "absl/status/status.h"
#include "absl/types/span.h"
#include "eval/public/activation.h"
#include "eval/public/containers/field_access.h"
#include "eval/public/containers/field_backed_list_impl.h"
#include "eval/public/containers/field_backed_map_impl.h"
#include "google/protobuf/message.h"

namespace google {
namespace api {
namespace expr {
namespace runtime {

namespace {

using google::protobuf::Arena;
using google::protobuf::Message;
using google::protobuf::FieldDescriptor;
using google::protobuf::Descriptor;

absl::Status CreateValueFromField(const google::protobuf::Message* msg,
                                  const FieldDescriptor* field_desc,
                                  google::protobuf::Arena* arena, CelValue* result) {
  if (field_desc->is_map()) {
    *result = CelValue::CreateMap(google::protobuf::Arena::Create<FieldBackedMapImpl>(
        arena, msg, field_desc, arena));
    return absl::OkStatus();
  } else if (field_desc->is_repeated()) {
    *result = CelValue::CreateList(google::protobuf::Arena::Create<FieldBackedListImpl>(
        arena, msg, field_desc, arena));
    return absl::OkStatus();
  } else {
    return CreateValueFromSingleField(msg, field_desc, arena, result);
  }
}

// Binds a field of the message to the activation. With kSkip, an unset
// non-repeated field is not bound.
absl::Status BindField(const Message* message,
                       const google::protobuf::Reflection* reflection,
                       const FieldDescriptor* field_desc, Arena* arena,
                       Activation* activation, ProtoUnsetFieldOptions options) {
  if (options == ProtoUnsetFieldOptions::kSkip) {
    if (!field_desc->is_repeated() &&
        !reflection->HasField(*message, field_desc)) {
      return absl::OkStatus();
    }
  }

  CelValue value;
  auto status = CreateValueFromField(message, field_desc, arena, &value);
  if (!status.ok()) {
    return status;
  }

  activation->InsertValue(field_desc->name(), value);
  return absl::OkStatus();
}

}  // namespace

absl::Status BindProtoToActivation(const Message* message, Arena* arena,
                                   Activation* activation,
                                   ProtoUnsetFieldOptions options) {
  // If we need to bind any types that are backed by an arena allocation, we
  // will cause a memory leak.
  if (arena == nullptr) {
    return absl::InvalidArgumentError(
        "arena must not be null for BindProtoToActivation.");
  }

  // TODO(issues/24): Improve the utilities to bind dynamic values as well.
  const Descriptor* desc = message->GetDescriptor();
  const google::protobuf::Reflection* reflection = message->GetReflection();
  for (int i = 0; i < desc->field_count(); i++) {
    auto status = BindField(message, reflection, desc->field(i), arena,
                            activation, options);
    if (!status.ok()) {
      return status;
    }
  }

  return absl::OkStatus();
}

absl::Status BindProtoToActivation(
    const Message* message, Arena* arena, Activation* activation,
    absl::Span<const FieldDescriptor* const> fields,
    ProtoUnsetFieldOptions options) {
  // If we need to bind any types that are backed by an arena allocation, we
  // will cause a memory leak.
  if (arena == nullptr) {
    return absl::InvalidArgumentError(
        "arena must not be null for BindProtoToActivation.");
  }

  const Descriptor* desc = message->GetDescriptor();
  const google::protobuf::Reflection* reflection = message->GetReflection();
  for (const FieldDescriptor* field_desc : fields) {
    if (field_desc == nullptr || field_desc->is_extension() ||
        field_desc->containing_type() != desc) {
      return absl::InvalidArgumentError(
          "fields must be non-null, non-extension fields of the message for "
          "BindProtoToActivation.");
    }
    auto status =
        BindField(message, reflection, field_desc, arena, activation, options);
    if (!status.ok()) {
      return status;
    }
  }

  return absl::OkStatus();
}

}  // namespace runtime
}  // namespace expr
}  // namespace api
}  // namespace google
