//===--  TileExpandPseudo.cpp - Expand Pseudo Instructions ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass expands pseudo instructions into target instructions after register
// allocation but before post-RA scheduling.
//
//===----------------------------------------------------------------------===//

#include "Tile.h"
#include "TileInstrInfo.h"
#include "TileMachineFunction.h"
#include "TileSubtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"

using namespace llvm;

#define DEBUG_TYPE "tile-expand-pseudo"

namespace {
struct TileExpandPseudo : public MachineFunctionPass {
  static char ID;
  TileExpandPseudo() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override { return "Tile PseudoInstrs Expansion"; }

  bool runOnMachineFunction(MachineFunction &F) override;

private:
  bool runOnMachineBasicBlock(MachineBasicBlock &MBB, const TileInstrInfo *TII);
};
char TileExpandPseudo::ID = 0;
} // end anonymous namespace

bool TileExpandPseudo::runOnMachineFunction(MachineFunction &F) {
  const TileInstrInfo *TII =
      static_cast<const TileInstrInfo *>(F.getSubtarget().getInstrInfo());
  bool Changed = false;
  for (MachineBasicBlock &MBB : F)
    Changed |= runOnMachineBasicBlock(MBB, TII);
  return Changed;
}

bool TileExpandPseudo::runOnMachineBasicBlock(MachineBasicBlock &MBB,
                                              const TileInstrInfo *TII) {
  bool Changed = false;
  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  TileFunctionInfo *TFI = MF.getInfo<TileFunctionInfo>();
  // Float-compare expansion (the only consumer) is staged in with float support
  // in a later phase; until then nothing here reads a per-function FMF flag.
  bool NoNaNs = false;

  for (MachineBasicBlock::iterator I = MBB.begin(); I != MBB.end();) {
    const MCInstrDesc &MCId = I->getDesc();

    switch (MCId.getOpcode()) {
    default:
      ++I;
      continue;
    case Tile::NET: {
      unsigned SraReg = I->getOperand(1).getReg();
      unsigned SrbReg = I->getOperand(2).getReg();
      if ((SraReg == Tile::UDN0 && SrbReg != Tile::ZERO) ||
          (SraReg == Tile::IDN0 && SrbReg != Tile::ZERO)) {
        BuildMI(MBB, I, I->getDebugLoc(), TII->get(Tile::ADD),
                I->getOperand(1).getReg())
            .addReg(Tile::ZERO)
            .addReg(SrbReg);
        break;
      }
      ++I;
      continue;
    }

    case Tile::VAARG_SP:
      BuildMI(MBB, I, I->getDebugLoc(), TII->get(Tile::ADDLI),
              I->getOperand(0).getReg())
          .addReg(I->getOperand(1).getReg())
          .addImm(MFI.getStackSize() + MFI.getOffsetAdjustment());
      break;

    // The address alloca returns must be adjusted by the size of the Tile
    // 16-byte zone and the outgoing-args zone.
    case Tile::ALLOCA_ADDR:
      BuildMI(MBB, I, I->getDebugLoc(), TII->get(Tile::ADDLI),
              I->getOperand(0).getReg())
          .addReg(I->getOperand(1).getReg())
          .addImm(TFI->getMaxCallFrameSize() + (MFI.hasCalls() ? 16 : 0));
      break;

    case Tile::ALLOCA_SP: {
      unsigned SrcSP = Tile::SP;
      if (MFI.hasCalls())
        SrcSP = I->getOperand(2).getReg();
      BuildMI(MBB, I, I->getDebugLoc(), TII->get(Tile::ADD),
              I->getOperand(0).getReg())
          .addReg(Tile::ZERO)
          .addReg(SrcSP);
      break;
    }

    case Tile::FSINGLE_CMP_LTO:
    case Tile::FSINGLE_CMP_LEO:
    case Tile::FSINGLE_CMP_GTO:
    case Tile::FSINGLE_CMP_GEO:
    case Tile::FSINGLE_CMP_EQO:
    case Tile::FSINGLE_CMP_NEO:
    case Tile::FDOUBLE_CMP_LTO:
    case Tile::FDOUBLE_CMP_LEO:
    case Tile::FDOUBLE_CMP_GTO:
    case Tile::FDOUBLE_CMP_GEO:
    case Tile::FDOUBLE_CMP_EQO:
    case Tile::FDOUBLE_CMP_NEO:
    case Tile::FSINGLE_CMP_LTO32:
    case Tile::FSINGLE_CMP_LEO32:
    case Tile::FSINGLE_CMP_GTO32:
    case Tile::FSINGLE_CMP_GEO32:
    case Tile::FSINGLE_CMP_EQO32:
    case Tile::FSINGLE_CMP_NEO32:
    case Tile::FDOUBLE_CMP_LTO32:
    case Tile::FDOUBLE_CMP_LEO32:
    case Tile::FDOUBLE_CMP_GTO32:
    case Tile::FDOUBLE_CMP_GEO32:
    case Tile::FDOUBLE_CMP_EQO32:
    case Tile::FDOUBLE_CMP_NEO32:
      if (!NoNaNs)
        llvm_unreachable("All SETOXX should be expanded to SETO and SETXX");
      [[fallthrough]];

    case Tile::FSINGLE_CMP_LT:
    case Tile::FSINGLE_CMP_LE:
    case Tile::FSINGLE_CMP_GT:
    case Tile::FSINGLE_CMP_GE:
    case Tile::FSINGLE_CMP_EQ:
    case Tile::FSINGLE_CMP_NE:
    case Tile::FSINGLE_CMP_LT32:
    case Tile::FSINGLE_CMP_LE32:
    case Tile::FSINGLE_CMP_GT32:
    case Tile::FSINGLE_CMP_GE32:
    case Tile::FSINGLE_CMP_EQ32:
    case Tile::FSINGLE_CMP_NE32:
    case Tile::FDOUBLE_CMP_LT:
    case Tile::FDOUBLE_CMP_LE:
    case Tile::FDOUBLE_CMP_GT:
    case Tile::FDOUBLE_CMP_GE:
    case Tile::FDOUBLE_CMP_EQ:
    case Tile::FDOUBLE_CMP_NE:
    case Tile::FDOUBLE_CMP_LT32:
    case Tile::FDOUBLE_CMP_LE32:
    case Tile::FDOUBLE_CMP_GT32:
    case Tile::FDOUBLE_CMP_GE32:
    case Tile::FDOUBLE_CMP_EQ32:
    case Tile::FDOUBLE_CMP_NE32: {
      unsigned DestReg = I->getOperand(0).getReg();
      unsigned SraReg = I->getOperand(1).getReg();
      unsigned SrbReg = I->getOperand(2).getReg();
      unsigned OldOpcode = MCId.getOpcode();
      unsigned NewOpcode = Tile::FDOUBLE_ADD_FLAGS;
      int64_t FPResOff[6] = {30, 29, 28, 27, 26, 31};
      int64_t BitOff;
      if (OldOpcode >= Tile::FSINGLE_CMP_EQ) {
        NewOpcode = Tile::FSINGLE_ADD1;
        BitOff = FPResOff[(OldOpcode - Tile::FSINGLE_CMP_EQ) / 6];
      } else
        BitOff = FPResOff[(OldOpcode - Tile::FDOUBLE_CMP_EQ) / 6];
      BuildMI(MBB, I, I->getDebugLoc(), TII->get(NewOpcode), DestReg)
          .addReg(SraReg)
          .addReg(SrbReg);
      BuildMI(MBB, I, I->getDebugLoc(), TII->get(Tile::BFEXTU), DestReg)
          .addReg(DestReg)
          .addImm(BitOff)
          .addImm(BitOff);
      break;
    }

    case Tile::FSINGLE_CMP_LTU:
    case Tile::FSINGLE_CMP_LEU:
    case Tile::FSINGLE_CMP_GTU:
    case Tile::FSINGLE_CMP_GEU:
    case Tile::FSINGLE_CMP_EQU:
    case Tile::FSINGLE_CMP_NEU:
    case Tile::FDOUBLE_CMP_LTU:
    case Tile::FDOUBLE_CMP_LEU:
    case Tile::FDOUBLE_CMP_GTU:
    case Tile::FDOUBLE_CMP_GEU:
    case Tile::FDOUBLE_CMP_EQU:
    case Tile::FDOUBLE_CMP_NEU:
    case Tile::FSINGLE_CMP_LTU32:
    case Tile::FSINGLE_CMP_LEU32:
    case Tile::FSINGLE_CMP_GTU32:
    case Tile::FSINGLE_CMP_GEU32:
    case Tile::FSINGLE_CMP_EQU32:
    case Tile::FSINGLE_CMP_NEU32:
    case Tile::FDOUBLE_CMP_LTU32:
    case Tile::FDOUBLE_CMP_LEU32:
    case Tile::FDOUBLE_CMP_GTU32:
    case Tile::FDOUBLE_CMP_GEU32:
    case Tile::FDOUBLE_CMP_EQU32:
    case Tile::FDOUBLE_CMP_NEU32: {
      unsigned DestReg = I->getOperand(0).getReg();
      unsigned SraReg = I->getOperand(1).getReg();
      unsigned SrbReg = I->getOperand(2).getReg();
      unsigned OldOpcode = MCId.getOpcode();
      unsigned NewOpcode = Tile::FDOUBLE_ADD_FLAGS;
      int64_t FPResOff[6] = {30, 29, 28, 27, 26, 31};
      int64_t BitOff;
      if (OldOpcode >= Tile::FSINGLE_CMP_EQ) {
        NewOpcode = Tile::FSINGLE_ADD1;
        BitOff = FPResOff[(OldOpcode - Tile::FSINGLE_CMP_EQ) / 6];
      } else
        BitOff = FPResOff[(OldOpcode - Tile::FDOUBLE_CMP_EQ) / 6];

      BuildMI(MBB, I, I->getDebugLoc(), TII->get(NewOpcode), DestReg)
          .addReg(SraReg)
          .addReg(SrbReg);
      if (NoNaNs) {
        BuildMI(MBB, I, I->getDebugLoc(), TII->get(Tile::BFEXTU), DestReg)
            .addReg(DestReg)
            .addImm(BitOff)
            .addImm(BitOff);
        break;
      }

      int64_t BitMask = (1LL << (BitOff - 25)) | 1LL;
      BuildMI(MBB, I, I->getDebugLoc(), TII->get(Tile::SHRUI), DestReg)
          .addReg(DestReg)
          .addImm(25);
      BuildMI(MBB, I, I->getDebugLoc(), TII->get(Tile::ANDI), DestReg)
          .addReg(DestReg)
          .addImm(BitMask);
      BuildMI(MBB, I, I->getDebugLoc(), TII->get(Tile::CMPNE), DestReg)
          .addReg(DestReg)
          .addReg(Tile::ZERO);
      break;
    }

    case Tile::FSINGLE_CMP_O:
    case Tile::FDOUBLE_CMP_O:
    case Tile::FSINGLE_CMP_UO:
    case Tile::FDOUBLE_CMP_UO:
    case Tile::FSINGLE_CMP_O32:
    case Tile::FDOUBLE_CMP_O32:
    case Tile::FSINGLE_CMP_UO32:
    case Tile::FDOUBLE_CMP_UO32: {
      unsigned DestReg = I->getOperand(0).getReg();
      unsigned SraReg = I->getOperand(1).getReg();
      unsigned SrbReg = I->getOperand(2).getReg();
      unsigned OldOpcode = MCId.getOpcode();
      unsigned NewOpcode1 = Tile::FDOUBLE_ADD_FLAGS;
      unsigned NewOpcode2 = Tile::CMPNE;

      if (OldOpcode >= Tile::FSINGLE_CMP_O)
        NewOpcode1 = Tile::FSINGLE_ADD1;

      if (OldOpcode == Tile::FSINGLE_CMP_O || OldOpcode == Tile::FDOUBLE_CMP_O ||
          OldOpcode == Tile::FSINGLE_CMP_O32 ||
          OldOpcode == Tile::FDOUBLE_CMP_O32)
        NewOpcode2 = Tile::CMPEQ;

      BuildMI(MBB, I, I->getDebugLoc(), TII->get(NewOpcode1), DestReg)
          .addReg(SraReg)
          .addReg(SrbReg);
      BuildMI(MBB, I, I->getDebugLoc(), TII->get(Tile::SHRUI), DestReg)
          .addReg(DestReg)
          .addImm(25);
      BuildMI(MBB, I, I->getDebugLoc(), TII->get(Tile::ANDI), DestReg)
          .addReg(DestReg)
          .addImm(0x1);
      BuildMI(MBB, I, I->getDebugLoc(), TII->get(NewOpcode2), DestReg)
          .addReg(DestReg)
          .addReg(Tile::ZERO);
      break;
    }
    }

    MBB.erase(I++);
    Changed = true;
  }

  return Changed;
}

FunctionPass *llvm::createTileExpandPseudoPass() {
  return new TileExpandPseudo();
}
