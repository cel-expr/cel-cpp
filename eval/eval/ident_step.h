#ifndef THIRD_PARTY_CEL_CPP_EVAL_EVAL_IDENT_STEP_H_
#define THIRD_PARTY_CEL_CPP_EVAL_EVAL_IDENT_STEP_H_

#include <cstddef>
#include <cstdint>
#include <memory>

#include "absl/strings/string_view.h"
#include "eval/eval/direct_expression_step.h"
#include "eval/eval/expression_step_logic.h"

namespace google::api::expr::runtime {

std::unique_ptr<DirectExpressionStep> CreateDirectIdentStep(
    absl::string_view identifier, int64_t expr_id);

std::unique_ptr<DirectExpressionStep> CreateDirectSlotIdentStep(
    absl::string_view identifier, size_t slot_index, int64_t expr_id);

// Factory method for Ident - based Execution step
std::unique_ptr<ExpressionStepLogic> CreateIdentStep(absl::string_view name);

}  // namespace google::api::expr::runtime

#endif  // THIRD_PARTY_CEL_CPP_EVAL_EVAL_IDENT_STEP_H_
