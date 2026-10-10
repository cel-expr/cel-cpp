#ifndef THIRD_PARTY_CEL_CPP_EVAL_EVAL_EVALUATOR_STACK_H_
#define THIRD_PARTY_CEL_CPP_EVAL_EVAL_EVALUATOR_STACK_H_

#include <algorithm>
#include <cstddef>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/dynamic_annotations.h"
#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/log/absl_check.h"
#include "absl/meta/type_traits.h"
#include "absl/types/span.h"
#include "common/internal/attribute_trail.h"
#include "common/value.h"
#include "internal/align.h"
#include "internal/new.h"

namespace google::api::expr::runtime {

// CelValue stack.
// Implementation is based on vector to allow passing parameters from
// stack as Span<>.
class EvaluatorStack {
 public:
  explicit EvaluatorStack(size_t max_size) { Reserve(max_size); }

  EvaluatorStack(const EvaluatorStack&) = delete;
  EvaluatorStack(EvaluatorStack&&) = delete;

  ~EvaluatorStack() {
    if (max_size() > 0) {
      cel::internal::SizedDelete(data_, SizeBytes(max_size_));
    }
  }

  EvaluatorStack& operator=(const EvaluatorStack&) = delete;
  EvaluatorStack& operator=(EvaluatorStack&&) = delete;

  // Return the current stack size.
  size_t size() const {
    ABSL_DCHECK_GE(values_, values_begin_);
    ABSL_DCHECK_LE(values_, values_begin_ + max_size_);
    ABSL_DCHECK_GE(attributes_, attributes_begin_);
    ABSL_DCHECK_LE(attributes_, attributes_begin_ + max_size_);
    ABSL_DCHECK_EQ(values_ - values_begin_, attributes_ - attributes_begin_);

    return values_ - values_begin_;
  }

  // Return the maximum size of the stack.
  size_t max_size() const {
    ABSL_DCHECK_GE(values_, values_begin_);
    ABSL_DCHECK_LE(values_, values_begin_ + max_size_);
    ABSL_DCHECK_GE(attributes_, attributes_begin_);
    ABSL_DCHECK_LE(attributes_, attributes_begin_ + max_size_);
    ABSL_DCHECK_EQ(values_ - values_begin_, attributes_ - attributes_begin_);

    return max_size_;
  }

  // Returns true if stack is empty.
  bool empty() const {
    ABSL_DCHECK_GE(values_, values_begin_);
    ABSL_DCHECK_LE(values_, values_begin_ + max_size_);
    ABSL_DCHECK_GE(attributes_, attributes_begin_);
    ABSL_DCHECK_LE(attributes_, attributes_begin_ + max_size_);
    ABSL_DCHECK_EQ(values_ - values_begin_, attributes_ - attributes_begin_);

    return values_ == values_begin_;
  }

  bool full() const {
    ABSL_DCHECK_GE(values_, values_begin_);
    ABSL_DCHECK_LE(values_, values_begin_ + max_size_);
    ABSL_DCHECK_GE(attributes_, attributes_begin_);
    ABSL_DCHECK_LE(attributes_, attributes_begin_ + max_size_);
    ABSL_DCHECK_EQ(values_ - values_begin_, attributes_ - attributes_begin_);

    return values_ == values_end_;
  }

  // Attributes stack size.
  ABSL_DEPRECATED("Use size()")
  size_t attribute_size() const { return size(); }

  // Check that stack has enough elements.
  bool HasEnough(size_t size) const { return this->size() >= size; }

  // Dumps the entire stack state as is.
  void Clear() {
    if (max_size() > 0) {
      ABSL_ANNOTATE_CONTIGUOUS_CONTAINER(
          values_begin_, values_begin_ + max_size_, values_, values_begin_);
      ABSL_ANNOTATE_CONTIGUOUS_CONTAINER(attributes_begin_,
                                         attributes_begin_ + max_size_,
                                         attributes_, attributes_begin_);

      values_ = values_begin_;
      attributes_ = attributes_begin_;
    }
  }

