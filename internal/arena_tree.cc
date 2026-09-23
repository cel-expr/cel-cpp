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

#include "internal/arena_tree.h"

#include "absl/base/nullability.h"

namespace cel::internal {

namespace {

void ArenaTreeRotateLeft(ArenaTreeNodeBase** head, ArenaTreeNodeBase* elem) {
  ArenaTreeNodeBase* tmp = ArenaTreeNodeGetRight(elem);
  if (ArenaTreeNodeSetRight(elem, ArenaTreeNodeGetLeft(tmp)) != nullptr) {
    ArenaTreeNodeSetParent(ArenaTreeNodeGetLeft(tmp), elem);
  }
  ArenaTreeNodeBase* parent = ArenaTreeNodeGetParent(elem);
  ArenaTreeNodeSetParent(tmp, parent);
  if (parent != nullptr) {
    if (elem == ArenaTreeNodeGetLeft(parent)) {
      ArenaTreeNodeSetLeft(parent, tmp);
    } else {
      ArenaTreeNodeSetRight(parent, tmp);
    }
  } else {
    *head = tmp;
  }
  ArenaTreeNodeSetLeft(tmp, elem);
  ArenaTreeNodeSetParent(elem, tmp);
}

void ArenaTreeRotateRight(ArenaTreeNodeBase** head, ArenaTreeNodeBase* elem) {
  ArenaTreeNodeBase* tmp = ArenaTreeNodeGetLeft(elem);
  if (ArenaTreeNodeSetLeft(elem, ArenaTreeNodeGetRight(tmp)) != nullptr) {
    ArenaTreeNodeSetParent(ArenaTreeNodeGetRight(tmp), elem);
  }
  ArenaTreeNodeBase* parent = ArenaTreeNodeGetParent(elem);
  ArenaTreeNodeSetParent(tmp, parent);
  if (parent != nullptr) {
    if (elem == ArenaTreeNodeGetLeft(parent)) {
      ArenaTreeNodeSetLeft(parent, tmp);
    } else {
      ArenaTreeNodeSetRight(parent, tmp);
    }
  } else {
    *head = tmp;
  }
  ArenaTreeNodeSetRight(tmp, elem);
  ArenaTreeNodeSetParent(elem, tmp);
}

void ArenaTreeRemoveColor(ArenaTreeNodeBase** head, ArenaTreeNodeBase* parent,
                          ArenaTreeNodeBase* elem) {
  ArenaTreeNodeBase* tmp;
  while ((elem == nullptr ||
          ArenaTreeNodeGetColor(elem) == ArenaTreeNodeColor::kBlack) &&
         elem != *head) {
    if (ArenaTreeNodeGetLeft(parent) == elem) {
      tmp = ArenaTreeNodeGetRight(parent);
      if (ArenaTreeNodeGetColor(tmp) == ArenaTreeNodeColor::kRed) {
        ArenaTreeNodeSetColor(tmp, ArenaTreeNodeColor::kBlack);
        ArenaTreeNodeSetColor(parent, ArenaTreeNodeColor::kRed);
        ArenaTreeRotateLeft(head, parent);
        tmp = ArenaTreeNodeGetRight(parent);
      }
      if ((ArenaTreeNodeGetLeft(tmp) == nullptr ||
           ArenaTreeNodeGetColor(ArenaTreeNodeGetLeft(tmp)) ==
               ArenaTreeNodeColor::kBlack) &&
          (ArenaTreeNodeGetRight(tmp) == nullptr ||
           ArenaTreeNodeGetColor(ArenaTreeNodeGetRight(tmp)) ==
               ArenaTreeNodeColor::kBlack)) {
        ArenaTreeNodeSetColor(tmp, ArenaTreeNodeColor::kRed);
        elem = parent;
        parent = ArenaTreeNodeGetParent(elem);
      } else {
        if (ArenaTreeNodeGetRight(tmp) == nullptr ||
            ArenaTreeNodeGetColor(ArenaTreeNodeGetRight(tmp)) ==
                ArenaTreeNodeColor::kBlack) {
          ArenaTreeNodeBase* left;
          if ((left = ArenaTreeNodeGetLeft(tmp)) != nullptr) {
            ArenaTreeNodeSetColor(left, ArenaTreeNodeColor::kBlack);
          }
          ArenaTreeNodeSetColor(tmp, ArenaTreeNodeColor::kRed);
          ArenaTreeRotateRight(head, tmp);
          tmp = ArenaTreeNodeGetRight(parent);
        }
        ArenaTreeNodeSetColor(tmp, ArenaTreeNodeGetColor(parent));
        ArenaTreeNodeSetColor(parent, ArenaTreeNodeColor::kBlack);
        if (ArenaTreeNodeGetRight(tmp) != nullptr) {
          ArenaTreeNodeSetColor(ArenaTreeNodeGetRight(tmp),
                                ArenaTreeNodeColor::kBlack);
        }
        ArenaTreeRotateLeft(head, parent);
        elem = *head;
        break;
      }
    } else {
      tmp = ArenaTreeNodeGetLeft(parent);
      if (ArenaTreeNodeGetColor(tmp) == ArenaTreeNodeColor::kRed) {
        ArenaTreeNodeSetColor(tmp, ArenaTreeNodeColor::kBlack);
        ArenaTreeNodeSetColor(parent, ArenaTreeNodeColor::kRed);
        ArenaTreeRotateRight(head, parent);
        tmp = ArenaTreeNodeGetLeft(parent);
      }
      if ((ArenaTreeNodeGetLeft(tmp) == nullptr ||
           ArenaTreeNodeGetColor(ArenaTreeNodeGetLeft(tmp)) ==
               ArenaTreeNodeColor::kBlack) &&
          (ArenaTreeNodeGetRight(tmp) == nullptr ||
           ArenaTreeNodeGetColor(ArenaTreeNodeGetRight(tmp)) ==
               ArenaTreeNodeColor::kBlack)) {
        ArenaTreeNodeSetColor(tmp, ArenaTreeNodeColor::kRed);
        elem = parent;
        parent = ArenaTreeNodeGetParent(elem);
      } else {
        if (ArenaTreeNodeGetLeft(tmp) == nullptr ||
            ArenaTreeNodeGetColor(ArenaTreeNodeGetLeft(tmp)) ==
                ArenaTreeNodeColor::kBlack) {
          ArenaTreeNodeBase* right;
          if ((right = ArenaTreeNodeGetRight(tmp)) != nullptr) {
            ArenaTreeNodeSetColor(right, ArenaTreeNodeColor::kBlack);
          }
          ArenaTreeNodeSetColor(tmp, ArenaTreeNodeColor::kRed);
          ArenaTreeRotateLeft(head, tmp);
          tmp = ArenaTreeNodeGetLeft(parent);
        }
        ArenaTreeNodeSetColor(tmp, ArenaTreeNodeGetColor(parent));
        ArenaTreeNodeSetColor(parent, ArenaTreeNodeColor::kBlack);
        if (ArenaTreeNodeGetLeft(tmp) != nullptr) {
          ArenaTreeNodeSetColor(ArenaTreeNodeGetLeft(tmp),
                                ArenaTreeNodeColor::kBlack);
        }
        ArenaTreeRotateRight(head, parent);
        elem = *head;
        break;
      }
    }
  }
  if (elem != nullptr) {
    ArenaTreeNodeSetColor(elem, ArenaTreeNodeColor::kBlack);
  }
}

}  // namespace

const ArenaTreeNodeBase* absl_nullable ArenaTreeNext(
    const ArenaTreeNodeBase* absl_nullable node) {
  if (node != nullptr) {
    const ArenaTreeNodeBase* right = ArenaTreeNodeGetRight(node);
    if (right != nullptr) {
      node = right;
      const ArenaTreeNodeBase* left;
      while ((left = ArenaTreeNodeGetLeft(node)) != nullptr) {
        node = left;
      }
    } else {
      const ArenaTreeNodeBase* parent = ArenaTreeNodeGetParent(node);
      if (parent != nullptr && node == ArenaTreeNodeGetLeft(parent)) {
        node = parent;
      } else {
        while (parent != nullptr && ArenaTreeNodeGetRight(parent) == node) {
          node = ArenaTreeNodeGetParent(node);
          parent = ArenaTreeNodeGetParent(node);
        }
        node = parent;
      }
    }
  }
  return node;
}

const ArenaTreeNodeBase* absl_nullable ArenaTreePrev(
    const ArenaTreeNodeBase* absl_nullable node) {
  if (node != nullptr) {
    const ArenaTreeNodeBase* left = ArenaTreeNodeGetLeft(node);
    if (left != nullptr) {
      node = left;
      const ArenaTreeNodeBase* right;
      while ((right = ArenaTreeNodeGetRight(node)) != nullptr) {
        node = right;
      }
    } else {
      const ArenaTreeNodeBase* parent = ArenaTreeNodeGetParent(node);
      if (parent != nullptr && node == ArenaTreeNodeGetRight(parent)) {
        node = parent;
      } else {
        while (parent != nullptr && ArenaTreeNodeGetLeft(parent) == node) {
          node = ArenaTreeNodeGetParent(node);
          parent = ArenaTreeNodeGetParent(node);
        }
        node = parent;
      }
    }
  }
  return node;
}

const ArenaTreeNodeBase* absl_nullable ArenaTreeMin(
    const ArenaTreeNodeBase* absl_nullable node) {
  const ArenaTreeNodeBase* tmp = node;
  const ArenaTreeNodeBase* parent = nullptr;
  while (tmp != nullptr) {
    parent = tmp;
    tmp = ArenaTreeNodeGetLeft(tmp);
  }
  return parent;
}

const ArenaTreeNodeBase* absl_nullable ArenaTreeMax(
    const ArenaTreeNodeBase* absl_nullable node) {
  const ArenaTreeNodeBase* tmp = node;
  const ArenaTreeNodeBase* parent = nullptr;
  while (tmp != nullptr) {
    parent = tmp;
    tmp = ArenaTreeNodeGetRight(tmp);
  }
  return parent;
}

void ArenaTreeRemove(ArenaTreeNodeBase* absl_nullable* absl_nonnull head,
                     ArenaTreeNodeBase* absl_nonnull elem) {
  ArenaTreeNodeBase* child;
  ArenaTreeNodeBase* parent;
  ArenaTreeNodeBase* const old = elem;
  ArenaTreeNodeColor color;
  if (ArenaTreeNodeGetLeft(elem) == nullptr) {
    child = ArenaTreeNodeGetRight(elem);
  } else if (ArenaTreeNodeGetRight(elem) == nullptr) {
    child = ArenaTreeNodeGetLeft(elem);
  } else {
    ArenaTreeNodeBase* left;
    elem = ArenaTreeNodeGetRight(elem);
    while ((left = ArenaTreeNodeGetLeft(elem)) != nullptr) {
      elem = left;
    }
    child = ArenaTreeNodeGetRight(elem);
    parent = ArenaTreeNodeGetParent(elem);
    color = ArenaTreeNodeGetColor(elem);
    if (child != nullptr) {
      ArenaTreeNodeSetParent(child, parent);
    }
    if (parent != nullptr) {
      if (ArenaTreeNodeGetLeft(parent) == elem) {
        ArenaTreeNodeSetLeft(parent, child);
      } else {
        ArenaTreeNodeSetRight(parent, child);
      }
    } else {
      *head = child;
    }
    if (ArenaTreeNodeGetParent(elem) == old) {
      parent = elem;
    }
    ArenaTreeNodeBase* old_parent = ArenaTreeNodeGetParent(old);
    if (old_parent != nullptr) {
      if (ArenaTreeNodeGetLeft(old_parent) == old) {
        ArenaTreeNodeSetLeft(old_parent, elem);
      } else {
        ArenaTreeNodeSetRight(old_parent, elem);
      }
    } else {
      *head = elem;
    }
    ArenaTreeNodeSetParent(ArenaTreeNodeGetLeft(old), elem);
    if (ArenaTreeNodeGetRight(old) != nullptr) {
      ArenaTreeNodeSetParent(ArenaTreeNodeGetRight(old), elem);
    }
    goto color;
  }
  parent = ArenaTreeNodeGetParent(elem);
  color = ArenaTreeNodeGetColor(elem);
  if (child != nullptr) {
    ArenaTreeNodeSetParent(child, parent);
  }
  if (parent != nullptr) {
    if (ArenaTreeNodeGetLeft(parent) == elem) {
      ArenaTreeNodeSetLeft(parent, child);
    } else {
      ArenaTreeNodeSetRight(parent, child);
    }
  } else {
    *head = child;
  }
color:
  if (color == ArenaTreeNodeColor::kBlack) {
    ArenaTreeRemoveColor(head, parent, child);
  }
  ArenaTreeNodeClear(old);
}

void ArenaTreeInsertColor(ArenaTreeNodeBase* absl_nullable* absl_nonnull head,
                          ArenaTreeNodeBase* absl_nonnull elem) {
  ArenaTreeNodeBase* parent;
  ArenaTreeNodeBase* grandparent;
  ArenaTreeNodeBase* tmp;
  while ((parent = ArenaTreeNodeGetParent(elem)) != nullptr &&
         ArenaTreeNodeGetColor(parent) == ArenaTreeNodeColor::kRed) {
    grandparent = ArenaTreeNodeGetParent(parent);
    if (parent == ArenaTreeNodeGetLeft(grandparent)) {
      tmp = ArenaTreeNodeGetRight(grandparent);
      if (tmp != nullptr &&
          ArenaTreeNodeGetColor(tmp) == ArenaTreeNodeColor::kRed) {
        ArenaTreeNodeSetColor(tmp, ArenaTreeNodeColor::kBlack);
        ArenaTreeNodeSetColor(parent, ArenaTreeNodeColor::kBlack);
        ArenaTreeNodeSetColor(grandparent, ArenaTreeNodeColor::kRed);
        elem = grandparent;
        continue;
      }
      if (ArenaTreeNodeGetRight(parent) == elem) {
        ArenaTreeRotateLeft(head, parent);
        tmp = parent;
        parent = elem;
        elem = tmp;
      }
      ArenaTreeNodeSetColor(parent, ArenaTreeNodeColor::kBlack);
      ArenaTreeNodeSetColor(grandparent, ArenaTreeNodeColor::kRed);
      ArenaTreeRotateRight(head, grandparent);
    } else {
      tmp = ArenaTreeNodeGetLeft(grandparent);
      if (tmp != nullptr &&
          ArenaTreeNodeGetColor(tmp) == ArenaTreeNodeColor::kRed) {
        ArenaTreeNodeSetColor(tmp, ArenaTreeNodeColor::kBlack);
        ArenaTreeNodeSetColor(parent, ArenaTreeNodeColor::kBlack);
        ArenaTreeNodeSetColor(grandparent, ArenaTreeNodeColor::kRed);
        elem = grandparent;
        continue;
      }
      if (ArenaTreeNodeGetLeft(parent) == elem) {
        ArenaTreeRotateRight(head, parent);
        tmp = parent;
        parent = elem;
        elem = tmp;
      }
      ArenaTreeNodeSetColor(parent, ArenaTreeNodeColor::kBlack);
      ArenaTreeNodeSetColor(grandparent, ArenaTreeNodeColor::kRed);
      ArenaTreeRotateLeft(head, grandparent);
    }
  }
  ArenaTreeNodeSetColor(*head, ArenaTreeNodeColor::kBlack);
}

}  // namespace cel::internal
