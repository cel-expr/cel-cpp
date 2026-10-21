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

#ifndef THIRD_PARTY_CEL_CPP_RUNTIME_INTERNAL_FUNCTION_MATCHING_TEST_UTIL_H_
#define THIRD_PARTY_CEL_CPP_RUNTIME_INTERNAL_FUNCTION_MATCHING_TEST_UTIL_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/cord.h"
#include "common/value.h"

namespace cel::runtime_internal::testing_internal {

using Pool = const google::protobuf::DescriptorPool*;
using Factory = google::protobuf::MessageFactory*;
using Arena = google::protobuf::Arena;

enum class Failure { kCreate, kNext, kEnd };
absl::Status Sentinel() {
  auto status = absl::DataLossError("distinct iterator failure");
  status.SetPayload("cel.test/observation", absl::Cord("preserved"));
  return status;
}

class FailingIterator final : public ValueIterator {
 public:
  explicit FailingIterator(Failure failure) : failure_(failure) {}
  bool HasNext() override { return failure_ != Failure::kEnd; }
  absl::Status Next(Pool, Factory, Arena*, Value*) override {
    return Sentinel();
  }
  absl::StatusOr<bool> Next2(Pool, Factory, Arena*, Value*, Value*) override {
    if (failure_ == Failure::kEnd) return false;
    return Sentinel();
  }

 private:
  Failure failure_;
};

class FailingList final : public CustomListValueInterface {
 public:
  FailingList(Failure failure, int* observations)
      : failure_(failure), observations_(observations) {}
  std::string DebugString() const override { return "test list"; }
  size_t Size() const override { return failure_ == Failure::kEnd ? 0 : 1; }
  absl::Status ConvertToJsonArray(Pool, Factory,
                                  google::protobuf::Message*) const override {
    return Sentinel();
  }
  absl::Status Get(size_t, Pool, Factory, Arena*, Value*) const override {
    return Sentinel();
  }
  absl::StatusOr<ValueIteratorPtr> NewIterator() const override {
    ++*observations_;
    if (failure_ == Failure::kCreate) return Sentinel();
    return std::make_unique<FailingIterator>(failure_);
  }
  CustomListValue Clone(Arena* arena) const override {
    return CustomListValue(this, arena);
  }
  NativeTypeId GetNativeTypeId() const override {
    return NativeTypeId::For<FailingList>();
  }

 private:
  Failure failure_;
  int* observations_;
};

class FailingMap final : public CustomMapValueInterface {
 public:
  FailingMap(Failure failure, int* observations)
      : failure_(failure), observations_(observations) {}
  std::string DebugString() const override { return "test map"; }
  size_t Size() const override { return failure_ == Failure::kEnd ? 0 : 1; }
  absl::Status ConvertToJsonObject(Pool, Factory,
                                   google::protobuf::Message*) const override {
    return Sentinel();
  }
  absl::Status ListKeys(Pool, Factory, Arena*, ListValue*) const override {
    return Sentinel();
  }
  absl::StatusOr<bool> Find(const Value&, Pool, Factory, Arena*,
                            Value*) const override {
    return Sentinel();
  }
  absl::StatusOr<bool> Has(const Value&, Pool, Factory, Arena*) const override {
    return Sentinel();
  }
  absl::StatusOr<ValueIteratorPtr> NewIterator() const override {
    ++*observations_;
    if (failure_ == Failure::kCreate) return Sentinel();
    return std::make_unique<FailingIterator>(failure_);
  }
  CustomMapValue Clone(Arena* arena) const override {
    return CustomMapValue(this, arena);
  }
  NativeTypeId GetNativeTypeId() const override {
    return NativeTypeId::For<FailingMap>();
  }

 private:
  Failure failure_;
  int* observations_;
};

class Box : public OpaqueValueInterface {
 public:
  explicit Box(OpaqueType type) : type_(type) {}
  std::string DebugString() const override { return "box"; }
  absl::string_view GetTypeName() const override { return type_.name(); }
  OpaqueType GetRuntimeType() const override { return type_; }
  absl::Status Equal(const OpaqueValue&, Pool, Factory, Arena*,
                     Value* result) const override {
    *result = BoolValue(false);
    return absl::OkStatus();
  }
  OpaqueValue Clone(Arena* arena) const override {
    return OpaqueValue(this, arena);
  }
  NativeTypeId GetNativeTypeId() const override {
    return NativeTypeId::For<Box>();
  }

