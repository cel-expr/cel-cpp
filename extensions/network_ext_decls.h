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

// Type declarations, opaque types, and compile-time literal validator for the
// CEL network extension library (`cel.lib.ext.network`), matching `cel-go`'s
// `ext.Network()` semantics.
//
// IP addresses and CIDR prefixes are parsed strictly: leading zeros, IPv6 zone
// identifiers, and IPv4-mapped IPv6 addresses (`::ffff:0:0/96`) are rejected.
//
// # Types
//
//   net.IP
//   net.CIDR
//
// # Global Functions
//
//   ip(string) -> net.IP
//   isIP(string) -> bool
//   ip.isCanonical(string) -> bool
//   cidr(string) -> net.CIDR
//   isCIDR(string) -> bool
//   string(net.IP) -> string
//   string(net.CIDR) -> string
//
// # net.IP Member Functions
//
//   <net.IP>.family() -> int
//   <net.IP>.isUnspecified() -> bool
//   <net.IP>.isLoopback() -> bool
//   <net.IP>.isGlobalUnicast() -> bool
//   <net.IP>.isLinkLocalMulticast() -> bool
//   <net.IP>.isLinkLocalUnicast() -> bool
//
// # net.CIDR Member Functions
//
//   <net.CIDR>.ip() -> net.IP
//   <net.CIDR>.masked() -> net.CIDR
//   <net.CIDR>.isMask() -> bool
//   <net.CIDR>.prefixLength() -> int
//   <net.CIDR>.containsIP(net.IP) -> bool
//   <net.CIDR>.containsIP(string) -> bool
//   <net.CIDR>.containsCIDR(net.CIDR) -> bool
//   <net.CIDR>.containsCIDR(string) -> bool

#ifndef THIRD_PARTY_CEL_CPP_EXTENSIONS_NETWORK_EXT_DECLS_H_
#define THIRD_PARTY_CEL_CPP_EXTENSIONS_NETWORK_EXT_DECLS_H_

#include "checker/type_checker_builder.h"
#include "common/type.h"
#include "compiler/compiler.h"
#include "validator/validator.h"

namespace cel::extensions {

constexpr int kNetworkExtensionLatestVersion = 1;

// Returns the CEL opaque type for `net.IP`.
OpaqueType NetIpType();

// Returns the CEL opaque type for `net.CIDR`.
OpaqueType NetCidrType();

// Type check declarations for the network extension library.
CheckerLibrary NetworkCheckerLibrary(
    int version = kNetworkExtensionLatestVersion);

// Compiler declarations for the network extension library.
CompilerLibrary NetworkCompilerLibrary(
    int version = kNetworkExtensionLatestVersion);

// Returns a `Validation` (`"cel.validator.network"`) that validates string
// literal arguments passed to global `ip(...)` and `cidr(...)` calls.
Validation NetworkValidator();

}  // namespace cel::extensions

#endif  // THIRD_PARTY_CEL_CPP_EXTENSIONS_NETWORK_EXT_DECLS_H_
