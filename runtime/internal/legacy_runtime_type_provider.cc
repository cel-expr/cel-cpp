// Copyright 2024 Google LLC
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

#include "runtime/internal/legacy_runtime_type_provider.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "common/legacy_value.h"
#include "common/value.h"
#include "common/values/value_builder.h"
#include "internal/status_macros.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/message.h"

namespace cel::runtime_internal {

namespace {

class LegacyValueBuilder final : public cel::ValueBuilder {
 public:
  LegacyValueBuilder(google::protobuf::Arena* absl_nonnull arena,
                     cel::ValueBuilderPtr builder)
      : arena_(arena), builder_(std::move(builder)) {}

  absl::StatusOr<std::optional<cel::ErrorValue>> SetFieldByName(
      absl::string_view name, cel::Value value) override {
    return builder_->SetFieldByName(name, std::move(value));
  }

  absl::StatusOr<std::optional<cel::ErrorValue>> SetFieldByNumber(
      int64_t number, cel::Value value) override {
    return builder_->SetFieldByNumber(number, std::move(value));
  }

  absl::StatusOr<cel::Value> Build() && override {
    CEL_ASSIGN_OR_RETURN(auto value, std::move(*builder_).Build(),
                         _.With(cel::ErrorValueReturn(arena_)));
    if (value.Is<MessageValue>()) {
      // Make the value behave like a legacy message. Minimizes further
      // legacy/modern conversions (e.g. on return and when accessing fields).
      CEL_ASSIGN_OR_RETURN(auto legacy_value, LegacyValue(arena_, value),
                           _.With(cel::ErrorValueReturn(arena_)));
      CEL_ASSIGN_OR_RETURN(auto result, ModernValue(arena_, legacy_value),
                           _.With(cel::ErrorValueReturn(arena_)));
      return result;
    }
    return value;
  }

 private:
  google::protobuf::Arena* const arena_;
  cel::ValueBuilderPtr builder_;
};

}  // namespace

absl::StatusOr<absl_nullable ValueBuilderPtr>
LegacyRuntimeTypeProvider::NewValueBuilder(
    absl::string_view name,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena) const {
  auto builder = common_internal::NewValueBuilder(arena, descriptor_pool_,
                                                  message_factory, name);
  if (builder == nullptr) {
    return nullptr;
  }
  return std::make_unique<LegacyValueBuilder>(arena, std::move(builder));
}

}  // namespace cel::runtime_internal
