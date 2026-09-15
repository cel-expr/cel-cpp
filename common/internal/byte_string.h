// Copyright 2024 Google LLC
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

#ifndef THIRD_PARTY_CEL_CPP_COMMON_INTERNAL_BYTE_STRING_H_
#define THIRD_PARTY_CEL_CPP_COMMON_INTERNAL_BYTE_STRING_H_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ostream>
#include <string>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/functional/overload.h"
#include "absl/hash/hash.h"
#include "absl/log/absl_check.h"
#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "google/protobuf/arena.h"

namespace cel {

class BytesValueInputStream;
class BytesValueOutputStream;
class StringValue;

namespace common_internal {

class [[nodiscard]] ByteString;

struct ByteStringTestFriend;

enum class ByteStringKind : unsigned int {
  kSmall = 0,
  kMedium,
  kLarge,
};

inline std::ostream& operator<<(std::ostream& out, ByteStringKind kind) {
  switch (kind) {
    case ByteStringKind::kSmall:
      return out << "SMALL";
    case ByteStringKind::kMedium:
      return out << "MEDIUM";
    case ByteStringKind::kLarge:
      return out << "LARGE";
  }
}

// Representation of small strings in ByteString, which are stored in place.
struct SmallByteStringRep final {
#ifdef _MSC_VER
#pragma pack(push, 1)
#endif
  struct ABSL_ATTRIBUTE_PACKED {
    std::uint8_t kind : 2;
    std::uint8_t size : 6;
  };
#ifdef _MSC_VER
#pragma pack(pop)
#endif
  char data[23 - sizeof(google::protobuf::Arena*)];
  google::protobuf::Arena* absl_nullable arena;
};

inline constexpr size_t kSmallByteStringCapacity =
    sizeof(SmallByteStringRep::data);

inline constexpr size_t kMediumByteStringSizeBits = sizeof(size_t) * 8 - 2;
inline constexpr size_t kMediumByteStringMaxSize =
    (size_t{1} << kMediumByteStringSizeBits) - 1;

inline constexpr size_t kLargeByteStringMaxSize =
    (size_t{1} << kMediumByteStringSizeBits) - 1;

inline constexpr size_t kByteStringViewSizeBits = sizeof(size_t) * 8 - 1;
inline constexpr size_t kByteStringViewMaxSize =
    (size_t{1} << kByteStringViewSizeBits) - 1;

// Representation of medium strings in ByteString. These are either owned by an
// arena or managed by a reference count. This is encoded in `owner` following
// the same semantics as `cel::Owner`.
struct MediumByteStringRep final {
#ifdef _MSC_VER
#pragma pack(push, 1)
#endif
  struct ABSL_ATTRIBUTE_PACKED {
    size_t kind : 2;
    size_t size : kMediumByteStringSizeBits;
  };
#ifdef _MSC_VER
#pragma pack(pop)
#endif
  const char* absl_nullability_unknown data;
  google::protobuf::Arena* absl_nullable arena;
};

// Representation of large strings in ByteString. These are stored as
// a pointer to `absl::Cord`.
struct LargeByteStringRep final {
#ifdef _MSC_VER
#pragma pack(push, 1)
#endif
  struct ABSL_ATTRIBUTE_PACKED {
    size_t kind : 2;
    size_t offset : kMediumByteStringSizeBits / 2;
    size_t size : kMediumByteStringSizeBits / 2;
  };
#ifdef _MSC_VER
#pragma pack(pop)
#endif
  const absl::Cord* absl_nullability_unknown data;
  google::protobuf::Arena* absl_nullable arena;
};

// Representation of ByteString.
union ByteStringRep final {
#ifdef _MSC_VER
#pragma pack(push, 1)
#endif
  struct ABSL_ATTRIBUTE_PACKED {
    ByteStringKind kind : 2;
  } header;
#ifdef _MSC_VER
#pragma pack(pop)
#endif
  SmallByteStringRep small;
  MediumByteStringRep medium;
  LargeByteStringRep large;
};

// Returns a `absl::string_view` from `ByteString`, using `arena` to make memory
// allocations if necessary. `stable` indicates whether `cel::Value` is in a
// location where it will not be moved, so that inline string/bytes storage can
// be referenced.
absl::string_view LegacyByteString(const ByteString& string, bool stable,
                                   google::protobuf::Arena* absl_nonnull arena);

// `ByteString` is a vocabulary type capable of representing copy-on-write
// strings efficiently for arenas and reference counting. The contents of the
// byte string are owned by an arena or managed by a reference count. All byte
// strings have an associated allocator specified at construction, once the byte
// string is constructed the allocator will not and cannot change. Copying and
// moving between different allocators is supported and dealt with
// transparently by copying.
class [[nodiscard]] ByteString final {
 public:
  static ByteString From(const char* absl_nullable value,
                         google::protobuf::Arena* absl_nonnull arena
                             ABSL_ATTRIBUTE_LIFETIME_BOUND);
  static ByteString From(absl::string_view value,
                         google::protobuf::Arena* absl_nonnull arena
                             ABSL_ATTRIBUTE_LIFETIME_BOUND);
  static ByteString From(const absl::Cord& value,
                         google::protobuf::Arena* absl_nonnull arena
                             ABSL_ATTRIBUTE_LIFETIME_BOUND);
  static ByteString From(std::string&& value,
                         google::protobuf::Arena* absl_nonnull arena
                             ABSL_ATTRIBUTE_LIFETIME_BOUND);

