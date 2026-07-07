//===-- TileMCAsmInfo.cpp - Tile Asm Properties ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations of the TileMCAsmInfo properties.
//
//===----------------------------------------------------------------------===//

#include "TileMCAsmInfo.h"
#include "TileBaseInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

StringRef Tile::getSpecifierName(uint16_t Spec) {
  switch (Spec) {
  case TileII::MO_HW0:
    return "hw0";
  case TileII::MO_HW1:
    return "hw1";
  case TileII::MO_HW1_LAST:
    return "hw1_last";
  case TileII::MO_HW2_LAST:
    return "hw2_last";
  case TileII::MO_HW0_GOT:
    return "hw0_got";
  case TileII::MO_HW1_LAST_GOT:
    return "hw1_last_got";
  // PC-relative GOT base self-computation prints the plain halfword operator
  // on a sym - label difference; GNU as derives the PCREL relocation from it.
  case TileII::MO_HW0_PIC:
    return "hw0";
  case TileII::MO_HW1_LAST_PIC:
    return "hw1_last";
  case TileII::MO_PLT_CALL:
    return "plt";
  case TileII::MO_HW0_TLS_GD:
    return "hw0_tls_gd";
  case TileII::MO_HW1_LAST_TLS_GD:
    return "hw1_last_tls_gd";
  case TileII::MO_HW0_TLS_IE:
    return "hw0_tls_ie";
  case TileII::MO_HW1_LAST_TLS_IE:
    return "hw1_last_tls_ie";
  case TileII::MO_HW0_TLS_LE:
    return "hw0_tls_le";
  case TileII::MO_HW1_LAST_TLS_LE:
    return "hw1_last_tls_le";
  case TileII::MO_TLS_ADD:
    return "tls_add";
  case TileII::MO_TLS_GD_ADD:
    return "tls_gd_add";
  case TileII::MO_TLS_GD_CALL:
    return "tls_gd_call";
  case TileII::MO_TLS_IE_LOAD:
    return "tls_ie_load";
  default:
    return StringRef();
  }
}

void TileMCAsmInfo::printSpecifierExpr(raw_ostream &OS,
                                       const MCSpecifierExpr &Expr) const {
  StringRef S = Tile::getSpecifierName(Expr.getSpecifier());
  if (!S.empty())
    OS << S << '(';
  printExpr(OS, *Expr.getSubExpr());
  if (!S.empty())
    OS << ')';
}

bool TileMCAsmInfo::evaluateAsRelocatableImpl(const MCSpecifierExpr &Expr,
                                              MCValue &Res,
                                              const MCAssembler *Asm) const {
  if (!Expr.getSubExpr()->evaluateAsRelocatable(Res, Asm))
    return false;
  Res.setSpecifier(Expr.getSpecifier());
  // The GOT base self-computation materializes _GLOBAL_OFFSET_TABLE_ - .Llabel
  // under the hw*_pic (PCREL) specifiers; the local-label subtraction is folded
  // into the relocation addend, matching GNU as. Every other specifier requires
  // a single symbol with no subtrahend.
  if (Expr.getSpecifier() == TileII::MO_HW0_PIC ||
      Expr.getSpecifier() == TileII::MO_HW1_LAST_PIC)
    return true;
  return !Res.getSubSym();
}

void TileMCAsmInfo::anchor() {}

TileMCAsmInfo::TileMCAsmInfo(const Triple &TheTriple,
                             const MCTargetOptions &Options)
    : MCAsmInfoELF() {
  IsLittleEndian = true;
  CodePointerSize = CalleeSaveStackSlotSize = 8;
  MinInstAlignment = 8;

  Data16bitsDirective = "\t.2byte\t";
  Data32bitsDirective = "\t.4byte\t";
  Data64bitsDirective = "\t.8byte\t";
  ZeroDirective = "\t.space\t";
  CommentString = "#";

  UsesELFSectionDirectiveForBSS = true;

  SupportsDebugInformation = true;
  ExceptionsType = ExceptionHandling::DwarfCFI;
  DwarfRegNumForCFI = true;
}
