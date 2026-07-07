//===-- TileFrameLowering.cpp - Tile Frame Information --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the Tile implementation of TargetFrameLowering class.
//
//===----------------------------------------------------------------------===//

#include "TileFrameLowering.h"
#include "TileInstrInfo.h"
#include "TileMachineFunction.h"
#include "TileSubtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Target/TargetOptions.h"

using namespace llvm;

static void emitCFI(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
                    const DebugLoc &DL, const TileInstrInfo &TII,
                    const MCCFIInstruction &CFI) {
  MachineFunction &MF = *MBB.getParent();
  unsigned Idx = MF.addFrameInst(CFI);
  BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
      .addCFIIndex(Idx)
      .setMIFlag(MachineInstr::FrameSetup);
}

bool TileFrameLowering::hasFPImpl(const MachineFunction &MF) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  return MF.getTarget().Options.DisableFramePointerElim(MF) ||
         MFI.hasVarSizedObjects() || MFI.isFrameAddressTaken();
}

bool TileFrameLowering::hasReservedCallFrame(const MachineFunction &MF) const {
  return !MF.getFrameInfo().hasVarSizedObjects();
}

static void addImmR(unsigned Dreg, unsigned Treg, int64_t Imm,
                    const TileInstrInfo &TII, MachineBasicBlock &MBB,
                    MachineBasicBlock::iterator II, const DebugLoc &DL) {
  if (isInt<8>(Imm))
    BuildMI(MBB, II, DL, TII.get(Tile::ADDI), Dreg).addReg(Dreg).addImm(Imm);
  else if (isInt<16>(Imm))
    BuildMI(MBB, II, DL, TII.get(Tile::ADDLI), Dreg).addReg(Dreg).addImm(Imm);
  else if (isInt<32>(Imm)) {
    BuildMI(MBB, II, DL, TII.get(Tile::MOVELI), Treg)
        .addImm((((uint64_t)Imm) >> 16) & 0xFFFF);
    BuildMI(MBB, II, DL, TII.get(Tile::SHL16INSLI), Treg)
        .addReg(Treg)
        .addImm(Imm & ((1 << 16) - 1));
    BuildMI(MBB, II, DL, TII.get(Tile::ADD), Dreg).addReg(Dreg).addReg(Treg);
  } else {
    assert(isInt<64>(Imm) && "do not support Imm > 32bit yet");
    BuildMI(MBB, II, DL, TII.get(Tile::MOVELI), Treg)
        .addImm((((uint64_t)Imm) >> 32) & 0xFFFF);
    BuildMI(MBB, II, DL, TII.get(Tile::SHL16INSLI), Treg)
        .addReg(Treg)
        .addImm((((uint64_t)Imm) >> 16) & 0xFFFF);
    BuildMI(MBB, II, DL, TII.get(Tile::SHL16INSLI), Treg)
        .addReg(Treg)
        .addImm(Imm & ((1 << 16) - 1));
    BuildMI(MBB, II, DL, TII.get(Tile::ADD), Dreg).addReg(Dreg).addReg(Treg);
  }
}

