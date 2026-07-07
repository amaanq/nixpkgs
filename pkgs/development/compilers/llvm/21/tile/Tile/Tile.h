//===-- Tile.h - Top-level interface for Tile representation ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the entry points for global functions defined in
// the LLVM Tile back-end.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_TILE_TILE_H
#define LLVM_LIB_TARGET_TILE_TILE_H

#include "MCTargetDesc/TileMCTargetDesc.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {
class TileTargetMachine;
class FunctionPass;
class MachineInstr;
class AsmPrinter;
class MCInst;
class PassRegistry;

FunctionPass *createTileISelDag(TileTargetMachine &TM, CodeGenOptLevel OptLevel);
FunctionPass *createTileExpandPseudoPass();
FunctionPass *createTileVLIWPacketizer();
void LowerTileMachineInstrToMCInst(const MachineInstr *MI, MCInst &OutMI,
                                   AsmPrinter &AP);

void initializeTileDAGToDAGISelLegacyPass(PassRegistry &);
void initializeTileVLIWPacketizerPass(PassRegistry &);
} // end namespace llvm

// TILE-Gx uses 10 registers, r0 ~ r9, for argument passing.
#define TILEGX_AREG_NUM 10
// TILE-Gx reserves the bottom 16 bytes on the frame for special usage.
#define TILEGX_BZONE_SIZE 16

#endif // LLVM_LIB_TARGET_TILE_TILE_H
