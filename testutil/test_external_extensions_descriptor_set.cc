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

#include <cstdint>
#include <iterator>

#include "google/protobuf/descriptor.pb.h"
#include "absl/base/attributes.h"
#include "absl/base/no_destructor.h"
#include "absl/base/optimization.h"
#include "absl/log/absl_check.h"

namespace cel::test {

namespace {

ABSL_CONST_INIT const uint8_t kTestExternalExtensionsDescriptorSet[] = {
#include "testutil/test_external_extensions_descriptor_set_embed.inc"
};

}  // namespace

const google::protobuf::FileDescriptorSet& GetTestExternalExtensionsFileDescriptorSet() {
  static const absl::NoDestructor<google::protobuf::FileDescriptorSet>
      file_descriptor_set([]() {
        google::protobuf::FileDescriptorSet file_desc_set;
        ABSL_CHECK(file_desc_set.ParseFromArray(  // Crash OK
           kTestExternalExtensionsDescriptorSet,
           std::size(kTestExternalExtensionsDescriptorSet)));
        return file_desc_set;
      }());
  return *file_descriptor_set;
}

const google::protobuf::FileDescriptorProto& GetTestExternalExtensionsFileDescriptor() {
  static const google::protobuf::FileDescriptorProto* const file_desc = []() {
    const auto& file_desc_set = GetTestExternalExtensionsFileDescriptorSet();
    for (const auto& file_desc : file_desc_set.file()) {
      if (file_desc.name() ==
          "testutil/test_external_extensions.proto") {
        return &file_desc;
      }
    }
    ABSL_UNREACHABLE();
  }();
  return *file_desc;
}

}  // namespace cel::test
