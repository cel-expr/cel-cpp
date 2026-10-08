#ifndef THIRD_PARTY_CEL_CPP_EVAL_EVAL_SELECT_STEP_H_
#define THIRD_PARTY_CEL_CPP_EVAL_EVAL_SELECT_STEP_H_

#include <memory>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "common/type.h"
#include "eval/eval/expression_step_logic.h"

namespace google::api::expr::runtime {

// Factory method for Select stack machine based Execution step
absl::StatusOr<std::unique_ptr<ExpressionStepLogic>> CreateSelectStep(
    absl::string_view field, bool test_only,
    bool enable_wrapper_type_null_unboxing, bool enable_optional_types = false);

absl::StatusOr<std::unique_ptr<ExpressionStepLogic>> CreateTypedSelectStep(
    absl::string_view field, cel::StructType resolved_operand_type,
    cel::StructTypeField resolved_field, bool test_only,
    bool enable_wrapper_type_null_unboxing, bool enable_optional_types);

}  // namespace google::api::expr::runtime

#endif  // THIRD_PARTY_CEL_CPP_EVAL_EVAL_SELECT_STEP_H_
