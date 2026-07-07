//===-- TileVLIWPacketizer.cpp - TILE-Gx VLIW Packetizer ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass packs independent instructions into real TILE-Gx VLIW bundles.
//
// The backend keeps exactly one fixed-slot encoding per instruction: a given
// mnemonic is either X0-encoded or X1-encoded (tilegx encodes the same op
// differently per pipe and the backend does not carry both forms). A bundle
// therefore pairs one X0-encoded instruction with one X1-encoded instruction
// in the two-slot X format; the code emitter merges the two halves into one
// 64-bit word. This is the common scalar-ILP case (a multiply, floating-point
// helper, mm/bit op in X0 alongside an add/logic/load/store in X1).
//
// Y-format (three slots) is not emitted: the backend has no Y-format encoding
// classes, so there is nothing to pack into the third slot. Control-flow,
// calls and inline asm are left solo for safety; they are correct unpacked and
// packing them buys little.
//
//===----------------------------------------------------------------------===//

#include "Tile.h"
#include "TileInstrInfo.h"
#include "TileSubtarget.h"
#include "MCTargetDesc/TileBaseInfo.h"
#include "llvm/CodeGen/DFAPacketizer.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

#define DEBUG_TYPE "tile-packetizer"

static cl::opt<bool>
    DisablePacketizer("tile-no-packetizer", cl::Hidden, cl::init(false),
                      cl::desc("Disable TILE-Gx VLIW packetization"));

namespace {

class TileVLIWPacketizer : public MachineFunctionPass {
public:
  static char ID;

  TileVLIWPacketizer() : MachineFunctionPass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<AAResultsWrapperPass>();
    AU.addRequired<MachineLoopInfoWrapperPass>();
    AU.addPreserved<MachineLoopInfoWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  StringRef getPassName() const override { return "TILE-Gx VLIW Packetizer"; }

  bool runOnMachineFunction(MachineFunction &Fn) override;
};

char TileVLIWPacketizer::ID = 0;

class TileVLIWPacketizerList : public VLIWPacketizerList {
public:
  TileVLIWPacketizerList(MachineFunction &MF, MachineLoopInfo &MLI,
                         AAResults *AA)
      : VLIWPacketizerList(MF, MLI, AA) {}

  bool ignorePseudoInstruction(const MachineInstr &MI,
                               const MachineBasicBlock *MBB) override;
  bool isSoloInstruction(const MachineInstr &MI) override;
  bool isLegalToPacketizeTogether(SUnit *SUI, SUnit *SUJ) override;

private:
  // True if MI is encoded for the X0 pipe (low slot). The complement is the X1
  // pipe (high slot); branches, jumps and the address/halfword sequence are all
  // X1-encoded.
  bool isX0Encoded(const MachineInstr &MI) const {
    return (MI.getDesc().TSFlags >> TileII::EncPipePos) & TileII::EncPipeMask;
  }

  // True if MI is an X1-encoded integer ALU op that also has an X0-pipe encoding,
  // so the packetizer can move it into the X0 slot to free X1 for another op.
  bool hasX0Twin(const MachineInstr &MI) const {
    return TileII::getX0PipeTwinOpcode(MI.getOpcode()) != 0;
  }
};

} // namespace

bool TileVLIWPacketizerList::ignorePseudoInstruction(
    const MachineInstr &MI, const MachineBasicBlock *) {
  // Meta instructions (CFI, IMPLICIT_DEF, KILL, debug) carry no encoding.
  return MI.isMetaInstruction();
}