  static ByteString Wrap(absl::string_view value,
                         google::protobuf::Arena* absl_nullable arena
                             ABSL_ATTRIBUTE_LIFETIME_BOUND);
  static ByteString Wrap(
      const absl::Cord* absl_nonnull value ABSL_ATTRIBUTE_LIFETIME_BOUND,
      google::protobuf::Arena* absl_nullable arena ABSL_ATTRIBUTE_LIFETIME_BOUND) {
    return Wrap(value, 0, value->size(), arena);
  }
  static ByteString Wrap(
      const absl::Cord* absl_nonnull value ABSL_ATTRIBUTE_LIFETIME_BOUND,
      size_t offset, size_t size,
      google::protobuf::Arena* absl_nullable arena ABSL_ATTRIBUTE_LIFETIME_BOUND);
  static ByteString Wrap(std::nullptr_t, google::protobuf::Arena*) = delete;
  static ByteString Wrap(std::nullptr_t, size_t, size_t,
                         google::protobuf::Arena*) = delete;
  static ByteString Wrap(std::string&& value, google::protobuf::Arena*) = delete;

  static ByteString WrapUnsafe(absl::string_view value);
  static ByteString WrapUnsafe(const absl::Cord* absl_nonnull value) {
    return WrapUnsafe(value, 0, value->size());
  }
  static ByteString WrapUnsafe(const absl::Cord* absl_nonnull value,
                               size_t offset, size_t size);
  static ByteString WrapUnsafe(std::nullptr_t) = delete;
  static ByteString WrapUnsafe(std::nullptr_t, size_t, size_t) = delete;

  static ByteString Concat(const ByteString& lhs, const ByteString& rhs,
                           google::protobuf::Arena* absl_nonnull arena);

  ByteString() noexcept { SetSmallEmpty(nullptr); }

  ByteString(const ByteString&) = default;
  ByteString(ByteString&&) = default;
  ByteString& operator=(const ByteString&) = default;
  ByteString& operator=(ByteString&&) = default;

  bool empty() const;

  size_t size() const;

  size_t max_size() const { return kByteStringViewMaxSize; }

  absl::optional<absl::string_view> TryFlat() const
      ABSL_ATTRIBUTE_LIFETIME_BOUND;

  bool Equals(absl::string_view rhs) const;
  bool Equals(const absl::Cord& rhs) const;
  bool Equals(const ByteString& rhs) const;

  int Compare(absl::string_view rhs) const;
  int Compare(const absl::Cord& rhs) const;
  int Compare(const ByteString& rhs) const;

