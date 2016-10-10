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

struct SetTrackingWalker : public PostWalker<SetTrackingWalker, Visitor<SetTrackingWalker>> {
  std::vector<Index> oldToNew; // old local id => id of the new SSA assignment to it
  Index nextIndex;

  SetTrackingWalker(Function* func) {
    // We begin with each param being assigned from the incoming value, and the zero-init for the locals,
    // so the initial state is the identity permutation
    oldToNew.resize(func->getNumLocals());
    setIdentity(oldToNew);
    nextIndex = func->getNumLocals();
    walk(func->body);
  }

  void visitSetLocal(SetLocal *curr) {
    oldToNew[curr->index] = nextIndex++;
  }

  // branches, joins, etc.
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

