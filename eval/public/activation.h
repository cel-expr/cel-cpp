#ifndef THIRD_PARTY_CEL_CPP_EVAL_PUBLIC_ACTIVATION_H_
#define THIRD_PARTY_CEL_CPP_EVAL_PUBLIC_ACTIVATION_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "base/attribute_matcher.h"
#include "eval/public/base_activation.h"
#include "eval/public/cel_attribute.h"
#include "eval/public/cel_function.h"
#include "eval/public/cel_value.h"
#include "eval/public/cel_value_producer.h"
#include "internal/status_macros.h"
#include "google/protobuf/arena.h"

namespace cel::runtime_internal {
class ActivationAttributeMatcherAccess;
}

namespace google::api::expr::runtime {

// Instance of Activation class is used by evaluator.
// It provides binding between references used in expressions
// and actual values.
class Activation : public BaseActivation {
 public:
  Activation() = default;

  // Non-copyable/non-assignable
  Activation(const Activation&) = delete;
  Activation& operator=(const Activation&) = delete;

  // Move-constructible/move-assignable
  Activation(Activation&& other) = default;
  Activation& operator=(Activation&& other) = default;

  // BaseActivation
  std::vector<const CelFunction*> FindFunctionOverloads(
      absl::string_view name) const override;

  absl::optional<CelValue> FindValue(absl::string_view name,
                                     google::protobuf::Arena* arena) const override;

  // Insert a function into the activation (ie a lazily bound function). Returns
  // a status if the name and shape of the function matches another one that has
  // already been bound.
  absl::Status InsertFunction(std::unique_ptr<CelFunction> function);

  // Insert value into Activation.
  void InsertValue(absl::string_view name, const CelValue& value);

  // Insert ValueProducer into Activation.
  void InsertValueProducer(absl::string_view name,
                           std::unique_ptr<CelValueProducer> value_producer);

  // Remove functions that have the same name and shape as descriptor. Returns
  // true if matching functions were found and removed.
  bool RemoveFunctionEntries(const CelFunctionDescriptor& descriptor);

  // Removes value or producer, returns true if entry with the name was found
  bool RemoveValueEntry(absl::string_view name);

  // Clears a cached value for a value producer, returns if true if entry was
  // found and cleared.
  bool ClearValueEntry(absl::string_view name);

  // Clears all cached values for value producers. Returns the number of entries
  // cleared.
  int ClearCachedValues();

  // Set missing attribute patterns for evaluation.
  //
  // If a field access is found to match any of the provided patterns, the
  // result is treated as a missing attribute error.
  absl::Status SetMissingAttributePatterns(
      const std::vector<CelAttributePattern>& missing_attribute_patterns) {
    missing_attributes_.ClearAttributes();
    for (const auto& pattern : missing_attribute_patterns) {
      // This function used to return void, thus they could not fail. We use
      // upsert because it just works most of the time, especially if the caller
      // provided overlapped attribute patterns. Ideally we would use insert.
      CEL_RETURN_IF_ERROR(missing_attributes_.UpsertAttribute(pattern));
    }
    return absl::OkStatus();
  }

  ABSL_DEPRECATED("Use SetUnknownAttributePatterns")
  void set_unknown_attribute_patterns(
      const std::vector<CelAttributePattern>& unknown_attribute_patterns) {
    SetUnknownAttributePatterns(std::move(unknown_attribute_patterns))
        .IgnoreError();
  }

  // Sets the collection of attribute patterns that will be recognized as
  // "unknown" values during expression evaluation.
  absl::Status SetUnknownAttributePatterns(
      const std::vector<CelAttributePattern>& unknown_attribute_patterns) {
    unknown_attributes_.ClearAttributes();
    for (const auto& pattern : unknown_attribute_patterns) {
      // This function used to return void, thus they could not fail. We use
      // upsert because it just works most of the time, especially if the caller
      // provided overlapped attribute patterns. Ideally we would use insert.
      CEL_RETURN_IF_ERROR(unknown_attributes_.UpsertAttribute(pattern));
    }
    return absl::OkStatus();
  }

  const cel::AttributeMatcher& GetUnknownAttributeMatcher() const final {
    return unknown_attributes_;
  }

  const cel::AttributeMatcher& GetKnownAttributeMatcher() const final {
    return known_attributes_;
  }

  const cel::AttributeMatcher& GetMissingAttributeMatcher() const final {
    return missing_attributes_;
  }

 private:
  class ValueEntry {
   public:
    explicit ValueEntry(std::unique_ptr<CelValueProducer> prod)
        : value_(), producer_(std::move(prod)) {}

    explicit ValueEntry(const CelValue& value) : value_(value), producer_() {}

    // Retrieve associated CelValue.
    // If the value is not set and producer is set,
    // obtain and cache value from producer.
    absl::optional<CelValue> RetrieveValue(google::protobuf::Arena* arena) const {
      if (!value_.has_value()) {
        if (producer_) {
          value_ = producer_->Produce(arena);
        }
      }

      return value_;
    }

    bool ClearValue() {
      bool result = value_.has_value();
      value_.reset();
      return result;
    }

    bool HasProducer() const { return producer_ != nullptr; }

   private:
    mutable absl::optional<CelValue> value_;
    std::unique_ptr<CelValueProducer> producer_;
  };

  friend class cel::runtime_internal::ActivationAttributeMatcherAccess;

  absl::flat_hash_map<std::string, ValueEntry> value_map_;
  absl::flat_hash_map<std::string, std::vector<std::unique_ptr<CelFunction>>>
      function_map_;

  cel::AttributeMatcher unknown_attributes_;
  cel::AttributeMatcher known_attributes_;
  cel::AttributeMatcher missing_attributes_;
};

}  // namespace google::api::expr::runtime

#endif  // THIRD_PARTY_CEL_CPP_EVAL_PUBLIC_ACTIVATION_H_
