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
// wasm => C console tool
//

#include "support/colors.h"
#include "support/command-line.h"
#include "support/file.h"

#include "wasm.h"
#include "wasm-io.h"
#include "wasm-traversal.h"
#include "pass.h"


struct CEmitter : public Visitor<CEmitter> {
  void emit(Module& wasm) {
    setModule(wasm);
    for (auto& func : wasm.functions) {
      std::cout << getCTypeName(func->result) << ' ' << func->name << '(';
      for (auto param : func->params) {
        std::cout << getCTypeName(param->type) << ' ' << param->name;
      }
      std::cout << ") {\n";
      indent++;
      doIndent();
      visitInsideBraces(func->body);
      indent--;
      std::cout << "\n}\n";
    }
  }

  // state and helpers

  size_t indent = 0;

  void doIndent() {
    for (size_t i = 0; i < indent; i++) {
      std::cout << "  ";
    }
  }

  void doNewlineIndent() {
    std:: cout << '\n';
    doIndent();
  }

  // special visitors

  // visit under the assumption we are the single child of something that
  // created {, } braces, so we don't need to emit any
  void visitInsideBraces(Expression* curr) {
    if (auto* block = curr->dynCast<Block>()) {
      visitBlockList(block);
    } else {
      visit(curr);
    }
  }

  void visitBlockList(Block* block) {
    if (block->list.empty()) return;
    auto* last = block->list.back();
    for (auto* item : block->list) {
      visit(item);
      std::cout << ';';
      if (item != last){
        doNewlineIndent();
      }
    }
  }

  // expression visitors. each is reached where the text cursor is at the
  // right place to begin writing. if it needs new lines, it must indent them.
  // it should end on its last character, without a newline.

  void visitBlock(Block* curr) {
    std::cout << '{';
    indent++;
    doNewlineIndent();
    visitBlockList(curr);
    indent--;
    doNewlineIndent();
    std::cout << '}';
  }
  void visitIf(If* curr) {
    std::cout << "if (";
    visit(curr->condition);
    std::cout << ") {";
    indent++;
    doNewlineIndent();
    visitInsideBraces(curr->ifTrue);
    indent--;
    doNewlineIndent();
    std::cout << '}';
    if (curr->ifFalse) {
      std::cout << " else {";
      indent++;
      doNewlineIndent();
      visitInsideBraces(curr->ifFalse);
      indent--;
      doNewlineIndent();
      std::cout << '}';
    }
  }
  void visitLoop(Loop* curr) {
    std::cout << "do {";
    indent++;
    doNewlineIndent();
    visitInsideBraces(curr->body);
    indent--;
    doNewlineIndent();
    std::cout << "} while (0);";
  }
  void visitBreak(Break* curr) {
  }
  void visitSwitch(Switch* curr) {
  }
  void visitCall(Call* curr) {
  }
  void visitCallImport(CallImport* curr) {
  }
  void visitCallIndirect(CallIndirect* curr) {
  }
  void visitGetLocal(GetLocal* curr) {
  }
  void visitSetLocal(SetLocal* curr) {
  }
  void visitGetGlobal(GetGlobal* curr) {
  }
  void visitSetGlobal(SetGlobal* curr) {
  }
  void visitLoad(Load* curr) {
  }
  void visitStore(Store* curr) {
  }
  void visitAtomicRMW(AtomicRMW* curr) {
  }
  void visitAtomicCmpxchg(AtomicCmpxchg* curr) {
  }
  void visitAtomicWait(AtomicWait* curr) {
  }
  void visitAtomicWake(AtomicWake* curr) {
  }
  void visitConst(Const* curr) {
  }
  void visitUnary(Unary* curr) {
  }
  void visitBinary(Binary* curr) {
  }
  void visitSelect(Select* curr) {
  }
  void visitDrop(Drop* curr) {
  }
  void visitReturn(Return* curr) {
  }
  void visitHost(Host* curr) {
  }
  void visitNop(Nop* curr) {
  }
  void visitUnreachable(Unreachable* curr) {
  }
};

int main(int argc, const char *argv[]) {
  OptimizationOptions options("wasm2c", "Translate wasm to C");
  options
      .add_positional("INFILE", Options::Arguments::One,
                      [](Options* o, const std::string& argument) {
                        o->extra["infile"] = argument;
                      });
  options.parse(argc, argv);

  // read
  Module wasm;
  ModuleReader reader;
  try {
    reader.read(options.extra["infile"], wasm);
  } catch (ParseException& p) {
    p.dump(std::cerr);
    Fatal() << "error in parsing input";
  }

  // prepare
  PassRunner runner(wasm);
  runner.add("flatten");
  runner.add("simplify-locals-nostructure-notee");
  runner.run();

  // write
  CEmitter emitter;
  emitter.emit(wasm);
}
