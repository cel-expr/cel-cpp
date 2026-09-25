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
#include "eval/eval/compiler_constant_step.h"

#include <memory>

#include "common/native_type.h"
#include "common/value.h"
#include "eval/eval/attribute_trail.h"
#include "eval/eval/direct_expression_step.h"
#include "eval/eval/evaluator_core.h"
#include "internal/testing.h"
#include "internal/testing_descriptor_pool.h"
#include "internal/testing_message_factory.h"
#include "runtime/activation.h"
#include "runtime/internal/runtime_type_provider.h"
#include "runtime/runtime_options.h"
#include "google/protobuf/arena.h"

namespace google::api::expr::runtime {

namespace {

class DirectCompilerConstantStepTest : public testing::Test {
 public:
  DirectCompilerConstantStepTest()
      : type_provider_(cel::internal::GetTestingDescriptorPool()) {}

 protected:
  google::protobuf::Arena arena_;
  cel::runtime_internal::RuntimeTypeProvider type_provider_;
  cel::Activation empty_activation_;
  cel::RuntimeOptions options_;
};

TEST_F(DirectCompilerConstantStepTest, Evaluate) {
  ExecutionFrameBase frame(empty_activation_, options_, type_provider_,
                           cel::internal::GetTestingDescriptorPool(),
                           cel::internal::GetTestingMessageFactory(), &arena_);
  DirectCompilerConstantStep step(cel::IntValue(42), -1);
  cel::Value result;
  AttributeTrail attr;

  ASSERT_THAT(step.Evaluate(frame, result, attr), absl_testing::IsOk());

  EXPECT_EQ(result.GetInt().NativeValue(), 42);
}

TEST_F(DirectCompilerConstantStepTest, TypeId) {
  DirectCompilerConstantStep step(cel::IntValue(42), -1);

  const DirectExpressionStep& abstract_step = step;
  EXPECT_EQ(abstract_step.GetNativeTypeId(),
            cel::NativeTypeId::For<DirectCompilerConstantStep>());
}

TEST_F(DirectCompilerConstantStepTest, Value) {
  DirectCompilerConstantStep step(cel::IntValue(42), -1);

  EXPECT_EQ(step.value().GetInt().NativeValue(), 42);
}

}  // namespace
}  // namespace google::api::expr::runtime
