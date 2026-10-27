// Copyright 2026 Google LLC
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

#ifndef THIRD_PARTY_CEL_CPP_POLICY_CEL_POLICY_PARSE_CONTEXT_H_
#define THIRD_PARTY_CEL_CPP_POLICY_CEL_POLICY_PARSE_CONTEXT_H_

#include <memory>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "policy/cel_policy.h"
#include "policy/cel_policy_parse_result.h"

namespace cel {

// A mutable context for parsing a CelPolicy. An instance of this class is
// created for each policy parse and is passed to the parser, which is meant to
// be stateless.
//
// Parsers call methods on this class to report issues and populate the policy
// being parsed. Call GetResult() to obtain the resulting CelPolicyParseResult,
// which takes ownership of the parsed policy.  Do not use the context after
// calling GetResult().
class CelPolicyParseContext {
 public:
  explicit CelPolicyParseContext(std::shared_ptr<CelPolicySource> policy_source)
      : policy_source_(std::move(policy_source)),
        policy_(std::make_unique<CelPolicy>(policy_source_)) {}

  CelPolicySource& policy_source() const { return *policy_source_; }

  // Returns the policy being parsed. It should not be used after
  // calling GetResult().
  CelPolicy& policy() const;

  // The context should not be used after calling GetResult().
  CelPolicyParseResult GetResult();

  // Reports an error for the given element with the given error message.
  void ReportError(CelPolicyElementId id, std::string_view message);

  CelPolicyElementId next_element_id() { return next_element_id_++; }

 private:
  std::shared_ptr<CelPolicySource> policy_source_;
  CelPolicyElementId next_element_id_ = 0;
  std::vector<CelPolicyIssue> issues_;
  std::unique_ptr<CelPolicy> policy_;
};

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_POLICY_CEL_POLICY_PARSE_CONTEXT_H_
