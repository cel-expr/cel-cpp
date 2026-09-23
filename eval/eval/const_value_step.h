#ifndef THIRD_PARTY_CEL_CPP_EVAL_EVAL_CONST_VALUE_STEP_H_
#define THIRD_PARTY_CEL_CPP_EVAL_EVAL_CONST_VALUE_STEP_H_

#include <cstdint>
#include <memory>
#include <utility>

#include "absl/status/statusor.h"
#include "common/value.h"
#include "eval/eval/compiler_constant_step.h"
#include "eval/eval/direct_expression_step.h"

namespace google::api::expr::runtime {

// Factory method for Constant AST node expression recursive step.
inline std::unique_ptr<DirectExpressionStep> CreateConstValueDirectStep(
    cel::Value value, int64_t id = -1) {
  return std::make_unique<DirectCompilerConstantStep>(std::move(value), id);
}

}  // namespace google::api::expr::runtime

#endif  // THIRD_PARTY_CEL_CPP_EVAL_EVAL_CONST_VALUE_STEP_H_
