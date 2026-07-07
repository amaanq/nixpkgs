//===-- TileAsmBackend.cpp - Tile Assembler Backend ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the TileAsmBackend class.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/TileFixupKinds.h"
#include "MCTargetDesc/TileMCTargetDesc.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/ErrorHandling.h"
#include <cstdint>

using namespace llvm;

// Place a resolved Value into the bitfield the fixup targets within the 64-bit
// bundle. The shifts mirror the TILE-Gx X1 pipe layout in binutils' howto
// tables (elfxx-tilegx.c); the unhandled kinds are deferred relocations.
static uint64_t adjustFixupValue(const MCFixup &Fixup, uint64_t Value) {
  switch (Fixup.getKind()) {
  default:
    return 0;
  case FK_Data_1:
  case FK_Data_2:
  case FK_Data_4:
  case FK_Data_8:
    return Value;
  case Tile::fixup_Tile_X1_JUMPOFF: {
    uint64_t Adj = Value >> 3;
    return (Adj & 0x7FFFFFF) << 31;
  }
  case Tile::fixup_Tile_X1_BROFF: {
    uint64_t Adj = Value >> 3;
    return ((Adj & 0x3F) << 31) | (((Adj & (0x7FFULL << 6)) >> 6) << 43);
  }
  }
}

namespace {
class TileAsmBackend : public MCAsmBackend {
  uint8_t OSABI;

public:
  TileAsmBackend(uint8_t OSABI)
      : MCAsmBackend(llvm::endianness::little), OSABI(OSABI) {}
  ~TileAsmBackend() override = default;

  void applyFixup(const MCFragment &F, const MCFixup &Fixup,
                  const MCValue &Target, MutableArrayRef<char> Data,
                  uint64_t Value, bool IsResolved) override {
    maybeAddReloc(F, Fixup, Target, Value, IsResolved);
    Value = adjustFixupValue(Fixup, Value);
    if (!Value)
      return;

    // Data already points at the fixup location, so the packed bits are merged
    // into the eight bytes of this bundle directly.
    for (unsigned I = 0; I != 8; ++I)
      Data[I] |= uint8_t((Value >> (I * 8)) & 0xff);
  }

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return createTileELFObjectWriter(OSABI);
  }

  // Fold an unsigned LEB128 whose value is a label difference spanning
  // fragments (as the __gcc_except_table call-site table emits) into an
  // absolute constant. TILE-Gx has no LEB128 linker relocation, so an
  // unresolved difference has nowhere to go and must be evaluated here.
  std::pair<bool, bool> relaxLEB128(MCLEBFragment &LF,
                                    int64_t &Value) const override {
    if (LF.isSigned())
      return std::make_pair(false, false);
    return std::make_pair(LF.getValue().evaluateKnownAbsolute(Value, *Asm),
                          false);
  }

  MCFixupKindInfo getFixupKindInfo(MCFixupKind Kind) const override {
    // Must stay in the order of the fixup_* enum in TileFixupKinds.h.
    const static MCFixupKindInfo Infos[Tile::NumTargetFixupKinds] = {
        {"fixup_Tile_X0_HW0", 0, 16, 0},
        {"fixup_Tile_X1_HW0", 0, 16, 0},
        {"fixup_Tile_X0_HW0_PCREL", 0, 16, 0},
        {"fixup_Tile_X1_HW0_PCREL", 0, 16, 0},
        {"fixup_Tile_X0_HW0_GOT", 0, 16, 0},
        {"fixup_Tile_X1_HW0_GOT", 0, 16, 0},
        {"fixup_Tile_X0_HW1", 0, 16, 0},
        {"fixup_Tile_X1_HW1", 0, 16, 0},
        {"fixup_Tile_X0_HW1_PCREL", 0, 16, 0},
        {"fixup_Tile_X1_HW1_PCREL", 0, 16, 0},
        {"fixup_Tile_X0_HW1_LAST", 0, 16, 0},
        {"fixup_Tile_X1_HW1_LAST", 0, 16, 0},
        {"fixup_Tile_X0_HW1_LAST_PCREL", 0, 16, 0},
        {"fixup_Tile_X1_HW1_LAST_PCREL", 0, 16, 0},
        {"fixup_Tile_X0_HW1_LAST_GOT", 0, 16, 0},
        {"fixup_Tile_X1_HW1_LAST_GOT", 0, 16, 0},
        {"fixup_Tile_X0_HW2_LAST", 0, 16, 0},
        {"fixup_Tile_X1_HW2_LAST", 0, 16, 0},
        {"fixup_Tile_X1_JUMPOFF", 31, 27, 0},
        {"fixup_Tile_X1_JUMPOFF_PLT", 31, 27, 0},
        {"fixup_Tile_X1_BROFF", 31, 17, 0},
        {"fixup_Tile_X1_HW0_TLS_GD", 31, 16, 0},
        {"fixup_Tile_X1_HW1_LAST_TLS_GD", 31, 16, 0},
        {"fixup_Tile_X1_HW0_TLS_IE", 31, 16, 0},
        {"fixup_Tile_X1_HW1_LAST_TLS_IE", 31, 16, 0},
        {"fixup_Tile_X1_HW0_TLS_LE", 31, 16, 0},
        {"fixup_Tile_X1_HW1_LAST_TLS_LE", 31, 16, 0},
        {"fixup_Tile_X1_TLS_GD_CALL", 31, 27, 0},
        {"fixup_Tile_X1_TLS_GD_ADD", 31, 8, 0},
        {"fixup_Tile_X1_TLS_ADD", 31, 8, 0},
        {"fixup_Tile_X1_TLS_IE_LOAD", 31, 8, 0},
    };

    if (Kind < FirstTargetFixupKind)
      return MCAsmBackend::getFixupKindInfo(Kind);

    assert(unsigned(Kind - FirstTargetFixupKind) < Tile::NumTargetFixupKinds &&
           "Invalid kind!");
    return Infos[Kind - FirstTargetFixupKind];
  }

  bool writeNopData(raw_ostream &OS, uint64_t Count,
                    const MCSubtargetInfo *STI) const override {
    if ((Count % 8) != 0)
      return false;
    // A bundle of two FNOPs: FNOP in X0 (bits 30-0) and X1 (bits 61-31).
    const uint64_t Nop = 0x51483000ULL | (0x50d46000ULL << 31);
    for (uint64_t I = 0; I < Count; I += 8)
      support::endian::write<uint64_t>(OS, Nop, llvm::endianness::little);
    return true;
  }
};
} // namespace

MCAsmBackend *llvm::createTileAsmBackend(const Target &T,
                                         const MCSubtargetInfo &STI,
                                         const MCRegisterInfo &MRI,
                                         const MCTargetOptions &Options) {
  uint8_t OSABI = MCELFObjectTargetWriter::getOSABI(STI.getTargetTriple().getOS());
  return new TileAsmBackend(OSABI);
}
