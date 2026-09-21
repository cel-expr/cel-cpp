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

#include "common/internal/byte_string.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/functional/overload.h"
#include "absl/hash/hash.h"
#include "absl/log/absl_check.h"
#include "absl/strings/cord.h"
#include "absl/strings/match.h"
#include "absl/strings/resize_and_overwrite.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "google/protobuf/arena.h"

namespace cel::common_internal {

namespace {

char* CopyCordToArray(const absl::Cord& cord, size_t offset, size_t size,
                      char* data) {
  for (auto chunk : cord.Chunks()) {
    if (size == 0) {
      break;
    }
    if (offset > 0) {
      size_t min_offset = std::min(chunk.size(), offset);
      offset -= min_offset;
      if (offset > 0) {
        continue;
      }
      chunk.remove_prefix(min_offset);
    }
    size_t min_size = std::min(size, chunk.size());
    std::memcpy(data, chunk.data(), min_size);
    data += min_size;
    size -= min_size;
  }
  return data;
}

char* CopyCordToArray(const absl::Cord& cord, char* data) {
  return (CopyCordToArray)(cord, 0, cord.size(), data);
}

void AppendCordToString(const absl::Cord& cord, size_t offset, size_t size,
                        std::string& data) {
  data.reserve(data.size() + size);
  for (auto chunk : cord.Chunks()) {
    if (size == 0) {
      break;
    }
    if (offset > 0) {
      size_t min_offset = std::min(chunk.size(), offset);
      offset -= min_offset;
      if (offset > 0) {
        continue;
      }
      chunk.remove_prefix(min_offset);
    }
    size_t min_size = std::min(size, chunk.size());
    data.append(absl::string_view(chunk.data(), min_size));
    size -= min_size;
  }
}

template <typename T>
T ConsumeAndDestroy(T& object) {
  T consumed = std::move(object);
  object.~T();  // NOLINT(bugprone-use-after-move)
  return consumed;
}

}  // namespace

ByteString ByteString::From(const char* absl_nullable value,
                            google::protobuf::Arena* absl_nonnull arena) {
  return From(absl::NullSafeStringView(value), arena);
}

ByteString ByteString::From(absl::string_view value,
                            google::protobuf::Arena* absl_nonnull arena) {
  ABSL_DCHECK(arena != nullptr);
  ByteString result(UninitializedTag{});
  if (value.size() <= kSmallByteStringCapacity) {
    result.SetSmall(arena, value);
  } else {
    char* arena_value =
        reinterpret_cast<char*>(arena->AllocateAligned(value.size()));
    std::memcpy(arena_value, value.data(), value.size());
    result.SetMedium(arena, absl::string_view(arena_value, value.size()));
  }
  return result;
}

ByteString ByteString::From(const absl::Cord& value,
                            google::protobuf::Arena* absl_nonnull arena) {
  ABSL_DCHECK(arena != nullptr);
  ByteString result(UninitializedTag{});
  if (value.size() <= kSmallByteStringCapacity) {
    result.SetSmall(arena, value);
  } else {
    result.SetLarge(arena, google::protobuf::Arena::Create<absl::Cord>(arena, value));
  }
  return result;
}

ByteString ByteString::From(std::string&& value,
                            google::protobuf::Arena* absl_nonnull arena) {
  ABSL_DCHECK(arena != nullptr);
  ByteString result(UninitializedTag{});
  if (value.size() <= kSmallByteStringCapacity) {
    result.SetSmall(arena, value);
  } else if (value.size() > sizeof(std::string)) {
    value.shrink_to_fit();
    result.SetMedium(
        arena, google::protobuf::Arena::Create<std::string>(arena, std::move(value)));
  } else {
    char* arena_value =
        reinterpret_cast<char*>(arena->AllocateAligned(value.size()));
    std::memcpy(arena_value, value.data(), value.size());
    result.SetMedium(arena, absl::string_view(arena_value, value.size()));
  }
  return result;
}

ByteString ByteString::Wrap(absl::string_view value,
                            google::protobuf::Arena* absl_nullable arena) {
  ByteString result(UninitializedTag{});
  result.SetMedium(arena, value);
  return result;
}

ByteString ByteString::Wrap(const absl::Cord* absl_nonnull value, size_t offset,
                            size_t size, google::protobuf::Arena* absl_nullable arena) {
  ByteString result(UninitializedTag{});
  result.SetLarge(arena, value, offset, size);
  return result;
}

ByteString ByteString::WrapUnsafe(absl::string_view value) {
  return Wrap(value, static_cast<google::protobuf::Arena*>(nullptr));
}

ByteString ByteString::WrapUnsafe(const absl::Cord* absl_nonnull value,
                                  size_t offset, size_t size) {
  return Wrap(value, offset, size, static_cast<google::protobuf::Arena*>(nullptr));
}

ByteString ByteString::Concat(const ByteString& lhs, const ByteString& rhs,
                              google::protobuf::Arena* absl_nonnull arena) {
  ABSL_DCHECK(arena != nullptr);

  if (lhs.empty()) {
    return rhs;
  }
  if (rhs.empty()) {
    return lhs;
  }

  if (lhs.GetKind() == ByteStringKind::kLarge ||
      rhs.GetKind() == ByteStringKind::kLarge) {
    // If either the left or right are absl::Cord, use absl::Cord.
    absl::Cord result;
    lhs.AppendToCord(&result);
    rhs.AppendToCord(&result);
    return From(result, arena);
  }

  const size_t lhs_size = lhs.size();
  const size_t rhs_size = rhs.size();
  const size_t result_size = lhs_size + rhs_size;
  ByteString result(UninitializedTag{});
  if (result_size <= kSmallByteStringCapacity) {
    // If the resulting string fits in inline storage, do it.
    result.rep_.header.kind = ByteStringKind::kSmall;
    result.rep_.small.size = result_size;
    result.rep_.small.arena = arena;
    lhs.CopyToArray(result.rep_.small.data);
    rhs.CopyToArray(result.rep_.small.data + lhs_size);
  } else {
    // Otherwise allocate on the arena.
    char* result_data =
        reinterpret_cast<char*>(arena->AllocateAligned(result_size));
    lhs.CopyToArray(result_data);
    rhs.CopyToArray(result_data + lhs_size);
    result.rep_.header.kind = ByteStringKind::kMedium;
    result.rep_.medium.data = result_data;
    result.rep_.medium.size = result_size;
    result.rep_.medium.arena = arena;
  }
  return result;
}

google::protobuf::Arena* absl_nullable ByteString::GetArena() const {
  switch (GetKind()) {
    case ByteStringKind::kSmall:
      return GetSmallArena();
    case ByteStringKind::kMedium:
      return GetMediumArena();
    case ByteStringKind::kLarge:
      return GetLargeArena();
  }
}

bool ByteString::empty() const {
  switch (GetKind()) {
    case ByteStringKind::kSmall:
      return rep_.small.size == 0;
    case ByteStringKind::kMedium:
      return rep_.medium.size == 0;
    case ByteStringKind::kLarge:
      return rep_.large.size == 0;
  }
}

size_t ByteString::size() const {
  switch (GetKind()) {
    case ByteStringKind::kSmall:
      return rep_.small.size;
    case ByteStringKind::kMedium:
      return rep_.medium.size;
    case ByteStringKind::kLarge:
      return rep_.large.size;
  }
}

absl::optional<absl::string_view> ByteString::TryFlat() const {
  switch (GetKind()) {
    case ByteStringKind::kSmall:
      return GetSmall();
    case ByteStringKind::kMedium:
      return GetMedium();
    case ByteStringKind::kLarge: {
      if (auto flat = rep_.large.data->TryFlat(); flat.has_value()) {
        return flat->substr(rep_.large.offset, rep_.large.offset);
      }
      return absl::nullopt;
    }
  }
}

bool ByteString::Equals(absl::string_view rhs) const {
  return Visit(absl::Overload(
      [&rhs](absl::string_view lhs) -> bool { return lhs == rhs; },
      [&rhs](const absl::Cord& lhs) -> bool { return lhs == rhs; }));
}

bool ByteString::Equals(const absl::Cord& rhs) const {
  return Visit(absl::Overload(
      [&rhs](absl::string_view lhs) -> bool { return lhs == rhs; },
      [&rhs](const absl::Cord& lhs) -> bool { return lhs == rhs; }));
}

int ByteString::Compare(absl::string_view rhs) const {
  return Visit(absl::Overload(
      [&rhs](absl::string_view lhs) -> int { return lhs.compare(rhs); },
      [&rhs](const absl::Cord& lhs) -> int { return lhs.Compare(rhs); }));
}

int ByteString::Compare(const absl::Cord& rhs) const {
  return Visit(absl::Overload(
      [&rhs](absl::string_view lhs) -> int { return -rhs.Compare(lhs); },
      [&rhs](const absl::Cord& lhs) -> int { return lhs.Compare(rhs); }));
}

bool ByteString::StartsWith(absl::string_view rhs) const {
  return Visit(absl::Overload(
      [&rhs](absl::string_view lhs) -> bool {
        return absl::StartsWith(lhs, rhs);
      },
      [&rhs](const absl::Cord& lhs) -> bool { return lhs.StartsWith(rhs); }));
}

bool ByteString::StartsWith(const absl::Cord& rhs) const {
  return Visit(absl::Overload(
      [&rhs](absl::string_view lhs) -> bool {
        return lhs.size() >= rhs.size() && lhs.substr(0, rhs.size()) == rhs;
      },
      [&rhs](const absl::Cord& lhs) -> bool { return lhs.StartsWith(rhs); }));
}

bool ByteString::EndsWith(absl::string_view rhs) const {
  return Visit(absl::Overload(
      [&rhs](absl::string_view lhs) -> bool {
        return absl::EndsWith(lhs, rhs);
      },
      [&rhs](const absl::Cord& lhs) -> bool { return lhs.EndsWith(rhs); }));
}

bool ByteString::EndsWith(const absl::Cord& rhs) const {
  return Visit(absl::Overload(
      [&rhs](absl::string_view lhs) -> bool {
        return lhs.size() >= rhs.size() &&
               lhs.substr(lhs.size() - rhs.size()) == rhs;
      },
      [&rhs](const absl::Cord& lhs) -> bool { return lhs.EndsWith(rhs); }));
}

absl::optional<size_t> ByteString::Find(absl::string_view needle,
                                        size_t pos) const {
  ABSL_DCHECK_LE(pos, size());

  return Visit(absl::Overload(
      [&needle, pos](absl::string_view lhs) -> absl::optional<size_t> {
        absl::string_view::size_type i = lhs.find(needle, pos);
        if (i == absl::string_view::npos) {
          return std::nullopt;
        }
        return i;
      },
      [&needle, pos](const absl::Cord& lhs) -> absl::optional<size_t> {
        absl::Cord cord = lhs.Subcord(pos, lhs.size() - pos);
        absl::Cord::CharIterator it = cord.Find(needle);
        if (it == cord.char_end()) {
          return std::nullopt;
        }
        return pos +
               static_cast<size_t>(absl::Cord::Distance(cord.char_begin(), it));
      }));
}

absl::optional<size_t> ByteString::Find(const absl::Cord& needle,
                                        size_t pos) const {
  ABSL_DCHECK_LE(pos, size());

  return Visit(absl::Overload(
      [&needle, pos](absl::string_view lhs) -> absl::optional<size_t> {
        if (auto flat_needle = needle.TryFlat(); flat_needle) {
          absl::string_view::size_type i = lhs.find(*flat_needle, pos);
          if (i == absl::string_view::npos) {
            return std::nullopt;
          }
          return i;
        }
        // Needle is fragmented, we have to do a linear scan.
        const size_t needle_size = needle.size();
        if (pos + needle_size > lhs.size()) {
          return std::nullopt;
        }
        if (ABSL_PREDICT_FALSE(needle_size == 0)) {
          return pos;
        }
        // Optimization: find the first chunk of the needle, then compare the
        // rest. If the first chunk is empty, `lhs.find` will return
        // `current_pos`, which correctly degrades to a linear scan.
        absl::string_view first_chunk = *needle.Chunks().begin();
        absl::Cord rest_of_needle = needle.Subcord(
            first_chunk.size(), needle_size - first_chunk.size());
        size_t current_pos = pos;
        while (true) {
          size_t found_pos = lhs.find(first_chunk, current_pos);
          if (found_pos == absl::string_view::npos ||
              found_pos > lhs.size() - needle_size) {
            return std::nullopt;
          }
          if (lhs.substr(found_pos + first_chunk.size(),
                         rest_of_needle.size()) == rest_of_needle) {
            return found_pos;
          }
          current_pos = found_pos + 1;
        }
      },
      [&needle, pos](const absl::Cord& lhs) -> absl::optional<size_t> {
        absl::Cord cord = lhs.Subcord(pos, lhs.size() - pos);
        absl::Cord::CharIterator it = cord.Find(needle);
        if (it == cord.char_end()) {
          return std::nullopt;
        }
        return pos +
               static_cast<size_t>(absl::Cord::Distance(cord.char_begin(), it));
      }));
}

ByteString ByteString::Substring(size_t pos, size_t npos) const {
  ABSL_DCHECK_LE(npos, size());
  ABSL_DCHECK_LE(pos, npos);

  switch (GetKind()) {
    case ByteStringKind::kSmall: {
      ByteString result(UninitializedTag{});
      result.SetSmall(GetSmallArena(), GetSmall().substr(pos, npos - pos));
      return result;
    }
    case ByteStringKind::kMedium: {
      ByteString result(UninitializedTag{});
      result.SetMedium(GetMediumArena(), GetMedium().substr(pos, npos - pos));
      return result;
    }
    case ByteStringKind::kLarge:
      ByteString result(UninitializedTag{});
      result.SetLarge(GetLargeArena(), rep_.large.data, rep_.large.offset + pos,
                      npos - pos);
      return result;
  }
}

void ByteString::RemovePrefix(size_t n) {
  ABSL_DCHECK_LE(n, size());
  if (n == 0) {
    return;
  }
  switch (GetKind()) {
    case ByteStringKind::kSmall:
      std::memmove(rep_.small.data, rep_.small.data + n, rep_.small.size - n);
      rep_.small.size = rep_.small.size - n;
      break;
    case ByteStringKind::kMedium:
      rep_.medium.data = rep_.medium.data + n;
      rep_.medium.size = rep_.medium.size - n;
      break;
    case ByteStringKind::kLarge:
      rep_.large.offset = rep_.large.offset + n;
      rep_.large.size = rep_.large.size - n;
      break;
  }
}

void ByteString::RemoveSuffix(size_t n) {
  ABSL_DCHECK_LE(n, size());
  if (n == 0) {
    return;
  }
  switch (GetKind()) {
    case ByteStringKind::kSmall:
      rep_.small.size = rep_.small.size - n;
      break;
    case ByteStringKind::kMedium:
      rep_.medium.size = rep_.medium.size - n;
      break;
    case ByteStringKind::kLarge:
      rep_.large.size = rep_.large.size - n;
      break;
  }
}

void ByteString::CopyToArray(char* absl_nonnull out) const {
  ABSL_DCHECK(out != nullptr);
  switch (GetKind()) {
    case ByteStringKind::kSmall: {
      absl::string_view small = GetSmall();
      std::memcpy(out, small.data(), small.size());
    } break;
    case ByteStringKind::kMedium: {
      absl::string_view medium = GetMedium();
      std::memcpy(out, medium.data(), medium.size());
    } break;
    case ByteStringKind::kLarge: {
      (CopyCordToArray)(*rep_.large.data, rep_.large.offset, rep_.large.size,
                        out);
    } break;
  }
}

std::string ByteString::ToString() const {
  switch (GetKind()) {
    case ByteStringKind::kSmall:
      return std::string(GetSmall());
    case ByteStringKind::kMedium:
      return std::string(GetMedium());
    case ByteStringKind::kLarge: {
      std::string result;
      absl::StringResizeAndOverwrite(
          result, rep_.large.size,
          [this](char* buffer, size_t buffer_size) -> size_t {
            (CopyCordToArray)(*rep_.large.data, rep_.large.offset,
                              rep_.large.size, buffer);
            return rep_.large.size;
          });
      return result;
    }
  }
}

void ByteString::CopyToString(std::string* absl_nonnull out) const {
  ABSL_DCHECK(out != nullptr);
  switch (GetKind()) {
    case ByteStringKind::kSmall:
      out->assign(GetSmall());
      break;
    case ByteStringKind::kMedium:
      out->assign(GetMedium());
      break;
    case ByteStringKind::kLarge:
      absl::StringResizeAndOverwrite(
          *out, rep_.large.size,
          [this](char* buffer, size_t buffer_size) -> size_t {
            (CopyCordToArray)(*rep_.large.data, rep_.large.offset,
                              rep_.large.size, buffer);
            return rep_.large.size;
          });
      break;
  }
}

void ByteString::AppendToString(std::string* absl_nonnull out) const {
  ABSL_DCHECK(out != nullptr);
  switch (GetKind()) {
    case ByteStringKind::kSmall:
      out->append(GetSmall());
      break;
    case ByteStringKind::kMedium:
      out->append(GetMedium());
      break;
    case ByteStringKind::kLarge:
      (AppendCordToString)(*rep_.large.data, rep_.large.offset, rep_.large.size,
                           *out);
      break;
  }
}

absl::Cord ByteString::ToCord() const& {
  switch (GetKind()) {
    case ByteStringKind::kSmall:
      return absl::Cord(GetSmall());
    case ByteStringKind::kMedium:
      return absl::Cord(GetMedium());
    case ByteStringKind::kLarge:
      return GetLarge();
  }
}

absl::Cord ByteString::ToCord() && {
  switch (GetKind()) {
    case ByteStringKind::kSmall:
      return absl::Cord(GetSmall());
    case ByteStringKind::kMedium:
      return absl::Cord(GetMedium());
    case ByteStringKind::kLarge:
      return GetLarge();
  }
}

void ByteString::CopyToCord(absl::Cord* absl_nonnull out) const {
  ABSL_DCHECK(out != nullptr);
  switch (GetKind()) {
    case ByteStringKind::kSmall:
      *out = absl::Cord(GetSmall());
      break;
    case ByteStringKind::kMedium:
      *out = absl::Cord(GetMedium());
      break;
    case ByteStringKind::kLarge:
      *out = GetLarge();
      break;
  }
}

void ByteString::AppendToCord(absl::Cord* absl_nonnull out) const {
  ABSL_DCHECK(out != nullptr);
  switch (GetKind()) {
    case ByteStringKind::kSmall:
      out->Append(GetSmall());
      break;
    case ByteStringKind::kMedium:
      out->Append(GetMedium());
      break;
    case ByteStringKind::kLarge:
      out->Append(GetLarge());
      break;
  }
}

absl::string_view ByteString::ToStringView(
    std::string* absl_nonnull scratch) const {
  ABSL_DCHECK(scratch != nullptr);
  switch (GetKind()) {
    case ByteStringKind::kSmall:
      return GetSmall();
    case ByteStringKind::kMedium:
      return GetMedium();
    case ByteStringKind::kLarge:
      if (auto flat = rep_.large.data->TryFlat(); flat.has_value()) {
        return flat->substr(rep_.large.offset, rep_.large.size);
      }
      absl::StringResizeAndOverwrite(
          *scratch, rep_.large.size,
          [this](char* buffer, size_t buffer_size) -> size_t {
            (CopyCordToArray)(*rep_.large.data, rep_.large.offset,
                              rep_.large.size, buffer);
            return rep_.large.size;
          });
      return absl::string_view(*scratch);
  }
}

absl::string_view ByteString::AsStringView() const {
  const ByteStringKind kind = GetKind();
  ABSL_CHECK(kind == ByteStringKind::kSmall ||  // Crash OK
             kind == ByteStringKind::kMedium);
  switch (kind) {
    case ByteStringKind::kSmall:
      return GetSmall();
    case ByteStringKind::kMedium:
      return GetMedium();
    case ByteStringKind::kLarge:
      ABSL_UNREACHABLE();
  }
}

ByteString ByteString::Clone(google::protobuf::Arena* absl_nonnull arena) const {
  ABSL_DCHECK(arena != nullptr);
  switch (GetKind()) {
    case ByteStringKind::kSmall: {
      ByteString result(UninitializedTag{});
      result.SetSmall(arena, GetSmall());
      return result;
    }
    case ByteStringKind::kMedium: {
      google::protobuf::Arena* absl_nullable other_arena = GetMediumArena();
      if (other_arena != arena) {
        return From(GetMedium(), arena);
      }
      return *this;
    }
    case ByteStringKind::kLarge: {
      google::protobuf::Arena* absl_nullable other_arena = GetLargeArena();
      if (other_arena != arena) {
        return From(GetLarge(), arena);
      }
      return *this;
    }
  }
}

void ByteString::HashValue(absl::HashState state) const {
  switch (GetKind()) {
    case ByteStringKind::kSmall:
      absl::HashState::combine(std::move(state), GetSmall());
      break;
    case ByteStringKind::kMedium:
      absl::HashState::combine(std::move(state), GetMedium());
      break;
    case ByteStringKind::kLarge:
      absl::HashState::combine(std::move(state), GetLarge());
      break;
  }
}

void ByteString::SetSmall(google::protobuf::Arena* absl_nullable arena,
                          absl::string_view string) {
  ABSL_DCHECK_LE(string.size(), kSmallByteStringCapacity);
  rep_.header.kind = ByteStringKind::kSmall;
  rep_.small.size = string.size();
  rep_.small.arena = arena;
  if (!string.empty()) {
    std::memcpy(rep_.small.data, string.data(), rep_.small.size);
  }
}

void ByteString::SetSmall(google::protobuf::Arena* absl_nullable arena,
                          const absl::Cord& cord) {
  ABSL_DCHECK_LE(cord.size(), kSmallByteStringCapacity);
  rep_.header.kind = ByteStringKind::kSmall;
  rep_.small.size = cord.size();
  rep_.small.arena = arena;
  (CopyCordToArray)(cord, rep_.small.data);
}

void ByteString::SetMedium(google::protobuf::Arena* absl_nullable arena,
                           absl::string_view string) {
  rep_.header.kind = ByteStringKind::kMedium;
  rep_.medium.size = string.size();
  rep_.medium.data = string.data();
  rep_.medium.arena = arena;
}

void ByteString::SetLarge(google::protobuf::Arena* absl_nullable arena,
                          const absl::Cord* absl_nonnull cord, size_t offset,
                          size_t size) {
  ABSL_DCHECK_LE(offset, cord->size());
  ABSL_DCHECK_LE(offset, kLargeByteStringMaxSize);
  rep_.header.kind = ByteStringKind::kLarge;
  rep_.large.offset = offset;
  if (size == static_cast<size_t>(-1)) {
    size = cord->size() - offset;
  }
  ABSL_DCHECK_LE(size, cord->size() - offset);
  ABSL_DCHECK_LE(size, kLargeByteStringMaxSize);
  rep_.large.size = size;
  rep_.large.data = cord;
  rep_.large.arena = arena;
}

absl::string_view LegacyByteString(const ByteString& string, bool stable,
                                   google::protobuf::Arena* absl_nonnull arena) {
  ABSL_DCHECK(arena != nullptr);
  if (string.empty()) {
    return absl::string_view();
  }
  const ByteStringKind kind = string.GetKind();
  if (kind == ByteStringKind::kMedium && string.GetMediumArena() == arena) {
    google::protobuf::Arena* absl_nullable other_arena = string.GetMediumArena();
    if (other_arena == arena || other_arena == nullptr) {
      // Legacy values do not preserve arena. For speed, we assume the arena is
      // compatible.
      return string.GetMedium();
    }
  }
  if (stable && kind == ByteStringKind::kSmall) {
    return string.GetSmall();
  }
  std::string* absl_nonnull result = google::protobuf::Arena::Create<std::string>(arena);
  switch (kind) {
    case ByteStringKind::kSmall:
      result->assign(string.GetSmall());
      break;
    case ByteStringKind::kMedium:
      result->assign(string.GetMedium());
      break;
    case ByteStringKind::kLarge:
      absl::CopyCordToString(string.GetLarge(), result);
      break;
  }
  return absl::string_view(*result);
}

}  // namespace cel::common_internal
