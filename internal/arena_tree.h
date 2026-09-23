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

// ArenaTree is a low level implementation of an RBTree tailored for use with
// google::protobuf::Arena.

#ifndef THIRD_PARTY_CEL_CPP_INTERNAL_ARENA_TREE_H_
#define THIRD_PARTY_CEL_CPP_INTERNAL_ARENA_TREE_H_

#include <cstdint>
#include <functional>
#include <type_traits>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/log/absl_check.h"
#include "google/protobuf/arena.h"

namespace cel::internal {

enum class ArenaTreeNodeColor : uintptr_t {
  kBlack = 0,
  kRed = 1,
};

struct ArenaTreeNodeBase;

[[nodiscard]]
const ArenaTreeNodeBase* absl_nullable ArenaTreeNext(
    const ArenaTreeNodeBase* absl_nullable node);

[[nodiscard]]
const ArenaTreeNodeBase* absl_nullable ArenaTreePrev(
    const ArenaTreeNodeBase* absl_nullable node);

[[nodiscard]]
const ArenaTreeNodeBase* absl_nullable ArenaTreeMin(
    const ArenaTreeNodeBase* absl_nullable node);

[[nodiscard]]
const ArenaTreeNodeBase* absl_nullable ArenaTreeMax(
    const ArenaTreeNodeBase* absl_nullable node);

template <typename T>
using IsDerivedFromArenaTreeNodeBase = std::conjunction<
    std::is_base_of<ArenaTreeNodeBase, T>,
    std::negation<std::is_same<ArenaTreeNodeBase, std::remove_cv_t<T>>>>;

template <typename T>
constexpr bool kIsDerivedFromArenaTreeNodeBase =
    IsDerivedFromArenaTreeNodeBase<T>::value;

template <typename T, typename U = void>
using EnableIfDerivedFromArenaTreeNodeBase =
    std::enable_if_t<kIsDerivedFromArenaTreeNodeBase<T>, U>;

template <typename T>
[[nodiscard]]
inline EnableIfDerivedFromArenaTreeNodeBase<T, T* absl_nullable> ArenaTreeNext(
    T* absl_nullable node) {
  return static_cast<T*>(const_cast<ArenaTreeNodeBase*>(
      (ArenaTreeNext)(static_cast<const ArenaTreeNodeBase*>(node))));
}

template <typename T>
[[nodiscard]]
inline EnableIfDerivedFromArenaTreeNodeBase<T, T* absl_nullable> ArenaTreePrev(
    T* absl_nullable node) {
  return static_cast<T*>(const_cast<ArenaTreeNodeBase*>(
      (ArenaTreePrev)(static_cast<const ArenaTreeNodeBase*>(node))));
}

template <typename T>
[[nodiscard]]
inline EnableIfDerivedFromArenaTreeNodeBase<T, T* absl_nullable> ArenaTreeMin(
    T* absl_nullable node) {
  return static_cast<T*>(const_cast<ArenaTreeNodeBase*>(
      (ArenaTreeMin)(static_cast<const ArenaTreeNodeBase*>(node))));
}

template <typename T>
[[nodiscard]]
inline EnableIfDerivedFromArenaTreeNodeBase<T, T* absl_nullable> ArenaTreeMax(
    T* absl_nullable node) {
  return static_cast<T*>(const_cast<ArenaTreeNodeBase*>(
      (ArenaTreeMax)(static_cast<const ArenaTreeNodeBase*>(node))));
}

[[nodiscard]]
ArenaTreeNodeBase* absl_nullability_unknown ArenaTreeNodeGetParent(
    const ArenaTreeNodeBase* absl_nonnull node);

void ArenaTreeNodeSetParent(ArenaTreeNodeBase* absl_nonnull node,
                            ArenaTreeNodeBase* absl_nullability_unknown parent);

[[nodiscard]]
ArenaTreeNodeColor ArenaTreeNodeGetColor(
    const ArenaTreeNodeBase* absl_nonnull node);

void ArenaTreeNodeSetColor(ArenaTreeNodeBase* absl_nonnull node,
                           ArenaTreeNodeColor color);

void ArenaTreeNodeSet(ArenaTreeNodeBase* absl_nonnull node,
                      ArenaTreeNodeBase* absl_nullable parent);

void ArenaTreeNodeClear(ArenaTreeNodeBase* absl_nonnull node);

[[nodiscard]]
ArenaTreeNodeBase* absl_nullability_unknown ArenaTreeNodeGetLeft(
    const ArenaTreeNodeBase* absl_nonnull node);

ArenaTreeNodeBase* absl_nullability_unknown ArenaTreeNodeSetLeft(
    ArenaTreeNodeBase* absl_nonnull node,
    ArenaTreeNodeBase* absl_nullability_unknown left);

[[nodiscard]]
ArenaTreeNodeBase* absl_nullability_unknown ArenaTreeNodeGetRight(
    const ArenaTreeNodeBase* absl_nonnull node);

ArenaTreeNodeBase* absl_nullability_unknown ArenaTreeNodeSetRight(
    ArenaTreeNodeBase* absl_nonnull node,
    ArenaTreeNodeBase* absl_nullability_unknown right);

struct ArenaTreeNodeBase {
 private:
  uintptr_t parent_and_color = 0;
  ArenaTreeNodeBase* absl_nullable left = nullptr;
  ArenaTreeNodeBase* absl_nullable right = nullptr;

