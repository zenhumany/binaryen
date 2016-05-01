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
    generateOptimizedBinary(wasm, buffer, options.debug);
  }

  if (options.debug) std::cerr << "writing to output..." << std::endl;
  Output output(options.extra["output"], Flags::Binary, options.debug ? Flags::Debug : Flags::Release);
  buffer.writeTo(output);

  if (options.debug) std::cerr << "Done." << std::endl;
}
