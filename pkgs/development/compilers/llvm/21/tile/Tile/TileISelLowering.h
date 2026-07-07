//===-- TileISelLowering.h - Tile DAG Lowering Interface --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that Tile uses to lower LLVM code into a
// selection DAG.
//
// This is a first-light subset: it lowers integer arguments, returns and calls
// well enough for simple functions. Address/TLS/float/vararg/byval lowering is
// staged in over later phases.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_TILE_TILEISELLOWERING_H
#define LLVM_LIB_TARGET_TILE_TILEISELLOWERING_H

#include "Tile.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetLowering.h"

namespace llvm {
class TileSubtarget;

namespace TileISD {
enum NodeType {
  // Start the numbering from where ISD NodeType finishes.
  FIRST_NUMBER = ISD::BUILTIN_OP_END,

  JmpLink,
  TailCall,
  SAR16,
  TLSRELAXADD0,
  TLSRELAXADD1,
  TLSGD,
  LDTLS,
  MOVE,
  Ret,
  MF,
  BFINS,
  BFEXTU,
  NET,
  BRINDJT,
  VAARG_SP,
  ALLOCA_SP,
  ALLOCA_ADDR
};
} // namespace TileISD

class TileTargetLowering : public TargetLowering {
public:
  explicit TileTargetLowering(const TargetMachine &TM,
                              const TileSubtarget &STI);

  bool isOffsetFoldingLegal(const GlobalAddressSDNode *GA) const override;

  // No native half-precision: soft-promote f16/bf16 to f32 with libcalls.
  bool softPromoteHalfType() const override { return true; }

  // TILE-Gx orders memory only through the explicit `mf` fence, so acquire and
  // release semantics come from fences the AtomicExpandPass brackets the access
  // with, leaving the load/store itself relaxed.
  bool shouldInsertFencesForAtomic(const Instruction *I) const override {
    return true;
  }

  // Only fetchadd/fetchand/fetchor/exch exist in hardware; the rest are built
  // from a compare-exchange loop.
  AtomicExpansionKind
  shouldExpandAtomicRMWInIR(AtomicRMWInst *AI) const override;

  // The libgcc personality routine delivers the exception pointer and type
  // selector to a landing pad in r12/r13 (EH_RETURN_DATA_REGNO(0,1) in the
  // tilegx ABI). Naming them here lets SelectionDAGBuilder materialize the
  // landingpad value instead of leaving it undefined.
  Register
  getExceptionPointerRegister(const Constant *PersonalityFn) const override;
  Register
  getExceptionSelectorRegister(const Constant *PersonalityFn) const override;

  bool isShuffleMaskLegal(ArrayRef<int> M, EVT VT) const override;

  unsigned getJumpTableEncoding() const override;

  MVT getScalarShiftAmountTy(const DataLayout &, EVT) const override {
    return MVT::i32;
  }

  SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;

  SDValue PerformDAGCombine(SDNode *N, DAGCombinerInfo &DCI) const override;

  const char *getTargetNodeName(unsigned Opcode) const override;

  EVT getSetCCResultType(const DataLayout &DL, LLVMContext &Context,
                         EVT VT) const override;

private:
  const TileSubtarget &Subtarget;

  SDValue getGlobalBaseReg(SelectionDAG &DAG) const;
  SDValue lowerGlobalAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerGlobalTLSAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerJumpTable(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerBR_JT(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerVASTART(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerVAARG(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerVACOPY(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerDYNAMIC_STACKALLOC(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerFRAMEADDR(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerRETURNADDR(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerConstantPool(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerFABS(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerFCOPYSIGN(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerATOMIC_LOAD(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerATOMIC_STORE(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerShiftLeftParts(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerShiftRightParts(SDValue Op, SelectionDAG &DAG, bool IsSRA) const;

  void passByValArg(SDValue Chain, const SDLoc &dl,
                    SmallVectorImpl<std::pair<unsigned, SDValue>> &RegsToPass,
                    SmallVectorImpl<SDValue> &MemOpChains, SDValue StackPtr,
                    MachineFrameInfo &MFI, SelectionDAG &DAG, SDValue Arg,
                    const CCValAssign &VA,
                    const ISD::ArgFlagsTy &Flags) const;

  SDValue LowerCallResult(SDValue Chain, SDValue InGlue,
                          CallingConv::ID CallConv, bool isVarArg,
                          const SmallVectorImpl<ISD::InputArg> &Ins,
                          const SDLoc &dl, SelectionDAG &DAG,
                          SmallVectorImpl<SDValue> &InVals) const;

  SDValue LowerFormalArguments(SDValue Chain, CallingConv::ID CallConv,
                               bool isVarArg,
                               const SmallVectorImpl<ISD::InputArg> &Ins,
                               const SDLoc &dl, SelectionDAG &DAG,
                               SmallVectorImpl<SDValue> &InVals) const override;

  SDValue LowerCall(TargetLowering::CallLoweringInfo &CLI,
                    SmallVectorImpl<SDValue> &InVals) const override;

  bool isEligibleForTailCallOptimization(const CCState &CCInfo,
                                         unsigned NextStackOffset,
                                         const CallLoweringInfo &CLI,
                                         const MachineFunction &MF) const;

  SDValue LowerReturn(SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      const SmallVectorImpl<SDValue> &OutVals, const SDLoc &dl,
                      SelectionDAG &DAG) const override;

  MachineBasicBlock *
  EmitInstrWithCustomInserter(MachineInstr &MI,
                              MachineBasicBlock *MBB) const override;

  ConstraintType getConstraintType(StringRef Constraint) const override;

  ConstraintWeight
  getSingleConstraintMatchWeight(AsmOperandInfo &info,
                                 const char *constraint) const override;

  std::pair<unsigned, const TargetRegisterClass *>
  getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                               StringRef Constraint, MVT VT) const override;

  void LowerAsmOperandForConstraint(SDValue Op, StringRef Constraint,
                                    std::vector<SDValue> &Ops,
                                    SelectionDAG &DAG) const override;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_TILE_TILEISELLOWERING_H
