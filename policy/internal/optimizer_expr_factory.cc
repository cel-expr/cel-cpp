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

#include "policy/internal/optimizer_expr_factory.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "common/ast.h"
#include "common/ast_rewrite.h"
#include "common/ast_traverse.h"
#include "common/ast_visitor_base.h"
#include "common/constant.h"
#include "common/expr.h"
#include "common/expr_factory.h"
#include "common/source.h"

namespace cel {

namespace {

class MaxIdVisitor final : public AstVisitorBase {
 public:
  ExprId max_id() const { return max_id_; }

  void PreVisitExpr(const Expr& expr) override {
    max_id_ = std::max(max_id_, expr.id());
  }

  void PostVisitExpr(const Expr&) override {}

  void PostVisitStruct(const Expr&, const StructExpr& struct_expr) override {
    for (const auto& field : struct_expr.fields()) {
      max_id_ = std::max(max_id_, field.id());
    }
  }

  void PostVisitMap(const Expr&, const MapExpr& map_expr) override {
    for (const auto& entry : map_expr.entries()) {
      max_id_ = std::max(max_id_, entry.id());
    }
  }

 private:
  ExprId max_id_ = 0;
};

ExprId GetMaxId(const Expr& expr) {
  MaxIdVisitor visitor;
  AstTraverse(expr, visitor);
  return visitor.max_id();
}

ExprId GetMaxId(const Ast& ast) {
  ExprId max_id = GetMaxId(ast.root_expr());
  for (const auto& [id, _] : ast.source_info().positions()) {
    max_id = std::max(max_id, id);
  }
  for (const auto& [id, expr] : ast.source_info().macro_calls()) {
    max_id = std::max(max_id, id);
    max_id = std::max(max_id, GetMaxId(expr));
  }
  return max_id;
}

// Replaces nested macros in a macro_calls expr with reference nodes.
//
// The macro_calls map is used for retaining the original structure of the
// parsed expression before macro expansion. When a macro appears inside another
// macro, the parser will replace the inner macro expr node with an unspecified
// expr with the inner macro's ID in the macro_calls map to save space.
class MakeMacroCallRewrite final : public AstRewriterBase {
 public:
  explicit MakeMacroCallRewrite(const SourceInfo& source_info)
      : source_info_(source_info) {}

  bool PreVisitRewrite(Expr& expr) override {
    if (source_info_.macro_calls().find(expr.id()) !=
        source_info_.macro_calls().end()) {
      ExprId id = expr.id();
      expr.mutable_kind() = UnspecifiedExpr();
      expr.set_id(id);
      return true;
    }
    return false;
  }

 private:
  const SourceInfo& source_info_;
};

// Updates macro_calls map entries to reflect a replaced expression in the
// main AST.
class ReplaceMacroCallRewrite final : public AstRewriterBase {
 public:
  ReplaceMacroCallRewrite(ExprId old_id, const Expr& replacement,
                          const SourceInfo& source_info)
      : old_id_(old_id), replacement_(replacement), source_info_(source_info) {}

  bool PreVisitRewrite(Expr& expr) override {
    if (expr.id() == old_id_) {
      expr = macro_replacement();
      return true;
    }
    return false;
  }

  Expr macro_replacement() {
    if (!macro_replacement_) {
      macro_replacement_.emplace(replacement_);
      MakeMacroCallRewrite hole_creator(source_info_);
      AstRewrite(*macro_replacement_, hole_creator);
    }
    return *macro_replacement_;
  }

 private:
  ExprId old_id_;
  const Expr& replacement_;
  absl::optional<Expr> macro_replacement_;
  const SourceInfo& source_info_;
};

void ReplaceSubExpr(Expr& expr, ExprId old_id, const Expr& replacement,
                    const SourceInfo& source_info) {
  ReplaceMacroCallRewrite rewriter(old_id, replacement, source_info);
  AstRewrite(expr, rewriter);
}

class IdRewriter : public AstRewriterBase {
  using CopyIdFn = absl::AnyInvocable<ExprId(ExprId)>;