bool TileVLIWPacketizerList::isSoloInstruction(const MachineInstr &MI) {
  // Position-sensitive meta that lowers to a label or CFI directive must break
  // the packet. The packetizer only skips ignored pseudos without ending the
  // packet, so anything left between two packetized instructions is swept into
  // the finalized bundle by finalizeBundle. The bundle emitter cannot encode
  // such an entry and drops it, which silently deletes an EH call-site label
  // and leaves the exception table referencing an undefined symbol.
  if (MI.isEHLabel() || MI.isGCLabel() || MI.isLabel() || MI.isCFIInstruction())
    return true;

  // Keep control flow, calls, inline asm and anything without a real X0/X1
  // encoding in their own bundle. They are correct unpacked, and packing them
  // is both lower value and higher risk.
  if (MI.isInlineAsm() || MI.isCall() || MI.isReturn() || MI.isBranch() ||
      MI.isTerminator() || MI.isIndirectBranch())
    return true;

  const uint64_t F = MI.getDesc().TSFlags;
  if ((F >> TileII::SoloPos) & TileII::SoloMask)
    return true;

  unsigned Format = (F >> TileII::FormatTypePos) & TileII::FormatTypeMask;
  if (Format == TileII::FrmPseudo)
    return true;

  // Loads and stores are emitted in the Y2 bundle mode and cannot be merged by
  // the simple slot-OR the code emitter uses, so keep them solo.
  if (!TileII::isBundleMergeable(F))
    return true;

  return false;
}

// SUI is the candidate instruction (later in program order); SUJ is already in
// the current packet (earlier). A pair is legal only when the two occupy
// complementary encoding slots and SUI carries no read-after-write or
// write-after-write hazard against SUJ. Bundle semantics read every operand at
// issue, so anti-dependences (write-after-read) are safe to share a bundle.
bool TileVLIWPacketizerList::isLegalToPacketizeTogether(SUnit *SUI,
                                                        SUnit *SUJ) {
  const MachineInstr &I = *SUI->getInstr();
  const MachineInstr &J = *SUJ->getInstr();

  // An X-format bundle has exactly two issue slots, X0 and X1.
  if (CurrentPacketMIs.size() >= 2)
    return false;

  bool IX0 = isX0Encoded(I), JX0 = isX0Encoded(J);
  if (IX0 == JX0) {
    // Same native pipe. Two X0-encoded ops cannot share a bundle: the backend
    // carries no X1 encoding for X0-native ops (multiply, fp helpers, mm). Two
    // X1-encoded ops can, but only if at least one is an integer ALU op with an
    // X0 twin that endPacket can move into the low slot.
    if (IX0)
      return false;
    if (!hasX0Twin(I) && !hasX0Twin(J))
      return false;
  }

  if (!SUJ->isSucc(SUI))
    return true;

  for (const SDep &Dep : SUJ->Succs) {
    if (Dep.getSUnit() != SUI)
      continue;
    // RAW (Data), WAW (Output) and memory/side-effect Order edges all force a
    // serialization that a single bundle cannot honor. Only WAR is bundle-safe.
    if (Dep.getKind() != SDep::Anti)
      return false;
  }

  return true;
}

bool TileVLIWPacketizer::runOnMachineFunction(MachineFunction &Fn) {
  if (DisablePacketizer)
    return false;

  const TileInstrInfo *TII = Fn.getSubtarget<TileSubtarget>().getInstrInfo();
  MachineLoopInfo &MLI = getAnalysis<MachineLoopInfoWrapperPass>().getLI();
  AAResults *AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();

  TileVLIWPacketizerList Packetizer(Fn, MLI, AA);
  assert(Packetizer.getResourceTracker() && "Empty DFA table!");

  for (MachineBasicBlock &MBB : Fn) {
    auto Begin = MBB.begin(), End = MBB.end();
    while (Begin != End) {
      // A scheduling region runs from one boundary to the next.
      MachineBasicBlock::iterator RB = Begin;
      while (RB != End && TII->isSchedulingBoundary(*RB, &MBB, Fn))
        ++RB;
      MachineBasicBlock::iterator RE = RB;
      while (RE != End && !TII->isSchedulingBoundary(*RE, &MBB, Fn))
        ++RE;
      if (RE != End)
        ++RE;
      if (RB != End)
        Packetizer.PacketizeMIs(&MBB, RB, RE);
      Begin = RE;
    }
  }

  return true;
}

INITIALIZE_PASS(TileVLIWPacketizer, "tile-packetizer",
                "TILE-Gx VLIW Packetizer", false, false)

FunctionPass *llvm::createTileVLIWPacketizer() {
  return new TileVLIWPacketizer();
}
