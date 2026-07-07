//===-- TileInstrInfo.cpp - Tile Instruction Information ------------------===//
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

#include "TileInstrInfo.h"
#include "TileSubtarget.h"
#include "llvm/CodeGen/DFAPacketizer.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/ErrorHandling.h"

#define GET_INSTRINFO_CTOR_DTOR
#include "TileGenInstrInfo.inc"

using namespace llvm;

// Defines TileGenSubtargetInfo::createDFAPacketizer, the auto-generated DFA
// resource model the VLIW packetizer reserves slots through.
#include "TileGenDFAPacketizer.inc"

DFAPacketizer *TileInstrInfo::CreateTargetScheduleState(
    const TargetSubtargetInfo &STI) const {
  const InstrItineraryData *II = STI.getInstrItineraryData();
  return static_cast<const TileSubtarget &>(STI).createDFAPacketizer(II);
}

TileInstrInfo::TileInstrInfo(const TileSubtarget &ST)
    : TileGenInstrInfo(Tile::ADJCALLSTACKDOWN, Tile::ADJCALLSTACKUP),
      RI(ST), UncondBrOpc(Tile::J) {}

void TileInstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator I,
                                const DebugLoc &DL, Register DestReg,
                                Register SrcReg, bool KillSrc,
                                bool RenamableDest, bool RenamableSrc) const {
  unsigned Opc = 0, ZeroReg = 0;

  if (Tile::CPURegsRegClass.contains(DestReg)) {
    assert(Tile::CPURegsRegClass.contains(SrcReg) && "Cannot copy registers");
    Opc = Tile::ADD, ZeroReg = Tile::ZERO;
  } else if (Tile::CPU32RegsRegClass.contains(DestReg)) {
    assert(Tile::CPU32RegsRegClass.contains(SrcReg) && "Cannot copy registers");
    Opc = Tile::ADDX, ZeroReg = Tile::ZERO_32;
  }

  assert(Opc && "Cannot copy registers");

  BuildMI(MBB, I, DL, get(Opc), DestReg)
      .addReg(ZeroReg)
      .addReg(SrcReg, getKillRegState(KillSrc));
}

void TileInstrInfo::storeRegToStackSlot(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator I, Register SrcReg,
    bool isKill, int FI, const TargetRegisterClass *RC,
    const TargetRegisterInfo *TRI, Register VReg,
    MachineInstr::MIFlag Flags) const {
  DebugLoc DL;
  if (I != MBB.end())
    DL = I->getDebugLoc();

  unsigned Opc = 0;
  if (RC == &Tile::CPURegsRegClass || RC == &Tile::SIMDRegsRegClass)
    Opc = Tile::ST;
  else if (RC == &Tile::CPU32RegsRegClass)
    Opc = Tile::ST4;

  assert(Opc && "Register class not handled!");
  BuildMI(MBB, I, DL, get(Opc))
      .addFrameIndex(FI)
      .addReg(SrcReg, getKillRegState(isKill));
}

void TileInstrInfo::loadRegFromStackSlot(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator I, Register DestReg,
    int FI, const TargetRegisterClass *RC, const TargetRegisterInfo *TRI,
    Register VReg, MachineInstr::MIFlag Flags) const {
  DebugLoc DL;
  if (I != MBB.end())
    DL = I->getDebugLoc();

  unsigned Opc = 0;
  if (RC == &Tile::CPURegsRegClass || RC == &Tile::SIMDRegsRegClass)
    Opc = Tile::LD;
  else if (RC == &Tile::CPU32RegsRegClass)
    Opc = Tile::LD4S32;

  assert(Opc && "Register class not handled!");
  BuildMI(MBB, I, DL, get(Opc), DestReg).addFrameIndex(FI);
}

//===----------------------------------------------------------------------===//
// Branch Analysis.
//===----------------------------------------------------------------------===//

static unsigned GetAnalyzableBrOpc(unsigned Opc) {
  return (Opc == Tile::BEQZ || Opc == Tile::BEQZ32 || Opc == Tile::BNEZ ||
          Opc == Tile::BNEZ32 || Opc == Tile::BGTZ || Opc == Tile::BGTZ32 ||
          Opc == Tile::BGEZ || Opc == Tile::BGEZ32 || Opc == Tile::BLTZ ||
          Opc == Tile::BLTZ32 || Opc == Tile::BLEZ || Opc == Tile::BLEZ32 ||
          Opc == Tile::J)
             ? Opc
             : 0;
}

unsigned Tile::GetOppositeBranchOpc(unsigned Opc) {
  switch (Opc) {
  default:
    llvm_unreachable("Illegal opcode!");
  case Tile::BEQZ:    return Tile::BNEZ;
  case Tile::BNEZ:    return Tile::BEQZ;
  case Tile::BGTZ:    return Tile::BLEZ;
  case Tile::BGEZ:    return Tile::BLTZ;
  case Tile::BLTZ:    return Tile::BGEZ;
  case Tile::BLEZ:    return Tile::BGTZ;
  case Tile::BEQZ32:  return Tile::BNEZ32;
  case Tile::BNEZ32:  return Tile::BEQZ32;
  case Tile::BGTZ32:  return Tile::BLEZ32;
  case Tile::BGEZ32:  return Tile::BLTZ32;
  case Tile::BLTZ32:  return Tile::BGEZ32;
  case Tile::BLEZ32:  return Tile::BGTZ32;
  }
}

