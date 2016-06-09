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
    curr->finalize(); // changes may have occured in our children
    maybeDrop(curr);
  }
  void visitIf(If *curr) {
    curr->finalize();
    maybeDrop(curr);
  }
  void visitLoop(Loop *curr) {
    curr->finalize();
    maybeDrop(curr);
  }
  void visitBreak(Break *curr) {
    if (!curr->value) return;
    // we may use a block return value, and send values to it using breaks, but the block return
    // value might be ignored. In that case, we'll drop() the block fallthrough, but we also
    // need to not use block return values, as they will not match the lack of a fallthrough
    auto check = [&](int i) {
      // i is the index of a block or loop. we need to see if it is used. if it is not,
      // we must drop our value
      auto smallStack = expressionStack;
      smallStack.resize(i + 1);
      if (!ExpressionAnalyzer::isResultUsed(expressionStack, getFunction())) {
        // drop the value. but, it may have a side effect!
        replaceCurrent(Builder(*getModule()).makeSequence(
          Builder(*getModule()).makeDrop(curr->value), // value is first in order of operations, so just pull it out
          curr
        ));
        curr->value = nullptr;
      }
    };
    for (int i = int(expressionStack.size()) - 1; i >= 0; i--) {
      if (auto* block = expressionStack[i]->dynCast<Block>()) {
        if (block->name == curr->name) {
          check(i);
          break;
        }
      } else if (auto* loop = expressionStack[i]->dynCast<Loop>()) {
        if (loop->in == curr->name) break;
        if (loop->out == curr->name) {
          check(i);
          break;
        }
      }
    }
  }
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
    curr->type = none; // TODO: use in wasm.h
    // if a store returns a value, we need to copy it to a local
    if (ExpressionAnalyzer::isResultUsed(expressionStack, getFunction())) {
      Index index = getFunction()->getNumLocals();
      getFunction()->vars.emplace_back(curr->value->type);
      Builder builder(*getModule());
      replaceCurrent(builder.makeSequence(
        builder.makeSequence(
          builder.makeSetLocal(index, curr->value),
          curr
        ),
        builder.makeGetLocal(index, curr->value->type)
      ));
      curr->value = builder.makeGetLocal(index, curr->value->type);
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

