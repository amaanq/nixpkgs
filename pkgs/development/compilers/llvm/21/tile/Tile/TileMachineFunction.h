//===-- TileMachineFunctionInfo.h - Private data used for Tile --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the Tile specific subclass of MachineFunctionInfo.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_TILE_TILEMACHINEFUNCTION_H
#define LLVM_LIB_TARGET_TILE_TILEMACHINEFUNCTION_H

#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"

namespace llvm {

class Function;
class TargetSubtargetInfo;

class TileFunctionInfo : public MachineFunctionInfo {
  virtual void anchor();

  Register SRetReturnReg;
  Register GlobalBaseReg;
  Register LinkReg;
  int VarArgsFrameIndex = 0;
  int VarArgsResSlot = 0;
  mutable int DynAllocFI = 0;
  unsigned MaxCallFrameSize = 0;

public:
  TileFunctionInfo(const Function &F, const TargetSubtargetInfo *STI) {}

  MachineFunctionInfo *
  clone(BumpPtrAllocator &Allocator, MachineFunction &DestMF,
        const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB)
      const override;

  // The first call to this function creates a frame object for the dynamically
  // allocated stack area.
  int getDynAllocFI(MachineFunction &MF) const {
    if (!DynAllocFI)
      DynAllocFI = MF.getFrameInfo().CreateFixedObject(8, 0, true);
    return DynAllocFI;
  }
  bool isDynAllocFI(int FI) const { return DynAllocFI && DynAllocFI == FI; }

  Register getSRetReturnReg() const { return SRetReturnReg; }
  void setSRetReturnReg(Register Reg) { SRetReturnReg = Reg; }

  bool globalBaseRegFixed() const;
  bool globalBaseRegSet() const;
  Register getGlobalBaseReg();
  bool linkRegFixed() const;
  bool linkRegSet() const;
  Register getLinkReg();

  int getVarArgsFrameIndex() const { return VarArgsFrameIndex; }
  void setVarArgsFrameIndex(int Index) { VarArgsFrameIndex = Index; }
  int getVarArgsResSlot() const { return VarArgsResSlot; }
  void setVarArgsResSlot(int Index) { VarArgsResSlot = Index; }
  unsigned getMaxCallFrameSize() const { return MaxCallFrameSize; }
  void setMaxCallFrameSize(unsigned S) { MaxCallFrameSize = S; }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_TILE_TILEMACHINEFUNCTION_H
