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

// Runtime bindings and opaque value registrations (`net.IP`, `net.CIDR`) for
// the CEL network extension library (`cel.lib.ext.network`).

#ifndef THIRD_PARTY_CEL_CPP_EXTENSIONS_NETWORK_EXT_H_
#define THIRD_PARTY_CEL_CPP_EXTENSIONS_NETWORK_EXT_H_

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "eval/public/cel_function_registry.h"
#include "eval/public/cel_options.h"
#include "extensions/network_ext_decls.h"  // IWYU pragma: export
#include "runtime/runtime_builder.h"

namespace cel::extensions {

// Registers the network extension functions and types (`net.IP`, `net.CIDR`)
// with `builder`.
//
// Requires `RuntimeOptions.enable_qualified_type_identifiers` and
// `RuntimeOptions.enable_heterogeneous_equality` to be enabled.
absl::Status RegisterNetworkFunctions(
    RuntimeBuilder& builder, int version = kNetworkExtensionLatestVersion);

// Registers the pure-string network extension functions (`isIP`, `isCIDR`,
// `ip.isCanonical`) for the legacy `google::api::expr::runtime::CelValue`
// runtime.
//
// Note: `CelValue` does not support opaque types, so functions producing or
// consuming `net.IP` or `net.CIDR` require `cel::RuntimeBuilder`.
absl::Status RegisterNetworkFunctions(
    google::api::expr::runtime::CelFunctionRegistry* absl_nonnull registry,
    const google::api::expr::runtime::InterpreterOptions& options,
    int version = kNetworkExtensionLatestVersion);

}  // namespace cel::extensions

#endif  // THIRD_PARTY_CEL_CPP_EXTENSIONS_NETWORK_EXT_H_
