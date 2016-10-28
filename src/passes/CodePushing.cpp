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
// Pushes code "forward" as much as possible, potentially into
// a location behind a condition, where it might not always execute.
//

#include <wasm.h>
#include <pass.h>
#include <wasm-builder.h>
#include <ast_utils.h>
#include <ast/count.h>

namespace wasm {

// Implement core optimization logic in a struct, used and then discarded entirely
// for each block
class Pusher {
  ExpressionList& list;
  LocalAnalyzer& analyzer;
  std::vector<Index>& numGetsSoFar;
  Module* module;
  Function* function;

public:
  Pusher(Block* block, LocalAnalyzer& analyzer, std::vector<Index>& numGetsSoFar, Module* module, Function* function) : list(block->list), analyzer(analyzer), numGetsSoFar(numGetsSoFar), module(module), function(function) {
    // Find an optimization segment: from the first pushable thing, to the first
    // point past which we want to push. We then push in that range before
    // continuing forward.
    Index relevant = list.size() - 1; // we never need to push past a final element, as
                                      // we couldn't be used after it.
    Index nothing = -1;
    Index i = 0;
    Index firstPushable = nothing;
    while (i < relevant) {
      if (firstPushable == nothing && isPushable(list[i])) {
        firstPushable = i;
        i++;
        continue;
      }
      if (firstPushable != nothing && isPushPoint(list[i])) {
        // optimize this segment, and proceed from where it tells us
        i = optimizeSegment(firstPushable, i);
        firstPushable = nothing;
        continue;
      }
      i++;
    }
  }

  bool pushedIntoIf = false;

private:
  SetLocal* isPushable(Expression* curr) {
    auto* set = curr->dynCast<SetLocal>();
    if (!set) return nullptr;
    auto index = set->index;
    return analyzer.isSFA(index) && numGetsSoFar[index] == analyzer.getNumGets(index) ? set : nullptr;
  }

  // Push past conditional control flow.
  bool isPushPoint(Expression* curr) {
    // look through drops
    if (auto* drop = curr->dynCast<Drop>()) {
      curr = drop->value;
    }
    if (curr->is<If>()) return true;
    if (auto* br = curr->dynCast<Break>()) {
      return !!br->condition;
    }
    return false;
  }

