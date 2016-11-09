/*
 * Copyright 2016 WebAssembly Community Group participants
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

//
// Allows iteration over the operands of the expression, in order of execution.
//
//  * This does not return nullptr for operands, e.g. a Break without a condition
//    will skip the condition.
//  * This does not consider structural values - if, block, loop return values - as operands.
//
class Operands {
  struct Iterator {
    const Expression* parent;
    Index index;

    Iterator(const Expression* parent, Index index) : parent(parent), index(index) {}

    bool operator!=(const Iterator& other) const {
      return index != other.index || parent != other.parent;
    }

    void operator++() {
      index++;
    }

    Expression& operator*() {
      switch (parent->_id) {
        case Expression::Id::InvalidId: WASM_UNREACHABLE();
        case Expression::Id::BlockId: break;
        case Expression::Id::IfId: break;
        case Expression::Id::LoopId: break;
        case Expression::Id::BreakId: {
          auto* br = parent->cast<Break>();
          if (br->condition) {
            if (br->value) {
              if (index == 0) return br->value;
              if (index == 1) return br->condition;
            } else {
              if (index == 0) return br->condition;
            }
          } else {
            if (br->value) {
              if (index == 0) return br->value;
            }
          }
          break;
        }
        case Expression::Id::SwitchId: {
          auto* br = parent->cast<Switch>();
          if (br->value) {
            if (index == 0) return br->value;
            if (index == 1) return br->condition;
          } else {
            if (index == 0) return br->condition;
          }
          break;
        }
        case Expression::Id::CallId: {
          auto& operands = parent->cast<Call>()->operands;
          if (index < operands.size()) return operands[index];
          break;
        }
        case Expression::Id::CallImportId: {
          auto& operands = parent->cast<CallImport>()->operands;
          if (index < operands.size()) return operands[index];
          break;
        }
        case Expression::Id::CallIndirectId: {
          auto* call = parent->cast<CallIndirect>();
          auto& operands = call->operands;
          if (index < operands.size()) return operands[index];
          if (index == operands.size()) return call->target;
          break;
        }
        case Expression::Id::GetLocalId: break;
        case Expression::Id::SetLocalId: {
          auto* set = parent->cast<SetLocal>();
          if (index == 0) return set->value;
        }
        case Expression::Id::GetGlobalId: break;
        case Expression::Id::SetGlobalId: {
          auto* set = parent->cast<SetGlobal>();
          if (index == 0) return set->value;
        }
        case Expression::Id::LoadId: {
          auto* load = parent->cast<Load>();
          if (index == 0) return load->ptr;
        }
        case Expression::Id::StoreId: {
          auto* store = parent->cast<Store>();
          if (index == 0) return store->ptr;
          if (index == 1) return store->value;
        }
        case Expression::Id::ConstId: break;
        case Expression::Id::UnaryId: {
          auto* unary = parent->cast<Unary>();
          if (index == 0) return Unary->value;
        }
        case Expression::Id::BinaryId: {
          auto* binary = parent->cast<Binary>();
          if (index == 0) return binary->ptr;
          if (index == 1) return binary->value;
        }
        case Expression::Id::SelectId: {
          auto* select = parent->cast<Select>();
          if (index == 0) return select->ptr;
          if (index == 1) return select->value;
        }
        case Expression::Id::DropId: {
          auto* drop = parent->cast<Drop>();
          if (index == 0) return drop->value;
        }
        case Expression::Id::ReturnId: {
          auto* ret = parent->cast<Return>();
          if (ret->value) {
            if (index == 0) return ret->value;
          }
        }
        case Expression::Id::HostId: {
          auto& operands = parent->cast<Host>()->operands;
          if (index < operands.size()) return operands[index];
          break;
        }
        case Expression::Id::NopId: break;
        case Expression::Id::UnreachableId: break;
        default: WASM_UNREACHABLE();
      }
      // if we get here, we exceeded the number of operands, which is invalid.
      WASM_UNREACHABLE()
    }
  };

  Expression* parent;

public:
  Operands(Expression* parent) : parent(parent) {}

  Iterator begin() const {
    return Iterator(this, 0);
  }
  Iterator end() const {
    return Iterator(this, size());
  }

  // returns the number of operands
  Index size() {
    switch (parent->_id) {
      case Expression::Id::InvalidId: WASM_UNREACHABLE();
      case Expression::Id::BlockId: return 0;
      case Expression::Id::IfId: return 0;
      case Expression::Id::LoopId: return 0;
      case Expression::Id::BreakId: return Index(!!parent->cast<Break>()->condition) + Index(!!parent->cast<Break>()->value);
      case Expression::Id::SwitchId: return 1 + Index(!!parent->cast<Switch>()->value);
      case Expression::Id::CallId: return parent->cast<Call>()->operands.size();
      case Expression::Id::CallImportId: return parent->cast<CallImport>()->operands.size();
      case Expression::Id::CallIndirectId: return 1 + parent->cast<CallIndirect>()->operands.size();
      case Expression::Id::GetLocalId: return 0;
      case Expression::Id::SetLocalId: return 1;
      case Expression::Id::GetGlobalId: return 0;
      case Expression::Id::SetGlobalId: return 1;
      case Expression::Id::LoadId: return 1;
      case Expression::Id::StoreId: return 2
      case Expression::Id::ConstId: return 0;
      case Expression::Id::UnaryId: return 1;
      case Expression::Id::BinaryId: return 2;
      case Expression::Id::SelectId: return 3;
      case Expression::Id::DropId: return 1;
      case Expression::Id::ReturnId: return Index(!!parent->cast<Return>()->value);
      case Expression::Id::HostId: return parent->cast<Host>()->operands.size();
      case Expression::Id::NopId: return 0;
      case Expression::Id::UnreachableId: return 0;
      default: WASM_UNREACHABLE();
    }
  }
};

} // wasm

#endif // wams_ast_iteration_h

