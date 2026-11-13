// Copyright 2023 Google LLC
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

#include "runtime/standard/container_membership_functions.h"

#include <array>
#include <cstdint>
#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
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
#include "runtime/runtime_options.h"

namespace cel {
namespace {

using ::cel::internal::Number;

static constexpr std::array<absl::string_view, 3> in_operators = {
    cel::builtin::kIn,            // @in for map and list types.
    cel::builtin::kInFunction,    // deprecated in() -- for backwards compat
    cel::builtin::kInDeprecated,  // deprecated _in_ -- for backwards compat
};

bool HomogenousValueEquals(const Value& lhs, const Value& rhs) {
  if (lhs.kind() != rhs.kind()) {
    return false;
  }
  switch (lhs.kind()) {
    case ValueKind::kBool:
      return lhs.GetBool().NativeValue() == rhs.GetBool().NativeValue();
    case ValueKind::kInt:
      return lhs.GetInt().NativeValue() == rhs.GetInt().NativeValue();
    case ValueKind::kUint:
      return lhs.GetUint().NativeValue() == rhs.GetUint().NativeValue();
    case ValueKind::kDouble:
      return lhs.GetDouble().NativeValue() == rhs.GetDouble().NativeValue();
    case ValueKind::kString:
      return lhs.GetString().Equals(rhs.GetString());
    case ValueKind::kBytes:
      return lhs.GetBytes().Equals(rhs.GetBytes());
    default:
      return false;
  }
}

absl::StatusOr<Value> EvaluateHomogenousListIn(
    const Value& key, const ListValue& list,
    const Function::InvokeContext& context) {
  switch (key.kind()) {
    case ValueKind::kBool:
    case ValueKind::kInt:
    case ValueKind::kUint:
    case ValueKind::kDouble:
    case ValueKind::kString:
    case ValueKind::kBytes:
      break;
    default:
      return absl::InvalidArgumentError("unsupported key kind for list in");
  }
  CEL_ASSIGN_OR_RETURN(auto size, list.Size());
  Value element;
  for (int i = 0; i < size; i++) {
    CEL_RETURN_IF_ERROR(list.Get(i, context.descriptor_pool(),
                                 context.message_factory(), context.arena(),
                                 &element));
    if (HomogenousValueEquals(element, key)) {
      return BoolValue(true);
    }
  }
  return BoolValue(false);
}

absl::StatusOr<Value> EvaluateMapIn(const Value& key, const MapValue& map_value,
                                    bool enable_heterogeneous_equality,
                                    const Function::InvokeContext& context) {
  switch (key.kind()) {
    case ValueKind::kString: {
      Value result;
      CEL_RETURN_IF_ERROR(map_value.Has(key, context.descriptor_pool(),
                                        context.message_factory(),
                                        context.arena(), &result));
      if (result.IsBool()) {
        return result;
      }
      if (enable_heterogeneous_equality) {
        return BoolValue(false);
      }
      return result;
    }
    case ValueKind::kBool: {
      Value has;
      CEL_RETURN_IF_ERROR(map_value.Has(key, context.descriptor_pool(),
                                        context.message_factory(),
                                        context.arena(), &has));
      if (has.IsTrue()) {
        return has;
      }
      if (enable_heterogeneous_equality) {
        return BoolValue(false);
      }
      return has;
    }
    case ValueKind::kInt: {
      Value result;
      CEL_RETURN_IF_ERROR(map_value.Has(key, context.descriptor_pool(),
                                        context.message_factory(),
                                        context.arena(), &result));
      if (enable_heterogeneous_equality) {
        if (result.IsTrue()) {
          return result;
        }
        Number number = Number::FromInt64(key.GetInt().NativeValue());
        if (number.LosslessConvertibleToUint()) {
          Value result_alt;
          CEL_RETURN_IF_ERROR(map_value.Has(
              UintValue(number.AsUint()), context.descriptor_pool(),
              context.message_factory(), context.arena(), &result_alt));
          if (result_alt.IsTrue()) {
            return result_alt;
          }
        }
        return BoolValue(false);
      }
      return result;
    }
    case ValueKind::kUint: {
      Value has;
      CEL_RETURN_IF_ERROR(map_value.Has(key, context.descriptor_pool(),
                                        context.message_factory(),
                                        context.arena(), &has));
      if (enable_heterogeneous_equality) {
        if (has.IsTrue()) {
          return has;
        }
        Number number = Number::FromUint64(key.GetUint().NativeValue());
        if (number.LosslessConvertibleToInt()) {
          Value has_alt;
          CEL_RETURN_IF_ERROR(map_value.Has(
              IntValue(number.AsInt()), context.descriptor_pool(),
              context.message_factory(), context.arena(), &has_alt));
          if (has_alt.IsTrue()) {
            return has_alt;
          }
        }
        return BoolValue(false);
      }
      return has;
    }
    case ValueKind::kDouble: {
      if (!enable_heterogeneous_equality) {
        return absl::InvalidArgumentError(
            "double key not supported for homogenous map in");
      }
      Number number = Number::FromDouble(key.GetDouble().NativeValue());
      if (number.LosslessConvertibleToInt()) {
        Value has;
        CEL_RETURN_IF_ERROR(
            map_value.Has(IntValue(number.AsInt()), context.descriptor_pool(),
                          context.message_factory(), context.arena(), &has));
        if (has.IsTrue()) {
          return has;
        }
      }
      if (number.LosslessConvertibleToUint()) {
        Value has;
        CEL_RETURN_IF_ERROR(
            map_value.Has(UintValue(number.AsUint()), context.descriptor_pool(),
                          context.message_factory(), context.arena(), &has));
        if (has.IsTrue()) {
          return has;
        }
      }
      return BoolValue(false);
    }
    default:
      return absl::InvalidArgumentError("unsupported key kind for map in");
  }
}

class ContainerMembershipFunction final : public Function {
 public:
  explicit ContainerMembershipFunction(bool enable_heterogeneous_equality)
      : enable_heterogeneous_equality_(enable_heterogeneous_equality) {}

