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
//    contents (not just sizes) are.
//


#include <atomic>
#include <memory>

#include <support/hash.h>
#include <wasm.h>
#include <pass.h>
#include <wasm-binary.h>

namespace wasm {

// Very simple string difference metric. Very loosely inspired by
//   http://dimacs.rutgers.edu/~graham/pubs/papers/editmovestalg.pdf
//   The String Edit Distance Matching Problem with Moves
//   GRAHAM CORMODE (AT&T Labs–Research) S. MUTHUKRISHNAN (Rutgers University)
// The idea is to hash substrings of various lengths in a deterministic
// manner, ignoring their location. This approximates the edit distance
// with moves, which makes sense for us since "moves" exist in gzip etc.
// compression.
static size_t simpleStringDifference(uint8_t* aData, size_t aSize, uint8_t* bData, size_t bSize) {
  typedef std::unordered_map<HashResult, size_t> HashCounts;

  auto hashSubstrings = [](uint8_t* data, size_t size, HashCounts& hashCounts) {
    // the largest substring to consider
    const size_t MAX_SUB_SIZE = 1024;

    auto hashString = [](uint8_t* data, size_t size) {
      HashResult ret = 0;
      while (size > 0) {
        ret = rehash(ret, *data);
        data++;
        size--;
      }
      return ret;
    };

    // start with a hash of the full string
    hashCounts[hashString(data, size)]++;
    // add hashes of substrings
    for (size_t i = 0; i < size; i++) {
      // starting from this location, add hashes of substrings of various sizes
      size_t subSize = 1;
      HashResult hash = 0;
      do {
        // don't rehash already hashed portions, hash just the later half
        hash = rehash(hash, hashString(data + i + (subSize / 2), subSize / 2));
        hashCounts[hash]++;
        subSize *= 2;
      } while (i + subSize <= size && subSize < MAX_SUB_SIZE);
    }
  };

  HashCounts a, b;
  hashSubstrings(aData, aSize, a);
  hashSubstrings(bData, bSize, b);
  size_t diff = 0;
  // add ones only in a, and diffs of ones in both
  for (auto& pair : a) {
    auto hash = pair.first;
    auto count = pair.second;
    auto iter = b.find(hash);
    if (iter == b.end()) {
      diff += count;
    } else {
      diff += std::abs(count - iter->second);
    }
  }
  // add ones only in b
  for (auto& pair : b) {
    auto hash = pair.first;
    auto count = pair.second;
    auto iter = a.find(hash);
    if (iter == a.end()) {
      diff += count;
    }
  }
  return diff;
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
    // refine by similarity
    refineBySimilarity(module, functionInfoMap);
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

  void refineBySimilarity(Module* module, FunctionInfoMap& functionInfoMap) {
    // Sort in chunks of a fixed size. This is useful because
    //  * We want to keep the number of bytes used by call instructions
    //    fixed, that is, if we sorted a function so it has an index
    //    in 0..127, then the LEB in the calls to it take one byte, and
    //    don't want that to change.
    //  * We do an O(n^2) operation we want to keep n (chunk size) low.
    //  * There is a quick diminishing return here, in that adjacent
    //    functions should be similar, and farther out it matters less,
    //    and we've already sorted by size, so almost identical ones
    //    tend to be close anyhow.
    //
    // The sort itself is greedy. In theory we could do better with a
    // clustering type algorithm.
    auto& functions = module->functions;
    const size_t chunkSize = 1 << BitsPerLEBByte;
    size_t start = 0;
    Name last; // we find the best match for the last one. this crosses
               // chunks, as it should
    while (start < functions.size()) {
      size_t end = std::min(start + chunkSize, functions.size());
      for (size_t i = start; i < end; i++) {
        if (!last.is()) {
          // this is the very first iteration. just leave the first (and
          // largest) function in place
        } else {
          // greedy: find the most similar function to the last
          size_t bestIndex = i;
          auto bestDifference = getDifference(last, functions[i]->name, functionInfoMap);
          for (size_t j = i + 1; j < end; j++) {
            auto currDifference = getDifference(last, functions[j]->name, functionInfoMap);
            if (currDifference < bestDifference) {
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

  // computes how different two sets of bytes are. the lower, the more similar
  size_t getDifference(Name a, Name b, FunctionInfoMap& functionInfoMap) {
    return simpleStringDifference(functionInfoMap[a].data, functionInfoMap[a].size,
                                  functionInfoMap[b].data, functionInfoMap[b].size);
  }
};

Pass *createReorderFunctionsPass() {
  return new ReorderFunctions();
}

} // namespace wasm