  bool StartsWith(absl::string_view rhs) const;
  bool StartsWith(const absl::Cord& rhs) const;
  bool StartsWith(const ByteString& rhs) const;

  bool EndsWith(absl::string_view rhs) const;
  bool EndsWith(const absl::Cord& rhs) const;
  bool EndsWith(const ByteString& rhs) const;

  // Finds the first occurrence of `needle` in this object, starting at byte
  // position `pos`. Returns `absl::nullopt` if `needle` is not found.
  // Note: Positions are byte-based, not code point based as in
  // `cel::StringValue`.
  absl::optional<size_t> Find(absl::string_view needle, size_t pos = 0) const;
  absl::optional<size_t> Find(const absl::Cord& needle, size_t pos = 0) const;
  absl::optional<size_t> Find(const ByteString& needle, size_t pos = 0) const;

  // Returns a new `ByteString` that is a substring of this object, starting at
  // byte position `pos` and with a length of `npos` bytes.
  // Note: Positions are byte-based, not code point based as in
  // `cel::StringValue`.
  ByteString Substring(size_t pos, size_t npos) const;
  ByteString Substring(size_t pos) const {
    ABSL_DCHECK_LE(pos, size());
    return Substring(pos, size());
  }

  void RemovePrefix(size_t n);

  void RemoveSuffix(size_t n);

  std::string ToString() const;

  void CopyToString(std::string* absl_nonnull out) const;

  void AppendToString(std::string* absl_nonnull out) const;

  absl::Cord ToCord() const&;

  absl::Cord ToCord() &&;

  void CopyToCord(absl::Cord* absl_nonnull out) const;

  void AppendToCord(absl::Cord* absl_nonnull out) const;

  absl::string_view ToStringView(
      std::string* absl_nonnull scratch
          ABSL_ATTRIBUTE_LIFETIME_BOUND) const ABSL_ATTRIBUTE_LIFETIME_BOUND;

  absl::string_view AsStringView() const ABSL_ATTRIBUTE_LIFETIME_BOUND;

  google::protobuf::Arena* absl_nullable GetArena() const;

  ByteString Clone(google::protobuf::Arena* absl_nonnull arena) const;

  void HashValue(absl::HashState state) const;

  template <typename Visitor>
  decltype(auto) Visit(Visitor&& visitor) const {
    switch (GetKind()) {
      case ByteStringKind::kSmall:
        return std::forward<Visitor>(visitor)(GetSmall());
      case ByteStringKind::kMedium:
        return std::forward<Visitor>(visitor)(GetMedium());
      case ByteStringKind::kLarge:
        return std::forward<Visitor>(visitor)(GetLarge());
    }
  }

  friend void swap(ByteString& lhs, ByteString& rhs) noexcept {
    using std::swap;
    swap(lhs.rep_, rhs.rep_);
  }

  template <typename H>
  friend H AbslHashValue(H state, const ByteString& byte_string) {
    byte_string.HashValue(absl::HashState::Create(&state));
    return state;
  }

 private:
  friend class ByteStringView;
  friend struct ByteStringTestFriend;
  friend class cel::BytesValueInputStream;
  friend class cel::BytesValueOutputStream;
  friend class cel::StringValue;
  friend absl::string_view LegacyByteString(const ByteString& string,
                                            bool stable,
                                            google::protobuf::Arena* absl_nonnull arena);

  struct UninitializedTag {
    explicit UninitializedTag() = default;
  };

  explicit ByteString(UninitializedTag) {}

  constexpr ByteStringKind GetKind() const { return rep_.header.kind; }

  absl::string_view GetSmall() const {
    ABSL_DCHECK_EQ(GetKind(), ByteStringKind::kSmall);
    return GetSmall(rep_.small);
  }

  static absl::string_view GetSmall(const SmallByteStringRep& rep) {
    return absl::string_view(rep.data, rep.size);
  }

  absl::string_view GetMedium() const {
    ABSL_DCHECK_EQ(GetKind(), ByteStringKind::kMedium);
    return GetMedium(rep_.medium);
  }