  // Gets the last size elements of the stack.
  // Checking that stack has enough elements is caller's responsibility.
  // Please note that calls to Push may invalidate returned Span object.
  absl::Span<const cel::Value> GetSpan(size_t size) const {
    ABSL_DCHECK(HasEnough(size));

    return absl::Span<const cel::Value>(values_ - size, size);
  }

  // Gets the last size attribute trails of the stack.
  // Checking that stack has enough elements is caller's responsibility.
  // Please note that calls to Push may invalidate returned Span object.
  absl::Span<const cel::common_internal::AttributeTrail> GetAttributeSpan(
      size_t size) const {
    ABSL_DCHECK(HasEnough(size));

    return absl::Span<const cel::common_internal::AttributeTrail>(
        attributes_ - size, size);
  }

  // Peeks the last element of the stack.
  // Checking that stack is not empty is caller's responsibility.
  cel::Value& Peek() {
    ABSL_DCHECK(HasEnough(1));

    return *(values_ - 1);
  }

  // Peeks the last element of the stack.
  // Checking that stack is not empty is caller's responsibility.
  const cel::Value& Peek() const {
    ABSL_DCHECK(HasEnough(1));

    return *(values_ - 1);
  }

  // Peeks the last element of the attribute stack.
  // Checking that stack is not empty is caller's responsibility.
  const cel::common_internal::AttributeTrail& PeekAttribute() const {
    ABSL_DCHECK(HasEnough(1));

    return *(attributes_ - 1);
  }

  // Peeks the last element of the attribute stack.
  // Checking that stack is not empty is caller's responsibility.
  cel::common_internal::AttributeTrail& PeekAttribute() {
    ABSL_DCHECK(HasEnough(1));

    return *(attributes_ - 1);
  }

  void Pop() {
    ABSL_DCHECK(!empty());

    --values_;
    --attributes_;

    ABSL_ANNOTATE_CONTIGUOUS_CONTAINER(values_begin_, values_begin_ + max_size_,
                                       values_ + 1, values_);
    ABSL_ANNOTATE_CONTIGUOUS_CONTAINER(attributes_begin_,
                                       attributes_begin_ + max_size_,
                                       attributes_ + 1, attributes_);
  }

  // Clears the last size elements of the stack.
  // Checking that stack has enough elements is caller's responsibility.
  void Pop(size_t size) {
    ABSL_DCHECK(HasEnough(size));

    for (; size > 0; --size) {
      Pop();
    }
  }

  template <typename V, typename = std::enable_if_t<std::conjunction_v<
                            std::is_convertible<V, cel::Value>>>>
  void Push(V&& value, cel::common_internal::AttributeTrail attribute) {
    ABSL_DCHECK(!full());
    ABSL_DCHECK(!attribute.IsFullMatch())
        << "Full matches should not be pushed onto the value stack, they "
           "should be handled directly and converted to an unknown value or "
           "missing error value";

    if (ABSL_PREDICT_FALSE(full())) {
      Grow();
    }

    ABSL_ANNOTATE_CONTIGUOUS_CONTAINER(values_begin_, values_begin_ + max_size_,
                                       values_, values_ + 1);
    ABSL_ANNOTATE_CONTIGUOUS_CONTAINER(attributes_begin_,
                                       attributes_begin_ + max_size_,
                                       attributes_, attributes_ + 1);

    *values_++ = std::forward<V>(value);
    *attributes_++ = attribute;
  }

  template <typename V,
            typename = std::enable_if_t<std::is_convertible_v<V, cel::Value>>>
  void Push(V&& value) {
    ABSL_DCHECK(!full());

    Push(std::forward<V>(value), cel::common_internal::AttributeTrail());
  }

  void Push() { Push(cel::Value(), cel::common_internal::AttributeTrail()); }

  // Equivalent to `PopAndPush(1, ...)`.
  template <typename V, typename = std::enable_if_t<std::conjunction_v<
                            std::is_convertible<V, cel::Value>>>>
  void PopAndPush(V&& value, cel::common_internal::AttributeTrail attribute) {
    ABSL_DCHECK(!empty());
    ABSL_DCHECK(!attribute.IsFullMatch())
        << "Full matches should not be pushed onto the value stack, they "
           "should be handled directly and converted to an unknown value or "
           "missing error value";

    *(values_ - 1) = std::forward<V>(value);
    *(attributes_ - 1) = attribute;
  }

