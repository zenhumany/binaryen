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
// wasm2asm console tool
//

#include "support/colors.h"
#include "support/command-line.h"
#include "support/file.h"
#include "support/learning.h"
#include "wasm-binary.h"
#include "wasm-s-parser.h"

using namespace cashew;
using namespace wasm;

// Optimization using opcode table and machine learning

struct Choice {
  // a choice of optimization options consists of the order of functions, and the
  // number and sizes of function sections
  std::vector<size_t> order;
  std::vector<size_t> sectionSizes;

  int32_t getFitness() { return fitness; }

  void setFitness(int32_t f) {
    fitness = f;
  }

  void verify() {
    // verify
    size_t total = 0;
    for (auto size : sectionSizes) {
      total += size;
    }
    assert(total == order.size());
  }

  void dump() {
    std::cerr << "Choice [on " << order.size() << " funcs, fitness=" << fitness << "]:\n";
    for (size_t i = 0; i < order.size(); i++) {
      std::cerr << "  order[" << i << "] = " << order[i] << '\n';
    }
    for (size_t i = 0; i < sectionSizes.size(); i++) {
      std::cerr << "  sectionSizes[" << i << "] = " << sectionSizes[i] << '\n';
    }
  }

private:
  // when learning, we will have our fitness calculated
  int32_t fitness;
};

void generateOptimizedBinary(Module& wasm, BufferWithRandomAccess& buffer, Choice& choice, bool debug) {
  if (debug) std::cerr << "preprocess to analyze opcode usage..." << std::endl;

  // Apply ordering from choice to the module itself, to avoid needing to have additional
  // complexity in the writer class itself.
  // First, save the original order on the side.
  std::vector<Function*> originalOrder;
  for (size_t i = 0; i < wasm.functions.size(); i++) {
    originalOrder.push_back(wasm.functions[i].release());
  }
  // Do the reordering
  for (size_t i = 0; i < wasm.functions.size(); i++) {
    wasm.functions[i] = std::unique_ptr<Function>(originalOrder[choice.order[i]]);
  }

  std::vector<OpcodeInfo> opcodeInfos;
  opcodeInfos.resize(choice.sectionSizes.size());

  WasmBinaryPreprocessor pre(&wasm, buffer, choice.sectionSizes, opcodeInfos, debug);
  pre.write();
  buffer.clear();

  if (debug) std::cerr << "generate opcode table..." << std::endl;
  std::vector<OpcodeTable> opcodeTables;
  for (auto& info : opcodeInfos) {
    opcodeTables.emplace_back(info);
    if (debug) opcodeTables.back().dump();
    // XXX opcodeTables.back().dump();
  }
  if (debug) std::cerr << "emit using opcode table..." << std::endl;
  WasmBinaryPostprocessor post(&wasm, buffer, choice.sectionSizes, opcodeTables, debug);
  post.write();

  // Undo reordering
  for (size_t i = 0; i < wasm.functions.size(); i++) {
    wasm.functions[i].release();
    wasm.functions[i] = std::unique_ptr<Function>(originalOrder[i]);
  }
}

// Generates elements to be learned on

struct Generator {
  Generator(Module& wasm, bool debug = false) : wasm(wasm), size(wasm.functions.size()), debug(debug) {}

  Choice* makeRandom() {
    auto* ret = new Choice();
    // shuffle the functions
    for (size_t i = 0; i < size; i++) ret->order.push_back(i);
    std::random_shuffle(ret->order.begin(), ret->order.end());
    // pick the number of function sections
    size_t num;
    if (rand() & 32) {
      num = std::max(rand() % size, 1U); // all possible sizes
    } else if (rand() & 16) {
      num = std::max(std::min(std::min(rand() % size, rand() % size), std::min(rand() % size, rand() % size)), 1U); // conservative small size
    } else {
      num = std::min(size, 1U + rand() % 8); // absolute small size
    }
    //std::cerr << "num sections " << num << " / " << size << '\n';
    // to get a uniform distribution of section sizes, randomly place markers
    // a marker means, "when you reach this, after it is a new section"
    std::vector<size_t> markers;
    for (size_t i = 0; i < num; i++) markers.push_back(rand() % size);
    std::sort(markers.begin(), markers.end());
    markers.push_back(size + 1); // buffer at the end, so we don't need to bounds check
    size_t currSectionSize = 0, nextMarker = 0;
    for (size_t i = 0; i < size; i++) {
      currSectionSize++;
      if (markers[nextMarker] <= i) { // may be less due to duplicates, we handle one per iter intentionally, so sections are not empty
        ret->sectionSizes.push_back(currSectionSize);
        currSectionSize = 0;
        nextMarker++;
      }
    }
    if (currSectionSize > 0) { // final section
      ret->sectionSizes.push_back(currSectionSize);
    }
    calcFitness(*ret);
    return ret;
  }

  void addSectionIndexes(Choice* choice, std::vector<size_t>& indexes) {
    size_t curr = 0;
    for (size_t s = 0; s < choice->sectionSizes.size(); s++) {
      auto sectionSize = choice->sectionSizes[s];
      for (size_t i = 0; i < sectionSize; i++) {
        indexes[choice->order[curr++]] += s;
      }
    }
    assert(curr == size);
  }