  friend ArenaTreeNodeBase* absl_nullability_unknown ArenaTreeNodeGetParent(
      const ArenaTreeNodeBase* absl_nonnull node);
  friend void ArenaTreeNodeSetParent(
      ArenaTreeNodeBase* absl_nonnull node,
      ArenaTreeNodeBase* absl_nullability_unknown parent);
  friend ArenaTreeNodeColor ArenaTreeNodeGetColor(
      const ArenaTreeNodeBase* absl_nonnull node);
  friend void ArenaTreeNodeSetColor(ArenaTreeNodeBase* absl_nonnull node,
                                    ArenaTreeNodeColor color);
  friend void ArenaTreeNodeSet(ArenaTreeNodeBase* absl_nonnull node,
                               ArenaTreeNodeBase* absl_nullable parent);
  friend void ArenaTreeNodeClear(ArenaTreeNodeBase* absl_nonnull node);
  friend ArenaTreeNodeBase* absl_nullability_unknown ArenaTreeNodeGetLeft(
      const ArenaTreeNodeBase* absl_nonnull node);
  friend ArenaTreeNodeBase* absl_nullability_unknown ArenaTreeNodeSetLeft(
      ArenaTreeNodeBase* absl_nonnull node,
      ArenaTreeNodeBase* absl_nullability_unknown left);
  friend ArenaTreeNodeBase* absl_nullability_unknown ArenaTreeNodeGetRight(
      const ArenaTreeNodeBase* absl_nonnull node);
  friend ArenaTreeNodeBase* absl_nullability_unknown ArenaTreeNodeSetRight(
      ArenaTreeNodeBase* absl_nonnull node,
      ArenaTreeNodeBase* absl_nullability_unknown right);
};

[[nodiscard]]
inline ArenaTreeNodeBase* absl_nullability_unknown ArenaTreeNodeGetParent(
    const ArenaTreeNodeBase* absl_nonnull node) {
  return reinterpret_cast<ArenaTreeNodeBase*>(node->parent_and_color &
                                              ~uintptr_t{1});
}

inline void ArenaTreeNodeSetParent(
    ArenaTreeNodeBase* absl_nonnull node,
    ArenaTreeNodeBase* absl_nullability_unknown parent) {
  node->parent_and_color = static_cast<uintptr_t>(ArenaTreeNodeGetColor(node)) |
                           reinterpret_cast<uintptr_t>(parent);
}

[[nodiscard]]
inline ArenaTreeNodeColor ArenaTreeNodeGetColor(
    const ArenaTreeNodeBase* absl_nonnull node) {
  return static_cast<ArenaTreeNodeColor>(node->parent_and_color & uintptr_t{1});
}

inline void ArenaTreeNodeSetColor(ArenaTreeNodeBase* absl_nonnull node,
                                  ArenaTreeNodeColor color) {
  node->parent_and_color =
      reinterpret_cast<uintptr_t>(ArenaTreeNodeGetParent(node)) |
      static_cast<uintptr_t>(color);
}

inline void ArenaTreeNodeSet(ArenaTreeNodeBase* absl_nonnull node,
                             ArenaTreeNodeBase* absl_nullable parent) {
  node->parent_and_color = reinterpret_cast<uintptr_t>(parent) |
                           static_cast<uintptr_t>(ArenaTreeNodeColor::kRed);
  node->left = node->right = nullptr;
}

inline void ArenaTreeNodeClear(ArenaTreeNodeBase* absl_nonnull node) {
  node->parent_and_color = 0;
  node->left = node->right = nullptr;
}

[[nodiscard]]
inline ArenaTreeNodeBase* absl_nullability_unknown ArenaTreeNodeGetLeft(
    const ArenaTreeNodeBase* absl_nonnull node) {
  return node->left;
}

inline ArenaTreeNodeBase* absl_nullability_unknown ArenaTreeNodeSetLeft(
    ArenaTreeNodeBase* absl_nonnull node,
    ArenaTreeNodeBase* absl_nullability_unknown left) {
  return node->left = left;
}

[[nodiscard]]
inline ArenaTreeNodeBase* absl_nullability_unknown ArenaTreeNodeGetRight(
    const ArenaTreeNodeBase* absl_nonnull node) {
  return node->right;
}

inline ArenaTreeNodeBase* absl_nullability_unknown ArenaTreeNodeSetRight(
    ArenaTreeNodeBase* absl_nonnull node,
    ArenaTreeNodeBase* absl_nullability_unknown right) {
  return node->right = right;
}

void ArenaTreeRemove(ArenaTreeNodeBase* absl_nullable* absl_nonnull head,
                     ArenaTreeNodeBase* absl_nonnull elem);

template <typename T>
[[nodiscard]]
inline EnableIfDerivedFromArenaTreeNodeBase<T> ArenaTreeRemove(
    T* absl_nullable* absl_nonnull head, T* absl_nonnull elem) {
  ArenaTreeNodeBase* head_base = *head;
  (ArenaTreeRemove)(&head_base, static_cast<ArenaTreeNodeBase*>(elem));
  *head = static_cast<T*>(head_base);
}

void ArenaTreeInsertColor(ArenaTreeNodeBase* absl_nullable* absl_nonnull head,
                          ArenaTreeNodeBase* absl_nonnull elem);

template <typename T>
struct ArenaTreeNode;

template <typename T>
[[nodiscard]]
const T& ArenaTreeNodeGetValue(const ArenaTreeNode<T>* absl_nonnull node);

template <typename T>
struct ArenaTreeNode : ArenaTreeNodeBase {
  template <typename... Args>
  explicit ArenaTreeNode(Args&&... args)
      : ArenaTreeNodeBase(), value(std::forward<Args>(args)...) {}

