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

#include "testutil/test_external_extensions_descriptor_set.h"

#include "internal/testing.h"
#include "google/protobuf/descriptor.h"

namespace cel::test {
namespace {

using ::testing::NotNull;

TEST(GetTestExternalExtensionsFileDescriptorSet, BuildsPool) {
  const auto& fds = GetTestExternalExtensionsFileDescriptorSet();
  EXPECT_GT(fds.file_size(), 0);

  google::protobuf::DescriptorPool pool;
  for (const auto& file : fds.file()) {
    EXPECT_THAT(pool.BuildFile(file), NotNull());
  }

  EXPECT_THAT(
      pool.FindMessageTypeByName("cel.cpp.testutil.TestExternalExtensions"),
      NotNull());
  EXPECT_THAT(pool.FindExtensionByName(
                  "cel.cpp.testutil.test_external_extensions_message"),
              NotNull());
  EXPECT_THAT(
      pool.FindExtensionByName(
          "cel.cpp.testutil.ScopedTestExternalExtensions.scoped_ext_string"),
      NotNull());
}

TEST(GetTestExternalExtensionsFileDescriptor, CorrectFileDescriptor) {
  const auto& fd = GetTestExternalExtensionsFileDescriptor();
  EXPECT_EQ(fd.name(),
            "testutil/test_external_extensions.proto");
  EXPECT_EQ(fd.package(), "cel.cpp.testutil");
  EXPECT_GT(fd.message_type_size(), 0);
  EXPECT_GT(fd.extension_size(), 0);
}

}  // namespace
}  // namespace cel::test
