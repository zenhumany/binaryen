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
// Sorts functions to reduce the size and improve compressibility of
// the output binary. This considers two factors:
//  * Functions with many uses should get lower indexes, so each call
//    to them is smaller.
//  * Similar functions should be close together, so that compression
//    is more effective. For example, a C++ template might generate
//    two almost-identical functions that differ in just one byte,
//    and if they are close they might almost be compressed to helf
//    their combined size. TODO
//


#include <atomic>
#include <memory>

#include <wasm.h>
#include <pass.h>

namespace wasm {

typedef std::unordered_map<Name, std::atomic<Index>> FunctionUseMap;

struct FunctionUseCounter : public WalkerPass<PostWalker<FunctionUseCounter>> {
  bool isFunctionParallel() override { return true; }

  FunctionUseCounter(FunctionUseMap* uses) : uses(uses) {}

  FunctionUseCounter* create() override {
    return new FunctionUseCounter(uses);
  }

  void visitCall(Call *curr) {
    (*uses)[curr->target]++;
  }

private:
  FunctionUseMap* uses;
};


struct ReorderFunctions : public Pass {
  void run(PassRunner* runner, Module* module) override {
    // Prepare to count uses in functions. We fill with 0 so
    // that we do not modify the vector in parallel
    FunctionUseMap uses;
    for (auto& func : module->functions) {
      uses[func->name] = 0;
    }
    // Find uses in function bodies
    {
      PassRunner runner(module);
      runner.setIsNested(true);
      runner.add<FunctionUseCounter>(&uses);
      runner.run();
    }
    // Find global uses
    if (module->start.is()) {
      uses[module->start]++;
    }
    for (auto& curr : module->exports) {
      uses[curr->value]++;
    }
    for (auto& segment : module->table.segments) {
      for (auto& curr : segment.data) {
        uses[curr]++;
      }
    }
    // sort by number of uses
    std::sort(module->functions.begin(), module->functions.end(), [&uses](
      const std::unique_ptr<Function>& a,
      const std::unique_ptr<Function>& b) -> bool {
      if (uses[a->name] == uses[b->name]) {
        return strcmp(a->name.str, b->name.str) > 0;
      }
      return uses[a->name] > uses[b->name];
    });
  }
};

Pass *createReorderFunctionsPass() {
  return new ReorderFunctions();
}

} // namespace wasm
