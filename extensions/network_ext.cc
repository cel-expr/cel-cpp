// Copyright 2026 Google LLC
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

#include "extensions/network_ext.h"

#include <string>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "common/native_type.h"
#include "common/standard_definitions.h"
#include "common/type.h"
#include "common/typeinfo.h"
#include "common/value.h"
#include "eval/public/cel_function_registry.h"
#include "eval/public/cel_options.h"
#include "extensions/network_ext_decls.h"
#include "internal/casts.h"
#include "internal/network_addr.h"
#include "internal/status_macros.h"
#include "runtime/function.h"
#include "runtime/function_adapter.h"
#include "runtime/function_registry.h"
#include "runtime/internal/errors.h"
#include "runtime/internal/runtime_friend_access.h"
#include "runtime/internal/runtime_impl.h"
#include "runtime/runtime.h"
#include "runtime/runtime_builder.h"
#include "runtime/runtime_options.h"
#include "runtime/type_registry.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"

namespace cel::extensions {
namespace {

struct IpOpaqueTraits {
  using Rep = internal::IpAddr;
  static absl::string_view TypeName() { return "net.IP"; }
  static OpaqueType RuntimeType() { return NetIpType(); }
};

struct CidrOpaqueTraits {
  using Rep = internal::CidrPrefix;
  static absl::string_view TypeName() { return "net.CIDR"; }
  static OpaqueType RuntimeType() { return NetCidrType(); }
};

template <typename Traits>
class NetworkOpaqueValue final : public OpaqueValueInterface {
 public:
  using Rep = typename Traits::Rep;

  explicit NetworkOpaqueValue(const Rep& rep) : rep_(rep) {}

  static const Rep* absl_nullable Unwrap(const OpaqueValue& val) {
    if (val.GetTypeId() != cel::TypeId<NetworkOpaqueValue<Traits>>() ||
        val.interface() == nullptr) {
      return nullptr;
    }
    const auto* self =
        static_cast<const NetworkOpaqueValue<Traits>*>(val.interface());
    return &self->rep_;
  }

  static Value Make(const Rep& rep, google::protobuf::Arena* absl_nonnull arena) {
    return OpaqueValue(
        google::protobuf::Arena::Create<NetworkOpaqueValue<Traits>>(arena, rep), arena);
  }

 private:
  std::string DebugString() const override { return rep_.ToCanonicalString(); }

  absl::string_view GetTypeName() const override { return Traits::TypeName(); }

  OpaqueType GetRuntimeType() const override { return Traits::RuntimeType(); }

  absl::Status Equal(const OpaqueValue& other,
                     const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
                     google::protobuf::MessageFactory* absl_nonnull message_factory,
                     google::protobuf::Arena* absl_nonnull arena,
                     Value* absl_nonnull result) const override {
    const Rep* other_rep = Unwrap(other);
    *result = BoolValue(other_rep != nullptr && rep_ == *other_rep);
    return absl::OkStatus();
  }

  OpaqueValue Clone(google::protobuf::Arena* absl_nonnull arena) const override {
    return OpaqueValue(
        google::protobuf::Arena::Create<NetworkOpaqueValue<Traits>>(arena, rep_), arena);
  }

  NativeTypeId GetNativeTypeId() const override {
    return cel::TypeId<NetworkOpaqueValue<Traits>>();
  }

