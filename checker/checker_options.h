// Copyright 2024 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef THIRD_PARTY_CEL_CPP_CHECKER_CHECKER_OPTIONS_H_
#define THIRD_PARTY_CEL_CPP_CHECKER_CHECKER_OPTIONS_H_

namespace cel {

// Options for enabling core type checker features.
struct CheckerOptions {
  // Enable overloads for numeric comparisons across types.
  // For example, 1.0 < 2 will resolve to lt_double_int.
  //
  // By default, this is disabled and expressions must explicitly cast to dyn or
  // the same type to compare.
  bool enable_cross_numeric_comparisons = false;

  // Enable legacy behavior for null assignment.
  //
  // Historically, CEL has allowed null to be assigned to structs, abstract
  // types, durations, timestamps, and any types. This is inconsistent with
  // CEL's usual interpretation of null as a literal JSON null.
  //
  // TODO(uncreated-issue/75): Need a concrete plan for updating existing CEL
  // expressions that depend on the old behavior.
  bool enable_legacy_null_assignment = true;

  // Enable updating parsed struct type names to the fully qualified type name
  // when resolved.
  //
  // Enabled by default, but can be disabled to preserve the original type name
  // as parsed.
  bool update_struct_type_names = true;

  // Temporary flag to enable type parameter name validation.
  //
  // When enabled, the TypeCheckerBuilder will validate that type parameter
  // names are simple identifiers when declared.
  bool enable_type_parameter_name_validation = true;

  // Well-known types defined by protobuf are treated specially in CEL, and
  // generally don't behave like other messages as runtime values. When used as
  // context declarations, this introduces some ambiguity about the intended
  // types of the field declarations, so it is disallowed by default.
  //
  // When enabled, the well-known types are treated like a normal message type
  // for the purposes for declaring context bindings (i.e no unpacking or
  // adapting), and use the Descriptor that is assumed by CEL.
  //
  // E.g. for google.protobuf.Any, the type checker will add a context binding
  // with `type_url: string` and `value: bytes` as top level variables.
  bool allow_well_known_type_context_declarations = false;

  // Maximum number (inclusive) of expression nodes to check for an input
  // expression.
  //
  // If exceeded, the checker should return a status with code InvalidArgument.
  int max_expression_node_count = 100000;

  // Maximum number (inclusive) of error-level issues to tolerate for an input
  // ast.
  //
  // If exceeded, the checker will stop processing the ast and return
  // the current set of issues.
  int max_error_issues = 20;

  // Maximum amount of nesting allowed for type declarations in function
  // signatures and variable declarations.
  //
  // If exceeded, the TypeCheckerBuilder will report an error when adding the
  // declaration.
  //
  // For untrusted declarations, the caller should set a lower limit to mitigate
  // expressions that compound nesting e.g.
  // type5(T)->type(type(type(type(type(T)))))); type5(type5(T)) -> type10(T)
  int max_type_decl_nesting = 13;

  // If true, the checker will include the resolved function name in the
  // reference map for the function call expr.
  //
  // If false, the function name will be empty and implied by the overload id
  // set. This matches the behavior in cel-go and cel-java.
  //
  // Temporary flag to allow rolling out the change. No functional changes to
  // evaluation behavior in either mode.
  bool enable_function_name_in_reference = false;

  // If true, the checker will use the proto json field names for protobuf
  // messages. Unlike protojson parsers, it will not accept the standard proto
  // field names as valid json field names.
  //
  // Note: The checked AST will contain the json field names and an extension
  // tag, but will require runtime support for resolving the json field names.
  bool use_json_field_names = false;

  // If true, values bound by cel.bind and cel.@block are treated as
  // polymorphic (Hindley-Milner let-polymorphism): type parameters of the
  // bound expression that remain unconstrained (e.g. the element type of `[]`)
  // are generalized, so each reference to the bound variable is inferred
  // independently instead of constraining or widening the others. Type
  // parameters shared with enclosing variables are not generalized.
  //
  // Also reports the type of the root expression with its free type
  // parameters preserved as named type parameters (e.g. `list(T1)`) via
  // ValidationResult::GetGenericResultType(), so bind-like composition outside
  // of the checker (e.g. policy variables) can declare variables that are
  // instantiated independently at each reference. Only populated if the AST
  // was checked with an explicit arena.
  //
  // Disabled by default; when disabled, type checking is unchanged. The
  // resolved type map and the checked AST type map are unaffected either way.
  bool use_polymorphic_bind = false;
};

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_CHECKER_CHECKER_OPTIONS_H_
