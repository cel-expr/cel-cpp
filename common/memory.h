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

#ifndef THIRD_PARTY_CEL_CPP_COMMON_MEMORY_H_
#define THIRD_PARTY_CEL_CPP_COMMON_MEMORY_H_

#include <cstddef>
#include <memory>
#include <ostream>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/log/absl_check.h"
#include "absl/numeric/bits.h"
#include "common/allocator.h"
#include "internal/to_address.h"  // IWYU pragma: keep
#include "google/protobuf/arena.h"

namespace cel {

// Obtain the address of the underlying element from a raw pointer or "fancy"
// pointer.
using internal::to_address;

// MemoryManagement is an enumeration of supported memory management forms
// underlying `cel::MemoryManager`.
enum class MemoryManagement {
  // Region-based (a.k.a. arena). Memory is allocated in fixed size blocks and
  // deallocated all at once upon destruction of the `cel::MemoryManager`.
  kPooling = 1,
  // Reference counting. Memory is allocated with an associated reference
  // counter. When the reference counter hits 0, it is deallocated.
  kReferenceCounting,
};

std::ostream& operator<<(std::ostream& out, MemoryManagement memory_management);

class MemoryManager;

// `ReferenceCountingMemoryManager` is a `MemoryManager` which employs automatic
// memory management through reference counting.
class ABSL_DEPRECATED("Do not use") ReferenceCountingMemoryManager final {
 public:
  ReferenceCountingMemoryManager(const ReferenceCountingMemoryManager&) =
      delete;
  ReferenceCountingMemoryManager(ReferenceCountingMemoryManager&&) = delete;
  ReferenceCountingMemoryManager& operator=(
      const ReferenceCountingMemoryManager&) = delete;
  ReferenceCountingMemoryManager& operator=(ReferenceCountingMemoryManager&&) =
      delete;

 private:
  static void* Allocate(size_t size, size_t alignment);

  static bool Deallocate(void* ptr, size_t size, size_t alignment) noexcept;

  explicit ReferenceCountingMemoryManager() = default;

  friend class MemoryManager;
};

// `PoolingMemoryManager` is a `MemoryManager` which employs automatic
// memory management through memory pooling.
class ABSL_DEPRECATED("Do not use") PoolingMemoryManager final {
 public:
  PoolingMemoryManager(const PoolingMemoryManager&) = delete;
  PoolingMemoryManager(PoolingMemoryManager&&) = delete;
  PoolingMemoryManager& operator=(const PoolingMemoryManager&) = delete;
  PoolingMemoryManager& operator=(PoolingMemoryManager&&) = delete;

 private:
  // Allocates memory directly from the allocator used by this memory manager.
  // If `memory_management()` returns `MemoryManagement::kReferenceCounting`,
  // this allocation *must* be explicitly deallocated at some point via
  // `Deallocate`. Otherwise deallocation is optional.
  ABSL_MUST_USE_RESULT static void* Allocate(google::protobuf::Arena* absl_nonnull arena,
                                             size_t size, size_t alignment) {
    ABSL_DCHECK(absl::has_single_bit(alignment))
        << "alignment must be a power of 2";
    if (size == 0) {
      return nullptr;
    }
    return arena->AllocateAligned(size, alignment);
  }

  // Attempts to deallocate memory previously allocated via `Allocate`, `size`
  // and `alignment` must match the values from the previous call to `Allocate`.
  // Returns `true` if the deallocation was successful and additional calls to
  // `Allocate` may re-use the memory, `false` otherwise. Returns `false` if
  // given `nullptr`.
  static bool Deallocate(google::protobuf::Arena* absl_nonnull, void*, size_t,
                         size_t alignment) noexcept {
    ABSL_DCHECK(absl::has_single_bit(alignment))
        << "alignment must be a power of 2";
    return false;
  }

  // Registers a custom destructor to be run upon destruction of the memory
  // management implementation. Return value is always `true`, indicating that
  // the destructor may be called at some point in the future.
  static bool OwnCustomDestructor(google::protobuf::Arena* absl_nonnull arena,
                                  void* object,
                                  void (*absl_nonnull destruct)(void*)) {
    ABSL_DCHECK(destruct != nullptr);
    arena->OwnCustomDestructor(object, destruct);
    return true;
  }

  template <typename T>
  static void DefaultDestructor(void* ptr) {
    static_assert(!std::is_trivially_destructible_v<T>);
    static_cast<T*>(ptr)->~T();
  }

