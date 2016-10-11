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

// Tracks assignments to locals, assuming single-assignment form, i.e.,
// each assignment creates a new variable.

struct SetTrackingWalker : public PostWalker<SubType, Visitor<SubType>> {
  typedef std::vector<Index> NameMapping; // old index (in original code) => new index (in SSA form, new variables)

  struct BreakInfo {
    NameMapping mapping;
    Expression** origin; // the origin of a node where a phi would go. note that *origin can be nullptr, in which case we can just fill it
    enum PhiType {
      Before = 0,  // a phi should go right before the origin (e.g., origin is a loop and this is the entrance)
      After = 1,   // a phi should go right after the origin (e.g. this is an if body)
      Internal = 2 // origin is the breaking instruction itself, we must add the phi internally (depending on if the break is condition or has a value, etc.,
                   //                                                                            or for a block as the last instruction)
    } type;
    // TODO: move semantics?
    BreakInfo(NameMapping mapping, Expression** origin, PhiType type) : mapping(mapping), origin(origin), type(type) {}
  };

  Index numLocals;
  NameMapping currMapping;
  Index nextIndex;
  std::vector<NameMapping> mappingStack; // used in ifs, loops
  std::map<Name, std::vector<BreakInfo>> breakInfos; // break target => infos that reach it

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

  void doVisitBlock(SubType* self, Expression** currp) {
    auto* curr = (*currp)->cast<Block>();
    if (curr->name.is() && self->breakInfos.find(curr->name) != self->breakInfos.end()) {
      auto& infos = self->breakInfos[curr->name];
      infos.emplace_back(currMapping, currp, BreakInfo::Internal);
      currMapping = std::move(self->merge(infos));
    }
  }
  static void doIfCondition(SubType* self, Expression** currp) {
    auto* curr = (*curr)->cast<If>();
    if (!curr->ifFalse) {
      mappingStack.push_back(currMapping);
    }
  }
  static void doIfTrue(SubType* self, Expression** currp) {
    auto* curr = (*curr)->cast<If>();
    if (curr->ifFalse) {
      mappingStack.push_back(currMapping);
    } else {
      // that's it for this if, merge
      std::vector<BreakInfo> breaks;
      breaks.emplace_back(currMapping, &curr->ifFalse, BreakInfo::After);
      breaks.emplace_back(mappingStack.back(), &curr->condition, BreakInfo::After);
      mappingStack.pop_back();
      currMapping = std::move(merge(breaks));
    }
  }
  static void doIfFalse(SubType* self, Expression** currp) {
    auto* curr = (*curr)->cast<If>();
    std::vector<BreakInfo> breaks;
    breaks.emplace_back(currMapping, &curr->ifFalse, BreakInfo::After);
    breaks.emplace_back(mappingStack.back(), &curr->ifTrue, BreakInfo::After);
    mappingStack.pop_back();
    currMapping = std::move(merge(breaks));
  }
  static void doPreLoop(SubType* self, Expression** currp) {
    // save the state before entering the loop, for calculation later of the merge at the loop top
    mappingStack.push_back(currMapping);
  }
  static void doVisitLoop(SubType* self, Expression** currp) {
    auto* curr = (*currp)->cast<Loop>();
    if (curr->name.is() && self->breakInfos.find(curr->name) != self->breakInfos.end()) {
      auto& infos = self->breakInfos[curr->name];
      infos.emplace_back(mappingStack.back(), currp, BreakInfo::Before);
      self->merge(infos); // output is not assigned anywhere, this is an interesting code path
    }
    mappingStack.pop_back();
  }
  static void visitBreak(SubType* self, Expression** currp) {
    breakInfos[curr->name].emplace_back(currMapping, currp, BreakInfo::Internal);
    if (!(*currp)->cast<Break>()->condition) {
      setUnreachable(currMapping);
    }
  }
  static void visitSwitch(SubType* self, Expression** currp) {
    auto* curr = (*currp)->cast<Switch>();
    XXX: do this for unique names
    for (auto target : curr->targets) {
      breakInfos[target].emplace_back(currMapping, currp, BreakInfo::Switch, need name here);
    }
    breakInfos[target].emplace_back(currMapping, currp, BreakInfo::Switch, need name here);
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

  // merges a bunch of infos into one. where necessary calls a phi hook.
  NameMapping& merge(std::vector<BreakInfo>& infos) {
    auto& out = infos[0];
    for (Index i = 0; i < numLocals; i++) {
      Index seen = -1;
      for (auto& info : infos) {
        if (isUnreachable(info.mapping)) continue;
        if (seen == -1) {
          seen = info.mapping[i];
        } else {
          if (info.mapping[i] != seen) {
            // we need a phi here
            seen = nextIndex++;
            createPhi(infos, i, seen);
            break;
          }
        }
      }
      out.mapping[i] = seen;
    }
    return out.mapping;
  }

  void createPhi(std::vector<BreakInfo>& infos, Index old, Index new_) {
    abort(); // override this in child classes
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

