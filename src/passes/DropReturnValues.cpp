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
// Stops using return values nodes that don't allow them. This converts
// a module from before we had drop and tee into after.
//

#include <wasm.h>
#include <pass.h>
#include <ast_utils.h>
#include <wasm-builder.h>

namespace wasm {

struct DropReturnValues : public WalkerPass<PostWalker<DropReturnValues, Visitor<DropReturnValues>>> {
  bool isFunctionParallel() override { return true; }

  Pass* create() override { return new DropReturnValues; }

  std::vector<Expression*> expressionStack;

  void maybeDrop(Expression* curr) {
    if (isConcreteWasmType(curr->type) && !ExpressionAnalyzer::isResultUsed(expressionStack, getFunction())) {
      replaceCurrent(Builder(*getModule()).makeDrop(curr));
    }
  }

  void visitBlock(Block *curr) {
    maybeDrop(curr);
  }
  void visitIf(If *curr) {
    maybeDrop(curr);
  }
  void visitLoop(Loop *curr) {
    maybeDrop(curr);
  }
  // TODO? void visitBreak(Break *curr) {}
  void visitCall(Call *curr) {
    maybeDrop(curr);
  }
  void visitCallImport(CallImport *curr) {
    maybeDrop(curr);
  }
  void visitCallIndirect(CallIndirect *curr) {
    maybeDrop(curr);
  }
  void visitGetLocal(GetLocal *curr) {
    maybeDrop(curr);
  }
  void visitSetLocal(SetLocal* curr) {
    if (curr->isTee() && !ExpressionAnalyzer::isResultUsed(expressionStack, getFunction())) {
      curr->setTee(false); // this is not a tee
    }
  }
  void visitLoad(Load *curr) {
    maybeDrop(curr);
  }
  void visitStore(Store* curr) {
    // if a store returns a value, we need to copy it to a local
    if (ExpressionAnalyzer::isResultUsed(expressionStack, getFunction())) {
      Index index = getFunction()->getNumLocals();
      getFunction()->vars.emplace_back(curr->type);
      Builder builder(*getModule());
      replaceCurrent(builder.makeSequence(
        builder.makeSequence(
          builder.makeSetLocal(index, curr->value),
          curr
        ),
        builder.makeGetLocal(index, curr->type)
      ));
      curr->value = builder.makeGetLocal(index, curr->type);
    }
  }
  void visitConst(Const *curr) {
    maybeDrop(curr);
  }
  void visitUnary(Unary *curr) {
    maybeDrop(curr);
  }
  void visitBinary(Binary *curr) {
    maybeDrop(curr);
  }
  void visitSelect(Select *curr) {
    maybeDrop(curr);
  }
  void visitHost(Host *curr) {
    maybeDrop(curr);
  }

  static void visitPre(DropReturnValues* self, Expression** currp) {
    self->expressionStack.push_back(*currp);
  }

  static void visitPost(DropReturnValues* self, Expression** currp) {
    self->expressionStack.pop_back();
  }

  static void scan(DropReturnValues* self, Expression** currp) {
    self->pushTask(visitPost, currp);

    WalkerPass<PostWalker<DropReturnValues, Visitor<DropReturnValues>>>::scan(self, currp);

    self->pushTask(visitPre, currp);
  }
};

static RegisterPass<DropReturnValues> registerPass("drop-return-values", "convert code to use drop and tee");

} // namespace wasm