 private:
  OpaqueType type_;
};

class DistinctBox final : public Box {
 public:
  using Box::Box;
  NativeTypeId GetNativeTypeId() const override {
    return NativeTypeId::For<DistinctBox>();
  }
};

inline Value MakeOpaque(Arena* arena, OpaqueType type, bool distinct = false) {
  if (distinct) {
    return OpaqueValue(Arena::Create<DistinctBox>(arena, type), arena);
  }
  return OpaqueValue(Arena::Create<Box>(arena, type), arena);
}

struct ObservationCounts {
  int creations = 0;
  int next = 0;
  int exhausted = 0;
};

// A completed first observation must not hide a necessary later failure.
class ReplayThenFailList final : public CustomListValueInterface {
 public:
  ReplayThenFailList(std::vector<Value> values, ObservationCounts* counts)
      : values_(std::move(values)), counts_(counts) {}
  std::string DebugString() const override { return "replay then fail list"; }
  size_t Size() const override { return values_.size(); }
  absl::Status ConvertToJsonArray(Pool, Factory,
                                  google::protobuf::Message*) const override {
    return absl::UnimplementedError("replay list JSON");
  }
  absl::Status Get(size_t index, Pool, Factory, Arena*,
                   Value* result) const override {
    *result = values_.at(index);
    return absl::OkStatus();
  }
  absl::StatusOr<ValueIteratorPtr> NewIterator() const override {
    if (++counts_->creations > 1) return Sentinel();
    return std::make_unique<Iterator>(&values_, counts_);
  }
  CustomListValue Clone(Arena* arena) const override {
    return CustomListValue(this, arena);
  }
  NativeTypeId GetNativeTypeId() const override {
    return NativeTypeId::For<ReplayThenFailList>();
  }

 private:
  class Iterator final : public ValueIterator {
   public:
    Iterator(const std::vector<Value>* values, ObservationCounts* counts)
        : values_(values), counts_(counts) {}
    bool HasNext() override { return index_ < values_->size(); }
    absl::Status Next(Pool, Factory, Arena*, Value* result) override {
      *result = values_->at(index_++);
      return absl::OkStatus();
    }
    absl::StatusOr<bool> Next2(Pool, Factory, Arena*, Value*,
                               Value* result) override {
      ++counts_->next;
      if (!HasNext()) {
        ++counts_->exhausted;
        return false;
      }
      *result = values_->at(index_++);
      return true;
    }

   private:
    const std::vector<Value>* values_;
    ObservationCounts* counts_;
    size_t index_ = 0;
  };
  std::vector<Value> values_;
  ObservationCounts* counts_;
};

// Native containers may expose exceptional elements, unlike CEL literal
// builders, which propagate exceptional elements while constructing a value.
class NativeList final : public CustomListValueInterface {
 public:
  explicit NativeList(std::vector<Value> values, int* reads = nullptr)
      : values_(std::move(values)), reads_(reads) {}
  std::string DebugString() const override { return "native list"; }
  size_t Size() const override { return values_.size(); }
  absl::Status ConvertToJsonArray(Pool, Factory,
                                  google::protobuf::Message*) const override {
    return absl::UnimplementedError("native list JSON");
  }
  absl::Status Get(size_t index, Pool, Factory, Arena*,
                   Value* result) const override {
    if (reads_ != nullptr) ++*reads_;
    *result = values_.at(index);
    return absl::OkStatus();
  }
  CustomListValue Clone(Arena* arena) const override {
    return CustomListValue(this, arena);
  }
  NativeTypeId GetNativeTypeId() const override {
    return NativeTypeId::For<NativeList>();
  }

 private:
  std::vector<Value> values_;
  int* reads_;
};

}  // namespace cel::runtime_internal::testing_internal
#endif  // THIRD_PARTY_CEL_CPP_RUNTIME_INTERNAL_FUNCTION_MATCHING_TEST_UTIL_H_
