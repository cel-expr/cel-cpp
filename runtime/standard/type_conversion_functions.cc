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

#include "runtime/standard/type_conversion_functions.h"

#include <charconv>
#include <cstdint>
#include <memory>
#include <system_error>  // NOLINT (required for std::to_chars_result)
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "base/builtins.h"
#include "common/function_descriptor.h"
#include "common/kind.h"
#include "common/value.h"
#include "common/value_kind.h"
#include "internal/overflow.h"
#include "internal/status_macros.h"
#include "internal/time.h"
#include "internal/utf8.h"
#include "runtime/function.h"
#include "runtime/function_registry.h"
#include "runtime/runtime_options.h"
#include "google/protobuf/arena.h"

#if defined(_LIBCPP_VERSION) && _LIBCPP_VERSION >= 14000 && \
        !defined(__APPLE__) ||                              \
    defined(__GNUC__) && __GNUC__ >= 13 ||                  \
    defined(_MSC_VER) && _MSC_VER >= 1920
#define _CEL_CHAR_CONV_DOUBLE_TO_CHARS 1
#endif

namespace cel {
namespace {

using ::cel::internal::EncodeDurationToJson;
using ::cel::internal::EncodeTimestampToJson;
using ::cel::internal::MaxTimestamp;
using ::cel::internal::MinTimestamp;

Value FormatDouble(double v, const Function::InvokeContext& context) {
  google::protobuf::Arena* arena = context.arena();
#if defined(CEL_NO_CHARCONV_DOUBLE_TO_CHARS) || \
    !defined(_CEL_CHAR_CONV_DOUBLE_TO_CHARS)
  // Fallback to absl::StrFormat. Slower and handles edge cases around precision
  // differently but safe and covers most cases.
  return StringValue::From(absl::StrFormat("%.17g", v), arena);
#else
  constexpr int kBufSize = 32;
  char buf[kBufSize];
  std::to_chars_result result =
      std::to_chars(buf, buf + kBufSize, v, std::chars_format::general);
  if (result.ec != std::errc()) {
    return cel::ErrorValue::From(
        absl::InvalidArgumentError(
            absl::StrCat("double format error: ",
                         std::make_error_code(result.ec).message())),
        context.arena());
  }
  absl::string_view out(buf, result.ptr - buf);
  return StringValue::From(out, arena);
#endif
}

Value LegacyFormatDouble(double v, const Function::InvokeContext& context) {
  return StringValue::From(absl::StrCat(v), context.arena());
}

Value ConvertToBool(const Value& arg, const Function::InvokeContext& context) {
  switch (arg.kind()) {
    case ValueKind::kBool:
      return arg;
    case ValueKind::kString: {
      const StringValue& v = arg.GetString();
      if ((v == "true") || (v == "True") || (v == "TRUE") || (v == "t") ||
          (v == "1")) {
        return TrueValue();
      }
      if ((v == "false") || (v == "FALSE") || (v == "False") || (v == "f") ||
          (v == "0")) {
        return FalseValue();
      }
      return ErrorValue::From(
          absl::InvalidArgumentError(
              "Type conversion error from 'string' to 'bool'"),
          context.arena());
    }
    default:
      return ErrorValue::From(
          absl::InvalidArgumentError("cannot convert value to bool"),
          context.arena());
  }
}

Value ConvertToInt(const Value& arg, const Function::InvokeContext& context) {
  switch (arg.kind()) {
    case ValueKind::kInt:
      return arg;
    case ValueKind::kBool:
      return IntValue(static_cast<int64_t>(arg.GetBool().NativeValue()));
    case ValueKind::kDouble: {
      auto conv =
          cel::internal::CheckedDoubleToInt64(arg.GetDouble().NativeValue());
      if (!conv.ok()) {
        return ErrorValue::From(conv.status(), context.arena());
      }
      return IntValue(*conv);
    }
    case ValueKind::kString: {
      int64_t result;
      if (!absl::SimpleAtoi(arg.GetString().ToString(), &result)) {
        return ErrorValue::From(
            absl::InvalidArgumentError("cannot convert string to int"),
            context.arena());
      }
      return IntValue(result);
    }
    case ValueKind::kTimestamp:
      return IntValue(absl::ToUnixSeconds(arg.GetTimestamp().ToTime()));
    case ValueKind::kUint: {
      auto conv =
          cel::internal::CheckedUint64ToInt64(arg.GetUint().NativeValue());
      if (!conv.ok()) {
        return ErrorValue::From(conv.status(), context.arena());
      }
      return IntValue(*conv);
    }
    default:
      return ErrorValue::From(
          absl::InvalidArgumentError("cannot convert value to int"),
          context.arena());
  }
}

Value ConvertToUint(const Value& arg, const Function::InvokeContext& context) {
  switch (arg.kind()) {
    case ValueKind::kUint:
      return arg;
    case ValueKind::kDouble: {
      auto conv =
          cel::internal::CheckedDoubleToUint64(arg.GetDouble().NativeValue());
      if (!conv.ok()) {
        return ErrorValue::From(conv.status(), context.arena());
      }
      return UintValue(*conv);
    }
    case ValueKind::kInt: {
      auto conv =
          cel::internal::CheckedInt64ToUint64(arg.GetInt().NativeValue());
      if (!conv.ok()) {
        return ErrorValue::From(conv.status(), context.arena());
      }
      return UintValue(*conv);
    }
    case ValueKind::kString: {
      uint64_t result;
      if (!absl::SimpleAtoi(arg.GetString().ToString(), &result)) {
        return ErrorValue::From(
            absl::InvalidArgumentError("cannot convert string to uint"),
            context.arena());
      }
      return UintValue(result);
    }
    default:
      return ErrorValue::From(
          absl::InvalidArgumentError("cannot convert value to uint"),
          context.arena());
  }
}

Value ConvertToDouble(const Value& arg,
                      const Function::InvokeContext& context) {
  switch (arg.kind()) {
    case ValueKind::kDouble:
      return arg;
    case ValueKind::kInt:
      return DoubleValue(static_cast<double>(arg.GetInt().NativeValue()));
    case ValueKind::kUint:
      return DoubleValue(static_cast<double>(arg.GetUint().NativeValue()));
    case ValueKind::kString: {
      double result;
      if (absl::SimpleAtod(arg.GetString().ToString(), &result)) {
        return DoubleValue(result);
      }
      return ErrorValue::From(
          absl::InvalidArgumentError("cannot convert string to double"),
          context.arena());
    }
    default:
      return ErrorValue::From(
          absl::InvalidArgumentError("cannot convert value to double"),
          context.arena());
  }
}

Value ConvertToString(const Value& arg, bool precision_preserving_double_format,
                      const Function::InvokeContext& context) {
  switch (arg.kind()) {
    case ValueKind::kString:
      return arg;
    case ValueKind::kBytes: {
      const BytesValue& value = arg.GetBytes();
      auto valid = value.NativeValue([](const auto& bytes) -> bool {
        return internal::Utf8IsValid(bytes);
      });
      if (!valid) {
        return ErrorValue::From(
            absl::InvalidArgumentError("malformed UTF-8 bytes"),
            context.arena());
      }
      return StringValue(value);
    }
    case ValueKind::kBool:
      return StringValue::WrapUnsafe(arg.GetBool().NativeValue() ? "true"
                                                                 : "false");
    case ValueKind::kDouble:
      return precision_preserving_double_format
                 ? FormatDouble(arg.GetDouble().NativeValue(), context)
                 : LegacyFormatDouble(arg.GetDouble().NativeValue(), context);
    case ValueKind::kInt:
      return StringValue::From(absl::StrCat(arg.GetInt().NativeValue()),
                               context.arena());
    case ValueKind::kUint:
      return StringValue::From(absl::StrCat(arg.GetUint().NativeValue()),
                               context.arena());
    case ValueKind::kDuration: {
      auto encode = EncodeDurationToJson(arg.GetDuration().ToDuration());
      if (!encode.ok()) {
        return ErrorValue::From(encode.status(), context.arena());
      }
      return StringValue::From(*encode, context.arena());
    }
    case ValueKind::kTimestamp: {
      auto encode = EncodeTimestampToJson(arg.GetTimestamp().ToTime());
      if (!encode.ok()) {
        return ErrorValue::From(encode.status(), context.arena());
      }
      return StringValue::From(*encode, context.arena());
    }
    default:
      return ErrorValue::From(
          absl::InvalidArgumentError("cannot convert value to string"),
          context.arena());
  }
}

Value ConvertToDuration(const Value& arg,
                        const Function::InvokeContext& context) {
  switch (arg.kind()) {
    case ValueKind::kDuration:
      return UnsafeDurationValue(arg.GetDuration().ToDuration());
    case ValueKind::kString: {
      absl::Duration d;
      if (!absl::ParseDuration(arg.GetString().ToString(), &d)) {
        return ErrorValue::From(
            absl::InvalidArgumentError("String to Duration conversion failed"),
            context.arena());
      }
      auto status = internal::ValidateDuration(d);
      if (!status.ok()) {
        return ErrorValue::From(std::move(status), context.arena());
      }
      return DurationValue(d);
    }
    default:
      return ErrorValue::From(
          absl::InvalidArgumentError("cannot convert value to duration"),
          context.arena());
  }
}

Value ConvertToTimestamp(const Value& arg,
                         bool enable_timestamp_duration_overflow_errors,
                         const Function::InvokeContext& context) {
  switch (arg.kind()) {
    case ValueKind::kTimestamp:
      return UnsafeTimestampValue(arg.GetTimestamp().ToTime());
    case ValueKind::kInt: {
      absl::Time ts = absl::FromUnixSeconds(arg.GetInt().NativeValue());
      if (enable_timestamp_duration_overflow_errors) {
        if (ts < MinTimestamp() || ts > MaxTimestamp()) {
          return ErrorValue::From(absl::OutOfRangeError("timestamp overflow"),
                                  context.arena());
        }
      }
      return UnsafeTimestampValue(ts);
    }
    case ValueKind::kString: {
      absl::Time ts;
      if (!absl::ParseTime(absl::RFC3339_full, arg.GetString().ToString(), &ts,
                           nullptr)) {
        return ErrorValue::From(
            absl::InvalidArgumentError("String to Timestamp conversion failed"),
            context.arena());
      }
      if (enable_timestamp_duration_overflow_errors) {
        if (ts < MinTimestamp() || ts > MaxTimestamp()) {
          return ErrorValue::From(absl::OutOfRangeError("timestamp overflow"),
                                  context.arena());
        }
      }
      return UnsafeTimestampValue(ts);
    }
    default:
      return ErrorValue::From(
          absl::InvalidArgumentError("cannot convert value to timestamp"),
          context.arena());
  }
}

class TypeConversionFunction final : public Function {
 public:
  enum class Target {
    kBool,
    kInt,
    kUint,
    kDouble,
    kString,
    kBytes,
    kDuration,
    kTimestamp,
    kDyn,
    kType,
  };

