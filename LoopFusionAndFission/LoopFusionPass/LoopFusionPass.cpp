#include "llvm/Pass.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/BasicBlock.h"
#include <unordered_set>
#include <unordered_map>
#include <vector>

using namespace llvm;

namespace {
struct OurLoopFusionPass : public FunctionPass {
  static char ID;
  std::vector<Loop *> LoopsInFunction;
  std::unordered_set<BasicBlock *> BlocksBetweenLoops;

  OurLoopFusionPass() : FunctionPass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<LoopInfoWrapperPass>();
  }

  // Prolazi kroz sve blokove funkcije i kupi top-level petlje redom.
  void collectLoops(Function &F, LoopInfo &LI) {
    LoopsInFunction.clear();

    for (BasicBlock &BB : F) {
      Loop *L = LI.getLoopFor(&BB);
      if (L && L->getHeader() == &BB && !L->getParentLoop()) {
        LoopsInFunction.push_back(L);
      }
    }
  }

  // Iz header-a petlje nalazimo brojac i granicu.
  AllocaInst *findLoopCounter(Loop *L, Value *&Bound) {
    for (Instruction &I : *L->getHeader()) {
      ICmpInst *Cmp = dyn_cast<ICmpInst>(&I);
      if (!Cmp)
        continue;

      LoadInst *Load = dyn_cast<LoadInst>(Cmp->getOperand(0));
      if (!Load)
        continue;

      AllocaInst *Counter = dyn_cast<AllocaInst>(Load->getPointerOperand());
      if (!Counter)
        continue;

      Bound = Cmp->getOperand(1);
      return Counter;
    }

    return nullptr;
  }

  // Nalazimo prvu konstantu koja je upisana u alloca.
  bool findInitConst(AllocaInst *A, int64_t &Val) {
    for (BasicBlock &BB : *A->getFunction()) {
      for (Instruction &I : BB) {
        StoreInst *Store = dyn_cast<StoreInst>(&I);
        if (!Store || Store->getPointerOperand() != A)
          continue;

        ConstantInt *CI = dyn_cast<ConstantInt>(Store->getValueOperand());
        if (!CI)
          continue;

        Val = CI->getSExtValue();
        return true;
      }
    }

    return false;
  }

  // Granice su iste ako su isti value ili load iz alloca sa istom konstantom.
  bool haveSameBound(Value *B1, Value *B2) {
    if (B1 == B2)
      return true;

    LoadInst *L1 = dyn_cast<LoadInst>(B1);
    LoadInst *L2 = dyn_cast<LoadInst>(B2);
    if (!L1 || !L2)
      return false;

    AllocaInst *A1 = dyn_cast<AllocaInst>(L1->getPointerOperand());
    AllocaInst *A2 = dyn_cast<AllocaInst>(L2->getPointerOperand());

    int64_t V1, V2;
    return A1 && A2 && findInitConst(A1, V1) &&
           findInitConst(A2, V2) && V1 == V2;
  }

  // DFS proverava da li od Current mozemo doci do Header-a druge petlje.
  bool canReachHeader(BasicBlock *Current, BasicBlock *Header,
                      std::unordered_set<BasicBlock *> &Visited) {
    if (Current == Header)
      return true;

    if (!Current || Visited.find(Current) != Visited.end())
      return false;

    Visited.insert(Current);

    for (size_t i = 0; i < Current->getTerminator()->getNumSuccessors(); i++) {
      BasicBlock *Successor = Current->getTerminator()->getSuccessor(i);
      if (canReachHeader(Successor, Header, Visited)) {
        BlocksBetweenLoops.insert(Current);
        return true;
      }
    }

    return false;
  }

  // Proveravamo da li prva petlja vodi u drugu, direktno ili kroz preheader.
  bool areAdjacent(Loop *L1, Loop *L2) {
    BasicBlock *Exit = L1->getExitBlock();
    if (!Exit)
      return false;

    BlocksBetweenLoops.clear();
    std::unordered_set<BasicBlock *> Visited;

    for (size_t i = 0; i < Exit->getTerminator()->getNumSuccessors(); i++) {
      BasicBlock *Successor = Exit->getTerminator()->getSuccessor(i);
      if (canReachHeader(Successor, L2->getHeader(), Visited)) {
        return true;
      }
    }

    return false;
  }

