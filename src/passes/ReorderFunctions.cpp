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
// the output binary. This considers several factors, in decreasing
// importance.
//  * Functions with many uses should get lower indexes, so each call
//    to them is smaller (i.e. the LEB with the index is small).
//  * All things considered, larger functions should be first. This is
//    helpful for JIT times as they may take longer to compile, and also
//    similar functions tend to of similar size, and they may compress
//    well if they are close together (for example, a C++ template might
//    generate two almost-identical functions that differ in just one
//    byte).
//  * All things considered, similar function should be close together,
//    and after the first two operations we also look at how similar the
//    contents (not just sizes) are. TODO
//


#include <atomic>
#include <memory>

#include <wasm.h>
#include <pass.h>
#include <wasm-binary.h>

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

struct FunctionInfo {
  uint8_t* data;
  size_t size;
  FunctionInfo() : data(nullptr), size(0) {}
  FunctionInfo(uint8_t* data, size_t size) : data(data), size(size) {}
};

typedef std::unordered_map<Name, FunctionInfo> FunctionInfoMap;

struct ReorderFunctions : public Pass {
  void run(PassRunner* runner, Module* module) override {
    // sort by uses
    sortByUses(module);
    // get binary data for remaining work
    BufferWithRandomAccess buffer;
    WasmBinaryWriter writer(module, buffer);
    writer.write();
    FunctionInfoMap functionInfoMap;
    for (size_t i = 0; i < module->functions.size(); i++) {
      functionInfoMap[module->functions[i]->name] = FunctionInfo(
        &buffer[writer.tableOfContents.functions[i].offset],
        writer.tableOfContents.functions[i].size
      );
    }
    // refine by size
    refineBySize(module, functionInfoMap);
  }

  void sortByUses(Module* module) {
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
    // Sort by number of uses
    std::sort(module->functions.begin(), module->functions.end(), [&uses](
      const std::unique_ptr<Function>& a,
      const std::unique_ptr<Function>& b) -> bool {
      if (uses[a->name] == uses[b->name]) {
        return strcmp(a->name.str, b->name.str) > 0;
      }
      return uses[a->name] > uses[b->name];
    });
  }

  void refineBySize(Module* module, FunctionInfoMap& functionInfoMap) {
    // Sort by function size, without moving past boundaries that would
    // change the LEB size of the call instructions.
    size_t start = 0,
           bits = 0;
    while (start < module->functions.size()) {
      bits += BitsPerLEBByte;
      size_t end;
      if (bits < std::numeric_limits<size_t>::digits) {
        end = std::min(start + (1 << bits), module->functions.size());
      } else {
        end = module->functions.size();
      }
      std::sort(module->functions.begin() + start, module->functions.begin() + end, [&functionInfoMap](
        const std::unique_ptr<Function>& a,
        const std::unique_ptr<Function>& b) -> bool {
        auto aSize = functionInfoMap[a->name].size,
             bSize = functionInfoMap[b->name].size;
        if (aSize == bSize) {
          return strcmp(a->name.str, b->name.str) > 0;
        }
        return aSize > bSize;
      });
      start = end;
    }
  }
};

Pass *createReorderFunctionsPass() {
  return new ReorderFunctions();
}

} // namespace wasm
