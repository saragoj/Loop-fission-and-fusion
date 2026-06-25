#include "llvm/Pass.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include "llvm/Support/raw_ostream.h"
#include <unordered_set>
#include <unordered_map>

using namespace llvm;

namespace {
struct OurLoopFissionPass : public LoopPass {
  static char ID;
  std::vector<BasicBlock *> LoopBasicBlocks;  //kupimo sve basic blokove iz nase petlje

  OurLoopFissionPass() : LoopPass(ID) {}


  BasicBlock *copyLoop(Loop *L){
    BasicBlock *Exit = L->getExitBlock();
    std::vector<BasicBlock *> LoopBasicBlocksCopy;
    std::unordered_map<Value *, Value *> Mapping;
    std::unordered_map<BasicBlock *, BasicBlock*> BasicBlocksMapping;
    std::unordered_set<BasicBlock *> BlocksToDelete;
    Instruction *Clone;
    IRBuilder<> Builder(Exit->getContext());

    BasicBlock *NewBasicBlock;
    for(BasicBlock *BB: LoopBasicBlocks){
        NewBasicBlock = BasicBlock::Create(Exit->getContext(), "", Exit->getParent(), Exit);
        LoopBasicBlocksCopy.push_back(NewBasicBlock);
        BasicBlocksMapping[BB] = NewBasicBlock;
    }

    for(BasicBlock *BB: LoopBasicBlocks){
        NewBasicBlock = BasicBlocksMapping[BB];
        Builder.SetInsertPoint(NewBasicBlock);
        for(Instruction &I : *BB){
            Clone = I.clone();
            Mapping[&I] = Clone;
            Builder.Insert(Clone);

            for (size_t i = 0 ; i< Clone -> getNumOperands(); i++){
                if(Mapping.find(Clone-> getOperand(i)) != Mapping.end()){
                    Clone -> setOperand(i, Mapping[Clone->getOperand(i)]);
                }
            }
        }
    }
    for(BasicBlock *BB: LoopBasicBlocksCopy){
        for(size_t i = 0; i < BB->getTerminator()->getNumSuccessors(); i++){
            BasicBlock *Successor = BB->getTerminator()->getSuccessor(i);
            if(BasicBlocksMapping.find(Successor)!= BasicBlocksMapping.end()){
                BB->getTerminator()->setSuccessor(i,BasicBlocksMapping[Successor]);
            }
        }
    }

    BasicBlock *BlockToStart = findIfBasicBlock(LoopBasicBlocksCopy, true);
    BasicBlock *BlockToStop = findIfBasicBlock(LoopBasicBlocksCopy, false);
    deleteAllBlocksFrom(BlockToStart, BlockToStop, BlocksToDelete);
    LoopBasicBlocksCopy.front()->getTerminator()->setSuccessor(0, BlockToStop);

    for(BasicBlock *BB: BlocksToDelete){
        BB->eraseFromParent();
    }

    return LoopBasicBlocksCopy.front();
  }
  
  void loopFission(Loop *L){
    BasicBlock *LoopCopy = copyLoop(L);
    LoopBasicBlocks.front()-> getTerminator()-> setSuccessor(1, LoopCopy); // Stavljamo da se sa prve petlje skace na drugu
  }

  BasicBlock *findIfBasicBlock(std::vector<BasicBlock *> &LoopBasicBlocks, bool findFirst){

    BasicBlock *LastBranchBlock = nullptr;
    // imamo if ako imamo compare ostim headera pa njega preskacemo
    for(size_t i = 1; i < LoopBasicBlocks.size(); i++){
        for(Instruction &I: *LoopBasicBlocks[i]){
            if(isa<ICmpInst>(&I)){
                LastBranchBlock = LoopBasicBlocks[i];
                if(findFirst){
                    return LoopBasicBlocks[i];
                }
                LastBranchBlock = LoopBasicBlocks[i];
            }    
        }
    }

    return LastBranchBlock;
  }

  void deleteAllBlocksFrom(BasicBlock *Current, BasicBlock *BlockToStop, std::unordered_set<BasicBlock *> &BlocksToDelete){ 
    
    if(Current == BlockToStop){
        return;
    }

    BlocksToDelete.insert(Current);

    for(size_t i = 0; i < Current->getTerminator()->getNumSuccessors(); i++){
        BasicBlock *Successor = Current->getTerminator()->getSuccessor(i);
        if(BlocksToDelete.find(Successor) == BlocksToDelete.end() && Successor != BlockToStop){
            deleteAllBlocksFrom(Successor, BlockToStop, BlocksToDelete);
        }
    }
  }

  bool runOnLoop(Loop *L, LPPassManager &LPM) override {
    LoopBasicBlocks = L->getBlocksVector();
    loopFission(L);
    BasicBlock *BranchBlock = findIfBasicBlock(LoopBasicBlocks, true);
    BranchInst *Branch = dyn_cast<BranchInst>(BranchBlock->getTerminator()->getSuccessor(1)->getTerminator());
    bool isConditional = Branch->isConditional();

    std::unordered_set<BasicBlock *> BlocksToDelete;

    if (!isConditional){ //imamo else granu
        deleteAllBlocksFrom(Branch->getSuccessor(0), L->getLoopLatch(), BlocksToDelete);
        // true basic block preusmerava na loop latch
        BranchBlock->getTerminator()->getSuccessor(0)->getTerminator()->setSuccessor(0, L->getLoopLatch());
        // false basic block preusmerava na loop latch
        BranchBlock->getTerminator()->getSuccessor(1)->getTerminator()->setSuccessor(0, L->getLoopLatch());
    }else{ // nemamo else granu
        //krecemo brisanje od drugogo if grana koji je zapravo prvi sucesor BranchBlock-a
        deleteAllBlocksFrom(BranchBlock->getTerminator()->getSuccessor(1), L->getLoopLatch(), BlocksToDelete);
        //ako uslov u if grani nije zadovoljen, preusmeravamo na loop latch
        BranchBlock->getTerminator()->setSuccessor(1, L->getLoopLatch());
        //ako je uslov u if grani zadovoljen, nakon tru bloka preusmeravamo na loop latch
        BranchBlock->getTerminator()->getSuccessor(0)->getTerminator()->setSuccessor(0, L->getLoopLatch());
    }



    for (BasicBlock *BB : BlocksToDelete) {
        BB->eraseFromParent();
    }
    
    return true;
  }
};
} // end anonymous namespace

char OurLoopFissionPass::ID = 0;
static RegisterPass<OurLoopFissionPass> Z("our-loop-fission", "Our Loop Fission Pass", false, false);