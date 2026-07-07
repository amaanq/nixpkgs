//===-- TileRegisterInfo.cpp - TILE Register Information ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the TILE implementation of the TargetRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#include "TileRegisterInfo.h"
#include "TileInstrInfo.h"
#include "TileMachineFunction.h"
#include "TileSubtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/Support/MathExtras.h"

#define GET_REGINFO_TARGET_DESC
#include "TileGenRegisterInfo.inc"

#define DEBUG_TYPE "tile-reg-info"

using namespace llvm;

TileRegisterInfo::TileRegisterInfo(const TileSubtarget &ST)
    : TileGenRegisterInfo(Tile::LR) {}

const MCPhysReg *
TileRegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  return CSR_Tile_SaveList;
}

const uint32_t *
TileRegisterInfo::getCallPreservedMask(const MachineFunction &MF,
                                       CallingConv::ID) const {
  return CSR_Tile_RegMask;
}

BitVector TileRegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  static const MCPhysReg ReservedCPURegs[] = {
      Tile::R49,  Tile::FP,   Tile::TP,   Tile::SP,    Tile::LR,   Tile::IDN0,
      Tile::IDN1, Tile::UDN0, Tile::UDN1, Tile::UDN2,  Tile::UDN3, Tile::ZERO};

  static const MCPhysReg ReservedCPU32Regs[] = {
      Tile::R49_32,  Tile::FP_32,   Tile::TP_32,   Tile::SP_32,
      Tile::LR_32,   Tile::IDN0_32, Tile::IDN1_32, Tile::UDN0_32,
      Tile::UDN1_32, Tile::UDN2_32, Tile::UDN3_32, Tile::ZERO_32};

  BitVector Reserved(getNumRegs());

  for (MCPhysReg R : ReservedCPURegs)
    Reserved.set(R);
  for (MCPhysReg R : ReservedCPU32Regs)
    Reserved.set(R);

  // If a register is dedicated as the global base register, reserve it.
  if (MF.getInfo<TileFunctionInfo>()->globalBaseRegFixed()) {
    Reserved.set(Tile::R51);
    Reserved.set(Tile::R50);
    Reserved.set(Tile::R51_32);
    Reserved.set(Tile::R50_32);
  }

  return Reserved;
}

bool TileRegisterInfo::requiresRegisterScavenging(
    const MachineFunction &MF) const {
  return true;
}

bool TileRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                           int SPAdj, unsigned FIOperandNum,
                                           RegScavenger *RS) const {
  assert(SPAdj == 0 && "Unexpected");

  MachineInstr &MI = *II;
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const TileInstrInfo &TII =
      *static_cast<const TileInstrInfo *>(MF.getSubtarget().getInstrInfo());
  DebugLoc DL = MI.getDebugLoc();
  const std::vector<CalleeSavedInfo> &CSI = MFI.getCalleeSavedInfo();
  int MinCSFI = 0;
  int MaxCSFI = -1;

  if (CSI.size()) {
    MinCSFI = CSI.front().getFrameIdx();
    MaxCSFI = CSI.back().getFrameIdx();
  }

  int Opc = MI.getOpcode();

  Register DestReg;
  MachineOperand *FrameIndexOp = nullptr;

  if (Opc == Tile::TileFI) {
    DestReg = MI.getOperand(0).getReg();
    FrameIndexOp = &MI.getOperand(1);
  } else if (Opc == Tile::ST || Opc == Tile::ST4 || Opc == Tile::LD ||
             Opc == Tile::LD4S32 || MI.isDebugValue()) {
    DestReg = Tile::R49;
    FrameIndexOp = &MI.getOperand(FIOperandNum);
  }

  unsigned StackReg = 0;
  int64_t Offset = 0;
  int FrameIndex = FrameIndexOp->getIndex();

  if (FrameIndex >= MinCSFI && FrameIndex <= MaxCSFI)
    StackReg = Tile::SP;
  else
    StackReg = getFrameRegister(MF);

  Offset = MFI.getObjectOffset(FrameIndex) + MFI.getOffsetAdjustment() +
           MFI.getStackSize();

  if (MI.isDebugValue()) {
    MI.getOperand(FIOperandNum).ChangeToRegister(StackReg, false);
    MI.getOperand(FIOperandNum + 1).ChangeToImmediate(Offset);
    return false;
  }

  if (isInt<8>(Offset))
    BuildMI(MBB, II, DL, TII.get(Tile::ADDI), DestReg)
        .addReg(StackReg)
        .addImm(Offset);
  else if (isInt<16>(Offset))
    BuildMI(MBB, II, DL, TII.get(Tile::ADDLI), DestReg)
        .addReg(StackReg)
        .addImm(Offset);
  else if (isInt<32>(Offset)) {
    BuildMI(MBB, II, DL, TII.get(Tile::MOVELI), DestReg)
        .addImm((((uint64_t)Offset) >> 16) & 0xFFFF);
    BuildMI(MBB, II, DL, TII.get(Tile::SHL16INSLI), DestReg)
        .addReg(DestReg)
        .addImm(Offset & ((1 << 16) - 1));
    BuildMI(MBB, II, DL, TII.get(Tile::ADD), DestReg)
        .addReg(DestReg)
        .addReg(StackReg);
  } else {
    assert(isInt<64>(Offset) && "do not support Offset > 32bit yet");
    BuildMI(MBB, II, DL, TII.get(Tile::MOVELI), DestReg)
        .addImm((((uint64_t)Offset) >> 32) & 0xFFFF);
    BuildMI(MBB, II, DL, TII.get(Tile::SHL16INSLI), DestReg)
        .addReg(DestReg)
        .addImm((((uint64_t)Offset) >> 16) & 0xFFFF);
    BuildMI(MBB, II, DL, TII.get(Tile::SHL16INSLI), DestReg)
        .addReg(DestReg)
        .addImm(Offset & ((1 << 16) - 1));
    BuildMI(MBB, II, DL, TII.get(Tile::ADD), DestReg)
        .addReg(DestReg)
        .addReg(StackReg);
  }

  switch (Opc) {
  default:
    llvm_unreachable("FrameIndex in unexpected instruction!");
  case Tile::TileFI:
    MI.eraseFromParent();
    return true;
  case Tile::ST:
  case Tile::ST4:
  case Tile::LD:
  case Tile::LD4S32:
    FrameIndexOp->ChangeToRegister(DestReg, false);
    break;
  }
  return false;
}

Register TileRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();
  return TFI->hasFP(MF) ? Tile::FP : Tile::SP;
}
