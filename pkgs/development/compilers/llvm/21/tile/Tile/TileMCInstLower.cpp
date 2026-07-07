//===-- TileMCInstLower.cpp - Convert Tile MachineInstr to MCInst ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains code to lower Tile MachineInstrs to their corresponding
// MCInst records.
//
// First-light subset: the packetizer is disabled, so every instruction is solo
// and maps directly to its base opcode. Relocation specifiers (hw0/hw1/plt/tls)
// are staged in with the integrated assembler in a later phase.
//
//===----------------------------------------------------------------------===//

#include "Tile.h"
#include "MCTargetDesc/TileBaseInfo.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"

using namespace llvm;

static MCOperand lowerSymbolOperand(const MachineOperand &MO,
                                    const MCSymbol *Symbol, AsmPrinter &AP) {
  MCContext &Ctx = AP.OutContext;
  const MCExpr *Expr = MCSymbolRefExpr::create(Symbol, Ctx);
  if (!MO.isJTI() && !MO.isMBB() && MO.getOffset())
    Expr = MCBinaryExpr::createAdd(
        Expr, MCConstantExpr::create(MO.getOffset(), Ctx), Ctx);

  // The hw0/hw1/hw2_last (and GOT/TLS) halfword-extraction relocation specifier
  // rides on the machine-operand target flag. The TileII::TOF value is used
  // directly as the MCSpecifierExpr code and decoded by the code emitter and
  // the asm printer.
  unsigned Flag = MO.getTargetFlags();
  if (Flag != TileII::MO_NO_FLAG)
    Expr = MCSpecifierExpr::create(Expr, Flag, Ctx);

  return MCOperand::createExpr(Expr);
}

void llvm::LowerTileMachineInstrToMCInst(const MachineInstr *MI, MCInst &OutMI,
                                         AsmPrinter &AP) {
  OutMI.setOpcode(MI->getOpcode());

  for (const MachineOperand &MO : MI->operands()) {
    MCOperand MCOp;
    switch (MO.getType()) {
    default:
      llvm_unreachable("unknown operand type");
    case MachineOperand::MO_Register:
      assert(!MO.getSubReg() && "Subregs should be eliminated!");
      MCOp = MCOperand::createReg(MO.getReg());
      break;
    case MachineOperand::MO_Immediate:
      MCOp = MCOperand::createImm(MO.getImm());
      break;
    case MachineOperand::MO_MachineBasicBlock:
      MCOp = MCOperand::createExpr(
          MCSymbolRefExpr::create(MO.getMBB()->getSymbol(), AP.OutContext));
      break;
    case MachineOperand::MO_GlobalAddress:
      MCOp = lowerSymbolOperand(MO, AP.getSymbol(MO.getGlobal()), AP);
      break;
    case MachineOperand::MO_ExternalSymbol:
      MCOp = lowerSymbolOperand(
          MO, AP.GetExternalSymbolSymbol(MO.getSymbolName()), AP);
      break;
    case MachineOperand::MO_JumpTableIndex:
      MCOp = lowerSymbolOperand(MO, AP.GetJTISymbol(MO.getIndex()), AP);
      break;
    case MachineOperand::MO_ConstantPoolIndex:
      MCOp = lowerSymbolOperand(MO, AP.GetCPISymbol(MO.getIndex()), AP);
      break;
    case MachineOperand::MO_BlockAddress:
      MCOp = lowerSymbolOperand(
          MO, AP.GetBlockAddressSymbol(MO.getBlockAddress()), AP);
      break;
    case MachineOperand::MO_RegisterMask:
      continue;
    }

    OutMI.addOperand(MCOp);
  }
}
