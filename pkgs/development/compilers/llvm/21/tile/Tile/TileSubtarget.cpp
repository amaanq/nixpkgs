//===-- TileSubtarget.cpp - Tile Subtarget Information --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Tile specific subclass of TargetSubtargetInfo.
//
//===----------------------------------------------------------------------===//

#include "TileSubtarget.h"
#include "Tile.h"
#include "llvm/MC/TargetRegistry.h"

#define DEBUG_TYPE "tile-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "TileGenSubtargetInfo.inc"

using namespace llvm;

void TileSubtarget::anchor() {}

TileSubtarget &TileSubtarget::initializeSubtargetDependencies(StringRef CPU,
                                                              StringRef TuneCPU,
                                                              StringRef FS) {
  if (CPU.empty())
    CPU = "tilegx";
  if (TuneCPU.empty())
    TuneCPU = CPU;
  ParseSubtargetFeatures(CPU, TuneCPU, FS);
  InstrItins = getInstrItineraryForCPU(CPU);
  return *this;
}

TileSubtarget::TileSubtarget(const StringRef &CPU, const StringRef &TuneCPU,
                             const StringRef &FS, const TargetMachine &TM)
    : TileGenSubtargetInfo(TM.getTargetTriple(), CPU, TuneCPU, FS),
      InstrInfo(initializeSubtargetDependencies(CPU, TuneCPU, FS)),
      TLInfo(TM, *this), FrameLowering(*this) {}
