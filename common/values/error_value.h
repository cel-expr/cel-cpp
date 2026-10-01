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

// IWYU pragma: private, include "common/value.h"
// IWYU pragma: friend "common/value.h"

#ifndef THIRD_PARTY_CEL_CPP_COMMON_VALUES_ERROR_VALUE_H_
#define THIRD_PARTY_CEL_CPP_COMMON_VALUES_ERROR_VALUE_H_

#include <cstddef>
#include <memory>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "common/type.h"
#include "common/value_kind.h"
#include "common/values/values.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/io/zero_copy_stream.h"
#include "google/protobuf/message.h"

namespace cel {

class Value;
class ErrorValue;

ErrorValue DuplicateKeyError();

namespace common_internal {
absl::Status MakeTypeConversionError(const Type& from, const Type& to);
absl::Status MakeTypeConversionError(absl::string_view from,
                                     absl::string_view to);
absl::Status MakeNoSuchFieldError(absl::string_view field);
absl::Status MakeNoSuchKeyError(absl::string_view key);
absl::Status MakeIndexOutOfBoundsError(size_t index);
absl::Status MakeIndexOutOfBoundsError(ptrdiff_t index);
template <typename T>
std::enable_if_t<std::conjunction_v<std::is_integral<T>, std::is_unsigned<T>,
                                    std::negation<std::is_same<T, size_t>>>,
                 absl::Status>
MakeIndexOutOfBoundsError(T index) {
  static_assert(sizeof(T) <= sizeof(size_t));
  return MakeIndexOutOfBoundsError(static_cast<size_t>(index));
}
template <typename T>
std::enable_if_t<std::conjunction_v<std::is_integral<T>, std::is_signed<T>,
                                    std::negation<std::is_same<T, ptrdiff_t>>>,
                 absl::Status>
MakeIndexOutOfBoundsError(T index) {
  static_assert(sizeof(T) <= sizeof(ptrdiff_t));
  return MakeIndexOutOfBoundsError(static_cast<ptrdiff_t>(index));
}
}  // namespace common_internal

// `ErrorValue` represents values of the `ErrorType`.
class ABSL_ATTRIBUTE_TRIVIAL_ABI ErrorValue final
    : private common_internal::ValueMixin<ErrorValue> {
 public:
  static constexpr ValueKind kKind = ValueKind::kError;

  // Returns a new ErrorValue created from absl::Status. The resulting storage
  // for the underlying representation of ErrorValue is stored on the arena.
  [[nodiscard]]
  static ErrorValue From(absl::Status value,
                         google::protobuf::Arena* absl_nonnull arena
                             ABSL_ATTRIBUTE_LIFETIME_BOUND) {
    ABSL_DCHECK(!value.ok()) << "ErrorValue requires a non-OK absl::Status";
    ABSL_DCHECK(arena != nullptr);
    return ErrorValue(
        arena, google::protobuf::Arena::Create<absl::Status>(arena, std::move(value)));
  }

  [[nodiscard]]
  static ErrorValue WrapUnsafe(const absl::Status* absl_nonnull value) {
    ABSL_DCHECK(!value->ok()) << "ErrorValue requires a non-OK absl::Status";
    return ErrorValue(nullptr, value);
  }

  // By default, this creates an UNKNOWN error. You should always create a more
  // specific error value.
  ErrorValue();

  ErrorValue(const ErrorValue&) = default;
  ErrorValue& operator=(const ErrorValue&) = default;

  static constexpr ValueKind kind() { return kKind; }

  static absl::string_view GetTypeName() { return ErrorType::kName; }

  std::string DebugString() const;

  // See Value::SerializeTo().
  absl::Status SerializeTo(
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::io::ZeroCopyOutputStream* absl_nonnull output) const;

  // See Value::ConvertToJson().
  absl::Status ConvertToJson(
      const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
      google::protobuf::MessageFactory* absl_nonnull message_factory,
      google::protobuf::Message* absl_nonnull json) const;

  absl::Status Equal(const Value& other,
                     const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
                     google::protobuf::MessageFactory* absl_nonnull message_factory,
                     google::protobuf::Arena* absl_nonnull arena,
                     Value* absl_nonnull result) const;
  using ValueMixin::Equal;

  bool IsZeroValue() const { return false; }

  ErrorValue Clone(google::protobuf::Arena* absl_nonnull arena) const;

  absl::Status ToStatus() const;

  ABSL_DEPRECATED("Use ToStatus()")
  absl::Status NativeValue() const& { return ToStatus(); }

  ABSL_DEPRECATED("Use ToStatus()")
  absl::Status NativeValue() && { return std::move(*this).ToStatus(); }

  friend void swap(ErrorValue& lhs, ErrorValue& rhs) noexcept {
    using std::swap;
    swap(lhs.arena_, rhs.arena_);
    swap(lhs.status_ptr_, rhs.status_ptr_);
  }

  explicit operator bool() const { return !status_ptr_->ok(); }

 private:
  friend ErrorValue DuplicateKeyError();
  friend class common_internal::ValueMixin<ErrorValue>;

  ErrorValue(google::protobuf::Arena* absl_nullable arena,
             const absl::Status* absl_nonnull status)
      : arena_(arena), status_ptr_(status) {}

  google::protobuf::Arena* absl_nullable arena_;
  const absl::Status* absl_nonnull status_ptr_;
};

ErrorValue NoSuchFieldError(absl::string_view field,
                            google::protobuf::Arena* absl_nonnull arena);

ErrorValue NoSuchKeyError(absl::string_view key,
                          google::protobuf::Arena* absl_nonnull arena);

ErrorValue NoSuchTypeError(absl::string_view type,
                           google::protobuf::Arena* absl_nonnull arena);

ErrorValue DuplicateKeyError();

ErrorValue TypeConversionError(absl::string_view from, absl::string_view to,
                               google::protobuf::Arena* absl_nonnull arena);

ErrorValue TypeConversionError(const Type& from, const Type& to,
                               google::protobuf::Arena* absl_nonnull arena);

ErrorValue IndexOutOfBoundsError(size_t index,
                                 google::protobuf::Arena* absl_nonnull arena);

ErrorValue IndexOutOfBoundsError(ptrdiff_t index,
                                 google::protobuf::Arena* absl_nonnull arena);

// Catch other integrals and forward them to the above ones. This is needed to
// avoid ambiguous overload issues for smaller integral types like `int`.
template <typename T>
std::enable_if_t<std::conjunction_v<std::is_integral<T>, std::is_unsigned<T>,
                                    std::negation<std::is_same<T, size_t>>>,
                 ErrorValue>
IndexOutOfBoundsError(T index, google::protobuf::Arena* absl_nonnull arena) {
  static_assert(sizeof(T) <= sizeof(size_t));
  return IndexOutOfBoundsError(static_cast<size_t>(index), arena);
}
template <typename T>
std::enable_if_t<std::conjunction_v<std::is_integral<T>, std::is_signed<T>,
                                    std::negation<std::is_same<T, ptrdiff_t>>>,
                 ErrorValue>
IndexOutOfBoundsError(T index, google::protobuf::Arena* absl_nonnull arena) {
  static_assert(sizeof(T) <= sizeof(ptrdiff_t));
  return IndexOutOfBoundsError(static_cast<ptrdiff_t>(index), arena);
}

inline std::ostream& operator<<(std::ostream& out, const ErrorValue& value) {
  return out << value.DebugString();
}

bool IsNoSuchField(const ErrorValue& value);

bool IsNoSuchKey(const ErrorValue& value);

class ErrorValueReturn final {
 public:
  explicit ErrorValueReturn(google::protobuf::Arena* absl_nonnull arena) : arena_(arena) {
    ABSL_DCHECK(arena != nullptr);
  }

