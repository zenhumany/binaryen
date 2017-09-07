/*
 * Copyright 2017 WebAssembly Community Group participants
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef wasm_ast_iteration_h
#define wasm_ast_iteration_h

#include "wasm.h"

namespace wasm {

// An abstract interface for iterating over a node's operands
// TODO: if useful, make sure this matches order of execution
struct Operands {
  // the node whose operands we represent
  const Expression* node;
  // the total number of operands it has
  Index total = 0;
  // we inline the operands when they are of a fixed size, so
  // each iteration doesn't branch over the type
  const static int MaxFixed = 3;
  Index numFixed = 0;
  Expression* fixed[MaxFixed];
  // a reference to any additional variable-number operands
  ExpressionList* list = nullptr;

  Operands(const Expression* node) : node(node) {
    switch (node->_id) {
      case Expression::Id::BlockId: {
        list = &node->cast<Block>()->list;
        break;
      }
      case Expression::Id::IfId: {
        fixed[0] = node->cast<If>()->condition;
        fixed[1] = node->cast<If>()->ifTrue;
        fixed[2] = node->cast<If>()->ifFalse;
        numFixed = 3;
        break;
      }
      case Expression::Id::LoopId: {
        fixed[0] = node->cast<Loop>()->body;
        numFixed = 1;
        break;
      }
      case Expression::Id::BreakId: {
        fixed[0] = node->cast<Break>()->condition;
        fixed[1] = node->cast<Break>()->value;
        numFixed = 2;
        break;
      }
      case Expression::Id::SwitchId: {
        fixed[0] = node->cast<Switch>()->condition;
        fixed[1] = node->cast<Switch>()->value;
        numFixed = 2;
        break;
      }
      case Expression::Id::CallId: {
        list = &node->cast<Call>()->operands;
        break;
      }
      case Expression::Id::CallImportId: {
        list = &node->cast<CallImport>()->operands;
        break;
      }
      case Expression::Id::CallIndirectId: {
        fixed[0] = node->cast<CallIndirect>()->target;
        numFixed = 1;
        list = &node->cast<CallIndirect>()->operands;
        break;
      }
      case Expression::Id::GetLocalId: {
        break;
      }
      case Expression::Id::SetLocalId: {
        fixed[0] = node->cast<SetLocal>()->value;
        numFixed = 1;
        break;
      }
      case Expression::Id::GetGlobalId: {
        break;
      }
      case Expression::Id::SetGlobalId: {
        fixed[0] = node->cast<SetGlobal>()->value;
        numFixed = 1;
        break;
      }
      case Expression::Id::LoadId: {
        fixed[0] = node->cast<Load>()->ptr;
        numFixed = 1;
        break;
      }
      case Expression::Id::StoreId: {
        fixed[0] = node->cast<If>()->ptr;
        fixed[1] = node->cast<If>()->value;
        numFixed = 2;
        break;
      }
      case Expression::Id::ConstId: {
        break;
      }
      case Expression::Id::UnaryId: {
        fixed[0] = node->cast<Unary>()->value;
        numFixed = 1;
        break;
      }
      case Expression::Id::BinaryId: {
        fixed[0] = node->cast<Binary>()->left;
        fixed[1] = node->cast<Binary>()->right;
        numFixed = 2;
        break;
      }
      case Expression::Id::SelectId: {
        fixed[0] = node->cast<Select>()->ifTrue;
        fixed[1] = node->cast<Select>()->ifFalse;
        fixed[2] = node->cast<Select>()->condition;
        numFixed = 3;
        break;
      }
      case Expression::Id::DropId: {
        fixed[0] = node->cast<If>()->value;
        numFixed = 1;
        break;
      }
      case Expression::Id::ReturnId: {
        fixed[0] = node->cast<If>()->value;
        numFixed = 1;
        break;
      }
      case Expression::Id::HostId: {
        list = &node->cast<Host>()->operands.size(); i++) {
        break;
      }
      case Expression::Id::NopId: {
        break;
      }
      case Expression::Id::UnreachableId: {
        break;
      }
      default: WASM_UNREACHABLE();
    }
    total = numFixed;
    if (list) {
      total += list->size();
    }
  }

  Expression*& operator[](Index index) {
    if (index < numFixed) {
      return fixed[index];
    }
    assert(list);
    index -= numFixed;
    assert(index < list->size());
    return (*list)[index];
  }

  class Iterator {
    const Operands& parent;
    Index index;

    OperandIterator(const Operands& parent, Index index) : parent(parent), index(index) {}

  public:
    bool operator!=(const Iterator& other) const {
      return index != other.index || &parent != &other.parent;
    }

    void operator++() {
      index++;
    }

    Iterator& operator+=(int off) {
      index += off;
      return *this;
    }

    const Iterator operator+(int off) const {
      return Iterator(*this) += off;
    }

    Expression*& operator*() {
      return parent[index];
    }
  };

  Iterator begin() const {
    return Iterator(static_cast<const SubType*>(this), 0);
  }
  Iterator end() const {
    return Iterator(static_cast<const SubType*>(this), total);
  }
};

} // namespace wasm

#endif // wasm_ast_iteration_h