  TypeConversionFunction(Target target,
                         bool precision_preserving_double_format = true,
                         bool enable_timestamp_duration_overflow_errors = false)
      : target_(target),
        precision_preserving_double_format_(precision_preserving_double_format),
        enable_timestamp_duration_overflow_errors_(
            enable_timestamp_duration_overflow_errors) {}

  absl::StatusOr<Value> Invoke(absl::Span<const Value> args,
                               const InvokeContext& context) const override {
    if (args.size() != 1) {
      return absl::InvalidArgumentError(
          "unexpected number of arguments for unary function");
    }
    const Value& arg = args[0];
    switch (target_) {
      case Target::kBool:
        return ConvertToBool(arg, context);
      case Target::kInt:
        return ConvertToInt(arg, context);
      case Target::kUint:
        return ConvertToUint(arg, context);
      case Target::kDouble:
        return ConvertToDouble(arg, context);
      case Target::kString:
        return ConvertToString(arg, precision_preserving_double_format_,
                               context);
      case Target::kBytes:
        if (arg.IsBytes()) {
          return arg;
        }
        if (arg.IsString()) {
          return BytesValue(arg.GetString());
        }
        return absl::InvalidArgumentError("cannot convert value to bytes");
      case Target::kDuration:
        return ConvertToDuration(arg, context);
      case Target::kTimestamp:
        return ConvertToTimestamp(
            arg, enable_timestamp_duration_overflow_errors_, context);
      case Target::kDyn:
        return arg;
      case Target::kType:
        return TypeValue(arg.GetRuntimeType());
    }
  }

