// Copyright 2023 Google LLC
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
//
// Interfaces for runtime concepts.

#ifndef THIRD_PARTY_CEL_CPP_RUNTIME_RUNTIME_H_
#define THIRD_PARTY_CEL_CPP_RUNTIME_RUNTIME_H_

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "base/ast.h"
#include "base/type_provider.h"
#include "common/native_type.h"
#include "common/value.h"
#include "runtime/activation_interface.h"
#include "runtime/runtime_issue.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"

namespace cel {

namespace runtime_internal {
class RuntimeFriendAccess;
}  // namespace runtime_internal

class EmbedderContext;

// Options for the Program::Evaluate call.
struct EvaluateOptions {
  // Optional message factory to use for the duration of the Evaluate call.
  // If unset, a default message factory will be provided by the runtime.
  google::protobuf::MessageFactory* absl_nullable message_factory = nullptr;

  // Optional embedder context to use for the duration of the Evaluate call.
  // This is used to access custom data in extension functions.
  // This is only propagated to functions that are marked as context sensitive.
  const EmbedderContext* absl_nullable embedder_context = nullptr;
};

// Representation of an evaluable CEL expression.
//
// See Runtime below for creating new programs.
class Program {
 public:
  virtual ~Program() = default;

  // Evaluate the program.
  //
  // Non-recoverable errors (i.e. outside of CEL's notion of an error) are
  // returned as a non-ok absl::Status. These are propagated immediately and do
  // not participate in CEL's notion of error handling.
  //
  // CEL errors are represented as result with an Ok status and a held
  // cel::ErrorValue result.
  //
  // Activation manages instances of variables available in the cel expression's
  // environment.
  //
  // Notes on lifetimes:
  //
  // The provided arena will be used as necessary to allocate complex values
  // and must outlive any returned value. Values created by the program may
  // depend on internal state in the runtime. In particular protobuf messages
  // may depend on the descriptor pool and message factory managed by the
  // runtime or program.
  //
  // Programs implicitly keep shared state in the runtime object alive so it
  // is sufficient to ensure that any cel::Value result is destroyed before the
  // cel::Program that created it.
  //
  // For consistency, users should use the same arena to create values placed in
  // the activation for calls to Program::Evaluate.
  absl::StatusOr<Value> Evaluate(
      google::protobuf::Arena* absl_nonnull arena ABSL_ATTRIBUTE_LIFETIME_BOUND,
      const ActivationInterface& activation,
      const EvaluateOptions& options = {}) const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return EvaluateImpl(activation, arena, options);
  }

  ABSL_DEPRECATED("Use the EvaluateOptions overload instead.")
  absl::StatusOr<Value> Evaluate(
      google::protobuf::Arena* absl_nonnull arena ABSL_ATTRIBUTE_LIFETIME_BOUND,
      google::protobuf::MessageFactory* absl_nullable message_factory
          ABSL_ATTRIBUTE_LIFETIME_BOUND,
      const ActivationInterface& activation) const
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return EvaluateImpl(activation, arena, {message_factory});
  }

  virtual const TypeProvider& GetTypeProvider() const = 0;

 protected:
  virtual absl::StatusOr<Value> EvaluateImpl(
      const ActivationInterface& activation,
      google::protobuf::Arena* absl_nonnull arena ABSL_ATTRIBUTE_LIFETIME_BOUND,
      const EvaluateOptions& options) const ABSL_ATTRIBUTE_LIFETIME_BOUND = 0;
};

// Representation for a traceable CEL expression.
//
// Implementations provide an additional Trace method that evaluates the
// expression and invokes a callback allowing callers to inspect intermediate
// state during evaluation.
class TraceableProgram : public Program {
 public:
  // EvaluationListener may be provided to an EvaluateWithCallback call to
  // inspect intermediate values during evaluation.
  //
  // The callback is called on after every program step that corresponds
  // to an AST expression node. The value provided is the top of the value
  // stack, corresponding to the result of evaluating the given sub expression.
  //
  // A returning a non-ok status stops evaluation and forwards the error.
  using EvaluationListener = absl::AnyInvocable<absl::Status(
      int64_t expr_id, const Value&, const google::protobuf::DescriptorPool* absl_nonnull,
      google::protobuf::MessageFactory* absl_nonnull, google::protobuf::Arena* absl_nonnull)>;

