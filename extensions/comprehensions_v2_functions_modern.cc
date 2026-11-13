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

#include "extensions/comprehensions_v2_functions_modern.h"

#include <utility>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/value.h"
#include "common/values/map_value_builder.h"
#include "internal/status_macros.h"
#include "runtime/function_adapter.h"
#include "runtime/function_registry.h"
#include "runtime/runtime_options.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"

namespace cel::extensions {

namespace {

absl::StatusOr<Value> MapInsertKeyValue(
    const MapValue& map, const Value& key, const Value& value,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena) {
  if (auto mutable_map_value = common_internal::AsMutableMapValue(map);
      mutable_map_value) {
    // Fast path, runtime has given us a mutable map. We can mutate it directly
    // and return it.
    CEL_RETURN_IF_ERROR(mutable_map_value->Put(key, value))
        .With(ErrorValueReturn(arena));
    return map;
  }
  // Slow path, we have to make a copy.
  cel::MapValueBuilderPtr builder = NewMapValueBuilder(arena);
  if (absl::StatusOr<size_t> size = map.Size(); size.ok()) {
    builder->Reserve(*size + 1);
  } else {
    size.IgnoreError();
  }
  CEL_RETURN_IF_ERROR(
      map.ForEach(
          [&builder](const Value& entry_key,
                     const Value& entry_value) -> absl::StatusOr<bool> {
            CEL_RETURN_IF_ERROR(builder->Put(entry_key, entry_value));
            return true;
          },
          descriptor_pool, message_factory, arena))
      .With(ErrorValueReturn(arena));
  CEL_RETURN_IF_ERROR(builder->Put(key, value)).With(ErrorValueReturn(arena));
  return std::move(*builder).Build();
}

absl::StatusOr<Value> MapInsertMap(
    const MapValue& map, const MapValue& value,
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool,
    google::protobuf::MessageFactory* absl_nonnull message_factory,
    google::protobuf::Arena* absl_nonnull arena) {
  if (auto mutable_map_value = common_internal::AsMutableMapValue(map);
      mutable_map_value) {
    // Fast path, runtime has given us a mutable map. We can mutate it directly
    // and return it.
    CEL_RETURN_IF_ERROR(
        value.ForEach(
            [&mutable_map_value](
                const Value& entry_key,
                const Value& entry_value) -> absl::StatusOr<bool> {
              CEL_RETURN_IF_ERROR(
                  mutable_map_value->Put(entry_key, entry_value));
              return true;
            },
            descriptor_pool, message_factory, arena))
        .With(ErrorValueReturn(arena));
    return map;
  }
  // Slow path, we have to make a copy.
  cel::MapValueBuilderPtr builder = NewMapValueBuilder(arena);
  if (absl::StatusOr<size_t> size = map.Size(); size.ok()) {
    builder->Reserve(*size + 1);
  } else {
    size.IgnoreError();
  }
  CEL_RETURN_IF_ERROR(
      map.ForEach(
          [&builder](const Value& entry_key,
                     const Value& entry_value) -> absl::StatusOr<bool> {
            CEL_RETURN_IF_ERROR(builder->Put(entry_key, entry_value));
            return true;
          },
          descriptor_pool, message_factory, arena))
      .With(ErrorValueReturn(arena));
  CEL_RETURN_IF_ERROR(
      value.ForEach(
          [&builder](const Value& entry_key,
                     const Value& entry_value) -> absl::StatusOr<bool> {
            CEL_RETURN_IF_ERROR(builder->Put(entry_key, entry_value));
            return true;
          },
          descriptor_pool, message_factory, arena))
      .With(ErrorValueReturn(arena));
  return std::move(*builder).Build();
}

}  // namespace

absl::Status RegisterComprehensionsV2Functions(FunctionRegistry& registry,
                                               const RuntimeOptions& options) {
  CEL_RETURN_IF_ERROR(registry.Register(
      TernaryFunctionAdapter<absl::StatusOr<Value>, MapValue, Value,
                             Value>::CreateDescriptor("cel.@mapInsert",
                                                      /*receiver_style=*/false),
      TernaryFunctionAdapter<absl::StatusOr<Value>, MapValue, Value,
                             Value>::WrapFunction(&MapInsertKeyValue)));

  CEL_RETURN_IF_ERROR(registry.Register(
      BinaryFunctionAdapter<absl::StatusOr<Value>, MapValue, MapValue>::
          CreateDescriptor("cel.@mapInsert",
                           /*receiver_style=*/false),
      BinaryFunctionAdapter<absl::StatusOr<Value>, MapValue,
                            MapValue>::WrapFunction(&MapInsertMap)));

  return absl::OkStatus();
}

}  // namespace cel::extensions
