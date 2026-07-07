//=== TileCallingConv.h - Tile Custom Calling Convention Routines -*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the custom routines for the Tile Calling Convention that
// aren't done by tablegen.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_TILE_TILECALLINGCONV_H
#define LLVM_LIB_TARGET_TILE_TILECALLINGCONV_H

#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/IR/CallingConv.h"

namespace llvm {

// The bottom 16 bytes of a Tile stack frame are reserved to hold the incoming
// sp/lr, so both incoming and outgoing stack args are lifted by 16 bytes.
inline bool CC_Tile_StackArg(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                             CCValAssign::LocInfo &LocInfo,
                             ISD::ArgFlagsTy &ArgFlags, CCState &State) {
  if (LocVT == MVT::i32 || LocVT == MVT::f32) {
    unsigned Offset4 = State.AllocateStack(4, Align(8)) + 16;
    State.addLoc(CCValAssign::getMem(ValNo, ValVT, Offset4, LocVT, LocInfo));
    return true;
  }

  if (LocVT == MVT::i64 || LocVT == MVT::f64) {
    unsigned Offset5 = State.AllocateStack(8, Align(8)) + 16;
    State.addLoc(CCValAssign::getMem(ValNo, ValVT, Offset5, LocVT, LocInfo));
    return true;
  }

  return false;
}

} // end namespace llvm

#endif // LLVM_LIB_TARGET_TILE_TILECALLINGCONV_H
