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

#include <string>
#include <utility>
#include <vector>

#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "internal/testing.h"

namespace cel::internal {
namespace {

using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::AllOf;
using ::testing::HasSubstr;
using ::testing::Not;
using ::testing::Property;

IpAddr MustParseIp(absl::string_view raw) {
  absl::StatusOr<IpAddr> parsed = ParseIp(raw);
  ABSL_CHECK_OK(parsed);
  return *parsed;
}

CidrPrefix MustParseCidr(absl::string_view raw) {
  absl::StatusOr<CidrPrefix> parsed = ParseCidr(raw);
  ABSL_CHECK_OK(parsed);
  return *parsed;
}

TEST(NetworkAddrTest, ParseIpv4AndCanonicalFormatting) {
  ASSERT_OK_AND_ASSIGN(IpAddr addr, ParseIp("192.168.0.1"));
  EXPECT_EQ(addr.Family(), 4);
  EXPECT_EQ(addr.ToCanonicalString(), "192.168.0.1");
  EXPECT_FALSE(addr.Is4In6());

  ASSERT_OK_AND_ASSIGN(IpAddr zero, ParseIp("0.0.0.0"));
  EXPECT_EQ(zero.ToCanonicalString(), "0.0.0.0");
  EXPECT_NE(addr, zero);
  EXPECT_EQ(addr, addr);
}

TEST(NetworkAddrTest, ParseIpv6AndRfc5952Formatting) {
  const std::vector<std::pair<std::string, std::string>> cases = {
      {"2001:0DB8:0000:0000:0008:0800:200C:417A", "2001:db8::8:800:200c:417a"},
      {"2001:db8:0:1:1:1:1:1", "2001:db8:0:1:1:1:1:1"},
      {"2001:0:0:1:0:0:0:1", "2001:0:0:1::1"},
      {"2001:db8:0:0:1:0:0:1", "2001:db8::1:0:0:1"},
      {"2001:0:0:1:0:0:1:1", "2001::1:0:0:1:1"},
      {"0:0:0:0:0:0:0:0", "::"},
      {"0:0:0:0:0:0:0:1", "::1"},
      {"1::", "1::"},
      {"1:2:3:4:5:6:7::", "1:2:3:4:5:6:7:0"},
      {"::2:3:4:5:6:7:8", "0:2:3:4:5:6:7:8"},
      {"64:ff9b::192.0.2.1", "64:ff9b::c000:201"},
      {"::192.168.0.1", "::c0a8:1"},
      {"::0.0.0.0", "::"},
      {"::0.0.0.1", "::1"},
      {"::ffff:0:192.168.0.1", "::ffff:0:c0a8:1"},
      {"1:2:3:4:5:6:1.2.3.4", "1:2:3:4:5:6:102:304"},
      {"1:2:3:4:5::1.2.3.4", "1:2:3:4:5:0:102:304"},
  };
  for (const auto& tc : cases) {
    EXPECT_THAT(
        ParseIp(tc.first),
        IsOkAndHolds(AllOf(Property(&IpAddr::Family, 6),
                           Property(&IpAddr::ToCanonicalString, tc.second))))
        << "Input: " << tc.first;
  }
}

TEST(NetworkAddrTest, IpPredicates) {
  // Unspecified.
  EXPECT_TRUE(MustParseIp("0.0.0.0").IsUnspecified());
  EXPECT_FALSE(MustParseIp("127.0.0.1").IsUnspecified());
  EXPECT_TRUE(MustParseIp("::").IsUnspecified());
  EXPECT_FALSE(MustParseIp("::1").IsUnspecified());

  // Loopback.
  EXPECT_TRUE(MustParseIp("127.0.0.0").IsLoopback());
  EXPECT_TRUE(MustParseIp("127.0.0.1").IsLoopback());
  EXPECT_TRUE(MustParseIp("127.255.255.255").IsLoopback());
  EXPECT_FALSE(MustParseIp("128.0.0.1").IsLoopback());
  EXPECT_TRUE(MustParseIp("::1").IsLoopback());
  EXPECT_FALSE(MustParseIp("::2").IsLoopback());

  // Link-local unicast.
  EXPECT_TRUE(MustParseIp("169.254.0.0").IsLinkLocalUnicast());
  EXPECT_TRUE(MustParseIp("169.254.169.254").IsLinkLocalUnicast());
  EXPECT_TRUE(MustParseIp("169.254.255.255").IsLinkLocalUnicast());
  EXPECT_FALSE(MustParseIp("192.168.0.1").IsLinkLocalUnicast());
  EXPECT_TRUE(MustParseIp("fe80::1").IsLinkLocalUnicast());
  EXPECT_TRUE(MustParseIp("febf:ffff::1").IsLinkLocalUnicast());
  EXPECT_FALSE(MustParseIp("fec0::1").IsLinkLocalUnicast());
  EXPECT_FALSE(MustParseIp("fd80::1").IsLinkLocalUnicast());

  // Link-local multicast & multicast.
  EXPECT_TRUE(MustParseIp("224.0.0.0").IsLinkLocalMulticast());
  EXPECT_TRUE(MustParseIp("224.0.0.255").IsLinkLocalMulticast());
  EXPECT_FALSE(MustParseIp("224.0.1.0").IsLinkLocalMulticast());
  EXPECT_TRUE(MustParseIp("224.0.1.0").IsMulticast());
  EXPECT_TRUE(MustParseIp("ff02::1").IsLinkLocalMulticast());
  EXPECT_TRUE(MustParseIp("ff12::1").IsLinkLocalMulticast());
  EXPECT_TRUE(MustParseIp("ff32::1").IsLinkLocalMulticast());
  EXPECT_FALSE(MustParseIp("ff01::1").IsLinkLocalMulticast());
  EXPECT_TRUE(MustParseIp("ff01::1").IsMulticast());
  EXPECT_FALSE(MustParseIp("fd00::1").IsLinkLocalMulticast());

  // Global unicast.
  EXPECT_TRUE(MustParseIp("192.168.0.1").IsGlobalUnicast());
  EXPECT_TRUE(MustParseIp("8.8.8.8").IsGlobalUnicast());
  EXPECT_TRUE(MustParseIp("0.1.2.3").IsGlobalUnicast());
  EXPECT_FALSE(MustParseIp("255.255.255.255").IsGlobalUnicast());
  EXPECT_FALSE(MustParseIp("224.0.0.1").IsGlobalUnicast());
  EXPECT_FALSE(MustParseIp("224.0.1.0").IsGlobalUnicast());
  EXPECT_FALSE(MustParseIp("239.0.0.1").IsGlobalUnicast());
  EXPECT_FALSE(MustParseIp("169.254.1.1").IsGlobalUnicast());
  EXPECT_FALSE(MustParseIp("127.0.0.1").IsGlobalUnicast());
  EXPECT_FALSE(MustParseIp("0.0.0.0").IsGlobalUnicast());
  EXPECT_TRUE(MustParseIp("2001:db8::abcd").IsGlobalUnicast());
  EXPECT_TRUE(MustParseIp("fec0::1").IsGlobalUnicast());
  EXPECT_TRUE(MustParseIp("fc00::1").IsGlobalUnicast());
  EXPECT_FALSE(MustParseIp("ff00::1").IsGlobalUnicast());
  EXPECT_FALSE(MustParseIp("ff01::1").IsGlobalUnicast());
  EXPECT_FALSE(MustParseIp("::1").IsGlobalUnicast());
  EXPECT_FALSE(MustParseIp("::").IsGlobalUnicast());
  EXPECT_FALSE(MustParseIp("fe80::1").IsGlobalUnicast());
}

TEST(NetworkAddrTest, CidrMaskAndContainment) {
  CidrPrefix unmasked_v4 = MustParseCidr("192.168.0.1/24");
  EXPECT_EQ(unmasked_v4.ToCanonicalString(), "192.168.0.1/24");
  EXPECT_FALSE(unmasked_v4.IsMask());
  EXPECT_EQ(unmasked_v4.Masked(), MustParseCidr("192.168.0.0/24"));
  EXPECT_TRUE(unmasked_v4.Masked().IsMask());
  EXPECT_NE(unmasked_v4, unmasked_v4.Masked());
  EXPECT_EQ(MustParseCidr("192.168.0.1/32").Masked(),
            MustParseCidr("192.168.0.1/32"));
  EXPECT_EQ(MustParseCidr("192.168.0.1/0").Masked(),
            MustParseCidr("0.0.0.0/0"));
  EXPECT_EQ(MustParseIp("192.168.0.1").Mask(-1), MustParseIp("0.0.0.0"));

  // IPv6 masks across 0, (0,64), 64, (64,128), 128.
  EXPECT_EQ(MustParseCidr("2001:db8::1/0").Masked(), MustParseCidr("::/0"));
  EXPECT_EQ(MustParseIp("2001:db8::1").Mask(-1), MustParseIp("::"));
  EXPECT_EQ(MustParseCidr("2001:db8::1/32").Masked(),
            MustParseCidr("2001:db8::/32"));
  EXPECT_EQ(MustParseCidr("2001:db8:0:1::1/64").Masked(),
            MustParseCidr("2001:db8:0:1::/64"));
  EXPECT_EQ(MustParseCidr("2001:db8::1:2/96").Masked(),
            MustParseCidr("2001:db8::/96"));
  EXPECT_EQ(MustParseCidr("2001:db8::1:2/112").Masked(),
            MustParseCidr("2001:db8::1:0/112"));
  EXPECT_FALSE(MustParseCidr("2001:db8::1:2/112").IsMask());
  EXPECT_TRUE(MustParseCidr("2001:db8::1:0/112").IsMask());
  EXPECT_EQ(MustParseCidr("2001:db8::1/128").Masked(),
            MustParseCidr("2001:db8::1/128"));

  // IP and CIDR containment (including unmasked host bits and cross-family).
  EXPECT_TRUE(
      MustParseCidr("192.168.0.1/24").ContainsIp(MustParseIp("192.168.0.0")));
  EXPECT_TRUE(
      MustParseCidr("192.168.0.0/24").ContainsIp(MustParseIp("192.168.0.255")));
  EXPECT_FALSE(
      MustParseCidr("192.168.0.0/24").ContainsIp(MustParseIp("192.168.1.0")));
  EXPECT_FALSE(
      MustParseCidr("192.168.0.0/24").ContainsIp(MustParseIp("2001:db8::1")));
  EXPECT_TRUE(MustParseCidr("2001:db8::1:0/112")
                  .ContainsIp(MustParseIp("2001:db8::1:ffff")));
  EXPECT_FALSE(MustParseCidr("2001:db8::1:0/112")
                   .ContainsIp(MustParseIp("2001:db8::2:0")));
  EXPECT_TRUE(MustParseCidr("192.168.1.5/24")
                  .ContainsCidr(MustParseCidr("192.168.1.130/25")));
  EXPECT_TRUE(MustParseCidr("192.168.1.5/24")
                  .ContainsCidr(MustParseCidr("192.168.1.99/24")));
  EXPECT_FALSE(MustParseCidr("192.168.0.0/24")
                   .ContainsCidr(MustParseCidr("192.168.0.0/23")));
  EXPECT_TRUE(MustParseCidr("2001:db8::/96")
                  .ContainsCidr(MustParseCidr("2001:db8::/112")));
  EXPECT_FALSE(MustParseCidr("2001:db8::/32")
                   .ContainsCidr(MustParseCidr("192.168.0.0/24")));
}

TEST(NetworkAddrTest, ParseIpErrors) {
  const std::vector<std::pair<std::string, std::string>> bad_ips = {
      {"", "unable to parse IP"},
      {"1", "unable to parse IP"},
      {"1..2.3", "must have at least one digit"},
      {".1.2.3", "must have at least one digit"},
      {"1.2.3.", "IPv4 address too short"},
      {"1.2.3", "IPv4 address too short"},
      {"192.168.0.a", "unexpected character"},
      {"192.168.0.1%eth0", "unexpected character"},
      {"192.168.0.1.0", "IPv4 address too long"},
      {"01.2.3.4", "leading zero"},
      {"256.0.0.1", "value >255"},
      {":%eth0", "address too short"},
      {"2001:0db80::1", "too many digits"},
      {"2001:db8::g", "unexpected character"},
      {"2001:db8:::68", "multiple ::"},
      {":::", "multiple ::"},
      {":1::", "colon-separated"},
      {"::1:", "trailing colon"},
      {":1", "colon-separated"},
      {"1:", "trailing colon"},
      {"192.168.0.1:80", "unexpected IPv4 prefix"},
      {"1.2.3.4::", "unexpected IPv4 prefix"},
      {"::1.2.3.4:5", "unexpected character"},
      {"1:2:3:4:5:6:7:1.2.3.4", "address too long"},
      {"1:2:3:4:5:6::1.2.3.4", "address too long"},
      {"1:2:3:4:5:1.2.3.4", "address too short"},
      {"2001:db8::192.168.0.01", "leading zero"},
      {"fe80::192.168.0.1%eth0", "zone value is not allowed"},
      {"1:2:3:4:5:6:7:8:9", "address too long"},
      {"1:2:3:4:5:6:7", "address too short"},
      {"2001:db8:1", "address too short"},
      {"1:2:3:4:5:6:7:8::", "address too long for :: compression"},
      {"::1:2:3:4:5:6:7:8", "address too long for :: compression"},
      {"fe80::1%en0", "zone value is not allowed"},
      {"fe80::1%", "zone must be non-empty"},
      {"::ffff:192.168.0.1", "IPv4-mapped IPv6 address"},
      {"::ffff:c0a8:1", "IPv4-mapped IPv6 address"},
  };
  for (const auto& tc : bad_ips) {
    EXPECT_THAT(ParseIp(tc.first), StatusIs(absl::StatusCode::kInvalidArgument,
                                            HasSubstr(tc.second)))
        << "Input: " << tc.first;
  }
}

TEST(NetworkAddrTest, ParseCidrErrorsAndNoDoubleWrapping) {
  EXPECT_THAT(
      ParseCidr("invalid/24"),
      StatusIs(absl::StatusCode::kInvalidArgument,
               AllOf(HasSubstr("CIDR 'invalid/24' parse error during "
                               "conversion from string: unable to parse IP"),
                     Not(HasSubstr("CIDR 'invalid' parse error")))));

  const std::vector<std::pair<std::string, std::string>> bad_cidrs = {
      {"192.168.0.0", "no '/'"},
      {":/64", "address too short"},
      {"999.0.0.1/24", "value >255"},
      {"192.168.0.0/", "empty prefix"},
      {"10.0.0.0/00", "leading zero"},
      {"192.168.0.0/08", "leading zero"},
      {"2001:db8::/1000", "out of range"},
      {"10.0.0.0/-1", "invalid prefix length"},
      {"10.0.0.0/+24", "invalid prefix length"},
      {"10.0.0.0/abcd", "invalid prefix length"},
      {"fe80::1/64%en0", "invalid prefix length"},
      {"192.168.0.1%eth0/24", "unexpected character"},
      {"192.168.0.0/33", "out of range"},
      {"2001:db8::/129", "out of range"},
      {"fe80::1%en0/64", "zone value is not allowed"},
      {"::ffff:192.168.0.1/24", "IPv4-mapped IPv6 address"},
  };
  for (const auto& tc : bad_cidrs) {
    EXPECT_THAT(
        ParseCidr(tc.first),
        StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr(tc.second)))
        << "Input: " << tc.first;
  }
}

}  // namespace
}  // namespace cel::internal