  // DFS skuplja blokove koje kasnije brisemo.
  void collectBlocksForDeletion(BasicBlock *Current, BasicBlock *BlockToStop,
                                std::unordered_set<BasicBlock *> &BlocksToDelete) {
    if (Current == BlockToStop ||
        BlocksToDelete.find(Current) != BlocksToDelete.end()) {
      return;
    }

    BlocksToDelete.insert(Current);

    for (size_t i = 0; i < Current->getTerminator()->getNumSuccessors(); i++) {
      collectBlocksForDeletion(Current->getTerminator()->getSuccessor(i),
                               BlockToStop, BlocksToDelete);
    }
  }

  // Kreiramo prazne blokove koji odgovaraju telu druge petlje.
  void cloneBlocks(Loop *L2, BasicBlock *InsertBefore,
                   std::unordered_map<BasicBlock *, BasicBlock *> &BasicBlockMapping) {
    std::vector<BasicBlock *> LoopBasicBlocks = L2->getBlocksVector();

    for (BasicBlock *BB : LoopBasicBlocks) {
      if (BB == L2->getHeader() || BB == L2->getLoopLatch())
        continue;

      BasicBlock *NewBasicBlock =
          BasicBlock::Create(BB->getContext(), "", BB->getParent(), InsertBefore);
      BasicBlockMapping[BB] = NewBasicBlock;
    }
  }

  // Kloniramo instrukcije iz starih blokova u nove blokove.
  void cloneInstructions(Loop *L2,
                         std::unordered_map<Value *, Value *> &ValueMapping,
                         std::unordered_map<BasicBlock *, BasicBlock *> &BasicBlockMapping) {
    std::vector<BasicBlock *> LoopBasicBlocks = L2->getBlocksVector();

    for (BasicBlock *BB : LoopBasicBlocks) {
      if (BasicBlockMapping.find(BB) == BasicBlockMapping.end())
        continue;

      BasicBlock *NewBasicBlock = BasicBlockMapping[BB];
      IRBuilder<> Builder(NewBasicBlock);

      for (Instruction &I : *BB) {
        Instruction *Clone = I.clone();
        ValueMapping[&I] = Clone;
        Builder.Insert(Clone);
      }
    }
  }

  // Remapiramo operande kloniranih instrukcija.
  void remapOperands(std::unordered_map<Value *, Value *> &ValueMapping,
                     std::unordered_map<BasicBlock *, BasicBlock *> &BasicBlockMapping) {
    for (auto &Pair : BasicBlockMapping) {
      BasicBlock *NewBasicBlock = Pair.second;

      for (Instruction &I : *NewBasicBlock) {
        for (size_t i = 0; i < I.getNumOperands(); i++) {
          if (ValueMapping.find(I.getOperand(i)) != ValueMapping.end()) {
            I.setOperand(i, ValueMapping[I.getOperand(i)]);
          }
        }
      }
    }
  }

  // Remapiramo grane izmedju kloniranih basic blokova.
  void remapCFG(std::unordered_map<BasicBlock *, BasicBlock *> &BasicBlockMapping) {
    for (auto &Pair : BasicBlockMapping) {
      BasicBlock *NewBasicBlock = Pair.second;

      for (size_t i = 0;
           i < NewBasicBlock->getTerminator()->getNumSuccessors(); i++) {
        BasicBlock *Successor = NewBasicBlock->getTerminator()->getSuccessor(i);
        if (BasicBlockMapping.find(Successor) != BasicBlockMapping.end()) {
          NewBasicBlock->getTerminator()->setSuccessor(
              i, BasicBlockMapping[Successor]);
        }
      }
    }
  }

  // Kopiramo telo druge petlje i remapiramo njen brojac na brojac prve.
  BasicBlock *copyLoop(Loop *L2, BasicBlock *InsertBefore,
                       AllocaInst *Counter1, AllocaInst *Counter2,
                       std::unordered_map<BasicBlock *, BasicBlock *> &BasicBlockMapping) {
    std::unordered_map<Value *, Value *> ValueMapping;
    ValueMapping[Counter2] = Counter1;

    cloneBlocks(L2, InsertBefore, BasicBlockMapping);
    cloneInstructions(L2, ValueMapping, BasicBlockMapping);
    remapOperands(ValueMapping, BasicBlockMapping);
    remapCFG(BasicBlockMapping);

    for (size_t i = 0;
         i < L2->getHeader()->getTerminator()->getNumSuccessors(); i++) {
      BasicBlock *Successor = L2->getHeader()->getTerminator()->getSuccessor(i);
      if (BasicBlockMapping.find(Successor) != BasicBlockMapping.end()) {
        return BasicBlockMapping[Successor];
      }
    }

    return nullptr;
  }

