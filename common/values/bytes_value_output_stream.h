// Copyright 2025 Google LLC
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

#ifndef THIRD_PARTY_CEL_CPP_COMMON_VALUES_BYTES_VALUE_OUTPUT_STREAM_H_
#define THIRD_PARTY_CEL_CPP_COMMON_VALUES_BYTES_VALUE_OUTPUT_STREAM_H_

#include <cstdint>
#include <string>
#include <utility>
#include <variant>

#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/functional/overload.h"
#include "absl/log/absl_check.h"
#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"
#include "common/internal/byte_string.h"
#include "common/values/bytes_value.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/io/zero_copy_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl_lite.h"

namespace cel {

class BytesValueOutputStream final : public google::protobuf::io::ZeroCopyOutputStream {
 public:
  BytesValueOutputStream() { Construct(); }

  explicit BytesValueOutputStream(const BytesValue& value) { Construct(value); }

  bool Next(void** data, int* size) override {
    return stream_->Next(data, size);
  }

  void BackUp(int count) override { stream_->BackUp(count); }

  int64_t ByteCount() const override { return stream_->ByteCount(); }

  bool WriteAliasedRaw(const void* data, int size) override {
    return stream_->WriteAliasedRaw(data, size);
  }

  bool AllowsAliasing() const override { return stream_->AllowsAliasing(); }

  bool WriteCord(const absl::Cord& out) override {
    return stream_->WriteCord(out);
  }

  [[nodiscard]]
  BytesValue Consume(google::protobuf::Arena* absl_nonnull arena) && {
    ABSL_DCHECK(arena != nullptr);
    return std::visit(
        absl::Overload([](std::monostate) -> BytesValue { ABSL_UNREACHABLE(); },
                       [arena](StringStream& stream) -> BytesValue {
                         return BytesValue::From(std::move(stream.target),
                                                 arena);
                       },
                       [arena](CordStream& stream) -> BytesValue {
                         return BytesValue::From(stream.Consume(), arena);
                       }),
        variant_);
  }

 private:
  struct StringStream final {
    explicit StringStream(absl::string_view target)
        : target(target), stream(&this->target) {}

    std::string target;
    google::protobuf::io::StringOutputStream stream;
  };
  using CordStream = google::protobuf::io::CordOutputStream;
  using Variant = std::variant<std::monostate, StringStream, CordStream>;

  void Construct() { stream_ = &variant_.emplace<CordStream>(); }

  void Construct(const BytesValue& value) {
    switch (value.value_.GetKind()) {
      case common_internal::ByteStringKind::kSmall:
        Construct(value.value_.GetSmall());
        break;
      case common_internal::ByteStringKind::kMedium:
        Construct(value.value_.GetMedium());
        break;
      case common_internal::ByteStringKind::kLarge:
        Construct(value.value_.GetLarge());
        break;
    }
  }

  void Construct(absl::string_view value) {
    stream_ = &variant_.emplace<StringStream>(value).stream;
  }

  void Construct(absl::Cord value) {
    stream_ = &variant_.emplace<CordStream>(std::move(value));
  }

  google::protobuf::io::ZeroCopyOutputStream* stream_;
  Variant variant_;
};

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMMON_VALUES_BYTES_VALUE_OUTPUT_STREAM_H_