 private:
  template <typename U>
  friend const U& ArenaTreeNodeGetValue(
      const ArenaTreeNode<U>* absl_nonnull node);

  T value;
};

template <typename T>
[[nodiscard]] inline const T& ArenaTreeNodeGetValue(
    const ArenaTreeNode<T>* absl_nonnull node) {
  return node->value;
}

template <typename T>
struct ArenaTreeNodeCrtp : ArenaTreeNodeBase {
  using ArenaTreeNodeBase::ArenaTreeNodeBase;
};

template <typename T>
[[nodiscard]] inline const T& ArenaTreeNodeGetValue(
    const ArenaTreeNodeCrtp<T>* absl_nonnull node) {
  return *static_cast<const T*>(node);
}

template <typename T, typename Compare>
[[nodiscard]]
T* absl_nonnull ArenaTreeInsert(T* absl_nullable* absl_nonnull head,
                                T* absl_nonnull elem, const Compare& compare) {
  T* tmp = *head;
  T* parent = nullptr;
  int diff = 0;
  while (tmp != nullptr) {
    parent = tmp;
    diff = std::invoke(compare, (ArenaTreeNodeGetValue)(elem),
                       (ArenaTreeNodeGetValue)(parent));
    if (diff < 0) {
      tmp = static_cast<T*>((ArenaTreeNodeGetLeft)(tmp));
    } else if (diff > 0) {
      tmp = static_cast<T*>((ArenaTreeNodeGetRight)(tmp));
    } else {
      return tmp;
    }
  }
  (ArenaTreeNodeSet)(elem, parent);
  if (parent != nullptr) {
    if (diff < 0) {
      (ArenaTreeNodeSetLeft)(parent, elem);
    } else {
      (ArenaTreeNodeSetRight)(parent, elem);
    }
  } else {
    *head = elem;
  }
  ArenaTreeNodeBase* head_base = *head;
  (ArenaTreeInsertColor)(&head_base, elem);
  *head = static_cast<T*>(head_base);
  return elem;
}

template <typename T>
struct ArenaTreeNodeConstructor {
  template <typename... Args>
  void operator()(Args&&... args) const {
    ABSL_DCHECK(*out == nullptr);
    *out = google::protobuf::Arena::Create<T>(arena, std::forward<Args>(args)...);
  }