  // Prevezujemo CFG tako da telo prve petlje vodi u kopiju tela druge petlje.
  void redirectCFG(Loop *L1, Loop *L2, BasicBlock *LoopCopy,
                   BasicBlock *L1Latch, BasicBlock *L1Exit, BasicBlock *L2Exit,
                   std::unordered_map<BasicBlock *, BasicBlock *> &BasicBlockMapping) {
    for (BasicBlock *BB : L1->getBlocksVector()) {
      if (BB == L1Latch)
        continue;

      for (size_t i = 0; i < BB->getTerminator()->getNumSuccessors(); i++) {
        if (BB->getTerminator()->getSuccessor(i) == L1Latch) {
          BB->getTerminator()->setSuccessor(i, LoopCopy);
        }
      }
    }

    for (auto &Pair : BasicBlockMapping) {
      BasicBlock *OldBasicBlock = Pair.first;
      BasicBlock *NewBasicBlock = Pair.second;

      for (size_t i = 0;
           i < NewBasicBlock->getTerminator()->getNumSuccessors(); i++) {
        BasicBlock *OldSuccessor = OldBasicBlock->getTerminator()->getSuccessor(i);
        if (BasicBlockMapping.find(OldSuccessor) == BasicBlockMapping.end()) {
          NewBasicBlock->getTerminator()->setSuccessor(i, L1Latch);
        }
      }
    }

    for (size_t i = 0; i < L1Exit->getTerminator()->getNumSuccessors(); i++) {
      BasicBlock *Successor = L1Exit->getTerminator()->getSuccessor(i);
      if (Successor == L2->getHeader() ||
          BlocksBetweenLoops.find(Successor) != BlocksBetweenLoops.end()) {
        L1Exit->getTerminator()->setSuccessor(i, L2Exit);
      }
    }
  }

  // Brisanje stare druge petlje je izdvojeno da loopFusion ostane pregledan.
  void deleteOldLoop(Loop *L2, BasicBlock *L2Exit) {
    std::unordered_set<BasicBlock *> BlocksToDelete;
    collectBlocksForDeletion(L2->getHeader(), L2Exit, BlocksToDelete);
    BlocksToDelete.insert(BlocksBetweenLoops.begin(), BlocksBetweenLoops.end());

    for (BasicBlock *BB : BlocksToDelete) {
      BB->dropAllReferences();
    }

    for (BasicBlock *BB : BlocksToDelete) {
      BB->eraseFromParent();
    }
  }

  // Spajamo drugu petlju u prvu: copy, redirect, delete.
  bool loopFusion(Loop *L1, Loop *L2, AllocaInst *Counter1,
                  AllocaInst *Counter2) {
    BasicBlock *L1Latch = L1->getLoopLatch();
    BasicBlock *L1Exit = L1->getExitBlock();
    BasicBlock *L2Exit = L2->getExitBlock();

    if (!L1Latch || !L1Exit || !L2Exit)
      return false;

    std::unordered_map<BasicBlock *, BasicBlock *> BasicBlockMapping;
    BasicBlock *LoopCopy =
        copyLoop(L2, L1Exit, Counter1, Counter2, BasicBlockMapping);

    if (!LoopCopy)
      return false;

    redirectCFG(L1, L2, LoopCopy, L1Latch, L1Exit, L2Exit, BasicBlockMapping);
    deleteOldLoop(L2, L2Exit);

    return true;
  }

  // Glavni prolaz trazi susedne kompatibilne petlje i spaja prvi par.
  bool runOnFunction(Function &F) override {
    LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
    collectLoops(F, LI);

    for (size_t i = 0; i + 1 < LoopsInFunction.size(); i++) {
      Loop *L1 = LoopsInFunction[i];
      Loop *L2 = LoopsInFunction[i + 1];
      Value *Bound1 = nullptr, *Bound2 = nullptr;
      AllocaInst *Counter1 = findLoopCounter(L1, Bound1);
      AllocaInst *Counter2 = findLoopCounter(L2, Bound2);
      int64_t Start1, Start2;

      if (!Counter1 || !Counter2)
        continue;
      if (!haveSameBound(Bound1, Bound2))
        continue;
      if (!findInitConst(Counter1, Start1) ||
          !findInitConst(Counter2, Start2))
        continue;
      if (Start1 != Start2)
        continue;
      if (!areAdjacent(L1, L2))
        continue;

      return loopFusion(L1, L2, Counter1, Counter2);
    }

    return false;
  }
};
} // end anonymous namespace

char OurLoopFusionPass::ID = 0;
static RegisterPass<OurLoopFusionPass> X("our-loop-fusion",
                                         "Our Loop Fusion Pass", false, false);
