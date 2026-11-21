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

#include "internal/network_addr.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "internal/status_macros.h"

namespace cel::internal {
namespace {

absl::Status MakeParseError(absl::string_view prefix, absl::string_view raw,
                            absl::string_view detail) {
  return absl::InvalidArgumentError(
      absl::StrCat(prefix, " '", raw,
                   "' parse error during conversion from string: ", detail));
}

constexpr uint64_t Ipv4Mask(int prefix_length) {
  if (prefix_length <= 0) {
    return 0;
  }
  if (prefix_length < 32) {
    return (uint64_t{0xFFFFFFFFU} << (32 - prefix_length)) &
           uint64_t{0xFFFFFFFFU};
  }
  return uint64_t{0xFFFFFFFFU};
}

constexpr uint64_t Ipv6HiMask(int prefix_length) {
  if (prefix_length <= 0) {
    return 0;
  }
  if (prefix_length < 64) {
    return ~uint64_t{0} << (64 - prefix_length);
  }
  return ~uint64_t{0};
}

constexpr uint64_t Ipv6LoMask(int prefix_length) {
  if (prefix_length <= 64) {
    return 0;
  }
  if (prefix_length < 128) {
    return ~uint64_t{0} << (128 - prefix_length);
  }
  return ~uint64_t{0};
}

struct ParsedIpWithZone {
  IpAddr ip;
  bool has_zone = false;
};

absl::StatusOr<uint32_t> ParseIpv4(absl::string_view s,
                                   absl::string_view error_prefix,
                                   absl::string_view raw_for_error) {
  uint32_t octet = 0;
  int digits = 0;
  int dot_count = 0;
  uint32_t addr = 0;
  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (absl::ascii_isdigit(static_cast<unsigned char>(c))) {
      if (digits == 1 && octet == 0) {
        return MakeParseError(error_prefix, raw_for_error,
                              "IPv4 field has octet with leading zero");
      }
      octet = octet * 10 + static_cast<uint32_t>(c - '0');
      ++digits;
      if (octet > 255) {
        return MakeParseError(error_prefix, raw_for_error,
                              "IPv4 field has value >255");
      }
    } else if (c == '.') {
      if (digits == 0) {
        return MakeParseError(error_prefix, raw_for_error,
                              "IPv4 field must have at least one digit");
      }
      if (dot_count == 3) {
        return MakeParseError(error_prefix, raw_for_error,
                              "IPv4 address too long");
      }
      addr = (addr << 8) | octet;
      octet = 0;
      digits = 0;
      ++dot_count;
    } else {
      return MakeParseError(error_prefix, raw_for_error,
                            "unexpected character");
    }
  }
  if (dot_count != 3 || digits == 0) {
    return MakeParseError(error_prefix, raw_for_error,
                          "IPv4 address too short");
  }
  return (addr << 8) | octet;
}

absl::StatusOr<IpAddr> ParseIpv6Body(absl::string_view s,
                                     absl::string_view error_prefix,
                                     absl::string_view raw_for_error) {
  if (s.size() < 2) {
    return MakeParseError(error_prefix, raw_for_error, "address too short");
  }

  std::array<uint16_t, 8> hextets{};
  int count = 0;
  int ellipsis = -1;
  size_t i = 0;

  if (s[0] == ':') {
    if (s[1] != ':') {
      return MakeParseError(error_prefix, raw_for_error,
                            "each colon-separated field must have a digit");
    }
    ellipsis = 0;
    i = 2;
  }

  while (i < s.size()) {
    size_t seg_start = i;
    int hex_digits = 0;
    bool hit_dot = false;

    while (i < s.size()) {
      char c = s[i];
      if (absl::ascii_isxdigit(static_cast<unsigned char>(c))) {
        if (hex_digits == 4) {
          return MakeParseError(error_prefix, raw_for_error,
                                "IPv6 field has too many digits");
        }
        ++hex_digits;
        ++i;
      } else if (c == ':') {
        break;
      } else if (c == '.') {
        hit_dot = true;
        break;
      } else {
        return MakeParseError(error_prefix, raw_for_error,
                              "unexpected character");
      }
    }

    if (hit_dot) {
      if (seg_start == 0) {
        return MakeParseError(error_prefix, raw_for_error,
                              "unexpected IPv4 prefix in IPv6 address");
      }
      if (count > 6 || (ellipsis >= 0 && count >= 6)) {
        return MakeParseError(error_prefix, raw_for_error, "address too long");
      }
      CEL_ASSIGN_OR_RETURN(uint32_t v4, ParseIpv4(s.substr(seg_start),
                                                  error_prefix, raw_for_error));
      hextets[count++] = static_cast<uint16_t>((v4 >> 16) & 0xFFFF);
      hextets[count++] = static_cast<uint16_t>(v4 & 0xFFFF);
      break;
    }

    if (hex_digits == 0) {
      // Encountered ':' immediately after consuming the previous ':' -> "::".
      if (ellipsis >= 0) {
        return MakeParseError(error_prefix, raw_for_error,
                              "multiple :: in IPv6 address");
      }
      ellipsis = count;
      ++i;
      continue;
    }

    if (count >= 8) {
      return MakeParseError(error_prefix, raw_for_error, "address too long");
    }
    uint16_t val = 0;
    const bool ok = absl::SimpleHexAtoi(s.substr(seg_start, hex_digits), &val);
    ABSL_DCHECK(ok);
    hextets[count++] = val;

    if (i == s.size()) {
      break;
    }
    ++i;
    if (i == s.size()) {
      return MakeParseError(error_prefix, raw_for_error, "trailing colon");
    }
  }

  std::array<uint16_t, 8> full{};
  if (ellipsis < 0) {
    if (count != 8) {
      return MakeParseError(error_prefix, raw_for_error, "address too short");
    }
    full = hextets;
  } else {
    if (count >= 8) {
      return MakeParseError(error_prefix, raw_for_error,
                            "address too long for :: compression");
    }
    int zeros = 8 - count;
    for (int idx = 0; idx < ellipsis; ++idx) {
      full[idx] = hextets[idx];
    }
    for (int idx = ellipsis; idx < count; ++idx) {
      full[idx + zeros] = hextets[idx];
    }
  }

  uint64_t hi_val = (static_cast<uint64_t>(full[0]) << 48) |
                    (static_cast<uint64_t>(full[1]) << 32) |
                    (static_cast<uint64_t>(full[2]) << 16) |
                    static_cast<uint64_t>(full[3]);
  uint64_t lo_val = (static_cast<uint64_t>(full[4]) << 48) |
                    (static_cast<uint64_t>(full[5]) << 32) |
                    (static_cast<uint64_t>(full[6]) << 16) |
                    static_cast<uint64_t>(full[7]);
  return IpAddr{6, hi_val, lo_val};
}

absl::StatusOr<ParsedIpWithZone> ParseIpWithOptionalZone(
    absl::string_view raw, absl::string_view error_prefix,
    absl::string_view raw_for_error) {
  if (raw.empty()) {
    return MakeParseError(error_prefix, raw_for_error, "unable to parse IP");
  }
  if (absl::StrContains(raw, ':')) {
    absl::string_view ip_part = raw;
    bool has_zone = false;
    size_t pct_idx = raw.find('%');
    if (pct_idx != absl::string_view::npos) {
      ip_part = raw.substr(0, pct_idx);
      absl::string_view zone = raw.substr(pct_idx + 1);
      if (zone.empty()) {
        return MakeParseError(error_prefix, raw_for_error,
                              "zone must be non-empty");
      }
      has_zone = true;
    }
    CEL_ASSIGN_OR_RETURN(IpAddr ip,
                         ParseIpv6Body(ip_part, error_prefix, raw_for_error));
    return ParsedIpWithZone{ip, has_zone};
  }
  if (absl::StrContains(raw, '.')) {
    CEL_ASSIGN_OR_RETURN(uint32_t v4,
                         ParseIpv4(raw, error_prefix, raw_for_error));
    return ParsedIpWithZone{IpAddr{4, 0, static_cast<uint64_t>(v4)}, false};
  }
  return MakeParseError(error_prefix, raw_for_error, "unable to parse IP");
}

absl::StatusOr<int> ParsePrefixLength(absl::string_view prefix_part,
                                      int max_bits,
                                      absl::string_view raw_for_error) {
  if (prefix_part.empty()) {
    return MakeParseError("CIDR", raw_for_error, "empty prefix");
  }
  for (char c : prefix_part) {
    if (!absl::ascii_isdigit(static_cast<unsigned char>(c))) {
      return MakeParseError("CIDR", raw_for_error, "invalid prefix length");
    }
  }
  if (prefix_part.size() > 1 && prefix_part[0] == '0') {
    return MakeParseError("CIDR", raw_for_error,
                          "prefix length has leading zero");
  }
  if (prefix_part.size() > 3) {
    return MakeParseError("CIDR", raw_for_error, "prefix length out of range");
  }
  int value = 0;
  if (!absl::SimpleAtoi(prefix_part, &value) || value < 0 || value > max_bits) {
    return MakeParseError("CIDR", raw_for_error, "prefix length out of range");
  }
  return value;
}

}  // namespace

