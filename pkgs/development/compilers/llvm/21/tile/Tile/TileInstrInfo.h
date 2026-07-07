//===-- TileInstrInfo.h - Tile Instruction Information ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the Tile implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_TILE_TILEINSTRINFO_H
#define LLVM_LIB_TARGET_TILE_TILEINSTRINFO_H

#include "TileRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

#define GET_INSTRINFO_HEADER
#include "TileGenInstrInfo.inc"

namespace llvm {

class TileSubtarget;

namespace Tile {
// Return the inverse of the specified opcode, e.g. turning BEQ to BNE.
unsigned GetOppositeBranchOpc(unsigned Opc);
} // namespace Tile

class TileInstrInfo : public TileGenInstrInfo {
  const TileRegisterInfo RI;
  unsigned UncondBrOpc;

public:
  explicit TileInstrInfo(const TileSubtarget &ST);

  const TileRegisterInfo &getRegisterInfo() const { return RI; }

  bool analyzeBranch(MachineBasicBlock &MBB, MachineBasicBlock *&TBB,
                     MachineBasicBlock *&FBB,
                     SmallVectorImpl<MachineOperand> &Cond,
                     bool AllowModify = false) const override;

  unsigned removeBranch(MachineBasicBlock &MBB,
                        int *BytesRemoved = nullptr) const override;

  unsigned insertBranch(MachineBasicBlock &MBB, MachineBasicBlock *TBB,
                        MachineBasicBlock *FBB, ArrayRef<MachineOperand> Cond,
                        const DebugLoc &DL,
                        int *BytesAdded = nullptr) const override;

  bool
  reverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond) const override;

  DFAPacketizer *
  CreateTargetScheduleState(const TargetSubtargetInfo &STI) const override;

  void copyPhysReg(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                   const DebugLoc &DL, Register DestReg, Register SrcReg,
                   bool KillSrc, bool RenamableDest = false,
                   bool RenamableSrc = false) const override;

  void storeRegToStackSlot(
      MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI, Register SrcReg,
      bool isKill, int FrameIndex, const TargetRegisterClass *RC,
      const TargetRegisterInfo *TRI, Register VReg,
      MachineInstr::MIFlag Flags = MachineInstr::NoFlags) const override;

  void loadRegFromStackSlot(
      MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
      Register DestReg, int FrameIndex, const TargetRegisterClass *RC,
      const TargetRegisterInfo *TRI, Register VReg,
      MachineInstr::MIFlag Flags = MachineInstr::NoFlags) const override;

private:
  void BuildCondBr(MachineBasicBlock &MBB, MachineBasicBlock *TBB,
                   const DebugLoc &DL,
                   ArrayRef<MachineOperand> Cond) const;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_TILE_TILEINSTRINFO_H