  using Program::Evaluate;

  // Evaluate the Program plan with a Listener.
  //
  // The given callback will be invoked after evaluating any program step
  // that corresponds to an AST node in the planned CEL expression.
  //
  // If the callback returns a non-ok status, evaluation stops and the Status
  // is forwarded as the result of the EvaluateWithCallback call.
  absl::StatusOr<Value> Trace(
      google::protobuf::Arena* absl_nonnull arena ABSL_ATTRIBUTE_LIFETIME_BOUND,
      const ActivationInterface& activation,
      EvaluationListener evaluation_listener,
      const EvaluateOptions& options = {}) const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return TraceImpl(activation, std::move(evaluation_listener), arena,
                     options);
  }

  ABSL_DEPRECATED("Use the EvaluateOptions overload instead.")
  absl::StatusOr<Value> Trace(
      google::protobuf::Arena* absl_nonnull arena ABSL_ATTRIBUTE_LIFETIME_BOUND,
      google::protobuf::MessageFactory* absl_nullable message_factory
          ABSL_ATTRIBUTE_LIFETIME_BOUND,
      const ActivationInterface& activation,
      EvaluationListener evaluation_listener) const
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return TraceImpl(activation, std::move(evaluation_listener), arena,
                     {message_factory});
  }

 protected:
  absl::StatusOr<Value> EvaluateImpl(const ActivationInterface& activation,
                                     google::protobuf::Arena* absl_nonnull arena
                                         ABSL_ATTRIBUTE_LIFETIME_BOUND,
                                     const EvaluateOptions& options) const
      ABSL_ATTRIBUTE_LIFETIME_BOUND override {
    return TraceImpl(activation, nullptr, arena, options);
  }

  virtual absl::StatusOr<Value> TraceImpl(
      const ActivationInterface& activation,
      EvaluationListener evaluation_listener,
      google::protobuf::Arena* absl_nonnull arena ABSL_ATTRIBUTE_LIFETIME_BOUND,
      const EvaluateOptions& options) const ABSL_ATTRIBUTE_LIFETIME_BOUND = 0;
};

// Interface for a CEL runtime.
//
// Manages the state necessary to generate Programs.
//
// Runtime instances should be created from a RuntimeBuilder rather than
// instantiated directly.
//
// Implementations provided by CEL will be thread-compatible, but write
// operations on the underlying environment (TypeRegistry, FunctionRegistry) or
// on the implementation via down casting must be synchronized by the caller and
// may invalidate any Programs created from the Runtime.
class Runtime {
 public:
  struct CreateProgramOptions {
    // Optional output for collecting issues encountered while planning.
    // If non-null, vector is cleared and encountered issues are added.
    std::vector<RuntimeIssue>* issues = nullptr;
  };

  virtual ~Runtime() = default;

  absl::StatusOr<std::unique_ptr<Program>> CreateProgram(
      std::unique_ptr<cel::Ast> ast) const {
    return CreateProgram(std::move(ast), CreateProgramOptions{});
  }

  virtual absl::StatusOr<std::unique_ptr<Program>> CreateProgram(
      std::unique_ptr<cel::Ast> ast,
      const CreateProgramOptions& options) const = 0;

  absl::StatusOr<std::unique_ptr<TraceableProgram>> CreateTraceableProgram(
      std::unique_ptr<cel::Ast> ast) const {
    return CreateTraceableProgram(std::move(ast), CreateProgramOptions{});
  }

  virtual absl::StatusOr<std::unique_ptr<TraceableProgram>>
  CreateTraceableProgram(std::unique_ptr<cel::Ast> ast,
                         const CreateProgramOptions& options) const = 0;

  virtual const TypeProvider& GetTypeProvider() const = 0;

  virtual const google::protobuf::DescriptorPool* absl_nonnull GetDescriptorPool()
      const = 0;

  virtual google::protobuf::MessageFactory* absl_nonnull GetMessageFactory() const = 0;

 private:
  friend class runtime_internal::RuntimeFriendAccess;

  virtual NativeTypeId GetNativeTypeId() const = 0;
};

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_RUNTIME_RUNTIME_H_