 public:
  explicit IdRewriter(CopyIdFn copy_id) : copy_id_(std::move(copy_id)) {}

  // No structure changes just ids.
  bool PreVisitRewrite(Expr& expr) override {
    expr.set_id(copy_id_(expr.id()));
    if (expr.has_struct_expr()) {
      for (auto& field : expr.mutable_struct_expr().mutable_fields()) {
        field.set_id(copy_id_(field.id()));
      }
    } else if (expr.has_map_expr()) {
      for (auto& entry : expr.mutable_map_expr().mutable_entries()) {
        entry.set_id(copy_id_(entry.id()));
      }
    }
    return false;
  }

 private:
  CopyIdFn copy_id_;
};

}  // namespace

OptimizerExprFactory::OptimizerExprFactory(Ast basis)
    : ast_(std::move(basis)), next_id_(GetMaxId(ast_) + 1) {}

OptimizerExprFactory::OptimizerExprFactory() : next_id_(1) {}

Expr OptimizerExprFactory::Copy(const Expr& expr) {
  Expr copied = expr;
  IdRewriter rewriter([this](ExprId id) { return CopyId(id); });
  AstRewrite(copied, rewriter);
  return copied;
}

ListExprElement OptimizerExprFactory::Copy(const ListExprElement& element) {
  return NewListElement(Copy(element.expr()), element.optional());
}

StructExprField OptimizerExprFactory::Copy(const StructExprField& field) {
  auto field_id = CopyId(field.id());
  auto field_value = Copy(field.value());
  return NewStructField(field_id, field.name(), std::move(field_value),
                        field.optional());
}

MapExprEntry OptimizerExprFactory::Copy(const MapExprEntry& entry) {
  auto entry_id = CopyId(entry.id());
  auto entry_key = Copy(entry.key());
  auto entry_value = Copy(entry.value());
  return NewMapEntry(entry_id, std::move(entry_key), std::move(entry_value),
                     entry.optional());
}

ExprId OptimizerExprFactory::NextId() { return next_id_++; }

ExprId OptimizerExprFactory::CopyId(ExprId id) {
  if (id == 0) {
    return 0;
  }
  auto it = renumbers_.find(id);
  if (it != renumbers_.end()) {
    return it->second;
  }
  ExprId new_id = NextId();
  renumbers_[id] = new_id;
  return new_id;
}

SourceInfo OptimizerExprFactory::RemapSourceInfo(const SourceInfo& info,
                                                 SourcePosition offset) {
  SourceInfo out;

  for (const auto& [old_id, macro_expr] : info.macro_calls()) {
    if (auto it = renumbers_.find(old_id); it != renumbers_.end()) {
      ExprId new_id = it->second;
      out.mutable_macro_calls()[new_id] = Copy(macro_expr);
    }
  }

  for (const auto& [old_id, new_id] : renumbers_) {
    if (auto it = info.positions().find(old_id); it != info.positions().end()) {
      out.mutable_positions()[new_id] = it->second + offset;
    }
  }

  return out;
}

void OptimizerExprFactory::MergeSourceInfo(const SourceInfo& info) {
  auto& target_info = ast_.mutable_source_info();

  for (const auto& [id, pos] : info.positions()) {
    auto [it, inserted] = target_info.mutable_positions().insert({id, pos});
    if (!inserted) {
      issues_.push_back(Issue{id, "conflicting ID in positions merge"});
    }
  }

  for (const auto& [id, expr] : info.macro_calls()) {
    auto [it, inserted] = target_info.mutable_macro_calls().insert({id, expr});
    if (!inserted) {
      issues_.push_back(Issue{id, "conflicting ID in macro calls merge"});
    }
  }

  // TODO(b/506179116): need to add some check that we aren't
  // introducing incompatible tags. Not possible in the policy compiler right
  // now.
  for (const auto& ext : info.extensions()) {
    auto& target_exts = target_info.mutable_extensions();
    if (!absl::c_linear_search(target_exts, ext)) {
      target_exts.push_back(ext);
    }
  }
}

void OptimizerExprFactory::RecordReplacement(ExprId id, const Expr& replacement,
                                             bool keep_metadata) {
  auto& source_info = ast_.mutable_source_info();
  if (!keep_metadata) {
    PruneMetadata(id);
  }

  for (auto& [macro_id, macro_expr] : source_info.mutable_macro_calls()) {
    ReplaceSubExpr(macro_expr, id, replacement, source_info);
  }
}

void OptimizerExprFactory::PruneMetadata(ExprId id) {
  SourceInfo& source_info = ast_.mutable_source_info();
  source_info.mutable_positions().erase(id);
  source_info.mutable_macro_calls().erase(id);
}

Expr OptimizerExprFactory::ReportError(absl::string_view message) {
  ExprId id = NextId();
  issues_.push_back(Issue{id, std::string(message)});
  return NewUnspecified(id);
}

Expr OptimizerExprFactory::ReportErrorAt(const Expr& expr,
                                         absl::string_view message) {
  issues_.push_back(Issue{expr.id(), std::string(message)});
  return NewUnspecified(NextId());
}

Expr OptimizerExprFactory::ReportErrorAtCopy(const Expr& expr,
                                             absl::string_view message) {
  issues_.push_back(Issue{CopyId(expr.id()), std::string(message)});
  return NewUnspecified(NextId());
}

Expr OptimizerExprFactory::NewUnspecified() { return NewUnspecified(NextId()); }

Expr OptimizerExprFactory::NewNullConst() { return NewNullConst(NextId()); }

Expr OptimizerExprFactory::NewBoolConst(bool value) {
  return NewBoolConst(NextId(), value);
}

Expr OptimizerExprFactory::NewIntConst(int64_t value) {
  return NewIntConst(NextId(), value);
}

Expr OptimizerExprFactory::NewUintConst(uint64_t value) {
  return NewUintConst(NextId(), value);
}

Expr OptimizerExprFactory::NewDoubleConst(double value) {
  return NewDoubleConst(NextId(), value);
}

Expr OptimizerExprFactory::NewBytesConst(std::string value) {
  return NewBytesConst(NextId(), std::move(value));
}

Expr OptimizerExprFactory::NewBytesConst(absl::string_view value) {
  return NewBytesConst(NextId(), value);
}

Expr OptimizerExprFactory::NewBytesConst(const char* value) {
  return NewBytesConst(NextId(), value);
}

Expr OptimizerExprFactory::NewStringConst(std::string value) {
  return NewStringConst(NextId(), std::move(value));
}

Expr OptimizerExprFactory::NewStringConst(absl::string_view value) {
  return NewStringConst(NextId(), value);
}

Expr OptimizerExprFactory::NewStringConst(const char* value) {
  return NewStringConst(NextId(), value);
}

absl::flat_hash_map<ExprId, ExprId> OptimizerExprFactory::ConsumeRenumbers() {
  using std::swap;
  absl::flat_hash_map<ExprId, ExprId> out;
  swap(out, renumbers_);
  return out;
}

void OptimizerExprFactory::StartCopyContext() { renumbers_.clear(); }

const std::vector<OptimizerExprFactory::Issue>& OptimizerExprFactory::issues()
    const {
  return issues_;
}

const Ast& OptimizerExprFactory::ast() const { return ast_; }

Ast& OptimizerExprFactory::mutable_ast() { return ast_; }

absl::string_view OptimizerExprFactory::AccuVarName() {
  return ExprFactory::AccuVarName();
}

Expr OptimizerExprFactory::NewAccuIdent() { return NewAccuIdent(NextId()); }

ExprId OptimizerExprFactory::CopyId(const Expr& expr) {
  return CopyId(expr.id());
}

}  // namespace cel