  static absl::string_view GetMedium(const MediumByteStringRep& rep) {
    return absl::string_view(rep.data, rep.size);
  }

  google::protobuf::Arena* absl_nullable GetSmallArena() const {
    ABSL_DCHECK_EQ(GetKind(), ByteStringKind::kSmall);
    return GetSmallArena(rep_.small);
  }

  static google::protobuf::Arena* absl_nullable GetSmallArena(
      const SmallByteStringRep& rep) {
    return rep.arena;
  }

  google::protobuf::Arena* absl_nullable GetMediumArena() const {
    ABSL_DCHECK_EQ(GetKind(), ByteStringKind::kMedium);
    return GetMediumArena(rep_.medium);
  }

  static google::protobuf::Arena* absl_nullable GetMediumArena(
      const MediumByteStringRep& rep) {
    return rep.arena;
  }

  static absl::Cord GetLarge(
      const LargeByteStringRep& rep ABSL_ATTRIBUTE_LIFETIME_BOUND) {
    return rep.data->Subcord(rep.offset, rep.size);
  }

  absl::Cord GetLarge() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    ABSL_DCHECK_EQ(GetKind(), ByteStringKind::kLarge);
    return GetLarge(rep_.large);
  }

  google::protobuf::Arena* absl_nullable GetLargeArena() const {
    ABSL_DCHECK_EQ(GetKind(), ByteStringKind::kLarge);
    return GetLargeArena(rep_.large);
  }

  static google::protobuf::Arena* absl_nullable GetLargeArena(
      const LargeByteStringRep& rep ABSL_ATTRIBUTE_LIFETIME_BOUND) {
    return rep.arena;
  }

  void SetSmallEmpty(google::protobuf::Arena* absl_nullable arena) {
    rep_.header.kind = ByteStringKind::kSmall;
    rep_.small.size = 0;
    rep_.small.arena = arena;
  }

  void SetSmall(google::protobuf::Arena* absl_nullable arena, absl::string_view string);

  void SetSmall(google::protobuf::Arena* absl_nullable arena, const absl::Cord& cord);

  void SetMedium(google::protobuf::Arena* absl_nullable arena, absl::string_view string);

  void SetMedium(google::protobuf::Arena* absl_nullable arena,
                 const std::string* absl_nonnull string) {
    SetMedium(arena, absl::string_view(*string));
  }

  void SetLarge(google::protobuf::Arena* absl_nullable arena,
                const absl::Cord* absl_nonnull cord, size_t offset = 0,
                size_t size = static_cast<size_t>(-1));

  void CopyToArray(char* absl_nonnull out) const;

