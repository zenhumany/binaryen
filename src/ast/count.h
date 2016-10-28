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

#ifndef wasm_ast_count_h
#define wasm_ast_count_h

namespace wasm {

struct GetLocalCounter : public PostWalker<GetLocalCounter, Visitor<GetLocalCounter>> {
  std::vector<Index> numGets;

  GetLocalCounter() {}
  GetLocalCounter(Function* func) {
    analyze(func, func->body);
  }
  GetLocalCounter(Function* func, Expression* ast) {
    analyze(func, ast);
  }

  void analyze(Function* func, Expression* ast) {
    numGets.resize(func->getNumLocals());
    std::fill(numGets.begin(), numGets.end(), 0);
    walk(ast);
  }

  void visitGetLocal(GetLocal *curr) {
    numGets[curr->index]++;
  }
};

//
// Analyzers some useful local properties: # of sets and gets, and SFA.
//
// Single First Assignment (SFA) form: the local has a single set_local, is
// not a parameter, and has no get_locals before the set_local in postorder.
// This is a much weaker property than SSA, obviously, but together with
// our implicit dominance properties in the structured AST is quite useful.
//
struct LocalAnalyzer : public PostWalker<LocalAnalyzer, Visitor<LocalAnalyzer>> {
  std::vector<bool> sfa;
  std::vector<Index> numSets;
  std::vector<Index> numGets;

  LocalAnalyzer() {}
  LocalAnalyzer(Function* func) {
    analyze(func);
  }

  void analyze(Function* func) {
    auto num = func->getNumLocals();
    numSets.resize(num);
    std::fill(numSets.begin(), numSets.end(), 0);
    numGets.resize(num);
    std::fill(numGets.begin(), numGets.end(), 0);
    sfa.resize(num);
    std::fill(sfa.begin(), sfa.begin() + func->getNumParams(), false);
    std::fill(sfa.begin() + func->getNumParams(), sfa.end(), true);
    walk(func->body);
    for (Index i = 0; i < num; i++) {
      if (numSets[i] == 0) sfa[i] = false;
    }
  }

  bool isSFA(Index i) {
    return sfa[i];
  }

  Index getNumGets(Index i) {
    return numGets[i];
  }

  void visitGetLocal(GetLocal *curr) {
    if (numSets[curr->index] == 0) {
      sfa[curr->index] = false;
    }
    numGets[curr->index]++;
  }

  void visitSetLocal(SetLocal *curr) {
    numSets[curr->index]++;
    if (numSets[curr->index] > 1) {
      sfa[curr->index] = false;
    }
  }
};

} // namespace wasm

#endif // wasm_ast_count_h