void TileFrameLowering::emitPrologue(MachineFunction &MF,
                                     MachineBasicBlock &MBB) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const TileInstrInfo &TII =
      *static_cast<const TileInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI =
      *MF.getSubtarget().getRegisterInfo();
  MachineBasicBlock::iterator MBBI = MBB.begin();
  DebugLoc dl = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();
  unsigned SP = Tile::SP;
  unsigned ZERO = Tile::ZERO;
  unsigned TempReg0 = Tile::R10;
  unsigned TempReg1 = Tile::R11;

  unsigned RegSize = 8;
  uint64_t StackSize = (MFI.hasCalls() ? 2 * RegSize : 0) +
                       alignTo(MFI.getStackSize(), getStackAlign());

  MFI.setStackSize(StackSize);

  // No need to allocate space on the stack.
  if (StackSize == 0 && !MFI.adjustsStack())
    return;

  if (MFI.hasCalls()) {
    // Copy incoming sp into TempReg1, then save lr to the caller's reserve slot.
    BuildMI(MBB, MBBI, dl, TII.get(Tile::ADD), TempReg1)
        .addReg(SP)
        .addReg(ZERO);
    BuildMI(MBB, MBBI, dl, TII.get(Tile::ST)).addReg(SP).addReg(Tile::LR);
    // lr is stored at the incoming sp, which is the entry CFA.
    emitCFI(MBB, MBBI, dl, TII,
            MCCFIInstruction::createOffset(
                nullptr, TRI.getDwarfRegNum(Tile::LR, true), 0));
  }

  addImmR(SP, TempReg0, -StackSize, TII, MBB, MBBI, dl);
  emitCFI(MBB, MBBI, dl, TII,
          MCCFIInstruction::cfiDefCfaOffset(nullptr, StackSize));

  if (MFI.hasCalls()) {
    BuildMI(MBB, MBBI, dl, TII.get(Tile::ADDI), TempReg0).addReg(SP).addImm(8);
    BuildMI(MBB, MBBI, dl, TII.get(Tile::ST))
        .addReg(TempReg0)
        .addReg(TempReg1);
  }

  for (const CalleeSavedInfo &CSI : MFI.getCalleeSavedInfo()) {
    int64_t Off = MFI.getObjectOffset(CSI.getFrameIdx()) +
                  MFI.getOffsetAdjustment();
    emitCFI(MBB, MBBI, dl, TII,
            MCCFIInstruction::createOffset(
                nullptr, TRI.getDwarfRegNum(CSI.getReg(), true), Off));
  }

  if (hasFP(MF)) {
    BuildMI(MBB, MBBI, dl, TII.get(Tile::ADD), Tile::FP)
        .addReg(SP)
        .addReg(ZERO);
    emitCFI(MBB, MBBI, dl, TII,
            MCCFIInstruction::createDefCfaRegister(
                nullptr, TRI.getDwarfRegNum(Tile::FP, true)));
  }
}

void TileFrameLowering::emitEpilogue(MachineFunction &MF,
                                     MachineBasicBlock &MBB) const {
  MachineBasicBlock::iterator MBBI = MBB.getLastNonDebugInstr();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const TileInstrInfo &TII =
      *static_cast<const TileInstrInfo *>(MF.getSubtarget().getInstrInfo());
  DebugLoc dl = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();
  unsigned SP = Tile::SP;
  unsigned FP = Tile::FP;
  unsigned LR = Tile::LR;
  unsigned ZERO = Tile::ZERO;
  unsigned TempReg = Tile::R10;

  if (hasFP(MF)) {
    MachineBasicBlock::iterator I = MBBI;
    for (unsigned i = 0, e = MFI.getCalleeSavedInfo().size(); i != e; ++i)
      --I;
    BuildMI(MBB, I, dl, TII.get(Tile::ADD), SP).addReg(FP).addReg(ZERO);
  }

  uint64_t StackSize = MFI.getStackSize();
  if (!StackSize)
    return;

  addImmR(SP, TempReg, StackSize, TII, MBB, MBBI, dl);

  if (MFI.hasCalls())
    BuildMI(MBB, MBBI, dl, TII.get(Tile::LD), LR).addReg(SP);
}

MachineBasicBlock::iterator TileFrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator I) const {
  // Simply discard ADJCALLSTACKDOWN / ADJCALLSTACKUP instructions.
  return MBB.erase(I);
}

StackOffset
TileFrameLowering::getFrameIndexReference(const MachineFunction &MF, int FI,
                                          Register &FrameReg) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  FrameReg = hasFP(MF) ? Tile::FP : Tile::SP;
  int64_t Offset = MFI.getObjectOffset(FI) + MFI.getOffsetAdjustment();
  if (!hasFP(MF))
    Offset += MFI.getStackSize();
  return StackOffset::getFixed(Offset);
}

void TileFrameLowering::determineCalleeSaves(MachineFunction &MF,
                                             BitVector &SavedRegs,
                                             RegScavenger *RS) const {
  TargetFrameLowering::determineCalleeSaves(MF, SavedRegs, RS);

  if (hasFP(MF))
    SavedRegs.set(Tile::FP);
}