  Choice* makeMixture(Choice* left, Choice* right) {
    auto* ret = new Choice();
    // Ideally, we should mix using the distance between each pair of functions, as
    // what really matters here is which functions end up together. However, that
    // would be quadratic. Instead, approximate by averaging section indexes.
    std::vector<size_t> merged; // function index => section index
    merged.resize(size);
    addSectionIndexes(left, merged);
    addSectionIndexes(right, merged);
    std::vector<std::vector<size_t>> sectionIndexes; // index => list of all functions in that section
    sectionIndexes.resize(std::max(left->sectionSizes.size(), right->sectionSizes.size()));
    // use the order from one of them. TODO: perhaps we should use both?
    auto* mixer = rand() & 1 ? left : right;
    for (size_t i = 0; i < size; i++) {
      auto functionIndex = mixer->order[i];
      auto sectionIndex = merged[functionIndex] /= 2; // really silly, but at least keeps functions together that were together
      sectionIndexes[sectionIndex].push_back(functionIndex);
    }
    // write out the sections and order
    for (auto& indexes : sectionIndexes) {
      if (indexes.size() == 0) continue; // don't emit empty sections
      for (size_t i : indexes) {
        ret->order.push_back(i);
      }
      ret->sectionSizes.push_back(indexes.size());
    }
    calcFitness(*ret);
    return ret;
  }

private:
  Module& wasm;
  size_t size;
  bool debug;

  void calcFitness(Choice& choice) {
    //choice.dump();
    choice.verify();
    // generate a wasm binary with the specified choice, the size indicates the fitness
    BufferWithRandomAccess buffer(debug);
    generateOptimizedBinary(wasm, buffer, choice, debug);
    choice.setFitness(-buffer.size()); // more is better in fitness
  }
};

void generateOptimizedBinaryUsingLearning(Module& wasm, BufferWithRandomAccess& buffer, bool debug) {
  {
    // emit a baseline
    BufferWithRandomAccess buffer(debug);
    std::vector<size_t> functionSectionSizes;
    WasmBinaryWriter writer(&wasm, buffer, functionSectionSizes, debug);
    writer.write();
    std::cerr << "unoptimzied size: " << buffer.size() << '\n';
  }
  {
    // emit a baseline opt
    BufferWithRandomAccess buffer(debug);
    Choice choice;
    for (size_t i = 0; i < wasm.functions.size(); i++) {
      choice.order.push_back(i);
    }
    choice.sectionSizes.push_back(wasm.functions.size());
    generateOptimizedBinary(wasm, buffer, choice, false);
    std::cerr << "optimized with just one function section / one opcoe table: " << buffer.size() << '\n';
  }

  Generator generator(wasm, debug);
  GeneticLearner<Choice, int32_t, Generator> learner(generator, 100);
  size_t i = 0;
  std::cerr << "*: top fitness: " << -learner.getBest()->getFitness() << " [" << learner.getBest()->sectionSizes.size() << " sections]\n";
  while (1) {
    learner.runGeneration();
    std::cerr << (i++) << ": top fitness: " << -learner.getBest()->getFitness() << " [" << learner.getBest()->sectionSizes.size() << " sections]\n";
  }
}

// Optimize using just opcode table, no learning. Uses a reasonable choice of opt options.

void generateOptimizedBinary(Module& wasm, BufferWithRandomAccess& buffer, bool debug) {
  size_t num = wasm.functions.size();
  Choice choice;
  // unchanged order
  for (size_t i = 0; i < num; i++) {
    choice.order.push_back(i);
  }
  // reasonably large chunks
  const size_t chunk = 100;
  while (num > chunk) {
    choice.sectionSizes.push_back(chunk);
    num -= chunk;
  }
  choice.sectionSizes.push_back(num);
  // generate using that choice
  generateOptimizedBinary(wasm, buffer, choice, debug);
}

// main

int main(int argc, const char *argv[]) {
  Options options("wasm-as", "Assemble a .wast (WebAssembly text format) into a .wasm (WebAssembly binary format)");
  options.add("--output", "-o", "Output file (stdout if not specified)",
              Options::Arguments::One,
              [](Options *o, const std::string &argument) {
                o->extra["output"] = argument;
                Colors::disable();
              })
      .add("--optimize", "-O", "Optimize output using opcode table",
           Options::Arguments::Zero,
           [](Options *o, const std::string &argument) {
             o->extra["optimize"] = "yes";
           })
      .add_positional("INFILE", Options::Arguments::One,
                      [](Options *o, const std::string &argument) {
                        o->extra["infile"] = argument;
                      });
  options.parse(argc, argv);

  auto input(read_file<std::string>(options.extra["infile"], Flags::Text, options.debug ? Flags::Debug : Flags::Release));

  if (options.debug) std::cerr << "s-parsing..." << std::endl;
  SExpressionParser parser(const_cast<char*>(input.c_str()));
  Element& root = *parser.root;

  if (options.debug) std::cerr << "w-parsing..." << std::endl;
  Module wasm;
  SExpressionWasmBuilder builder(wasm, *root[0], [&]() { abort(); });

  if (options.debug) std::cerr << "binarification..." << std::endl;
  BufferWithRandomAccess buffer(options.debug);
  if (options.extra.count("optimize") == 0) {
    std::vector<size_t> functionSectionSizes;
    WasmBinaryWriter writer(&wasm, buffer, functionSectionSizes, options.debug);
    writer.write();
  } else {
    generateOptimizedBinaryUsingLearning(wasm, buffer, options.debug);
  }

  if (options.debug) std::cerr << "writing to output..." << std::endl;
  Output output(options.extra["output"], Flags::Binary, options.debug ? Flags::Debug : Flags::Release);
  buffer.writeTo(output);

  if (options.debug) std::cerr << "Done." << std::endl;
}
