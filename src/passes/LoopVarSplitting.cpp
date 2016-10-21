/*
 * Copyright 2015 WebAssembly Community Group participants
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
// Splits critical loop vars - phis to the head of the loop - so that
// coalescing can be more effective. Consider
//   i = 0;
//   loop {
//     i2 = i + 1;
//     .. use i and i2, potentially making them conflict
//     if (cond) {
//       i = i2;
//       continue;
//     }
//   }
// This pass separates the task of getting the phi var to the top
// of the loop from the task of keeping it alive throughout the loop.
// This adds a copy for the new var; coalesce-locals can then decide
// which of the copies is more important to remove, often removing
// the critical one before the continue. Note that at least one of
// the two should be removed, since the new var conflicts with
// neither of the other two.

#include <wasm.h>
#include <pass.h>
#include <wasm-builder.h>

namespace wasm {

struct LoopVarSplitting : public WalkerPass<LinearExecutionWalker<LoopVarSplitting, Visitor<LoopVarSplitting>>> {
  bool isFunctionParallel() override { return true; }

  Pass* create() override { return new LoopVarSplitting; }

  // We track final sets - the last set seen of a local, and no get for that local
  // after it. There must be such a set on each branch to a loop top for a phi.
  typedef std::unordered_map<Index, SetLocal*> FinalSets;

  FinalSets currFinalSets;

  std::map<Name, std::vector<FinalSets>> loopEntries; // loop name -> a finalSets for each of the loop top entries

  void noteNonLinear(Expression* curr) {
    if (auto* br = curr->dynCast<Break>()) {
      if (br->condition) {
        loopEntries.erase(br->name); // loop phi must arrive unconditionally
      } else if (loopEntries.count(br->name) > 0) {
        // this is a continue to the loop top
        assert(!br->value); // br to loop top cannot have a value
        loopEntries[br->name].emplace_back(std::move(currFinalSets));
      }
    } else if (auto* loop = curr->dynCast<Loop>()) {
      // this is a loop top
      if (loop->name.is()) {
        loopEntries[loop->name].emplace_back(std::move(currFinalSets));
      }
    }
    // non-linearity cleares the current final sets
    currFinalSets.clear();
  }

  void visitLoop(Loop* curr) {
    // the critical point: we traversed the loop body, and know all we need, and
    // can do our optimization
    if (!curr->name.is()) return;
    auto& entries = loopEntries[curr->name];
    if (entries.size() >= 2) {
      // we need to find a local that is a final-set in all entries
      // TODO: the loop top might be larger, so looping based on the 2nd might be faster
      for (const auto& iter : entries[0]) {
        auto index = iter.first;
        auto* set = iter.second;
        bool inAll = true;
        for (Index i = 1; i < entries.size(); i++) {
          if (entries[i].count(index) == 0) {
            inAll = false;
            break;
          }
        }
        if (!inAll) continue;
        // It's in all of them, this is great, we can do this
        Builder builder(*getModule());
        // create a new helper index, and write to it instead of the old one
        auto type = getFunction()->getLocalType(index);
        auto newIndex = builder.addVar(getFunction(), type);
        set->index = newIndex;
        for (Index i = 1; i < entries.size(); i++) {
          entries[i][index]->index = newIndex;
        }
        // the new index did the task of getting the value to the top of the loop,
        // now assign it to the old variable.
        curr->body = builder.makeSequence(
          builder.makeSetLocal(index, builder.makeGetLocal(newIndex, type)),
          curr->body
        );
      }
    }
    loopEntries.erase(curr->name); // not needed, but cleaning up early might help
  }

  void visitSwitch(Switch* curr) {
    // switches directly to a loop top implies there is no phi there
    for (auto name : curr->targets) {
      loopEntries.erase(name); // TODO: optimize, many here may be duplicates?
    }
    loopEntries.erase(curr->default_);
  }

  void visitGetLocal(GetLocal* curr) {
    currFinalSets.erase(curr->index); // TODO: do something clever?
  }
  void visitSetLocal(SetLocal* curr) {
    currFinalSets[curr->index] = curr;
  }
};

Pass *createLoopVarSplittingPass() {
  return new LoopVarSplitting();
}

} // namespace wasm
