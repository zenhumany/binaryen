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
// importance:
//
//  * Functions with many uses should get lower indexes, so each call
//    to them is smaller (i.e. the LEB with the index is small).
//  * Similar functions should be close together, since they may compress
//    well that way (otherwise, similar things may be out of range
//    for the compression sliding window etc.).
//  * Large functions should be earlier, as they may take longer to
//    compile.
//


#include <atomic>
#include <memory>

#include <third_party/zlib/binaryen-interface.h>

#include <support/hash.h>
#include <wasm.h>
#include <pass.h>
#include <wasm-binary.h>

namespace wasm {

struct StringRef {
  uint8_t* data;
  size_t size;
  StringRef() : data(nullptr), size(0) {}
  StringRef(uint8_t* data, size_t size) : data(data), size(size) {}
};

// Mutual compressibility metric: see how well two pieces of code compress
// together vs separately. If they are "similar" then the shared compression
// should be smaller.
// The return result assumes we compare it to other invocations with the
// first argument, i.e., f(F, X) <> f(F, Y) where X and Y change but
// F does not.
// @return: the lower the result the smaller the difference (higher similarity)
static size_t compressibleDifference(StringRef a, StringRef b) {
  // there isn't much point to look at anything bigger than the sliding window
  // size of typical compression. We just need the last part of a and the first
  // part of b, as they are what will potentially be compressed together.
  // Actually much less is probably fine too TODO
  const size_t MAX_SIZE = 32 * 1024;
  if (a.size > MAX_SIZE) {
    a.data += a.size - MAX_SIZE;
    a.size = MAX_SIZE;
  }
  if (b.size > MAX_SIZE) {
    b.size = MAX_SIZE;
  }
  // create a string with the concatenated data
  StringRef c((uint8_t*)malloc(a.size + b.size), a.size + b.size);
  memcpy(c.data, a.data, a.size);
  memcpy(c.data + a.size, b.data, b.size);
  // find compression results. since we assume a is fixed,
  // we don't need to compute its compressed size.
  auto bComp = zlib::getCompressedSize(b.data, b.size),
       cComp = zlib::getCompressedSize(c.data, c.size);
  free(c.data);
  // compute the similarity
  // the combined result should be bigger than each one separately; if not through
  // some miracle of compression, just return 0
  if (cComp <= bComp) return 0;
  // remove the separate compression of b from the shared compression.
  return cComp - bComp;
}

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
  typedef std::unordered_map<Name, StringRef> FunctionDataMap;

  void run(PassRunner* runner, Module* module) override {
    // sort by uses
    sortByUses(module);
    // get binary data for remaining work
    BufferWithRandomAccess buffer;
    WasmBinaryWriter writer(module, buffer);
    writer.write();
    FunctionDataMap functionDataMap;
    for (size_t i = 0; i < module->functions.size(); i++) {
      functionDataMap[module->functions[i]->name] = StringRef(
        &buffer[writer.tableOfContents.functions[i].offset],
        writer.tableOfContents.functions[i].size
      );
    }
    refineBySimilarity(module, functionDataMap);
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
    // Sort by number of uses, break ties by original index
    std::unordered_map<Name, Index> originalIndex;
    std::for_each(module->functions.begin(), module->functions.end(), [&originalIndex](const std::unique_ptr<Function>& a) {
      originalIndex[a->name] = originalIndex.size();
    });
    std::sort(module->functions.begin(), module->functions.end(), [&uses, &originalIndex](
      const std::unique_ptr<Function>& a,
      const std::unique_ptr<Function>& b) -> bool {
      if (uses[a->name] == uses[b->name]) {
        return originalIndex[a->name] < originalIndex[b->name];
      }
      return uses[a->name] > uses[b->name];
    });
  }

  void refineBySimilarity(Module* module, FunctionDataMap& functionDataMap) {
    auto& functions = module->functions;
    size_t start = 0,
           bits = 0;
    // similarity is to the last element, which may be from a previous chunk
    Name last;
    while (start < functions.size()) {
      bits += BitsPerLEBByte;
      size_t end;
      if (bits < std::numeric_limits<size_t>::digits) {
        end = std::min(start + (1 << bits), functions.size());
      } else {
        end = functions.size();
      }
      // sort them using the distance metric, plus size as secondary
      for (size_t i = start; i < end; i++) {
        if (!last.is()) {
          // this is the very first iteration. just leave the first (and
          // largest) function in place
        } else {
          // greedy: find the most similar function to the last
          size_t bestIndex = i;
          auto bestDifference = compressibleDifference(functionDataMap[last], functionDataMap[functions[i]->name]);
          for (size_t j = i + 1; j < end; j++) {
            auto currDifference = compressibleDifference(functionDataMap[last], functionDataMap[functions[j]->name]);
            if (currDifference < bestDifference ||
                (currDifference == bestDifference && functionDataMap[functions[j]->name].size >
                                                     functionDataMap[functions[bestIndex]->name].size)) {
              bestDifference = currDifference;
              bestIndex = j;
            }
          }
          std::swap(functions[i], functions[bestIndex]);
        }
        last = functions[i]->name;
      }
      start = end;
    }
  }
};

Pass *createReorderFunctionsPass() {
  return new ReorderFunctions();
}

} // namespace wasm
