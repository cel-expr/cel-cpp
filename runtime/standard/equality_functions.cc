// Copyright 2022 Google LLC
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

#include "runtime/standard/equality_functions.h"

#include <cstdint>
#include <memory>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "base/builtins.h"
#include "common/function_descriptor.h"
#include "common/kind.h"
#include "common/value.h"
#include "common/value_kind.h"
#include "internal/number.h"
#include "internal/status_macros.h"
#include "runtime/function.h"
#include "runtime/function_registry.h"
#include "runtime/internal/errors.h"
#include "runtime/runtime_options.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"

namespace cel {
namespace {

using ::cel::builtin::kEqual;
using ::cel::builtin::kInequal;
using ::cel::internal::Number;

// Declaration for the functors for generic equality operator.
// Equal only defined for same-typed values.
// Nullopt is returned if equality is not defined.
struct HomogenousEqualProvider {
  static constexpr bool kIsHeterogeneous = false;
  absl::StatusOr<absl::optional<bool>> operator()(
      const Value& lhs, const Value& rhs,
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena) const;
};

// Equal defined between compatible types.
// Nullopt is returned if equality is not defined.
struct HeterogeneousEqualProvider {
  static constexpr bool kIsHeterogeneous = true;

  absl::StatusOr<absl::optional<bool>> operator()(
      const Value& lhs, const Value& rhs,
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Arena* absl_nonnull arena) const;
};

// Equality for lists. Template parameter provides either heterogeneous or
// homogenous equality for comparing members.
template <typename EqualsProvider>
absl::StatusOr<absl::optional<bool>> ListEqual(
    const ListValue& lhs, const ListValue& rhs,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena) {
  if (&lhs == &rhs) {
    return true;
  }
  CEL_ASSIGN_OR_RETURN(auto lhs_size, lhs.Size());
  CEL_ASSIGN_OR_RETURN(auto rhs_size, rhs.Size());
  if (lhs_size != rhs_size) {
    return false;
  }

  for (int i = 0; i < lhs_size; ++i) {
    CEL_ASSIGN_OR_RETURN(auto lhs_i,
                         lhs.Get(i, descriptor_pool, message_factory, arena));
    CEL_ASSIGN_OR_RETURN(auto rhs_i,
                         rhs.Get(i, descriptor_pool, message_factory, arena));
    CEL_ASSIGN_OR_RETURN(absl::optional<bool> eq,
                         EqualsProvider()(lhs_i, rhs_i, descriptor_pool,
                                          message_factory, arena));
    if (!eq.has_value() || !*eq) {
      return eq;
    }
  }
  return true;
}

// Opaque types only support heterogeneous equality, and by extension that means
// optionals. Heterogeneous equality being enabled is enforced by
// `EnableOptionalTypes`.
absl::StatusOr<absl::optional<bool>> OpaqueEqual(
    const OpaqueValue& lhs, const OpaqueValue& rhs,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena) {
  Value result;
  CEL_RETURN_IF_ERROR(
      lhs.Equal(rhs, descriptor_pool, message_factory, arena, &result));
  if (auto bool_value = result.AsBool(); bool_value) {
    return bool_value->NativeValue();
  }
  return common_internal::MakeTypeConversionError(result.GetTypeName(), "bool");
}

absl::optional<Number> NumberFromValue(const Value& value) {
  if (value.Is<IntValue>()) {
    return Number::FromInt64(value.GetInt().NativeValue());
  } else if (value.Is<UintValue>()) {
    return Number::FromUint64(value.GetUint().NativeValue());
  } else if (value.Is<DoubleValue>()) {
    return Number::FromDouble(value.GetDouble().NativeValue());
  }

  return absl::nullopt;
}

absl::StatusOr<absl::optional<Value>> CheckAlternativeNumericType(
    const Value& key, const MapValue& rhs,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena) {
  absl::optional<Number> number = NumberFromValue(key);

  if (!number.has_value()) {
    return absl::nullopt;
  }

  if (!key.IsInt() && number->LosslessConvertibleToInt()) {
    absl::optional<Value> entry;
    CEL_ASSIGN_OR_RETURN(entry,
                         rhs.Find(IntValue(number->AsInt()), descriptor_pool,
                                  message_factory, arena));
    if (entry) {
      return entry;
    }
  }

  if (!key.IsUint() && number->LosslessConvertibleToUint()) {
    absl::optional<Value> entry;
    CEL_ASSIGN_OR_RETURN(entry,
                         rhs.Find(UintValue(number->AsUint()), descriptor_pool,
                                  message_factory, arena));
    if (entry) {
      return entry;
    }
  }

  return absl::nullopt;
}

// Equality for maps. Template parameter provides either heterogeneous or
// homogenous equality for comparing values.
template <typename EqualsProvider>
absl::StatusOr<absl::optional<bool>> MapEqual(
    const MapValue& lhs, const MapValue& rhs,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena) {
  if (&lhs == &rhs) {
    return true;
  }
  if (lhs.Size() != rhs.Size()) {
    return false;
  }

  CEL_ASSIGN_OR_RETURN(auto iter, lhs.NewIterator());

  while (iter->HasNext()) {
    CEL_ASSIGN_OR_RETURN(auto lhs_key,
                         iter->Next(descriptor_pool, message_factory, arena));

    absl::optional<Value> entry;
    CEL_ASSIGN_OR_RETURN(
        entry, rhs.Find(lhs_key, descriptor_pool, message_factory, arena));

    if (!entry && EqualsProvider::kIsHeterogeneous) {
      CEL_ASSIGN_OR_RETURN(
          entry, CheckAlternativeNumericType(lhs_key, rhs, descriptor_pool,
                                             message_factory, arena));
    }
    if (!entry) {
      return false;
    }

    CEL_ASSIGN_OR_RETURN(auto lhs_value, lhs.Get(lhs_key, descriptor_pool,
                                                 message_factory, arena));
    CEL_ASSIGN_OR_RETURN(absl::optional<bool> eq,
                         EqualsProvider()(lhs_value, *entry, descriptor_pool,
                                          message_factory, arena));

    if (!eq.has_value() || !*eq) {
      return eq;
    }
  }

  return true;
}

template <typename EqualsProvider>
absl::StatusOr<absl::optional<bool>> HomogenousValueEqual(
    const Value& v1, const Value& v2,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena) {
  if (v1.kind() != v2.kind()) {
    return absl::nullopt;
  }

  static_assert(std::is_lvalue_reference_v<decltype(v1.GetString())>,
                "unexpected value copy");

  switch (v1.kind()) {
    case ValueKind::kBool:
      return v1.GetBool().NativeValue() == v2.GetBool().NativeValue();
    case ValueKind::kNull:
      return true;
    case ValueKind::kInt:
      return v1.GetInt().NativeValue() == v2.GetInt().NativeValue();
    case ValueKind::kUint:
      return v1.GetUint().NativeValue() == v2.GetUint().NativeValue();
    case ValueKind::kDouble:
      return v1.GetDouble().NativeValue() == v2.GetDouble().NativeValue();
    case ValueKind::kDuration:
      return v1.GetDuration().ToDuration() == v2.GetDuration().ToDuration();
    case ValueKind::kTimestamp:
      return v1.GetTimestamp().ToTime() == v2.GetTimestamp().ToTime();
    case ValueKind::kCelType:
      return v1.GetType().name() == v2.GetType().name();
    case ValueKind::kString:
      return v1.GetString().Equals(v2.GetString());
    case ValueKind::kBytes:
      return v1.GetBytes().Equals(v2.GetBytes());
    case ValueKind::kList:
      return ListEqual<EqualsProvider>(v1.GetList(), v2.GetList(),
                                       descriptor_pool, message_factory, arena);
    case ValueKind::kMap:
      return MapEqual<EqualsProvider>(v1.GetMap(), v2.GetMap(), descriptor_pool,
                                      message_factory, arena);
    case ValueKind::kOpaque:
      return OpaqueEqual(v1.GetOpaque(), v2.GetOpaque(), descriptor_pool,
                         message_factory, arena);
    default:
      return absl::nullopt;
  }
}

class HomogenousEqualityFunction final : public Function {
 public:
  explicit HomogenousEqualityFunction(bool negation) : negation_(negation) {}

