//===-- TileTargetInfo.cpp - Tile Target Implementation -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TargetInfo/TileTargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
using namespace llvm;

Target &llvm::getTheTileTarget() {
  static Target TheTileTarget;
  return TheTileTarget;
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeTileTargetInfo() {
  RegisterTarget<Triple::tilegx, /*HasJIT=*/false> X(
      getTheTileTarget(), "tilegx", "TILE-Gx (64-bit)", "Tile");
}
