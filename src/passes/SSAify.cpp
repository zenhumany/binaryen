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
#include "cfg/cfg-traversal.h"

namespace wasm {

// Helper classes

struct SetLocalCounter : public PostWalker<SetLocalCounter, Visitor<SetLocalCounter>> {
  std::vector<Index>* numSetLocals;

  SetLocalCounter(std::vector<Index>* numSetLocals, Function* func) : numSetLocals(numSetLocals) {
    numSetLocals.resize(func->getNumLocals());
    std::fill(numSetLocals.begin(), numSetLocals.end(), 0);
    walk(func->body);
  }

  void visitSetLocal(SetLocal *curr) {
    (*numSetLocals)[curr->index]++;
  }

};

struct BlockInfo {
};

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