  absl::StatusOr<Value> Invoke(absl::Span<const Value> args,
                               const InvokeContext& context) const override {
    if (args.size() != 2) {
      return absl::InvalidArgumentError(
          "unexpected number of arguments for binary function");
    }
    if ((args[0].IsStruct() && args[1].IsNull()) ||
        (args[0].IsNull() && args[1].IsStruct())) {
      return BoolValue(negation_);
    }
    CEL_ASSIGN_OR_RETURN(absl::optional<bool> result,
                         HomogenousValueEqual<HomogenousEqualProvider>(
                             args[0], args[1], context.descriptor_pool(),
                             context.message_factory(), context.arena()));
    if (!result.has_value()) {
      return ErrorValue::From(
          cel::runtime_internal::CreateNoMatchingOverloadError(
              negation_ ? kInequal : kEqual),
          context.arena());
    }
    return BoolValue(negation_ ? !*result : *result);
  }

 private:
  bool negation_;
};

class HeterogeneousEqualityFunction final : public Function {
 public:
  explicit HeterogeneousEqualityFunction(bool negation) : negation_(negation) {}

  absl::StatusOr<Value> Invoke(absl::Span<const Value> args,
                               const InvokeContext& context) const override {
    if (args.size() != 2) {
      return absl::InvalidArgumentError(
          "unexpected number of arguments for binary function");
    }
    CEL_ASSIGN_OR_RETURN(absl::optional<bool> result,
                         runtime_internal::ValueEqualImpl(
                             args[0], args[1], context.descriptor_pool(),
                             context.message_factory(), context.arena()));
    if (!result.has_value()) {
      return ErrorValue::From(
          cel::runtime_internal::CreateNoMatchingOverloadError(
              negation_ ? kInequal : kEqual),
          context.arena());
    }
    return BoolValue(negation_ ? !*result : *result);
  }

