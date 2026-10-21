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

#include "runtime/internal/function_matching.h"

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include "internal/status_macros.h"

namespace cel::runtime_internal {
namespace {

using common_internal::AddFunctionTypeAtomic;
using common_internal::AddFunctionTypeStructure;
using common_internal::AddFunctionTypeUnknown;
using common_internal::BuildFunctionRuntimeType;
using common_internal::CompareFunctionTypes;
using common_internal::ConstrainFunctionTypes;
using common_internal::FunctionTypeMatchState;
using common_internal::FunctionTypesMatchPreflight;
using common_internal::NewFunctionTypeScope;

// Missing evidence is an empty container child. Dyn instead summarizes
// conflicting observations within a single syntactic parameter occurrence.
struct Evidence {
  enum class State { kHole, kDyn, kAtomic, kList, kMap, kOptional };
  State state = State::kHole;
  std::optional<Type> type;
  std::vector<Evidence> parameters;
};

bool IsContainer(const Type& type) {
  return type.IsList() || type.IsMap() ||
         (type.IsOpaque() && type.GetOpaque() && type.IsOptional());
}

void Join(const Evidence& source, Evidence& target) {
  using State = Evidence::State;
  if (source.state == State::kHole || target.state == State::kDyn) {
    return;
  }
  if (target.state == State::kHole) {
    target = source;
    return;
  }
  if (source.state == target.state && source.state != State::kDyn &&
      source.state != State::kAtomic) {
    for (size_t i = 0; i < target.parameters.size(); ++i) {
      Join(source.parameters[i], target.parameters[i]);
    }
    return;
  }
  if (source.state == State::kAtomic && target.state == State::kAtomic &&
      CompareFunctionTypes(*source.type, *target.type) == 0)
    return;
  target.state = State::kDyn;
  target.type = DynType();
  target.parameters.clear();
}

absl::Status Observe(const Value& value, const Function::InvokeContext& context,
                     Evidence& evidence) {
  evidence.type = value.GetRuntimeType();
  if (evidence.type->IsDyn()) {
    evidence.state = Evidence::State::kDyn;
    evidence.parameters.clear();
    return absl::OkStatus();
  }
  evidence.state = Evidence::State::kAtomic;
  if (value.IsList() || value.IsMap()) {
    const bool map = value.IsMap();
    evidence.state = map ? Evidence::State::kMap : Evidence::State::kList;
    evidence.parameters.resize(map ? 2 : 1);
    CEL_ASSIGN_OR_RETURN(auto iterator, map ? value.GetMap().NewIterator()
                                            : value.GetList().NewIterator());
    for (;;) {
      Value key, element;
      CEL_ASSIGN_OR_RETURN(
          bool next,
          iterator->Next2(context.descriptor_pool(), context.message_factory(),
                          context.arena(), &key, &element));
      if (!next) break;
      if (map) {
        Evidence child;
        CEL_RETURN_IF_ERROR(Observe(key, context, child));
        Join(child, evidence.parameters[0]);
      }
      Evidence child;
      CEL_RETURN_IF_ERROR(Observe(element, context, child));
      Join(child, evidence.parameters.back());
    }
  } else if (value.IsOptional()) {
    evidence.state = Evidence::State::kOptional;
    evidence.parameters.resize(1);
    if (value.GetOptional().HasValue()) {
      CEL_RETURN_IF_ERROR(Observe(value.GetOptional().Value(), context,
                                  evidence.parameters[0]));
    }
  }
  return absl::OkStatus();
}

struct Occurrence {
  size_t variable;
  Evidence evidence;
};

struct Pattern {
  Type type;
  size_t term;
  std::optional<size_t> occurrence;
  std::vector<Pattern> parameters;
};

class ArgumentMatcher {
 public:
  ArgumentMatcher(const Function::InvokeContext& context,
                  FunctionTypeMatchState& type_state)
      : context_(context), type_state_(type_state) {}

  std::vector<Pattern> Build(const FunctionDescriptor& descriptor) {
    const size_t scope = NewFunctionTypeScope(type_state_);
    std::vector<Pattern> patterns;
    for (const auto& type : descriptor.types()) {
      patterns.push_back(BuildPattern(type, scope));
    }
    return patterns;
  }

  bool Constrain(const std::vector<Pattern>& registration,
                 const std::vector<Pattern>& implementation) {
    for (size_t i = 0; i < registration.size(); ++i) {
      if (!ConstrainFunctionTypes(type_state_, registration[i].term,
                                  implementation[i].term)) {
        return false;
      }
    }
    return true;
  }

