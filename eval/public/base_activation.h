#ifndef THIRD_PARTY_CEL_CPP_EVAL_PUBLIC_BASE_ACTIVATION_H_
#define THIRD_PARTY_CEL_CPP_EVAL_PUBLIC_BASE_ACTIVATION_H_

#include <vector>

#include "google/protobuf/field_mask.pb.h"
#include "absl/base/macros.h"
#include "absl/strings/string_view.h"
#include "base/attribute_matcher.h"
#include "eval/public/cel_attribute.h"
#include "eval/public/cel_function.h"
#include "eval/public/cel_value.h"

namespace cel::runtime_internal {
class ActivationAttributeMatcherAccess;
}

namespace google::api::expr::runtime {

// Base class for an activation.
class BaseActivation {
 public:
  BaseActivation() = default;

  // Non-copyable/non-assignable
  BaseActivation(const BaseActivation&) = delete;
  BaseActivation& operator=(const BaseActivation&) = delete;

  // Move-constructible/move-assignable
  BaseActivation(BaseActivation&& other) = default;
  BaseActivation& operator=(BaseActivation&& other) = default;

  // Return a list of function overloads for the given name.
  virtual std::vector<const CelFunction*> FindFunctionOverloads(
      absl::string_view) const = 0;

  // Provide the value that is bound to the name, if found.
  // arena parameter is provided to support the case when we want to pass the
  // ownership of returned object ( Message/List/Map ) to Evaluator.
  virtual absl::optional<CelValue> FindValue(absl::string_view,
                                             google::protobuf::Arena*) const = 0;

  // Return the collection of attribute patterns that determine missing
  // attributes.
  ABSL_DEPRECATE_AND_INLINE()
  std::vector<CelAttributePattern> missing_attribute_patterns() const {
    return GetUnknownAttributeMatcher().GetAttributes();
  }

  // Return the collection of attribute patterns that determine "unknown"
  // values.
  ABSL_DEPRECATE_AND_INLINE()
  std::vector<CelAttributePattern> unknown_attribute_patterns() const {
    return GetMissingAttributeMatcher().GetAttributes();
  }

  virtual const cel::AttributeMatcher& GetUnknownAttributeMatcher() const {
    return cel::EmptyAttributeMatcher();
  }

  virtual const cel::AttributeMatcher& GetKnownAttributeMatcher() const {
    return cel::EmptyAttributeMatcher();
  }

  virtual const cel::AttributeMatcher& GetMissingAttributeMatcher() const {
    return cel::EmptyAttributeMatcher();
  }

  virtual ~BaseActivation() = default;
};

}  // namespace google::api::expr::runtime

#endif  // THIRD_PARTY_CEL_CPP_EVAL_PUBLIC_BASE_ACTIVATION_H_