  // Equivalent to `PopAndPush(1, ...)`.
  template <typename V,
            typename = std::enable_if_t<std::is_convertible_v<V, cel::Value>>>
  void PopAndPush(V&& value) {
    ABSL_DCHECK(!empty());

    PopAndPush(std::forward<V>(value), cel::common_internal::AttributeTrail());
  }

  // Equivalent to `Pop(n)` followed by `Push(...)`. Both `V` and `A` MUST NOT
  // be located on the stack. If this is the case, use SwapAndPop instead.
  template <typename V, typename = std::enable_if_t<std::conjunction_v<
                            std::is_convertible<V, cel::Value>>>>
  void PopAndPush(size_t n, V&& value,
                  cel::common_internal::AttributeTrail attribute) {
    if (n > 0) {
      ABSL_DCHECK(!attribute.IsFullMatch())
          << "Full matches should not be pushed onto the value stack, they "
             "should be handled directly and converted to an unknown value or "
             "missing error value";
      if constexpr (std::is_same_v<cel::Value, absl::remove_cvref_t<V>>) {
        ABSL_DCHECK(&value < values_begin_ ||
                    &value >= values_begin_ + max_size_)
            << "Attmpting to push a value about to be popped, use PopAndSwap "
               "instead.";
      }

      Pop(n - 1);

      ABSL_DCHECK(!empty());

      *(values_ - 1) = std::forward<V>(value);
      *(attributes_ - 1) = attribute;
    } else {
      Push(std::forward<V>(value), attribute);
    }
  }

  // Equivalent to `Pop(n)` followed by `Push(...)`. `V` MUST NOT be located on
  // the stack. If this is the case, use SwapAndPop instead.
  template <typename V,
            typename = std::enable_if_t<std::is_convertible_v<V, cel::Value>>>
  void PopAndPush(size_t n, V&& value) {
    PopAndPush(n, std::forward<V>(value),
               cel::common_internal::AttributeTrail());
  }

  // Given the top `n` the elements of the stack, swap the `i`th element with
  // the 0th, then pop n - 1 elements (leaving the `i`th element as the new
  // top).
  void SwapAndPop(size_t n, size_t i) {
    ABSL_DCHECK_GT(n, 0);
    ABSL_DCHECK_LT(i, n);
    ABSL_DCHECK(HasEnough(n - 1));

    using std::swap;

    if (i > 0) {
      swap(*(values_ - n), *(values_ - n + i));
      swap(*(attributes_ - n), *(attributes_ - n + i));
    }
    Pop(n - 1);
  }

  // Update the max size of the stack and update capacity if needed.
  void SetMaxSize(size_t size) { Reserve(size); }

 private:
  static size_t AttributesBytesOffset(size_t size) {
    return cel::internal::AlignUp(sizeof(cel::Value) * size,
                                  __STDCPP_DEFAULT_NEW_ALIGNMENT__);
  }

  static size_t SizeBytes(size_t size) {
    return AttributesBytesOffset(size) +
           (sizeof(cel::common_internal::AttributeTrail) * size);
  }

  void Grow();

  // Preallocate stack.
  void Reserve(size_t size);

  cel::Value* absl_nullability_unknown values_ = nullptr;
  cel::Value* absl_nullability_unknown values_begin_ = nullptr;
  cel::common_internal::AttributeTrail* absl_nullability_unknown attributes_ =
      nullptr;
  cel::common_internal::AttributeTrail* absl_nullability_unknown
      attributes_begin_ = nullptr;
  cel::Value* absl_nullability_unknown values_end_ = nullptr;
  void* absl_nullability_unknown data_ = nullptr;
  size_t max_size_ = 0;
};

}  // namespace google::api::expr::runtime

#endif  // THIRD_PARTY_CEL_CPP_EVAL_EVAL_EVALUATOR_STACK_H_
