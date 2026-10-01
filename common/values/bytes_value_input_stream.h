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

#ifndef THIRD_PARTY_CEL_CPP_COMMON_VALUES_BYTES_VALUE_INPUT_STREAM_H_
#define THIRD_PARTY_CEL_CPP_COMMON_VALUES_BYTES_VALUE_INPUT_STREAM_H_

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <variant>

#include "absl/log/absl_check.h"
#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"
#include "common/internal/byte_string.h"
#include "common/values/bytes_value.h"
#include "google/protobuf/io/zero_copy_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl_lite.h"

namespace cel {

class BytesValueInputStream final : public google::protobuf::io::ZeroCopyInputStream {
 public:
  explicit BytesValueInputStream(const BytesValue& value) { Construct(value); }

  bool Next(const void** data, int* size) override {
    return stream_->Next(data, size);
  }

  void BackUp(int count) override { stream_->BackUp(count); }

  bool Skip(int count) override { return stream_->Skip(count); }

  int64_t ByteCount() const override { return stream_->ByteCount(); }

  bool ReadCord(absl::Cord* cord, int count) override {
    return stream_->ReadCord(cord, count);
  }

 private:
  using ArrayStream = google::protobuf::io::ArrayInputStream;
  struct CordStream {
    explicit CordStream(absl::Cord cord)
        : cord(std::move(cord)), stream(&this->cord) {}

    absl::Cord cord;
    google::protobuf::io::CordInputStream stream;
  };
  using Variant = std::variant<std::monostate, ArrayStream, CordStream>;

  void Construct(const BytesValue& value) {
    switch (value.value_.GetKind()) {
      case common_internal::ByteStringKind::kSmall:
        small_ = value.value_.rep_.small;
        Construct(absl::string_view(small_.data, small_.size));
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
    ABSL_DCHECK_LE(value.size(),
                   static_cast<size_t>(std::numeric_limits<int>::max()));
    stream_ = &variant_.emplace<ArrayStream>(value.data(),
                                             static_cast<int>(value.size()));
  }

  void Construct(absl::Cord value) {
    stream_ = &variant_.emplace<CordStream>(std::move(value)).stream;
  }

  google::protobuf::io::ZeroCopyInputStream* stream_;
  common_internal::SmallByteStringRep small_;
  Variant variant_;
};

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMMON_VALUES_BYTES_VALUE_INPUT_STREAM_H_