 private:
  bool negation_;
};

absl::Status RegisterHomogenousPair(FunctionRegistry& registry, Kind lhs,
                                    Kind rhs) {
  CEL_RETURN_IF_ERROR(registry.Register(
      FunctionDescriptor(kInequal, /*is_receiver_style=*/false, {lhs, rhs}),
      std::make_unique<HomogenousEqualityFunction>(/*negation=*/true)));
  return registry.Register(
      FunctionDescriptor(kEqual, /*is_receiver_style=*/false, {lhs, rhs}),
      std::make_unique<HomogenousEqualityFunction>(/*negation=*/false));
}

absl::Status RegisterHomogenousEqualityFunctions(
    cel::FunctionRegistry& registry) {
  static constexpr Kind kKinds[] = {
      Kind::kBool,   Kind::kInt,   Kind::kUint,     Kind::kDouble,
      Kind::kString, Kind::kBytes, Kind::kDuration, Kind::kTimestamp,
      Kind::kNull,   Kind::kType,  Kind::kList,     Kind::kMap,
  };
  for (Kind kind : kKinds) {
    CEL_RETURN_IF_ERROR(RegisterHomogenousPair(registry, kind, kind));
  }
  return absl::OkStatus();
}

absl::Status RegisterNullMessageEqualityFunctions(FunctionRegistry& registry) {
  CEL_RETURN_IF_ERROR(
      RegisterHomogenousPair(registry, Kind::kStruct, Kind::kNull));
  return RegisterHomogenousPair(registry, Kind::kNull, Kind::kStruct);
}

absl::Status RegisterHeterogeneousEqualityFunctions(
    cel::FunctionRegistry& registry) {
  CEL_RETURN_IF_ERROR(registry.Register(
      FunctionDescriptor(kEqual, /*is_receiver_style=*/false,
                         {Kind::kAny, Kind::kAny}),
      std::make_unique<HeterogeneousEqualityFunction>(/*negation=*/false)));

  return registry.Register(
      FunctionDescriptor(kInequal, /*is_receiver_style=*/false,
                         {Kind::kAny, Kind::kAny}),
      std::make_unique<HeterogeneousEqualityFunction>(/*negation=*/true));
}

absl::StatusOr<absl::optional<bool>> HomogenousEqualProvider::operator()(
    const Value& lhs, const Value& rhs,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena) const {
  return HomogenousValueEqual<HomogenousEqualProvider>(
      lhs, rhs, descriptor_pool, message_factory, arena);
}

absl::StatusOr<absl::optional<bool>> HeterogeneousEqualProvider::operator()(
    const Value& lhs, const Value& rhs,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena) const {
  return runtime_internal::ValueEqualImpl(lhs, rhs, descriptor_pool,
                                          message_factory, arena);
}

}  // namespace

namespace runtime_internal {

absl::StatusOr<absl::optional<bool>> ValueEqualImpl(
    const Value& v1, const Value& v2,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena) {
  if (v1.kind() == v2.kind()) {
    if (v1.IsStruct() && v2.IsStruct()) {
      CEL_ASSIGN_OR_RETURN(
          Value result,
          v1.GetStruct().Equal(v2, descriptor_pool, message_factory, arena));
      if (result.IsBool()) {
        return result.GetBool().NativeValue();
      }
      return false;
    }
    return HomogenousValueEqual<HeterogeneousEqualProvider>(
        v1, v2, descriptor_pool, message_factory, arena);
  }

  absl::optional<Number> lhs = NumberFromValue(v1);
  absl::optional<Number> rhs = NumberFromValue(v2);

  if (rhs.has_value() && lhs.has_value()) {
    return *lhs == *rhs;
  }

  // TODO(uncreated-issue/6): It's currently possible for the interpreter to create a
  // map containing an Error. Return no matching overload to propagate an error
  // instead of a false result.
  if (v1.IsError() || v1.IsUnknown() || v2.IsError() || v2.IsUnknown()) {
    return absl::nullopt;
  }

  return false;
}

}  // namespace runtime_internal

absl::Status RegisterEqualityFunctions(FunctionRegistry& registry,
                                       const RuntimeOptions& options) {
  if (options.enable_heterogeneous_equality) {
    if (options.enable_fast_builtins) {
      // If enabled, the evaluator provides an implementation that works
      // directly on the value stack.
      return absl::OkStatus();
    }
    // Heterogeneous equality uses one generic overload that delegates to the
    // right equality implementation at runtime.
    CEL_RETURN_IF_ERROR(RegisterHeterogeneousEqualityFunctions(registry));
  } else {
    CEL_RETURN_IF_ERROR(RegisterHomogenousEqualityFunctions(registry));

    CEL_RETURN_IF_ERROR(RegisterNullMessageEqualityFunctions(registry));
  }
  return absl::OkStatus();
}

}  // namespace cel
