//===-- TileMCTargetDesc.h - Tile Target Descriptions -----------*- C++ -*-===//
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

#ifndef LLVM_LIB_TARGET_TILE_MCTARGETDESC_TILEMCTARGETDESC_H
#define LLVM_LIB_TARGET_TILE_MCTARGETDESC_TILEMCTARGETDESC_H

#include "llvm/Support/DataTypes.h"
#include <memory>

namespace llvm {
class MCAsmBackend;
class MCCodeEmitter;
class MCContext;
class MCInstrInfo;
class MCObjectTargetWriter;
class MCRegisterInfo;
class MCSubtargetInfo;
class MCTargetOptions;
class Target;

MCCodeEmitter *createTileMCCodeEmitter(const MCInstrInfo &MCII, MCContext &Ctx);

MCAsmBackend *createTileAsmBackend(const Target &T, const MCSubtargetInfo &STI,
                                   const MCRegisterInfo &MRI,
                                   const MCTargetOptions &Options);

std::unique_ptr<MCObjectTargetWriter> createTileELFObjectWriter(uint8_t OSABI);
} // namespace llvm

#define GET_REGINFO_ENUM
#include "TileGenRegisterInfo.inc"

#define GET_INSTRINFO_ENUM
#define GET_INSTRINFO_MC_HELPER_DECLS
#include "TileGenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "TileGenSubtargetInfo.inc"

#endif // LLVM_LIB_TARGET_TILE_MCTARGETDESC_TILEMCTARGETDESC_H
