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

#include <support/hash.h>
#include <wasm.h>
#include <pass.h>
#include <wasm-binary.h>

namespace wasm {

// Very simple string difference "metric". Very loosely inspired by
//   http://dimacs.rutgers.edu/~graham/pubs/papers/editmovestalg.pdf
//   The String Edit Distance Matching Problem with Moves
//   GRAHAM CORMODE (AT&T Labs–Research) S. MUTHUKRISHNAN (Rutgers University)
// The idea is to hash substrings of various lengths in a deterministic
// manner, ignoring their location. This approximates the edit distance
// with moves, which makes sense for us since "moves" exist in gzip etc.
// compression.

struct StringRef {
  uint8_t* data;
  size_t size;
  StringRef(uint8_t* data, size_t size) : data(data), size(size) {}
};

class StringSignature {
  // each hash value => how many times it was seen
  std::unordered_map<HashResult, size_t> hashCounts;

  // the largest substring to consider. there are diminishing gains
  // fairly quickly.
  static const size_t MAX_SUB_SIZE = 32;

  // the maximum number of hashes to consider. this avoids very long
  // computation time, which often isn't worth it.
  static const size_t MAX_HASHES = 4096;

public:
  StringSignature(StringRef str) {
    // start with a hash of the full string
std::cout << "  FULL: ";
for (auto z = str.data; z < str.data + str.size; z++)
  std::cout << int(*z) << ' ';
std::cout << "\n";
std::cout << "  addddd " << (hashString(str.data, str.size)) << "\n";
    hashCounts[hashString(str.data, str.size)]++;
    // add hashes of substrings
    for (size_t i = 0; i < str.size; i++) {
      // starting from this location, add hashes of substrings of various sizes
      size_t subSize = 1;
      HashResult hash = 0;
      do {
        // don't rehash already hashed portions, hash just the later half
        hash = rehash(hash, hashString(str.data + i + (subSize / 2), subSize / 2));
std::cout << "  scaning ";
for (auto z = str.data + i + (subSize / 2); z < str.data + i + (subSize / 2) + (subSize / 2); z++)
  std::cout << int(*z) << ' ';
std::cout << "\n";
        // increment this hash, but only if we haven't seen too many unique ones
        if (hashCounts.size() < MAX_HASHES || hashCounts.count(hash) > 0) {
std::cout << "  add " << hash << "\n";
          hashCounts[hash]++;
        }
        subSize *= 2;
      } while (i + subSize <= str.size && subSize < MAX_SUB_SIZE);
    }
  }

  size_t difference(StringSignature& other) {
std::cout << "comparing with sizes " << hashCounts.size() << " : " << other.hashCounts.size() << "\n";
    size_t diff = 0;
    // add ones only in us, and diffs of ones in both
    for (auto& pair : hashCounts) {
      auto hash = pair.first;
      auto count = pair.second;
      auto iter = other.hashCounts.find(hash);
      if (iter == other.hashCounts.end()) {
        diff += count;
      } else {
        diff += std::abs(ssize_t(count) - ssize_t(iter->second));
      }
    }
    // add ones only in the other
    for (auto& pair : other.hashCounts) {
      auto hash = pair.first;
      auto count = pair.second;
      auto iter = hashCounts.find(hash);
      if (iter == hashCounts.end()) {
        diff += count;
      }
    }
std::cout << "diff: " << diff << '\n';
    return diff;
  }

private:
  HashResult hashString(uint8_t* data, size_t size) {
    HashResult ret = 0;
    while (size > 0) {
      ret = rehash(ret, *data);
      data++;
      size--;
    }
    return ret;
  };
};

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

  void refineBySimilarity(Module* module, FunctionInfoMap& functionInfoMap) {
    auto& functions = module->functions;
    size_t start = 0,
           bits = 0;
    // similarity is to the last element, which may be from a previous chunk
    std::unordered_map<Name, StringSignature> functionSignatures;
    Name last;
    while (start < functions.size()) {
      bits += BitsPerLEBByte;
      size_t end;
      if (bits < std::numeric_limits<size_t>::digits) {
        end = std::min(start + (1 << bits), functions.size());
      } else {
        end = functions.size();
      }
      // calculate the signature of the contents of each function
      std::for_each(functions.begin() + start, functions.begin() + end, [&](const std::unique_ptr<Function>& func) {
std::cout << "calc sig for " << func->name << "\n";
        functionSignatures.emplace(func->name, StringRef(functionInfoMap[func->name].data, functionInfoMap[func->name].size));
      });
      // sort them using the distance metric, plus size as secondary
      for (size_t i = start; i < end; i++) {
        if (!last.is()) {
          // this is the very first iteration. just leave the first (and
          // largest) function in place
        } else {
          // greedy: find the most similar function to the last
          size_t bestIndex = i;
std::cout << "calc diff " << last << " : " << functions[i]->name << '\n';
          auto bestDifference = functionSignatures.at(last).difference(functionSignatures.at(functions[i]->name));
          for (size_t j = i + 1; j < end; j++) {
std::cout << "calc diff " << last << " : " << functions[j]->name << '\n';
            auto currDifference = functionSignatures.at(last).difference(functionSignatures.at(functions[j]->name));
            if (currDifference < bestDifference ||
                (currDifference == bestDifference && functionInfoMap[functions[j]->name].size >
                                                     functionInfoMap[functions[bestIndex]->name].size)) {
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
