//===-- TileTargetMachine.cpp - Define TargetMachine for Tile -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements the info about Tile target spec.
//
//===----------------------------------------------------------------------===//

#include "TileTargetMachine.h"
#include "Tile.h"
#include "TileMachineFunction.h"
#include "TargetInfo/TileTargetInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include <optional>

using namespace llvm;

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void LLVMInitializeTileTarget() {
  RegisterTargetMachine<TileTargetMachine> X(getTheTileTarget());

  PassRegistry &PR = *PassRegistry::getPassRegistry();
  initializeTileDAGToDAGISelLegacyPass(PR);
  initializeTileVLIWPacketizerPass(PR);
}

static Reloc::Model getEffectiveRelocModel(std::optional<Reloc::Model> RM) {
  return RM.value_or(Reloc::Static);
}

static std::string computeDataLayout(const Triple &TT) {
  return "e-m:e-p:64:64-i8:8:32-i16:16:32-i64:64-n32:64-S128";
}

TileTargetMachine::TileTargetMachine(const Target &T, const Triple &TT,
                                     StringRef CPU, StringRef FS,
                                     const TargetOptions &Options,
                                     std::optional<Reloc::Model> RM,
                                     std::optional<CodeModel::Model> CM,
                                     CodeGenOptLevel OL, bool JIT)
    : CodeGenTargetMachineImpl(T, computeDataLayout(TT), TT, CPU, FS, Options,
                               getEffectiveRelocModel(RM),
                               getEffectiveCodeModel(CM, CodeModel::Small), OL),
      TLOF(std::make_unique<TargetLoweringObjectFileELF>()) {
  initAsmInfo();
}

TileTargetMachine::~TileTargetMachine() = default;

const TileSubtarget *
TileTargetMachine::getSubtargetImpl(const Function &F) const {
  Attribute CPUAttr = F.getFnAttribute("target-cpu");
  Attribute TuneAttr = F.getFnAttribute("tune-cpu");
  Attribute FSAttr = F.getFnAttribute("target-features");

  std::string CPU =
      CPUAttr.isValid() ? CPUAttr.getValueAsString().str() : TargetCPU;
  std::string TuneCPU =
      TuneAttr.isValid() ? TuneAttr.getValueAsString().str() : CPU;
  std::string FS =
      FSAttr.isValid() ? FSAttr.getValueAsString().str() : TargetFS;

  auto &I = SubtargetMap[CPU + FS];
  if (!I)
    I = std::make_unique<TileSubtarget>(CPU, TuneCPU, FS, *this);
  return I.get();
}

MachineFunctionInfo *TileTargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return TileFunctionInfo::create<TileFunctionInfo>(Allocator, F, STI);
}

namespace {
class TilePassConfig : public TargetPassConfig {
public:
  TilePassConfig(TileTargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {}

  TileTargetMachine &getTileTargetMachine() const {
    return getTM<TileTargetMachine>();
  }

  void addIRPasses() override;
  bool addInstSelector() override;
  void addPreSched2() override;
  void addPreEmitPass() override;
};
} // namespace

TargetPassConfig *TileTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new TilePassConfig(*this, PM);
}

void TilePassConfig::addIRPasses() {
  // Bracket ordered atomic accesses with `mf` fences and lower wide atomics to
  // libcalls; TILE-Gx has no ordering-carrying load/store form.
  addPass(createAtomicExpandLegacyPass());
  TargetPassConfig::addIRPasses();
}

bool TilePassConfig::addInstSelector() {
  addPass(createTileISelDag(getTileTargetMachine(), getOptLevel()));
  return false;
}

void TilePassConfig::addPreSched2() {
  addPass(createTileExpandPseudoPass());
}

void TilePassConfig::addPreEmitPass() {
  // Pack independent instructions into VLIW bundles once pseudos are expanded,
  // registers are allocated and the frame is final. Bundling is pure
  // performance, so only run it above -O0.
  if (getOptLevel() != CodeGenOptLevel::None)
    addPass(createTileVLIWPacketizer());
}