bool IpAddr::IsUnspecified() const {
  if (family == 4) {
    return lo == 0;
  }
  return hi == 0 && lo == 0;
}

bool IpAddr::IsLoopback() const {
  if (family == 4) {
    return ((lo >> 24) & 0xFF) == 127;
  }
  return hi == 0 && lo == 1;
}

bool IpAddr::IsLinkLocalUnicast() const {
  if (family == 4) {
    return ((lo >> 24) & 0xFF) == 169 && ((lo >> 16) & 0xFF) == 254;
  }
  return ((hi >> 56) & 0xFF) == 0xFE && ((hi >> 48) & 0xC0) == 0x80;
}

bool IpAddr::IsLinkLocalMulticast() const {
  if (family == 4) {
    return ((lo >> 24) & 0xFF) == 224 && ((lo >> 16) & 0xFF) == 0 &&
           ((lo >> 8) & 0xFF) == 0;
  }
  return ((hi >> 56) & 0xFF) == 0xFF && ((hi >> 48) & 0x0F) == 0x02;
}

bool IpAddr::IsMulticast() const {
  if (family == 4) {
    return ((lo >> 24) & 0xF0) == 0xE0;
  }
  return ((hi >> 56) & 0xFF) == 0xFF;
}

bool IpAddr::IsGlobalUnicast() const {
  if (family == 4 && lo == 0xFFFFFFFFULL) {
    return false;
  }
  return !IsUnspecified() && !IsLoopback() && !IsMulticast() &&
         !IsLinkLocalUnicast();
}