  Rep rep_;
};

using IpOpaqueValue = NetworkOpaqueValue<IpOpaqueTraits>;
using CidrOpaqueValue = NetworkOpaqueValue<CidrOpaqueTraits>;

Value NoMatchingOverload(absl::string_view fn,
                         google::protobuf::Arena* absl_nonnull arena) {
  return ErrorValue::From(runtime_internal::CreateNoMatchingOverloadError(fn),
                          arena);
}

template <typename Traits, typename Fn>
absl::Status RegisterOpaqueUnaryMember(FunctionRegistry& registry,
                                       absl::string_view fn_name, Fn fn) {
  return UnaryFunctionAdapter<Value, const OpaqueValue&>::
      RegisterMemberOverload(
          fn_name,
          [fn_name, fn](const OpaqueValue& val,
                        const Function::InvokeContext& context) -> Value {
            const auto* rep = NetworkOpaqueValue<Traits>::Unwrap(val);
            if (rep == nullptr) {
              return NoMatchingOverload(fn_name, context.arena());
            }
            return fn(*rep, context.arena());
          },
          registry);
}

Value IpFromString(const StringValue& raw,
                   const Function::InvokeContext& context) {
  std::string scratch;
  absl::string_view raw_view = raw.ToStringView(&scratch);
  CEL_ASSIGN_OR_RETURN(internal::IpAddr ip, internal::ParseIp(raw_view),
                       _.With(ErrorValueReturn(context.arena())));
  return IpOpaqueValue::Make(ip, context.arena());
}

Value CidrFromString(const StringValue& raw,
                     const Function::InvokeContext& context) {
  std::string scratch;
  absl::string_view raw_view = raw.ToStringView(&scratch);
  CEL_ASSIGN_OR_RETURN(internal::CidrPrefix cidr, internal::ParseCidr(raw_view),
                       _.With(ErrorValueReturn(context.arena())));
  return CidrOpaqueValue::Make(cidr, context.arena());
}

Value StringFromOpaque(const OpaqueValue& val,
                       const Function::InvokeContext& context) {
  if (const internal::IpAddr* ip = IpOpaqueValue::Unwrap(val); ip != nullptr) {
    return StringValue::From(ip->ToCanonicalString(), context.arena());
  }
  if (const internal::CidrPrefix* cidr = CidrOpaqueValue::Unwrap(val);
      cidr != nullptr) {
    return StringValue::From(cidr->ToCanonicalString(), context.arena());
  }
  return NoMatchingOverload(StandardFunctions::kString, context.arena());
}

Value CidrContainsIp(const OpaqueValue& cidr_val, const OpaqueValue& ip_val,
                     const Function::InvokeContext& context) {
  const internal::CidrPrefix* cidr = CidrOpaqueValue::Unwrap(cidr_val);
  const internal::IpAddr* ip = IpOpaqueValue::Unwrap(ip_val);
  if (cidr == nullptr || ip == nullptr) {
    return NoMatchingOverload("containsIP", context.arena());
  }
  return BoolValue(cidr->ContainsIp(*ip));
}

Value CidrContainsIpString(const OpaqueValue& cidr_val,
                           const StringValue& ip_str,
                           const Function::InvokeContext& context) {
  const internal::CidrPrefix* cidr = CidrOpaqueValue::Unwrap(cidr_val);
  if (cidr == nullptr) {
    return NoMatchingOverload("containsIP", context.arena());
  }
  std::string scratch;
  absl::string_view raw_view = ip_str.ToStringView(&scratch);
  CEL_ASSIGN_OR_RETURN(internal::IpAddr ip, internal::ParseIp(raw_view),
                       _.With(ErrorValueReturn(context.arena())));
  return BoolValue(cidr->ContainsIp(ip));
}

Value CidrContainsCidr(const OpaqueValue& parent_val,
                       const OpaqueValue& child_val,
                       const Function::InvokeContext& context) {
  const internal::CidrPrefix* parent = CidrOpaqueValue::Unwrap(parent_val);
  const internal::CidrPrefix* child = CidrOpaqueValue::Unwrap(child_val);
  if (parent == nullptr || child == nullptr) {
    return NoMatchingOverload("containsCIDR", context.arena());
  }
  return BoolValue(parent->ContainsCidr(*child));
}

Value CidrContainsCidrString(const OpaqueValue& parent_val,
                             const StringValue& child_str,
                             const Function::InvokeContext& context) {
  const internal::CidrPrefix* parent = CidrOpaqueValue::Unwrap(parent_val);
  if (parent == nullptr) {
    return NoMatchingOverload("containsCIDR", context.arena());
  }
  std::string scratch;
  absl::string_view raw_view = child_str.ToStringView(&scratch);
  CEL_ASSIGN_OR_RETURN(internal::CidrPrefix child,
                       internal::ParseCidr(raw_view),
                       _.With(ErrorValueReturn(context.arena())));
  return BoolValue(parent->ContainsCidr(child));
}

bool IsIpString(const StringValue& raw) {
  std::string scratch;
  return internal::ParseIp(raw.ToStringView(&scratch)).ok();
}

bool IsCidrString(const StringValue& raw) {
  std::string scratch;
  return internal::ParseCidr(raw.ToStringView(&scratch)).ok();
}

Value IpIsCanonicalString(const StringValue& raw,
                          const Function::InvokeContext& context) {
  std::string scratch;
  absl::string_view raw_view = raw.ToStringView(&scratch);
  CEL_ASSIGN_OR_RETURN(internal::IpAddr ip, internal::ParseIp(raw_view),
                       _.With(ErrorValueReturn(context.arena())));
  return BoolValue(ip.ToCanonicalString() == raw_view);
}

absl::Status RegisterStringNetworkFunctions(FunctionRegistry& registry) {
  CEL_RETURN_IF_ERROR(
      (UnaryFunctionAdapter<bool, const StringValue&>::RegisterGlobalOverload(
          "isIP", &IsIpString, registry)));
  CEL_RETURN_IF_ERROR(
      (UnaryFunctionAdapter<bool, const StringValue&>::RegisterGlobalOverload(
          "isCIDR", &IsCidrString, registry)));
  CEL_RETURN_IF_ERROR(
      (UnaryFunctionAdapter<Value, const StringValue&>::RegisterGlobalOverload(
          "ip.isCanonical", &IpIsCanonicalString, registry)));
  return absl::OkStatus();
}

}  // namespace

absl::Status RegisterNetworkFunctions(RuntimeBuilder& builder, int version) {
  (void)version;
  Runtime& base_runtime =
      runtime_internal::RuntimeFriendAccess::GetMutableRuntime(builder);
  if (runtime_internal::RuntimeFriendAccess::RuntimeTypeId(base_runtime) !=
      cel::TypeId<runtime_internal::RuntimeImpl>()) {
    return absl::UnimplementedError(
        "network_ext is only supported on the default cel::Runtime "
        "implementation");
  }
  auto& runtime =
      cel::internal::down_cast<runtime_internal::RuntimeImpl&>(base_runtime);
  const RuntimeOptions& options = runtime.expr_builder().options();
  if (!options.enable_qualified_type_identifiers) {
    return absl::FailedPreconditionError(
        "network_ext requires "
        "RuntimeOptions.enable_qualified_type_identifiers");
  }
  // NOLINTNEXTLINE(clang-diagnostic-deprecated-declarations)
  if (!options.enable_heterogeneous_equality) {
    return absl::FailedPreconditionError(
        "network_ext requires RuntimeOptions.enable_heterogeneous_equality");
  }

  CEL_RETURN_IF_ERROR(builder.type_registry().RegisterType(NetIpType()));
  CEL_RETURN_IF_ERROR(builder.type_registry().RegisterType(NetCidrType()));

  FunctionRegistry& registry = builder.function_registry();
  CEL_RETURN_IF_ERROR(RegisterStringNetworkFunctions(registry));

  CEL_RETURN_IF_ERROR(
      (UnaryFunctionAdapter<Value, const StringValue&>::RegisterGlobalOverload(
          "ip", &IpFromString, registry)));
  CEL_RETURN_IF_ERROR(
      (UnaryFunctionAdapter<Value, const StringValue&>::RegisterGlobalOverload(
          "cidr", &CidrFromString, registry)));
  if (options.enable_string_conversion) {
    CEL_RETURN_IF_ERROR((
        UnaryFunctionAdapter<Value, const OpaqueValue&>::RegisterGlobalOverload(
            StandardFunctions::kString, &StringFromOpaque, registry)));
  }

  CEL_RETURN_IF_ERROR(RegisterOpaqueUnaryMember<CidrOpaqueTraits>(
      registry, "ip",
      [](const internal::CidrPrefix& cidr, google::protobuf::Arena* absl_nonnull arena) {
        return IpOpaqueValue::Make(cidr.ip, arena);
      }));
  CEL_RETURN_IF_ERROR(RegisterOpaqueUnaryMember<CidrOpaqueTraits>(
      registry, "masked",
      [](const internal::CidrPrefix& cidr, google::protobuf::Arena* absl_nonnull arena) {
        return CidrOpaqueValue::Make(cidr.Masked(), arena);
      }));
  CEL_RETURN_IF_ERROR(RegisterOpaqueUnaryMember<CidrOpaqueTraits>(
      registry, "isMask",
      [](const internal::CidrPrefix& cidr, google::protobuf::Arena* absl_nonnull) {
        return BoolValue(cidr.IsMask());
      }));
  CEL_RETURN_IF_ERROR(RegisterOpaqueUnaryMember<CidrOpaqueTraits>(
      registry, "prefixLength",
      [](const internal::CidrPrefix& cidr, google::protobuf::Arena* absl_nonnull) {
        return IntValue(cidr.prefix_length);
      }));

  CEL_RETURN_IF_ERROR(
      (BinaryFunctionAdapter<Value, const OpaqueValue&, const OpaqueValue&>::
           RegisterMemberOverload("containsIP", &CidrContainsIp, registry)));
  CEL_RETURN_IF_ERROR(
      (BinaryFunctionAdapter<Value, const OpaqueValue&, const StringValue&>::
           RegisterMemberOverload("containsIP", &CidrContainsIpString,
                                  registry)));
  CEL_RETURN_IF_ERROR((
      BinaryFunctionAdapter<Value, const OpaqueValue&, const OpaqueValue&>::
          RegisterMemberOverload("containsCIDR", &CidrContainsCidr, registry)));
  CEL_RETURN_IF_ERROR(
      (BinaryFunctionAdapter<Value, const OpaqueValue&, const StringValue&>::
           RegisterMemberOverload("containsCIDR", &CidrContainsCidrString,
                                  registry)));

  CEL_RETURN_IF_ERROR(RegisterOpaqueUnaryMember<IpOpaqueTraits>(
      registry, "family",
      [](const internal::IpAddr& ip, google::protobuf::Arena* absl_nonnull) {
        return IntValue(ip.Family());
      }));
  CEL_RETURN_IF_ERROR(RegisterOpaqueUnaryMember<IpOpaqueTraits>(
      registry, "isUnspecified",
      [](const internal::IpAddr& ip, google::protobuf::Arena* absl_nonnull) {
        return BoolValue(ip.IsUnspecified());
      }));
  CEL_RETURN_IF_ERROR(RegisterOpaqueUnaryMember<IpOpaqueTraits>(
      registry, "isLoopback",
      [](const internal::IpAddr& ip, google::protobuf::Arena* absl_nonnull) {
        return BoolValue(ip.IsLoopback());
      }));
  CEL_RETURN_IF_ERROR(RegisterOpaqueUnaryMember<IpOpaqueTraits>(
      registry, "isGlobalUnicast",
      [](const internal::IpAddr& ip, google::protobuf::Arena* absl_nonnull) {
        return BoolValue(ip.IsGlobalUnicast());
      }));
  CEL_RETURN_IF_ERROR(RegisterOpaqueUnaryMember<IpOpaqueTraits>(
      registry, "isLinkLocalMulticast",
      [](const internal::IpAddr& ip, google::protobuf::Arena* absl_nonnull) {
        return BoolValue(ip.IsLinkLocalMulticast());
      }));
  CEL_RETURN_IF_ERROR(RegisterOpaqueUnaryMember<IpOpaqueTraits>(
      registry, "isLinkLocalUnicast",
      [](const internal::IpAddr& ip, google::protobuf::Arena* absl_nonnull) {
        return BoolValue(ip.IsLinkLocalUnicast());
      }));

  return absl::OkStatus();
}

absl::Status RegisterNetworkFunctions(
    google::api::expr::runtime::CelFunctionRegistry* absl_nonnull registry,
    const google::api::expr::runtime::InterpreterOptions&, int version) {
  (void)version;
  if (registry == nullptr) {
    return absl::InvalidArgumentError("registry must not be null");
  }
  return RegisterStringNetworkFunctions(registry->InternalGetRegistry());
}

}  // namespace cel::extensions