  explicit PoolingMemoryManager() = default;

  friend class MemoryManager;
};

// `MemoryManager` is an abstraction for supporting automatic memory management.
// All objects created by the `MemoryManager` have a lifetime governed by the
// underlying memory management strategy. Currently `MemoryManager` is a
// composed type that holds either a reference to
// `ReferenceCountingMemoryManager` or owns a `PoolingMemoryManager`.
//
// ============================ Reference Counting ============================
// `Unique`: The object is valid until destruction of the `Unique`.
//
// `Shared`: The object is valid so long as one or more `Shared` managing the
// object exist.
//
// ================================= Pooling ==================================
// `Unique`: The object is valid until destruction of the underlying memory
// resources or of the `Unique`.
//
// `Shared`: The object is valid until destruction of the underlying memory
// resources.
class MemoryManager final {
 public:
  // Returns a `MemoryManager` which utilizes an arena.
  ABSL_MUST_USE_RESULT static MemoryManager Pooling(
      google::protobuf::Arena* absl_nonnull arena) {
    return MemoryManager(arena);
  }

  explicit MemoryManager(Allocator<> allocator) : arena_(allocator.arena()) {}

  MemoryManager() = delete;
  MemoryManager(const MemoryManager&) = default;
  MemoryManager& operator=(const MemoryManager&) = default;

  MemoryManagement memory_management() const noexcept {
    return arena_ == nullptr ? MemoryManagement::kReferenceCounting
                             : MemoryManagement::kPooling;
  }

  // Allocates memory directly from the allocator used by this memory manager.
  // If `memory_management()` returns `MemoryManagement::kReferenceCounting`,
  // this allocation *must* be explicitly deallocated at some point via
  // `Deallocate`. Otherwise deallocation is optional.
  ABSL_MUST_USE_RESULT void* Allocate(size_t size, size_t alignment) {
    if (arena_ == nullptr) {
      return ReferenceCountingMemoryManager::Allocate(size, alignment);
    } else {
      return PoolingMemoryManager::Allocate(arena_, size, alignment);
    }
  }

  // Attempts to deallocate memory previously allocated via `Allocate`, `size`
  // and `alignment` must match the values from the previous call to `Allocate`.
  // Returns `true` if the deallocation was successful and additional calls to
  // `Allocate` may re-use the memory, `false` otherwise. Returns `false` if
  // given `nullptr`.
  bool Deallocate(void* ptr, size_t size, size_t alignment) noexcept {
    if (arena_ == nullptr) {
      return ReferenceCountingMemoryManager::Deallocate(ptr, size, alignment);
    } else {
      return PoolingMemoryManager::Deallocate(arena_, ptr, size, alignment);
    }
  }

  // Registers a custom destructor to be run upon destruction of the memory
  // management implementation. A return of `true` indicates the destructor may
  // be called at some point in the future, `false` if will definitely not be
  // called. All pooling memory managers return `true` while the reference
  // counting memory manager returns `false`.
  bool OwnCustomDestructor(void* object, void (*absl_nonnull destruct)(void*)) {
    ABSL_DCHECK(destruct != nullptr);
    if (arena_ == nullptr) {
      return false;
    } else {
      return PoolingMemoryManager::OwnCustomDestructor(arena_, object,
                                                       destruct);
    }
  }

  google::protobuf::Arena* absl_nullable arena() const noexcept { return arena_; }

  template <typename T>
  // NOLINTNEXTLINE(google-explicit-constructor)
  operator Allocator<T>() const {
    return arena();
  }

  friend void swap(MemoryManager& lhs, MemoryManager& rhs) noexcept {
    using std::swap;
    swap(lhs.arena_, rhs.arena_);
  }

 private:
  friend class PoolingMemoryManager;

  explicit MemoryManager(std::nullptr_t) : arena_(nullptr) {}

  explicit MemoryManager(google::protobuf::Arena* absl_nonnull arena) : arena_(arena) {}

  // If `nullptr`, we are using reference counting. Otherwise we are using
  // Pooling. We use `UnreachablePooling()` as a sentinel to detect use after
  // move otherwise the moved-from `MemoryManager` would be in a valid state and
  // utilize reference counting.
  google::protobuf::Arena* absl_nullable arena_;
};

using MemoryManagerRef = MemoryManager;

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMMON_MEMORY_H_