IpAddr IpAddr::Mask(int prefix_length) const {
  if (family == 4) {
    return IpAddr{4, 0, lo & Ipv4Mask(prefix_length)};
  }
  return IpAddr{6, hi & Ipv6HiMask(prefix_length),
                lo & Ipv6LoMask(prefix_length)};
}

bool IpAddr::MatchesPrefix(const IpAddr& other, int prefix_length) const {
  if (family != other.family) {
    return false;
  }
  if (family == 4) {
    return ((lo ^ other.lo) & Ipv4Mask(prefix_length)) == 0;
  }
  return ((hi ^ other.hi) & Ipv6HiMask(prefix_length)) == 0 &&
         ((lo ^ other.lo) & Ipv6LoMask(prefix_length)) == 0;
}

std::string IpAddr::ToCanonicalString() const {
  if (family == 4) {
    return absl::StrFormat("%u.%u.%u.%u",
                           static_cast<unsigned>((lo >> 24) & 0xFF),
                           static_cast<unsigned>((lo >> 16) & 0xFF),
                           static_cast<unsigned>((lo >> 8) & 0xFF),
                           static_cast<unsigned>(lo & 0xFF));
  }

  std::array<uint16_t, 8> h = {
      static_cast<uint16_t>((hi >> 48) & 0xFFFF),
      static_cast<uint16_t>((hi >> 32) & 0xFFFF),
      static_cast<uint16_t>((hi >> 16) & 0xFFFF),
      static_cast<uint16_t>(hi & 0xFFFF),
      static_cast<uint16_t>((lo >> 48) & 0xFFFF),
      static_cast<uint16_t>((lo >> 32) & 0xFFFF),
      static_cast<uint16_t>((lo >> 16) & 0xFFFF),
      static_cast<uint16_t>(lo & 0xFFFF),
  };

  int best_start = -1;
  int best_len = 0;
  int cur_start = -1;
  int cur_len = 0;
  for (int i = 0; i < 8; ++i) {
    if (h[i] == 0) {
      if (cur_start < 0) {
        cur_start = i;
        cur_len = 1;
      } else {
        ++cur_len;
      }
      if (cur_len > best_len && cur_len >= 2) {
        best_start = cur_start;
        best_len = cur_len;
      }
    } else {
      cur_start = -1;
      cur_len = 0;
    }
  }

  std::string out;
  out.reserve(39);
  for (int i = 0; i < 8; ++i) {
    if (i == best_start) {
      out.append("::");
      i += best_len - 1;
      continue;
    }
    if (i > 0 && !(best_start >= 0 && i == best_start + best_len)) {
      out.push_back(':');
    }
    absl::StrAppendFormat(&out, "%x", static_cast<unsigned>(h[i]));
  }
  return out;
}

