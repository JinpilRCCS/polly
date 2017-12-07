//===--- SPDIR.h - SPD Intermediate Representation --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// SPD Intermediate Representation
//
//===----------------------------------------------------------------------===//

#include "isl/map.h"
#include "isl/set.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Value.h"
#include "polly/ScopInfo.h"
#include "polly/CodeGen/SPDIR.h"
#include "polly/CodeGen/SPDPrinter.h"
#include <vector>

// FIXME for test
#include "isl/id.h"
#include <iostream>
#include <cstdio>

using namespace llvm;
using namespace polly;

SPDInstr *SPDInstr::get(Instruction *I,
                        const ScopStmt *Stmt, SPDIR *IR) {
  if (I->mayReadOrWriteMemory()) {
    MemoryAccess *MA = Stmt->getArrayAccessOrNULLFor(I);
    unsigned Num = MA->getNumSubscripts();
    for (unsigned i = 0; i < Num; i++) {
      const SCEVAddRecExpr *SExpr
        = dyn_cast<SCEVAddRecExpr>(MA->getSubscript(i));
      assert(SExpr->isAffine() &&
             "array subscripts should be expressed by an affine function");

// FIXME current impl only allows {0,+,1}<loop>
/*
      const SCEV *StartExpr = SExpr->getStart();
      assert(StartExpr->isZero() && "FIXME: currently StartExpr should be 0");
      const SCEV *StepExpr = SExpr->getStepRecurrence(*(Stmt->getParent()->getSE()));
      assert(StepExpr->isOne() && "FIXME: currently StepExpr should be 1");
*/
    }

    return new SPDInstr(I, Stmt, IR);
  }

  switch (I->getOpcode()) {
  default:
    return nullptr;
  case Instruction::Add:
  case Instruction::FAdd:
  case Instruction::Sub:
  case Instruction::FSub:
  case Instruction::Mul:
  case Instruction::FMul:
  case Instruction::UDiv:
  case Instruction::SDiv:
  case Instruction::FDiv:
    return new SPDInstr(I, Stmt, IR);
  }

  return nullptr;
}

bool SPDInstr::equal(Instruction *I) const {
  return LLVMInstr == I;
}

bool SPDInstr::isDeadInstr() const {
  if (LLVMInstr->mayWriteToMemory()) {
    return false;
  }

  for (auto UI = LLVMInstr->use_begin(),
       UE = LLVMInstr->use_end(); UI != UE;) {
    const Use *U = &*UI;
    ++UI;
    Instruction *UserInstr = dyn_cast<Instruction>(U->getUser());
    if (ParentIR->has(UserInstr)) {
      return false;
    }
  }

  return true;
}

void SPDInstr::dump() const {
  LLVMInstr->dump();
}

SPDIR::SPDIR(const Scop &S) {
  for (const ScopStmt &Stmt : S) {
    assert(Stmt.isBlockStmt() && "ScopStmt is not a BlockStmt\n");
    BasicBlock *BB = Stmt.getBasicBlock();

    isl_set *DomainSet = isl_set_reset_tuple_id(Stmt.getDomain());
    isl_set *StreamSet = isl_set_empty(isl_set_get_space(DomainSet));
    for (BasicBlock::iterator IIB = BB->begin(), IIE = BB->end();
         IIB != IIE; ++IIB) {
      Instruction &I = *IIB;
      if (I.mayReadOrWriteMemory()) {
        MemoryAccess *MA = Stmt.getArrayAccessOrNULLFor(&I);
        assert(MA != nullptr && "cannot find a MemoryAccess");

        isl_map *MAMap = MA->getLatestAccessRelation();
        MAMap = isl_map_reset_tuple_id(MAMap, isl_dim_in);
        MAMap = isl_map_reset_tuple_id(MAMap, isl_dim_out);

        isl_set *TempSet = isl_set_empty(isl_set_get_space(DomainSet));
        TempSet = isl_set_union(TempSet, isl_set_copy(DomainSet));
        TempSet = isl_set_apply(TempSet, isl_map_copy(MAMap));
        StreamSet = isl_set_union(StreamSet, isl_set_copy(TempSet));
        StreamSet = isl_set_remove_redundancies(StreamSet);

        isl_set_free(TempSet);
        isl_map_free(MAMap);
      }

      SPDInstr *NewInstr = SPDInstr::get(&I, &Stmt, this);
      if (NewInstr != nullptr) {
        InstrList.push_back(NewInstr);
      }
    }

    printf("ACC_RANGE: %s\n", isl_set_to_str(StreamSet));
    isl_set_free(StreamSet);
    isl_set_free(DomainSet);
  }

  removeDeadInstrs();
}

void SPDIR::removeDeadInstrs() {
  bool ContinueRemove = false;
  do {
    ContinueRemove = false;
    for (auto Iter = InstrList.begin(), IterEnd = InstrList.end();
         Iter != IterEnd; ++Iter) {
      SPDInstr *I = *Iter;
      if (I->isDeadInstr()) {
        InstrList.erase(Iter);
        ContinueRemove = true;
        break;
      }
    }
  } while (ContinueRemove);
}

bool SPDIR::has(Instruction *TargetInstr) const {
  for (SPDInstr *I : InstrList) {
    if (I->equal(TargetInstr)) {
      return true;
    }
  }

  return false;
}

void SPDIR::dump() const {
  std::cerr << "SPDIR::dump() ---------------------------\n";
  for (SPDInstr *I : InstrList) {
    I->dump();
  }
}
