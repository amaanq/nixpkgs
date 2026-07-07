//===-- TileMachineFunctionInfo.cpp - Private data used for Tile ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TileMachineFunction.h"
#include "MCTargetDesc/TileMCTargetDesc.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

static cl::opt<bool>
FixGlobalBaseReg("tile-fix-global-base-reg", cl::Hidden, cl::init(true),
                 cl::desc("Always use $51 as the global base register."));
static cl::opt<bool>
FixLinkReg("tile-fix-link-reg", cl::Hidden, cl::init(true),
           cl::desc("Always use $50 as the link register."));

void TileFunctionInfo::anchor() {}

MachineFunctionInfo *TileFunctionInfo::clone(
    BumpPtrAllocator &Allocator, MachineFunction &DestMF,
    const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB) const {
  return DestMF.cloneInfo<TileFunctionInfo>(*this);
}

bool TileFunctionInfo::globalBaseRegFixed() const { return FixGlobalBaseReg; }

bool TileFunctionInfo::globalBaseRegSet() const { return GlobalBaseReg.isValid(); }

Register TileFunctionInfo::getGlobalBaseReg() {
  if (GlobalBaseReg)
    return GlobalBaseReg;
  // First light always pins the global base to $51; a virtual-register base for
  // PIC is wired up in a later phase.
  assert(FixGlobalBaseReg && "non-fixed global base register not yet supported");
  return GlobalBaseReg = Tile::R51;
}

bool TileFunctionInfo::linkRegFixed() const { return FixLinkReg; }

bool TileFunctionInfo::linkRegSet() const { return LinkReg.isValid(); }

Register TileFunctionInfo::getLinkReg() {
  if (LinkReg)
    return LinkReg;
  assert(FixLinkReg && "non-fixed link register not yet supported");
  return LinkReg = Tile::R50;
}