  absl::StatusOr<bool> Collect(const std::vector<Pattern>& patterns,
                               absl::Span<const Value> arguments) {
    for (size_t i = 0; i < patterns.size(); ++i) {
      CEL_ASSIGN_OR_RETURN(bool matches,
                           CollectValue(patterns[i], arguments[i]));
      if (!matches) return false;
    }
    return true;
  }

  bool Solve() {
    // Occurrences stay distinct even when their variables were unified.
    // Joining must finish before any occurrence constrains a shared variable.
    for (const auto& occurrence : occurrences_) {
      if (!ConstrainFunctionTypes(type_state_, occurrence.variable,
                                  BuildEvidence(occurrence.evidence))) {
        return false;
      }
    }
    return true;
  }

 private:
  Pattern BuildPattern(const Type& type, size_t scope) {
    Pattern pattern{type, 0, std::nullopt, {}};
    if (IsContainer(type)) {
      std::vector<size_t> children;
      for (const Type& parameter : type.GetParameters()) {
        pattern.parameters.push_back(BuildPattern(parameter, scope));
        children.push_back(pattern.parameters.back().term);
      }
      pattern.term =
          AddFunctionTypeStructure(type_state_, type, std::move(children));
    } else {
      pattern.term = BuildFunctionRuntimeType(type_state_, type, scope);
      if (type.IsTypeParam()) {
        pattern.occurrence = occurrences_.size();
        occurrences_.push_back({pattern.term, {}});
      }
    }
    return pattern;
  }

  size_t BuildEvidence(const Evidence& evidence) {
    if (evidence.state == Evidence::State::kHole ||
        evidence.state == Evidence::State::kDyn) {
      return AddFunctionTypeUnknown(type_state_);
    }
    if (evidence.state == Evidence::State::kAtomic) {
      return AddFunctionTypeAtomic(type_state_, *evidence.type);
    }
    std::vector<size_t> children;
    for (const auto& parameter : evidence.parameters) {
      children.push_back(BuildEvidence(parameter));
    }
    return AddFunctionTypeStructure(type_state_, *evidence.type,
                                    std::move(children));
  }

  absl::StatusOr<bool> CollectValue(const Pattern& pattern,
                                    const Value& value) {
    const Type& expected = pattern.type;
    if (expected.IsAny() || expected.IsDyn()) return true;
    if (pattern.occurrence.has_value()) {
      Evidence evidence;
      CEL_RETURN_IF_ERROR(Observe(value, context_, evidence));
      Join(evidence, occurrences_[*pattern.occurrence].evidence);
      return true;
    }
    const Type actual = value.GetRuntimeType();
    if (expected.kind() != actual.kind()) return false;
    if (expected.IsStruct()) {
      return !expected.GetStruct() ||
             expected.GetStruct().name() == actual.GetStruct().name();
    }
    if (expected.IsOpaque() && !expected.GetOpaque()) return true;
    if (expected.IsOptional()) {
      if (!value.IsOptional()) return false;
      if (!value.GetOptional().HasValue() || pattern.parameters.empty()) {
        return true;
      }
      return CollectValue(pattern.parameters[0], value.GetOptional().Value());
    }
    if (expected.IsList() || expected.IsMap()) {
      // list<dyn>/map<dyn,dyn> impose no constraints on contents.
      bool unconstrained = true;
      for (const auto& parameter : pattern.parameters) {
        unconstrained &= parameter.type.IsAny() || parameter.type.IsDyn();
      }
      if (unconstrained) return true;
      const bool map = expected.IsMap();
      CEL_ASSIGN_OR_RETURN(auto iterator, map ? value.GetMap().NewIterator()
                                              : value.GetList().NewIterator());
      for (;;) {
        Value key, element;
        CEL_ASSIGN_OR_RETURN(bool next,
                             iterator->Next2(context_.descriptor_pool(),
                                             context_.message_factory(),
                                             context_.arena(), &key, &element));
        if (!next) return true;
        if (map) {
          CEL_ASSIGN_OR_RETURN(bool matches,
                               CollectValue(pattern.parameters[0], key));
          if (!matches) return false;
        }
        CEL_ASSIGN_OR_RETURN(bool matches,
                             CollectValue(pattern.parameters.back(), element));
        if (!matches) return false;
      }
    }
    return CompareFunctionTypes(expected, actual) == 0;
  }

