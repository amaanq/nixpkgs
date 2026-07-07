//===-- TileELFObjectWriter.cpp - Tile ELF Writer ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/TileFixupKinds.h"
#include "MCTargetDesc/TileMCTargetDesc.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/ErrorHandling.h"
#include <cstdint>

using namespace llvm;

namespace {
class TileELFObjectWriter : public MCELFObjectTargetWriter {
public:
  TileELFObjectWriter(uint8_t OSABI);
  ~TileELFObjectWriter() override = default;

protected:
  unsigned getRelocType(const MCFixup &Fixup, const MCValue &Target,
                        bool IsPCRel) const override;
};
} // namespace

// TILE-Gx is 64-bit and uses RELA relocations.
TileELFObjectWriter::TileELFObjectWriter(uint8_t OSABI)
    : MCELFObjectTargetWriter(/*Is64Bit=*/true, OSABI, ELF::EM_TILEGX,
                              /*HasRelocationAddend=*/true) {}

unsigned TileELFObjectWriter::getRelocType(const MCFixup &Fixup,
                                           const MCValue &Target,
                                           bool IsPCRel) const {
  switch (Fixup.getKind()) {
  default:
    llvm_unreachable("invalid fixup kind!");
  case FK_Data_1:
    return IsPCRel ? ELF::R_TILEGX_8_PCREL : ELF::R_TILEGX_8;
  case FK_Data_2:
    return IsPCRel ? ELF::R_TILEGX_16_PCREL : ELF::R_TILEGX_16;
  case FK_Data_4:
    return IsPCRel ? ELF::R_TILEGX_32_PCREL : ELF::R_TILEGX_32;
  case FK_Data_8:
    return IsPCRel ? ELF::R_TILEGX_64_PCREL : ELF::R_TILEGX_64;
  case Tile::fixup_Tile_X0_HW0:
    return ELF::R_TILEGX_IMM16_X0_HW0;
  case Tile::fixup_Tile_X1_HW0:
    return ELF::R_TILEGX_IMM16_X1_HW0;
  case Tile::fixup_Tile_X0_HW0_PCREL:
    return ELF::R_TILEGX_IMM16_X0_HW0_PCREL;
  case Tile::fixup_Tile_X1_HW0_PCREL:
    return ELF::R_TILEGX_IMM16_X1_HW0_PCREL;
  case Tile::fixup_Tile_X0_HW0_GOT:
    return ELF::R_TILEGX_IMM16_X0_HW0_GOT;
  case Tile::fixup_Tile_X1_HW0_GOT:
    return ELF::R_TILEGX_IMM16_X1_HW0_GOT;
  case Tile::fixup_Tile_X0_HW1:
    return ELF::R_TILEGX_IMM16_X0_HW1;
  case Tile::fixup_Tile_X1_HW1:
    return ELF::R_TILEGX_IMM16_X1_HW1;
  case Tile::fixup_Tile_X0_HW1_PCREL:
    return ELF::R_TILEGX_IMM16_X0_HW1_PCREL;
  case Tile::fixup_Tile_X1_HW1_PCREL:
    return ELF::R_TILEGX_IMM16_X1_HW1_PCREL;
  case Tile::fixup_Tile_X0_HW1_LAST:
    return ELF::R_TILEGX_IMM16_X0_HW1_LAST;
  case Tile::fixup_Tile_X1_HW1_LAST:
    return ELF::R_TILEGX_IMM16_X1_HW1_LAST;
  case Tile::fixup_Tile_X0_HW1_LAST_PCREL:
    return ELF::R_TILEGX_IMM16_X0_HW1_LAST_PCREL;
  case Tile::fixup_Tile_X1_HW1_LAST_PCREL:
    return ELF::R_TILEGX_IMM16_X1_HW1_LAST_PCREL;
  case Tile::fixup_Tile_X0_HW1_LAST_GOT:
    return ELF::R_TILEGX_IMM16_X0_HW1_LAST_GOT;
  case Tile::fixup_Tile_X1_HW1_LAST_GOT:
    return ELF::R_TILEGX_IMM16_X1_HW1_LAST_GOT;
  case Tile::fixup_Tile_X0_HW2_LAST:
    return ELF::R_TILEGX_IMM16_X0_HW2_LAST;
  case Tile::fixup_Tile_X1_HW2_LAST:
    return ELF::R_TILEGX_IMM16_X1_HW2_LAST;
  case Tile::fixup_Tile_X1_JUMPOFF:
    return ELF::R_TILEGX_JUMPOFF_X1;
  case Tile::fixup_Tile_X1_JUMPOFF_PLT:
    return ELF::R_TILEGX_JUMPOFF_X1_PLT;
  case Tile::fixup_Tile_X1_BROFF:
    return ELF::R_TILEGX_BROFF_X1;
  case Tile::fixup_Tile_X1_HW0_TLS_GD:
    return ELF::R_TILEGX_IMM16_X1_HW0_TLS_GD;
  case Tile::fixup_Tile_X1_HW1_LAST_TLS_GD:
    return ELF::R_TILEGX_IMM16_X1_HW1_LAST_TLS_GD;
  case Tile::fixup_Tile_X1_HW0_TLS_IE:
    return ELF::R_TILEGX_IMM16_X1_HW0_TLS_IE;
  case Tile::fixup_Tile_X1_HW1_LAST_TLS_IE:
    return ELF::R_TILEGX_IMM16_X1_HW1_LAST_TLS_IE;
  case Tile::fixup_Tile_X1_HW0_TLS_LE:
    return ELF::R_TILEGX_IMM16_X1_HW0_TLS_LE;
  case Tile::fixup_Tile_X1_HW1_LAST_TLS_LE:
    return ELF::R_TILEGX_IMM16_X1_HW1_LAST_TLS_LE;
  case Tile::fixup_Tile_X1_TLS_GD_CALL:
    return ELF::R_TILEGX_TLS_GD_CALL;
  case Tile::fixup_Tile_X1_TLS_GD_ADD:
    return ELF::R_TILEGX_IMM8_X1_TLS_GD_ADD;
  case Tile::fixup_Tile_X1_TLS_ADD:
    return ELF::R_TILEGX_IMM8_X1_TLS_ADD;
  case Tile::fixup_Tile_X1_TLS_IE_LOAD:
    return ELF::R_TILEGX_TLS_IE_LOAD;
  }
}

std::unique_ptr<MCObjectTargetWriter>
llvm::createTileELFObjectWriter(uint8_t OSABI) {
  return std::make_unique<TileELFObjectWriter>(OSABI);
}
