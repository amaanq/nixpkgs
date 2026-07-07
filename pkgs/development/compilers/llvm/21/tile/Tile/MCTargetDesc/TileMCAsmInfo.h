//===-- TileMCAsmInfo.h - Tile Asm Info ------------------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the TileMCAsmInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_TILE_MCTARGETDESC_TILEMCASMINFO_H
#define LLVM_LIB_TARGET_TILE_MCTARGETDESC_TILEMCASMINFO_H

#include "llvm/MC/MCAsmInfoELF.h"

#include "llvm/ADT/StringRef.h"

namespace llvm {

class Triple;
class MCTargetOptions;
class MCSpecifierExpr;
class MCValue;
class MCAssembler;
class raw_ostream;

class TileMCAsmInfo : public MCAsmInfoELF {
  void anchor() override;

public:
  explicit TileMCAsmInfo(const Triple &TheTriple,
                         const MCTargetOptions &Options);

  void printSpecifierExpr(raw_ostream &OS,
                          const MCSpecifierExpr &Expr) const override;

  bool evaluateAsRelocatableImpl(const MCSpecifierExpr &Expr, MCValue &Res,
                                 const MCAssembler *Asm) const override;
};

namespace Tile {
// The relocation-specifier name (hw0/hw1/hw2_last/...) as accepted by the GNU
// tilegx assembler. The specifier code is a TileII::TOF machine-operand flag.
StringRef getSpecifierName(uint16_t Spec);
} // namespace Tile

} // namespace llvm

#endif // LLVM_LIB_TARGET_TILE_MCTARGETDESC_TILEMCASMINFO_H