  google::protobuf::Arena* const absl_nonnull arena;
  T** out;
};

template <typename T, typename K, typename Compare, typename Emplacer>
[[nodiscard]]
std::pair<T* absl_nonnull, bool> ArenaTreeLazyEmplace(
    google::protobuf::Arena* absl_nonnull arena, T* absl_nullable* absl_nonnull head,
    const K& key, const Compare& compare, Emplacer&& emplacer) {
  T* tmp = *head;
  T* parent = nullptr;
  int diff = 0;
  while (tmp != nullptr) {
    parent = tmp;
    diff = std::invoke(compare, key, (ArenaTreeNodeGetValue)(parent));
    if (diff < 0) {
      tmp = static_cast<T*>((ArenaTreeNodeGetLeft)(tmp));
    } else if (diff > 0) {
      tmp = static_cast<T*>((ArenaTreeNodeGetRight)(tmp));
    } else {
      return {tmp, false};
    }
  }
  T* elem = nullptr;
  ArenaTreeNodeConstructor<T> constructor{
      .arena = arena,
      .out = &elem,
  };
  std::invoke(std::forward<Emplacer>(emplacer),
              static_cast<const ArenaTreeNodeConstructor<T>&>(constructor));
  ABSL_DCHECK(elem != nullptr);
  (ArenaTreeNodeSet)(elem, parent);
  if (parent != nullptr) {
    if (diff < 0) {
      (ArenaTreeNodeSetLeft)(parent, elem);
    } else {
      (ArenaTreeNodeSetRight)(parent, elem);
    }
  } else {
    *head = elem;
  }
  ArenaTreeNodeBase* head_base = *head;
  (ArenaTreeInsertColor)(&head_base, elem);
  *head = static_cast<T*>(head_base);
  return {elem, true};
}

template <typename T, typename K, typename Compare>
[[nodiscard]]
const T* absl_nullable ArenaTreeFind(const T* absl_nullable head, const K& key,
                                     const Compare& compare) {
  const T* tmp = head;
  while (tmp != nullptr) {
    int diff = std::invoke(compare, key, (ArenaTreeNodeGetValue)(tmp));
    if (diff < 0) {
      tmp = static_cast<const T*>((ArenaTreeNodeGetLeft)(tmp));
    } else if (diff > 0) {
      tmp = static_cast<const T*>((ArenaTreeNodeGetRight)(tmp));
    } else {
      return tmp;
    }
  }
  return nullptr;
}

template <typename T, typename K, typename Compare>
[[nodiscard]]
T* absl_nullable ArenaTreeFind(T* absl_nullable head, const K& key,
                               const Compare& compare) {
  return (ArenaTreeFind)(static_cast<const T*>(head), key, compare);
}

}  // namespace cel::internal

#endif  // THIRD_PARTY_CEL_CPP_INTERNAL_ARENA_TREE_H_
