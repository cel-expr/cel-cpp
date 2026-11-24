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

#include "validator/bind_nesting_validator.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "common/expr.h"
#include "common/navigable_ast.h"
#include "validator/validator.h"

namespace cel {

namespace {

constexpr absl::string_view kUnusedIterVar = "#unused";

bool IsCelBind(const NavigableAstNode& node) {
  if (node.node_kind() != NodeKind::kComprehension) {
    return false;
  }
  const ComprehensionExpr& comp = node.expr()->comprehension_expr();
  return comp.iter_var() == kUnusedIterVar && comp.has_iter_range() &&
         comp.iter_range().has_list_expr() &&
         comp.iter_range().list_expr().elements().empty() &&
         comp.has_loop_condition() && comp.loop_condition().has_const_expr() &&
         comp.loop_condition().const_expr().has_bool_value() &&
         !comp.loop_condition().const_expr().bool_value() &&
         comp.has_loop_step() && comp.loop_step().has_ident_expr() &&
         comp.loop_step().ident_expr().name() == comp.accu_var();
}

}  // namespace

Validation BindNestingLimitValidator(int limit) {
  return Validation(
      [limit](ValidationContext& context) -> bool {
        bool is_valid = true;
        for (const NavigableAstNode& node :
             context.navigable_ast().Root().DescendantsPostorder()) {
          if (!IsCelBind(node)) {
            continue;
          }

          int count = 0;
          const NavigableAstNode* current = &node;
          while (current != nullptr) {
            if (IsCelBind(*current)) {
              ++count;
            }
            current = current->parent();
          }
          if (count > limit) {
            context.ReportErrorAt(
                node.expr()->id(),
                absl::StrCat("cel.bind nesting level of ", count,
                             " exceeds limit of ", limit));
            is_valid = false;
            break;
          }
        }
        return is_valid;
      },
      "cel.validator.bind_nesting_limit");
}

}  // namespace cel