 private:
  Target target_;
  bool precision_preserving_double_format_;
  bool enable_timestamp_duration_overflow_errors_;
};

absl::Status RegisterConversionOverloads(
    FunctionRegistry& registry, absl::string_view name,
    TypeConversionFunction::Target target, absl::Span<const Kind> arg_kinds,
    bool precision_preserving_double_format = true,
    bool enable_timestamp_duration_overflow_errors = false) {
  for (Kind kind : arg_kinds) {
    CEL_RETURN_IF_ERROR(registry.Register(
        FunctionDescriptor(name, /*is_receiver_style=*/false, {kind}),
        std::make_unique<TypeConversionFunction>(
            target, precision_preserving_double_format,
            enable_timestamp_duration_overflow_errors)));
  }
  return absl::OkStatus();
}

}  // namespace

absl::Status RegisterTypeConversionFunctions(FunctionRegistry& registry,
                                             const RuntimeOptions& options) {
  static constexpr Kind kBoolKinds[] = {Kind::kBool, Kind::kString};
  CEL_RETURN_IF_ERROR(RegisterConversionOverloads(
      registry, cel::builtin::kBool, TypeConversionFunction::Target::kBool,
      kBoolKinds));

  static constexpr Kind kBytesKinds[] = {Kind::kBytes, Kind::kString};
  CEL_RETURN_IF_ERROR(RegisterConversionOverloads(
      registry, cel::builtin::kBytes, TypeConversionFunction::Target::kBytes,
      kBytesKinds));

  static constexpr Kind kDoubleKinds[] = {Kind::kDouble, Kind::kInt,
                                          Kind::kString, Kind::kUint};
  CEL_RETURN_IF_ERROR(RegisterConversionOverloads(
      registry, cel::builtin::kDouble, TypeConversionFunction::Target::kDouble,
      kDoubleKinds));

  static constexpr Kind kIntKinds[] = {Kind::kBool,      Kind::kDouble,
                                       Kind::kInt,       Kind::kString,
                                       Kind::kTimestamp, Kind::kUint};
  CEL_RETURN_IF_ERROR(RegisterConversionOverloads(
      registry, cel::builtin::kInt, TypeConversionFunction::Target::kInt,
      kIntKinds));

  if (options.enable_string_conversion) {
    static constexpr Kind kStringKinds[] = {
        Kind::kBytes,  Kind::kBool, Kind::kDouble,   Kind::kInt,
        Kind::kString, Kind::kUint, Kind::kDuration, Kind::kTimestamp};
    CEL_RETURN_IF_ERROR(RegisterConversionOverloads(
        registry, cel::builtin::kString,
        TypeConversionFunction::Target::kString, kStringKinds,
        options.enable_precision_preserving_double_format));
  }

  static constexpr Kind kUintKinds[] = {Kind::kDouble, Kind::kInt,
                                        Kind::kString, Kind::kUint};
  CEL_RETURN_IF_ERROR(RegisterConversionOverloads(
      registry, cel::builtin::kUint, TypeConversionFunction::Target::kUint,
      kUintKinds));

  static constexpr Kind kDurationKinds[] = {Kind::kString, Kind::kDuration};
  CEL_RETURN_IF_ERROR(RegisterConversionOverloads(
      registry, cel::builtin::kDuration,
      TypeConversionFunction::Target::kDuration, kDurationKinds));

  static constexpr Kind kTimestampKinds[] = {Kind::kInt, Kind::kTimestamp,
                                             Kind::kString};
  CEL_RETURN_IF_ERROR(RegisterConversionOverloads(
      registry, cel::builtin::kTimestamp,
      TypeConversionFunction::Target::kTimestamp, kTimestampKinds,
      /*precision_preserving_double_format=*/true,
      options.enable_timestamp_duration_overflow_errors));

  // dyn() identity function.
  // TODO(issues/102): strip dyn() function references at type-check time.
  CEL_RETURN_IF_ERROR(registry.Register(
      FunctionDescriptor(cel::builtin::kDyn, /*is_receiver_style=*/false,
                         {Kind::kAny}),
      std::make_unique<TypeConversionFunction>(
          TypeConversionFunction::Target::kDyn)));

  // type(dyn) -> type
  return registry.Register(
      FunctionDescriptor(cel::builtin::kType, /*is_receiver_style=*/false,
                         {Kind::kAny}),
      std::make_unique<TypeConversionFunction>(
          TypeConversionFunction::Target::kType));
}

}  // namespace cel