static void analyzeCondBr(const MachineInstr *Inst, unsigned Opc,
                          MachineBasicBlock *&BB,
                          SmallVectorImpl<MachineOperand> &Cond) {
  assert(GetAnalyzableBrOpc(Opc) && "Not an analyzable branch");
  int NumOp = Inst->getNumExplicitOperands();

  // For both int and fp branches, the last explicit operand is the MBB.
  BB = Inst->getOperand(NumOp - 1).getMBB();
  Cond.push_back(MachineOperand::CreateImm(Opc));

  for (int i = 0; i < NumOp - 1; i++)
    Cond.push_back(Inst->getOperand(i));
}

bool TileInstrInfo::analyzeBranch(MachineBasicBlock &MBB,
                                  MachineBasicBlock *&TBB,
                                  MachineBasicBlock *&FBB,
                                  SmallVectorImpl<MachineOperand> &Cond,
                                  bool AllowModify) const {
  MachineBasicBlock::reverse_iterator I = MBB.rbegin(), REnd = MBB.rend();

  while (I != REnd && I->isDebugInstr())
    ++I;

  if (I == REnd || !isUnpredicatedTerminator(*I)) {
    // This block ends with no branches (it just falls through to its succ).
    TBB = FBB = nullptr;
    return false;
  }

  MachineInstr *LastInst = &*I;
  unsigned LastOpc = LastInst->getOpcode();

  // Not an analyzable branch (must be an indirect jump).
  if (!GetAnalyzableBrOpc(LastOpc))
    return true;

  unsigned SecondLastOpc = 0;
  MachineInstr *SecondLastInst = nullptr;

  if (++I != REnd) {
    SecondLastInst = &*I;
    SecondLastOpc = GetAnalyzableBrOpc(SecondLastInst->getOpcode());

    if (isUnpredicatedTerminator(*SecondLastInst) && !SecondLastOpc)
      return true;
  }

  if (!SecondLastOpc) {
    if (LastOpc == UncondBrOpc) {
      TBB = LastInst->getOperand(0).getMBB();
      return false;
    }
    analyzeCondBr(LastInst, LastOpc, TBB, Cond);
    return false;
  }

  // If there are three terminators, we don't know what sort of block this is.
  if (++I != REnd && isUnpredicatedTerminator(*I))
    return true;

  if (SecondLastOpc == UncondBrOpc) {
    if (!AllowModify)
      return true;

    TBB = SecondLastInst->getOperand(0).getMBB();
    LastInst->eraseFromParent();
    return false;
  }

  // Conditional branch followed by an unconditional branch.
  if (LastOpc != UncondBrOpc)
    return true;

  analyzeCondBr(SecondLastInst, SecondLastOpc, TBB, Cond);
  FBB = LastInst->getOperand(0).getMBB();
  return false;
}

void TileInstrInfo::BuildCondBr(MachineBasicBlock &MBB, MachineBasicBlock *TBB,
                                const DebugLoc &DL,
                                ArrayRef<MachineOperand> Cond) const {
  unsigned Opc = Cond[0].getImm();
  const MCInstrDesc &MCID = get(Opc);
  MachineInstrBuilder MIB = BuildMI(&MBB, DL, MCID);

  for (unsigned i = 1, e = Cond.size(); i != e; ++i)
    MIB.addReg(Cond[i].getReg());

  MIB.addMBB(TBB);
}

unsigned TileInstrInfo::insertBranch(MachineBasicBlock &MBB,
                                     MachineBasicBlock *TBB,
                                     MachineBasicBlock *FBB,
                                     ArrayRef<MachineOperand> Cond,
                                     const DebugLoc &DL, int *BytesAdded) const {
  assert(TBB && "insertBranch must not be told to insert a fallthrough");
  assert(!BytesAdded && "code size not handled");
  assert((Cond.size() <= 3) && "# of Tile branch conditions must be <= 3!");

  if (FBB) {
    BuildCondBr(MBB, TBB, DL, Cond);
    BuildMI(&MBB, DL, get(UncondBrOpc)).addMBB(FBB);
    return 2;
  }

  if (Cond.empty())
    BuildMI(&MBB, DL, get(UncondBrOpc)).addMBB(TBB);
  else
    BuildCondBr(MBB, TBB, DL, Cond);
  return 1;
}

unsigned TileInstrInfo::removeBranch(MachineBasicBlock &MBB,
                                     int *BytesRemoved) const {
  assert(!BytesRemoved && "code size not handled");

  MachineBasicBlock::reverse_iterator I = MBB.rbegin(), REnd = MBB.rend();
  unsigned removed = 0;

  // Up to 2 trailing branches are removed. Indirect branches are not removed.
  while (I != REnd && removed < 2) {
    MachineInstr &MI = *I;
    if (MI.isDebugInstr()) {
      ++I;
      continue;
    }
    if (!GetAnalyzableBrOpc(MI.getOpcode()))
      break;
    ++I;
    MI.eraseFromParent();
    ++removed;
  }

  return removed;
}

bool TileInstrInfo::reverseBranchCondition(
    SmallVectorImpl<MachineOperand> &Cond) const {
  assert((Cond.size() && Cond.size() <= 3) && "Invalid Tile branch condition!");
  Cond[0].setImm(Tile::GetOppositeBranchOpc(Cond[0].getImm()));
  return false;
}
