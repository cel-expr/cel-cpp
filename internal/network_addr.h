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

// Portable IPv4/IPv6 address and CIDR prefix representation, strict parsing,
// and RFC 5952 canonical formatting matching Go's `net/netip` and CEL's
// `cel.lib.ext.network` extension semantics.

#ifndef THIRD_PARTY_CEL_CPP_INTERNAL_NETWORK_ADDR_H_
#define THIRD_PARTY_CEL_CPP_INTERNAL_NETWORK_ADDR_H_

#include <cstdint>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace cel::internal {

// Value representation of an IPv4 or IPv6 address in network byte order
// (`family == 4` stores the 32-bit address in the low 32 bits of `lo`;
// `family == 6` stores the 128-bit address across `hi` and `lo`).
struct IpAddr {
  uint8_t family = 4;  // 4 or 6
  uint64_t hi = 0;
  uint64_t lo = 0;

  bool Is4In6() const { return family == 6 && hi == 0 && (lo >> 32) == 0xFFFF; }

  int64_t Family() const { return static_cast<int64_t>(family); }

  bool IsUnspecified() const;
  bool IsLoopback() const;
  bool IsLinkLocalUnicast() const;
  bool IsLinkLocalMulticast() const;
  bool IsMulticast() const;
  bool IsGlobalUnicast() const;

  IpAddr Mask(int prefix_length) const;
  bool MatchesPrefix(const IpAddr& other, int prefix_length) const;

  // Formats IPv4 dotted-decimal or RFC 5952 canonical IPv6 text.
  std::string ToCanonicalString() const;

  bool operator==(const IpAddr& other) const {
    return family == other.family && hi == other.hi && lo == other.lo;
  }

  bool operator!=(const IpAddr& other) const { return !(*this == other); }
};

// Value representation of a CIDR prefix (`ip/prefix_length`), preserving any
// unmasked host bits in `ip` to match Go `netip.Prefix`.
struct CidrPrefix {
  IpAddr ip;
  int prefix_length = 0;

  CidrPrefix Masked() const {
    return CidrPrefix{ip.Mask(prefix_length), prefix_length};
  }

  bool IsMask() const { return ip == ip.Mask(prefix_length); }

  bool ContainsIp(const IpAddr& other) const {
    return ip.MatchesPrefix(other, prefix_length);
  }

  bool ContainsCidr(const CidrPrefix& child) const {
    return ip.family == child.ip.family &&
           prefix_length <= child.prefix_length && ContainsIp(child.ip);
  }

  std::string ToCanonicalString() const;

  bool operator==(const CidrPrefix& other) const {
    return prefix_length == other.prefix_length && ip == other.ip;
  }

  bool operator!=(const CidrPrefix& other) const { return !(*this == other); }
};

// Strictly parses an IPv4 or IPv6 address, rejecting leading zeros, zone
// identifiers, and IPv4-mapped IPv6 addresses (`::ffff:0:0/96`).
absl::StatusOr<IpAddr> ParseIp(absl::string_view raw);

// Strictly parses a CIDR prefix (`addr/prefix_length`), rejecting leading
// zeros, zone identifiers, and IPv4-mapped IPv6 addresses (`::ffff:0:0/96`).
absl::StatusOr<CidrPrefix> ParseCidr(absl::string_view raw);

}  // namespace cel::internal

#endif  // THIRD_PARTY_CEL_CPP_INTERNAL_NETWORK_ADDR_H_
