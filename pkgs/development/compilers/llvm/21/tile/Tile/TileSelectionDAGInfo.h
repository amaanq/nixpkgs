//===-- TileSelectionDAGInfo.h - Tile SelectionDAG Info ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the Tile subclass for SelectionDAGTargetInfo.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_TILE_TILESELECTIONDAGINFO_H
#define LLVM_LIB_TARGET_TILE_TILESELECTIONDAGINFO_H

#include "llvm/CodeGen/SelectionDAGTargetInfo.h"

namespace llvm {

class TileSelectionDAGInfo : public SelectionDAGTargetInfo {
public:
  TileSelectionDAGInfo();
  ~TileSelectionDAGInfo() override;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_TILE_TILESELECTIONDAGINFO_H