std::string CidrPrefix::ToCanonicalString() const {
  return absl::StrCat(ip.ToCanonicalString(), "/", prefix_length);
}

absl::StatusOr<IpAddr> ParseIp(absl::string_view raw) {
  CEL_ASSIGN_OR_RETURN(ParsedIpWithZone parsed,
                       ParseIpWithOptionalZone(raw, "IP Address", raw));
  if (parsed.has_zone) {
    return absl::InvalidArgumentError(
        absl::StrCat("IP address '", raw, "' with zone value is not allowed"));
  }
  if (parsed.ip.Is4In6()) {
    return absl::InvalidArgumentError(
        absl::StrCat("IPv4-mapped IPv6 address '", raw, "' is not allowed"));
  }
  return parsed.ip;
}

absl::StatusOr<CidrPrefix> ParseCidr(absl::string_view raw) {
  size_t slash = raw.rfind('/');
  if (slash == absl::string_view::npos) {
    return MakeParseError("CIDR", raw, "no '/'");
  }
  absl::string_view ip_part = raw.substr(0, slash);
  absl::string_view prefix_part = raw.substr(slash + 1);

  CEL_ASSIGN_OR_RETURN(ParsedIpWithZone parsed,
                       ParseIpWithOptionalZone(ip_part, "CIDR", raw));

  int max_bits = parsed.ip.family == 4 ? 32 : 128;
  CEL_ASSIGN_OR_RETURN(int prefix_len,
                       ParsePrefixLength(prefix_part, max_bits, raw));

  if (parsed.has_zone) {
    return absl::InvalidArgumentError(
        absl::StrCat("CIDR '", raw, "' with zone value is not allowed"));
  }
  if (parsed.ip.Is4In6()) {
    return absl::InvalidArgumentError(
        absl::StrCat("IPv4-mapped IPv6 address '", raw, "' is not allowed"));
  }
  return CidrPrefix{parsed.ip, prefix_len};
}

}  // namespace cel::internal