  ErrorValue operator()(absl::Status status) const {
    return ErrorValue::From(std::move(status), arena_);
  }

 private:
  google::protobuf::Arena* arena_ = nullptr;
};

namespace common_internal {

struct ImplicitlyConvertibleStatus {
  // NOLINTNEXTLINE(google-explicit-constructor)
  operator absl::Status() const { return absl::OkStatus(); }

  template <typename T>
  // NOLINTNEXTLINE(google-explicit-constructor)
  operator absl::StatusOr<T>() const {
    return T();
  }
};

}  // namespace common_internal

// For use with `RETURN_IF_ERROR(...).With(cel::ErrorValueAssign(&result,
// arena))` and `ASSIGN_OR_RETURN(..., ...,
// _.With(cel::ErrorValueAssign(&result, arena)))`.
//
// IMPORTANT:
// If the returning type is `absl::Status` the result will be
// `absl::OkStatus()`. If the returning type is `absl::StatusOr<T>` the result
// will be `T()`.
class ErrorValueAssign final {
 public:
  ErrorValueAssign() = delete;

  ErrorValueAssign(Value& value ABSL_ATTRIBUTE_LIFETIME_BOUND,
                   google::protobuf::Arena* absl_nonnull arena)
      : ErrorValueAssign(std::addressof(value), arena) {}

  ErrorValueAssign(Value* absl_nonnull value ABSL_ATTRIBUTE_LIFETIME_BOUND,
                   google::protobuf::Arena* absl_nonnull arena)
      : value_(value), arena_(arena) {
    ABSL_DCHECK(value != nullptr);
    ABSL_DCHECK(arena != nullptr);
  }

  common_internal::ImplicitlyConvertibleStatus operator()(
      absl::Status status) const;

 private:
  Value* absl_nonnull value_;
  google::protobuf::Arena* arena_ = nullptr;
};

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMMON_VALUES_ERROR_VALUE_H_