  const Function::InvokeContext& context_;
  FunctionTypeMatchState& type_state_;
  std::vector<Occurrence> occurrences_;
};

bool AllowsArguments(const FunctionDescriptor& implementation,
                     absl::Span<const Value> arguments) {
  if (implementation.types().size() != arguments.size()) return false;
  if (implementation.is_strict()) {
    for (const auto& value : arguments) {
      if (value.IsError() || value.IsUnknown()) return false;
    }
  }
  return true;
}

bool KindsMatch(const FunctionDescriptor& descriptor,
                absl::Span<const Value> arguments) {
  if (descriptor.kinds().size() != arguments.size()) return false;
  for (size_t i = 0; i < arguments.size(); ++i) {
    if (!common_internal::FunctionKindMatches(
            descriptor.kinds()[i], ValueKindToKind(arguments[i].kind()))) {
      return false;
    }
  }
  return true;
}

std::vector<Type> ShallowQuery(absl::Span<const Value> arguments) {
  std::vector<Type> query;
  query.reserve(arguments.size());
  for (const auto& argument : arguments) {
    query.emplace_back(ValueKindToKind(argument.kind()));
  }
  return query;
}

absl::StatusOr<bool> MatchArguments(const FunctionDescriptor& implementation,
                                    absl::Span<const Value> arguments,
                                    const Function::InvokeContext& context,
                                    bool type_level, bool preflight) {
  if (!AllowsArguments(implementation, arguments) ||
      !KindsMatch(implementation, arguments))
    return false;
  if (!type_level) return true;
  if (preflight && !FunctionTypesMatchPreflight(implementation.types(),
                                                ShallowQuery(arguments))) {
    return false;
  }
  FunctionTypeMatchState type_state;
  ArgumentMatcher matcher(context, type_state);
  const auto patterns = matcher.Build(implementation);
  CEL_ASSIGN_OR_RETURN(bool matches, matcher.Collect(patterns, arguments));
  return matches && matcher.Solve();
}

absl::StatusOr<bool> MatchArguments(const FunctionDescriptor& registration,
                                    const FunctionDescriptor& implementation,
                                    absl::Span<const Value> arguments,
                                    const Function::InvokeContext& context,
                                    bool type_level, bool preflight) {
  if (registration.name() != implementation.name() ||
      registration.receiver_style() != implementation.receiver_style() ||
      registration.types().size() != arguments.size() ||
      !AllowsArguments(implementation, arguments) ||
      !KindsMatch(registration, arguments) ||
      !KindsMatch(implementation, arguments))
    return false;
  if (!type_level) {
    return true;
  }
  if (preflight &&
      !FunctionTypesMatchPreflight(registration.types(), implementation.types(),
                                   ShallowQuery(arguments))) {
    return false;
  }
  FunctionTypeMatchState type_state;
  ArgumentMatcher matcher(context, type_state);
  const auto registered = matcher.Build(registration);
  const auto implemented = matcher.Build(implementation);
  if (!matcher.Constrain(registered, implemented)) return false;
  CEL_ASSIGN_OR_RETURN(bool registered_match,
                       matcher.Collect(registered, arguments));
  if (!registered_match) return false;
  CEL_ASSIGN_OR_RETURN(bool implemented_match,
                       matcher.Collect(implemented, arguments));
  return implemented_match && matcher.Solve();
}

}  // namespace

absl::StatusOr<bool> MatchFunctionArguments(
    const FunctionDescriptor& implementation, absl::Span<const Value> arguments,
    const Function::InvokeContext& context, bool type_level) {
  return MatchArguments(implementation, arguments, context, type_level, true);
}

absl::StatusOr<bool> MatchFunctionArguments(
    const FunctionDescriptor& registration,
    const FunctionDescriptor& implementation, absl::Span<const Value> arguments,
    const Function::InvokeContext& context, bool type_level) {
  return MatchArguments(registration, implementation, arguments, context,
                        type_level, true);
}

absl::StatusOr<bool> MatchFunctionArgumentsContentForTesting(
    const FunctionDescriptor& implementation, absl::Span<const Value> arguments,
    const Function::InvokeContext& context) {
  return MatchArguments(implementation, arguments, context, true, false);
}

absl::StatusOr<bool> MatchFunctionArgumentsContentForTesting(
    const FunctionDescriptor& registration,
    const FunctionDescriptor& implementation, absl::Span<const Value> arguments,
    const Function::InvokeContext& context) {
  return MatchArguments(registration, implementation, arguments, context, true,
                        false);
}

}  // namespace cel::runtime_internal