  Index optimizeSegment(Index firstPushable, Index pushPoint) {
    // The interesting part. Starting at firstPushable, try to push
    // code past pushPoint. We start at the end since we are pushing
    // forward, that way we can push later things out of the way
    // of earlier ones. Once we know all we can push, we push it all
    // in one pass, keeping the order of the pushables intact.
    assert(firstPushable != Index(-1) && pushPoint != Index(-1) && firstPushable < pushPoint);
    auto* pushPointExpr = list[pushPoint];
    EffectAnalyzer cumulativeEffects; // everything that matters if you want
                                      // to be pushed past the pushPoint
    cumulativeEffects.analyze(pushPointExpr);
    cumulativeEffects.branches = false; // it is ok to ignore the branching here,
                                        // that is the crucial point of this opt
    std::vector<SetLocal*> toPush;
    // if handling
    auto* iff = pushPointExpr->dynCast<If>();
    std::unique_ptr<EffectAnalyzer> ifCondition;
    std::unique_ptr<GetLocalCounter> ifTrueCounter, ifFalseCounter;
    std::vector<SetLocal*> toPushToIfTrue, toPushToIfFalse;
    Builder builder(*module);
    // loop
    Index i = pushPoint - 1;
    while (1) {
      auto* pushable = isPushable(list[i]);
      if (pushable) {
        auto iter = pushableEffects.find(pushable);
        if (iter == pushableEffects.end()) {
          pushableEffects.emplace(pushable, pushable);
        }
        auto& effects = pushableEffects[pushable];
        if (cumulativeEffects.invalidates(effects)) {
          // we can't push this
          bool stays = true;
          if (iff) {
            // we can't push *past* the if, but maybe we can push
            // into it
            if (!ifCondition) {
              ifCondition = make_unique<EffectAnalyzer>(iff->condition);
              if (!ifCondition->invalidates(effects)) {
                // we can push past the condition
                Index index = pushable->index;
                ifTrueCounter = make_unique<GetLocalCounter>(function, iff->ifTrue);
                if (ifTrueCounter->numGets[index] == analyzer.getNumGets(index)) {
                  // all uses are in the ifTrue, good
                  toPushToIfTrue.push_back(pushable);
                  list[i] = builder.makeNop();
                  stays = false;
                } else if (iff->ifFalse) {
                  ifFalseCounter = make_unique<GetLocalCounter>(function, iff->ifFalse);
                  if (ifFalseCounter->numGets[index] == analyzer.getNumGets(index)) {
                    // all uses are in the ifFalse, good
                    toPushToIfFalse.push_back(pushable);
                    list[i] = builder.makeNop();
                    stays = false;
                  }
                }
              }
            }
          }
          if (stays) {
            // this stays in place, further pushables must pass it
            cumulativeEffects.mergeIn(effects);
          }
        } else {
          // we can push this, great!
          toPush.push_back(pushable);
        }
        if (i == firstPushable) {
          // no point in looking further
          break;
        }
      } else {
        // something that can't be pushed, so it might block further pushing
        cumulativeEffects.analyze(list[i]);
      }
      assert(i > 0);
      i--;
    }
    Index total = toPush.size();
    if (total == 0 && toPushToIfTrue.empty() && toPushToIfFalse.empty()) {
      // nothing to do, can only continue after the push point
      return pushPoint + 1;
    }
    // we have work to do!
    // first, skip past the pushed elements
    Index last = total - 1;
    Index skip = 0;
    for (Index i = firstPushable; i <= pushPoint; i++) {
      // we see the first elements at the end of toPush
      if (skip < total && list[i] == toPush[last - skip]) {
        // this is one of our elements to push, skip it
        skip++;
      } else {
        if (skip) {
          list[i - skip] = list[i];
        }
      }
    }
    assert(skip == total);
    // write out the skipped elements
    for (Index i = 0; i < total; i++) {
      list[pushPoint - i] = toPush[i];
    }
    // handle elements pushed into ifs
    if (iff) {
      auto pushInto = [&builder](std::vector<SetLocal*>& toPush, Expression*& arm) {
        auto* block = builder.makeBlock();
        Index total = toPush.size();
        block->list.resize(total + 1);
        for (Index i = 0; i < total; i++) {
          block->list[total - 1 - i] = toPush[i];
        }
        block->list[total] = arm;
        arm = block;
      };
      if (!toPushToIfTrue.empty()) {
        pushInto(toPushToIfTrue, iff->ifTrue);
        pushedIntoIf = true;
      }
      if (!toPushToIfFalse.empty()) {
        pushInto(toPushToIfFalse, iff->ifFalse);
        pushedIntoIf = true;
      }
    }
    // proceed right after the push point, we may push the pushed elements again
    return pushPoint - total + 1;
  }

  // Pushables may need to be scanned more than once, so cache their effects.
  std::unordered_map<SetLocal*, EffectAnalyzer> pushableEffects;
};

struct CodePushing : public WalkerPass<PostWalker<CodePushing, Visitor<CodePushing>>> {
  bool isFunctionParallel() override { return true; }

  Pass* create() override { return new CodePushing; }

  LocalAnalyzer analyzer;

  // gets seen so far in the main traversal
  std::vector<Index> numGetsSoFar;

  bool anotherCycle;

  void doWalkFunction(Function* func) {
    // pre-scan to find which vars are sfa, and also count their gets&sets
    analyzer.analyze(func);
    while (1) {
      // prepare to walk
      anotherCycle = false;
      numGetsSoFar.resize(func->getNumLocals());
      std::fill(numGetsSoFar.begin(), numGetsSoFar.end(), 0);
      // walk and optimize
      walk(func->body);
      if (!anotherCycle) break;
    }
  }

  void visitGetLocal(GetLocal *curr) {
    numGetsSoFar[curr->index]++;
  }

  void visitBlock(Block* curr) {
    // Pushing code only makes sense if we are size 3 or above: we need
    // one element to push, an element to push it past, and an element to use
    // what we pushed.
    if (curr->list.size() < 3) return;
    // At this point in the postorder traversal we have gone through all our children.
    // Therefore any variable whose gets seen so far is equal to the total gets must
    // have no further users after this block. And therefore when we see an SFA
    // variable defined here, we know it isn't used before it either, and has just this
    // one assign. So we can push it forward while we don't hit a non-control-flow
    // ordering invalidation issue, since if this isn't a loop, it's fine (we're not
    // used outside), and if it is, we hit the assign before any use (as we can't
    // push it past a use).
    Pusher pusher(curr, analyzer, numGetsSoFar, getModule(), getFunction());
    // if we pushed into an if, we need another cycle to continue pushing inside it
    if (pusher.pushedIntoIf) anotherCycle = true;
  }
};

Pass *createCodePushingPass() {
  return new CodePushing();
}

} // namespace wasm

