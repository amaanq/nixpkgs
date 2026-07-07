//===-- TileMCTargetDesc.cpp - Tile Target Descriptions -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides Tile specific target descriptions.
//
//===----------------------------------------------------------------------===//

#include "TileMCTargetDesc.h"
#include "TileInstPrinter.h"
#include "TileMCAsmInfo.h"
#include "TargetInfo/TileTargetInfo.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#define ENABLE_INSTR_PREDICATE_VERIFIER
#include "TileGenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "TileGenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "TileGenRegisterInfo.inc"

static MCAsmInfo *createTileMCAsmInfo(const MCRegisterInfo &MRI,
                                      const Triple &TT,
                                      const MCTargetOptions &Options) {
  MCAsmInfo *MAI = new TileMCAsmInfo(TT, Options);
  unsigned Reg = MRI.getDwarfRegNum(Tile::SP, true);
  MCCFIInstruction Inst = MCCFIInstruction::cfiDefCfa(nullptr, Reg, 0);
  MAI->addInitialFrameState(Inst);
  return MAI;
}

static MCInstrInfo *createTileMCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitTileMCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createTileMCRegisterInfo(const Triple &TT) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitTileMCRegisterInfo(X, Tile::LR);
  return X;
}

static MCSubtargetInfo *
createTileMCSubtargetInfo(const Triple &TT, StringRef CPU, StringRef FS) {
  if (CPU.empty())
    CPU = "tilegx";
  return createTileMCSubtargetInfoImpl(TT, CPU, /*TuneCPU=*/CPU, FS);
}

static MCInstPrinter *createTileMCInstPrinter(const Triple &T,
                                              unsigned SyntaxVariant,
                                              const MCAsmInfo &MAI,
                                              const MCInstrInfo &MII,
                                              const MCRegisterInfo &MRI) {
  return new TileInstPrinter(MAI, MII, MRI);
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeTileTargetMC() {
  Target &T = getTheTileTarget();
  RegisterMCAsmInfoFn X(T, createTileMCAsmInfo);
  TargetRegistry::RegisterMCInstrInfo(T, createTileMCInstrInfo);
  TargetRegistry::RegisterMCRegInfo(T, createTileMCRegisterInfo);
  TargetRegistry::RegisterMCSubtargetInfo(T, createTileMCSubtargetInfo);
  TargetRegistry::RegisterMCInstPrinter(T, createTileMCInstPrinter);
  TargetRegistry::RegisterMCCodeEmitter(T, createTileMCCodeEmitter);
  TargetRegistry::RegisterMCAsmBackend(T, createTileAsmBackend);
}
