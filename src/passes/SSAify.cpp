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

//
// Transforms code into SSA form. That ensures each variable has a
// single assignment. For phis, we do not add a new node to the AST,
// so the result is multiple assignments but with the guarantee that
// they all travel directly to the same basic block, i.e., they are
// a way to represent a phi in our AST.
//

#include "wasm.h"
#include "pass.h"
#include "support/permutations.h"

namespace wasm {

// Helper class, tracks assignments to locals, assuming single-assignment form, i.e.,
// each assignment creates a new variable.

struct SetTrackingWalker : public PostWalker<SubType, Visitor<SubType>> {
  typedef std::vector<Index> NameMapping; // old index (in original code) => new index (in SSA form, new variables)

  struct BreakInfo {
    NameMapping mapping;
    Expression** origin; // the node performing the break - a phi must happen right before this node's jump operation
  };

  Index numLocals;
  NameMapping currMapping;
  Index nextIndex;
  std::vector<NameMapping> mappingStack; // used in ifs, loops
  std::map<Name, std::vector<BreakInfo<> breakInfos; // break target => infos that reach it

  SetTrackingWalker(Function* func) {
    numLocals = func->getNumLocals();
    if (numLocals == 0) return; // nothing to do
    // We begin with each param being assigned from the incoming value, and the zero-init for the locals,
    // so the initial state is the identity permutation
    currMapping.resize(numLocals);
    setIdentity(currMapping);
    nextIndex = numLocals;
    walk(func->body);
  }

  void visitBlock(Block *curr) {
    if (curr->name.is() && breakInfos.find(curr->name) != breakInfos.end()) {
      // merge all incoming
      auto& infos = breakInfos[curr->name];
      for (auto& info : infos) {
        merge(currMapping, info.mapping, curr - need replaceCurrent, info.origin);
      }
    }
  }
  void ifCondition(If *curr) {
    mappingStack.push_back(currMapping);
  }
  void ifTrue(If *curr) {
    if (curr->ifFalse) {
      mappingStack.push_back(currMapping);
    } else {
      // that's it for this if, merge
      merge(currMapping, mappingStack.back(), &curr->ifFalse);
      mappingStack.pop_back();
    }
  }
  void ifFalse(If *curr) {
    merge(currMapping, mappingStack.back());
    mappingStack.pop_back();
  }
  void preLoop(Loop *curr) {
  }
  void visitLoop(Loop *curr) {
  }
  void visitBreak(Break *curr) {
    ...
    if (!curr->condition) {
      setUnreachable(currMapping);
    }
  }
  void visitSwitch(Switch *curr) {
    ...
    setUnreachable(currMapping);
  }
  void visitReturn(Return *curr) {
    setUnreachable(currMapping);
  }
  void visitUnreachable(Unreachable *curr) {
    setUnreachable(currMapping);
  }

  void visitSetLocal(SetLocal *curr) {
    currMapping[curr->index] = nextIndex++; // a new assignment, trample the old
  }

  // traversal

  static void doIfCondition(SubType* self, Expression** currp) { self->ifCondition((*currp)->cast<Loop>()); }
  static void doIfTrue(SubType* self, Expression** currp) { self->ifTrue((*currp)->cast<Loop>()); }
  static void doIfFalse(SubType* self, Expression** currp) { self->ifFalse((*currp)->cast<Loop>()); }
  static void doPreLoop(SubType* self, Expression** currp) { self->preLoop((*currp)->cast<Loop>()); }

  static void scan(SubType* self, Expression** currp) {
    if (auto* iff = (*currp)->dynCast<If>()) {
      // if needs special handling
      if (iff->ifFalse) {
        self->pushTask(SubType::doIfFalse, currp);
        self->pushTask(SubType::scan, iff->ifFalse);
      }
      self->pushTask(SubType::doIfTrue, currp);
      self->pushTask(SubType::scan, iff->ifTrue);
      self->pushTask(SubType::doIfCondition, currp);
      self->pushTask(SubType::scan, iff->condition);
    } else {
      PostWalker<SubType, Visitor<SubType>>::scan(self, currp);
    }

    // loops need pre-order visiting too
    if ((*currp)->is<Loop>()) {
      self->pushTask(SubType::doPreLoop, currp);
    }
  }

  // helpers

  void setUnreachable(NameMapping& mapping) {
    mapping[0] = Index(-1);
  }

  bool isUnreachable(NameMapping& mapping) {
    return mapping[0] == Index(-1);
  }

  void merge(NameMapping& into, NameMapping& from) {
    if (isUnreachable(into)) {
      into.swap(from);
      return;
    }
    if (isUnreachable(from)) return;
    for (Index i = 0; i < numLocals; i++) {
      if (into[i] != from[i]) {
        // we need a phi here
        into[i] = nextIndex++;
        // TODO: requestPhi
      }
    }
  }
};

child class that finds out which loop incoming vars need a phi

second child class that does the second pass and finishees it all


struct BlockInfo {
};

CFG walker? normal walkser is ok

struct SSAify : public WalkerPass<CFGWalker<SSAify, Visitor<SSAify>, BlockInfo>> {
  bool isFunctionParallel() override { return true; }

  Pass* create() override { return new SSAify; }

  std::vector<Index> numSetLocals;

  bool hasTooManySets(Index index) {
    // parameters are assigned on entry
hmmf, there is the zero-init assign to locals too...  
    if (getFunction()->isParam(index)) {
      return numSetLocals[index] > 0;
    } else {
      return numSetLocals[index] > 1;
    }
  }

  void visitGetLocal(GetLocal* curr) {
    if (numSetLocals[curr->index]
  }

  void doWalkFunction(Function* func) {
    // count how many set_locals each local has. if it already has just 1, we can ignore it.
    SetLocalCounter counter(&numSetLocals, func);
    // main pass
    CFGWalker<SetLocalCounter, Visitor<SetLocalCounter>>::doWalkFunction(func);
  }
};

Pass *createSSAifyPass() {
  return new SSAify();
}

} // namespace wasm