  absl::StatusOr<Value> Invoke(absl::Span<const Value> args,
                               const InvokeContext& context) const override {
    if (args.size() != 2) {
      return absl::InvalidArgumentError(
          "unexpected number of arguments for binary function");
    }
    switch (args[1].kind()) {
      case ValueKind::kList:
        if (enable_heterogeneous_equality_) {
          return args[1].GetList().Contains(args[0], context.descriptor_pool(),
                                            context.message_factory(),
                                            context.arena());
        }
        return EvaluateHomogenousListIn(args[0], args[1].GetList(), context);
      case ValueKind::kMap:
        return EvaluateMapIn(args[0], args[1].GetMap(),
                             enable_heterogeneous_equality_, context);
      default:
        return absl::InvalidArgumentError("expected list or map value");
    }
  }

 private:
  bool enable_heterogeneous_equality_;
};

absl::Status RegisterListMembershipFunctions(FunctionRegistry& registry,
                                             const RuntimeOptions& options) {
  static constexpr Kind kHomogenousListKeyKinds[] = {
      Kind::kBool,   Kind::kInt,    Kind::kUint,
      Kind::kDouble, Kind::kString, Kind::kBytes,
  };
  for (absl::string_view op : in_operators) {
    if (options.enable_heterogeneous_equality) {
      CEL_RETURN_IF_ERROR(registry.Register(
          FunctionDescriptor(op, /*is_receiver_style=*/false,
                             {Kind::kAny, Kind::kList}),
          std::make_unique<ContainerMembershipFunction>(true)));
    } else {
      for (Kind kind : kHomogenousListKeyKinds) {
        CEL_RETURN_IF_ERROR(registry.Register(
            FunctionDescriptor(op, /*is_receiver_style=*/false,
                               {kind, Kind::kList}),
            std::make_unique<ContainerMembershipFunction>(false)));
      }
    }
  }
  return absl::OkStatus();
}

absl::Status RegisterMapMembershipFunctions(FunctionRegistry& registry,
                                            const RuntimeOptions& options) {
  const bool enable_heterogeneous_equality =
      options.enable_heterogeneous_equality;
  static constexpr Kind kMapKeyKinds[] = {
      Kind::kString,
      Kind::kBool,
      Kind::kInt,
      Kind::kUint,
  };
  for (absl::string_view op : in_operators) {
    for (Kind kind : kMapKeyKinds) {
      CEL_RETURN_IF_ERROR(
          registry.Register(FunctionDescriptor(op, /*is_receiver_style=*/false,
                                               {kind, Kind::kMap}),
                            std::make_unique<ContainerMembershipFunction>(
                                enable_heterogeneous_equality)));
    }
    if (enable_heterogeneous_equality) {
      CEL_RETURN_IF_ERROR(registry.Register(
          FunctionDescriptor(op, /*is_receiver_style=*/false,
                             {Kind::kDouble, Kind::kMap}),
          std::make_unique<ContainerMembershipFunction>(true)));
    }
  }
  return absl::OkStatus();
}

}  // namespace

absl::Status RegisterContainerMembershipFunctions(
    FunctionRegistry& registry, const RuntimeOptions& options) {
  if (options.enable_list_contains) {
    CEL_RETURN_IF_ERROR(RegisterListMembershipFunctions(registry, options));
  }
  return RegisterMapMembershipFunctions(registry, options);
}

}  // namespace cel
