#ifndef THIRD_PARTY_CEL_CPP_EVAL_EVAL_SELECT_STEP_H_
#define THIRD_PARTY_CEL_CPP_EVAL_EVAL_SELECT_STEP_H_

#include <cstdint>
#include <memory>

#include "absl/status/statusor.h"
#include "common/type.h"
#include "common/value.h"
#include "eval/eval/direct_expression_step.h"
#include "eval/eval/evaluator_core.h"

namespace google::api::expr::runtime {

// Factory method for recursively evaluated select step.
std::unique_ptr<DirectExpressionStep> CreateDirectSelectStep(
    std::unique_ptr<DirectExpressionStep> operand, cel::StringValue field,
    bool test_only, int64_t expr_id, bool enable_wrapper_type_null_unboxing,
    bool enable_optional_types = false);

// Factory method for Select stack machine based Execution step
absl::StatusOr<std::unique_ptr<ExpressionStepLogic>> CreateSelectStep(
    cel::StringValue field, bool test_only,
    bool enable_wrapper_type_null_unboxing, bool enable_optional_types = false);

absl::StatusOr<std::unique_ptr<ExpressionStepLogic>> CreateTypedSelectStep(
    cel::StringValue field, cel::StructType resolved_operand_type,
    cel::StructTypeField resolved_field, bool test_only,
    bool enable_wrapper_type_null_unboxing, bool enable_optional_types);

}  // namespace google::api::expr::runtime

#endif  // THIRD_PARTY_CEL_CPP_EVAL_EVAL_SELECT_STEP_H_