  ByteStringRep rep_;
};

inline bool ByteString::Equals(const ByteString& rhs) const {
  return rhs.Visit(absl::Overload(
      [this](absl::string_view rhs) -> bool { return Equals(rhs); },
      [this](const absl::Cord& rhs) -> bool { return Equals(rhs); }));
}

inline int ByteString::Compare(const ByteString& rhs) const {
  return rhs.Visit(absl::Overload(
      [this](absl::string_view rhs) -> int { return Compare(rhs); },
      [this](const absl::Cord& rhs) -> int { return Compare(rhs); }));
}

inline bool ByteString::StartsWith(const ByteString& rhs) const {
  return rhs.Visit(absl::Overload(
      [this](absl::string_view rhs) -> bool { return StartsWith(rhs); },
      [this](const absl::Cord& rhs) -> bool { return StartsWith(rhs); }));
}

inline bool ByteString::EndsWith(const ByteString& rhs) const {
  return rhs.Visit(absl::Overload(
      [this](absl::string_view rhs) -> bool { return EndsWith(rhs); },
      [this](const absl::Cord& rhs) -> bool { return EndsWith(rhs); }));
}

inline absl::optional<size_t> ByteString::Find(const ByteString& needle,
                                               size_t pos) const {
  return needle.Visit(absl::Overload(
      [this, pos](absl::string_view rhs) -> absl::optional<size_t> {
        return Find(rhs, pos);
      },
      [this, pos](const absl::Cord& rhs) -> absl::optional<size_t> {
        return Find(rhs, pos);
      }));
}

inline bool operator==(const ByteString& lhs, const ByteString& rhs) {
  return lhs.Equals(rhs);
}

inline bool operator==(const ByteString& lhs, absl::string_view rhs) {
  return lhs.Equals(rhs);
}

inline bool operator==(absl::string_view lhs, const ByteString& rhs) {
  return rhs.Equals(lhs);
}

inline bool operator==(const ByteString& lhs, const absl::Cord& rhs) {
  return lhs.Equals(rhs);
}

inline bool operator==(const absl::Cord& lhs, const ByteString& rhs) {
  return rhs.Equals(lhs);
}

inline bool operator!=(const ByteString& lhs, const ByteString& rhs) {
  return !operator==(lhs, rhs);
}

inline bool operator!=(const ByteString& lhs, absl::string_view rhs) {
  return !operator==(lhs, rhs);
}

inline bool operator!=(absl::string_view lhs, const ByteString& rhs) {
  return !operator==(lhs, rhs);
}

inline bool operator!=(const ByteString& lhs, const absl::Cord& rhs) {
  return !operator==(lhs, rhs);
}

inline bool operator!=(const absl::Cord& lhs, const ByteString& rhs) {
  return !operator==(lhs, rhs);
}

inline bool operator<(const ByteString& lhs, const ByteString& rhs) {
  return lhs.Compare(rhs) < 0;
}

inline bool operator<(const ByteString& lhs, absl::string_view rhs) {
  return lhs.Compare(rhs) < 0;
}

inline bool operator<(absl::string_view lhs, const ByteString& rhs) {
  return -rhs.Compare(lhs) < 0;
}

inline bool operator<(const ByteString& lhs, const absl::Cord& rhs) {
  return lhs.Compare(rhs) < 0;
}

inline bool operator<(const absl::Cord& lhs, const ByteString& rhs) {
  return -rhs.Compare(lhs) < 0;
}

inline bool operator<=(const ByteString& lhs, const ByteString& rhs) {
  return lhs.Compare(rhs) <= 0;
}

inline bool operator<=(const ByteString& lhs, absl::string_view rhs) {
  return lhs.Compare(rhs) <= 0;
}

inline bool operator<=(absl::string_view lhs, const ByteString& rhs) {
  return -rhs.Compare(lhs) <= 0;
}

inline bool operator<=(const ByteString& lhs, const absl::Cord& rhs) {
  return lhs.Compare(rhs) <= 0;
}

inline bool operator<=(const absl::Cord& lhs, const ByteString& rhs) {
  return -rhs.Compare(lhs) <= 0;
}

inline bool operator>(const ByteString& lhs, const ByteString& rhs) {
  return lhs.Compare(rhs) > 0;
}

inline bool operator>(const ByteString& lhs, absl::string_view rhs) {
  return lhs.Compare(rhs) > 0;
}

inline bool operator>(absl::string_view lhs, const ByteString& rhs) {
  return -rhs.Compare(lhs) > 0;
}

inline bool operator>(const ByteString& lhs, const absl::Cord& rhs) {
  return lhs.Compare(rhs) > 0;
}

inline bool operator>(const absl::Cord& lhs, const ByteString& rhs) {
  return -rhs.Compare(lhs) > 0;
}

inline bool operator>=(const ByteString& lhs, const ByteString& rhs) {
  return lhs.Compare(rhs) >= 0;
}

inline bool operator>=(const ByteString& lhs, absl::string_view rhs) {
  return lhs.Compare(rhs) >= 0;
}

inline bool operator>=(absl::string_view lhs, const ByteString& rhs) {
  return -rhs.Compare(lhs) >= 0;
}

inline bool operator>=(const ByteString& lhs, const absl::Cord& rhs) {
  return lhs.Compare(rhs) >= 0;
}

inline bool operator>=(const absl::Cord& lhs, const ByteString& rhs) {
  return -rhs.Compare(lhs) >= 0;
}

}  // namespace common_internal

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMMON_INTERNAL_BYTE_STRING_H_
