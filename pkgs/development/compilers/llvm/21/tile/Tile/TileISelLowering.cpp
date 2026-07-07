//===-- TileISelLowering.cpp - Tile DAG Lowering Implementation -----------===//
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
// First-light subset: enough to lower integer formal arguments and returns for
// simple functions. Calls, byval, varargs, address/TLS and float lowering are
// staged in over later phases.
//
//===----------------------------------------------------------------------===//

#include "TileISelLowering.h"
#include "MCTargetDesc/TileBaseInfo.h"
#include "TileCallingConv.h"
#include "TileMachineFunction.h"
#include "TileSubtarget.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define DEBUG_TYPE "tile-lower"

static const MCPhysReg TileIntRegs[TILEGX_AREG_NUM] = {
    Tile::R0, Tile::R1, Tile::R2, Tile::R3, Tile::R4,
    Tile::R5, Tile::R6, Tile::R7, Tile::R8, Tile::R9};

static bool CC_TileByval(unsigned ValNo, MVT ValVT, MVT LocVT,
                         CCValAssign::LocInfo LocInfo, ISD::ArgFlagsTy ArgFlags,
                         CCState &State) {
  Align Alignment = std::max(ArgFlags.getNonZeroByValAlign(), Align(8));
  unsigned Size = alignTo(ArgFlags.getByValSize(), Alignment);
  unsigned FirstIdx = State.getFirstUnallocated(TileIntRegs);

  assert(Alignment <= Align(16) && "Cannot handle alignments larger than 16.");

  // If byval is 16-byte aligned, the first arg register must be even.
  if (Alignment == Align(16) && (FirstIdx % 2)) {
    State.AllocateReg(TileIntRegs[FirstIdx]);
    ++FirstIdx;
  }

  for (unsigned I = FirstIdx; Size && (I < TILEGX_AREG_NUM); Size -= 8, ++I)
    State.AllocateReg(TileIntRegs[I]);

  unsigned Offset = State.AllocateStack(Size, Alignment);

  if (FirstIdx < TILEGX_AREG_NUM)
    State.addLoc(CCValAssign::getReg(ValNo, ValVT, TileIntRegs[FirstIdx], LocVT,
                                     LocInfo));
  else
    State.addLoc(CCValAssign::getMem(ValNo, ValVT, Offset, LocVT, LocInfo));

  return true;
}

#include "TileGenCallingConv.inc"

TileTargetLowering::TileTargetLowering(const TargetMachine &TM,
                                       const TileSubtarget &STI)
    : TargetLowering(TM), Subtarget(STI) {
  setBooleanContents(ZeroOrOneBooleanContent);
  setBooleanVectorContents(ZeroOrOneBooleanContent);

  addRegisterClass(MVT::i64, &Tile::CPURegsRegClass);
  addRegisterClass(MVT::i32, &Tile::CPU32RegsRegClass);

  // TILE-Gx has no dedicated float register file; float and double values live
  // in the general registers and the few fp helper instructions assist a short
  // software sequence. Reuse the integer register classes for f32/f64 so the
  // fsingle/fdouble helper opcodes (lowered in DAGToDAG) and the soft-float
  // libcalls both operate on them.
  addRegisterClass(MVT::f32, &Tile::CPU32RegsRegClass);
  addRegisterClass(MVT::f64, &Tile::CPURegsRegClass);

  setStackPointerRegisterToSaveRestore(Tile::SP);

  // i1 loads always widen to a full register.
  setLoadExtAction(ISD::EXTLOAD, MVT::i64, MVT::i1, Promote);
  setLoadExtAction(ISD::ZEXTLOAD, MVT::i64, MVT::i1, Promote);
  setLoadExtAction(ISD::SEXTLOAD, MVT::i64, MVT::i1, Promote);

  // Population count on 32-bit values runs through the 64-bit pcnt.
  setOperationAction(ISD::CTPOP, MVT::i32, Promote);

  // Comparisons produce a 64-bit boolean; the 32-bit form is promoted so the
  // high bits are well defined when a setcc feeds a 64-bit consumer.
  setOperationAction(ISD::SETCC, MVT::i32, Promote);

  // The condition of a branch and the result of a select stay as Tile nodes
  // and are matched by the cmoveqz/cmovnez and beqz/bnez patterns; expanding
  // BR_CC and SELECT_CC reduces them onto setcc + these primitives.
  setOperationAction(ISD::BR_CC, MVT::i32, Expand);
  setOperationAction(ISD::BR_CC, MVT::i64, Expand);
  setOperationAction(ISD::SELECT_CC, MVT::i32, Expand);
  setOperationAction(ISD::SELECT_CC, MVT::i64, Expand);
  setOperationAction(ISD::SELECT, MVT::i64, Custom);
  setOperationAction(ISD::BRCOND, MVT::Other, Custom);

  // TILE-Gx has no single 64-bit multiply; it is expanded to a multiply-add
  // sequence during selection, so keep the node off the legalizer's libcall
  // path by marking it Custom.
  setOperationAction(ISD::MUL, MVT::i64, Custom);
  // The high half of a 64x64 multiply has no single instruction either; it is
  // built from the same 32-bit partial products during selection (i64) or by
  // widening to a full i64 product (i32). Keeping MULHU/MULHS Custom rather than
  // Legal both supplies the missing pattern and tells the magic-number divide
  // combiner that a multiply-high is available, so divide/modulo by a constant
  // selects instead of crashing.
  setOperationAction(ISD::MULHU, MVT::i64, Custom);
  setOperationAction(ISD::MULHS, MVT::i64, Custom);
  setOperationAction(ISD::MULHU, MVT::i32, Custom);
  setOperationAction(ISD::MULHS, MVT::i32, Custom);
  setOperationAction(ISD::SDIV, MVT::i32, Expand);
  setOperationAction(ISD::SREM, MVT::i32, Expand);
  setOperationAction(ISD::SDIV, MVT::i64, Expand);
  setOperationAction(ISD::SREM, MVT::i64, Expand);
  setOperationAction(ISD::UDIV, MVT::i32, Expand);
  setOperationAction(ISD::UREM, MVT::i32, Expand);
  setOperationAction(ISD::UDIV, MVT::i64, Expand);
  setOperationAction(ISD::UREM, MVT::i64, Expand);
  setOperationAction(ISD::SDIVREM, MVT::i32, Expand);
  setOperationAction(ISD::UDIVREM, MVT::i32, Expand);
  setOperationAction(ISD::SDIVREM, MVT::i64, Expand);
  setOperationAction(ISD::UDIVREM, MVT::i64, Expand);
  setOperationAction(ISD::UMUL_LOHI, MVT::i64, Expand);
  setOperationAction(ISD::SMUL_LOHI, MVT::i64, Expand);

  // Only left rotate is provided; right rotate becomes a negated left rotate.
  setOperationAction(ISD::ROTR, MVT::i32, Expand);
  setOperationAction(ISD::ROTR, MVT::i64, Expand);
  setOperationAction(ISD::ROTL, MVT::i32, Expand);

  // Sub-word sign extension is realized with a shift pair.
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1, Expand);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i8, Expand);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i16, Expand);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i32, Expand);

  // Addresses are materialized with the moveli/shl16insli halfword sequence.
  setOperationAction(ISD::GlobalAddress, MVT::i64, Custom);
  setOperationAction(ISD::GlobalTLSAddress, MVT::i64, Custom);
  setOperationAction(ISD::JumpTable, MVT::i64, Custom);
  setOperationAction(ISD::ConstantPool, MVT::i64, Custom);
  setOperationAction(ISD::BR_JT, MVT::Other, Custom);

  // va_list is a two-pointer struct {next_arg, sp_at_entry}; the reserved
  // bottom 16 bytes of each frame force a custom va_arg that steps over them.
  setOperationAction(ISD::VASTART, MVT::Other, Custom);
  setOperationAction(ISD::VAARG, MVT::Other, Custom);
  setOperationAction(ISD::VACOPY, MVT::Other, Custom);
  setOperationAction(ISD::VAEND, MVT::Other, Expand);

  // alloca keeps the 16-byte reserve at the new top of stack, so it is lowered
  // by hand rather than through the generic stack-pointer adjust.
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i64, Custom);

  setOperationAction(ISD::FRAMEADDR, MVT::i64, Custom);
  setOperationAction(ISD::RETURNADDR, MVT::i64, Custom);

  // Floating point.
  //
  // TILE-Gx mirrors GCC-12: FADD/FSUB/FMUL on f32 and f64 are expanded inline
  // during selection into the fsingle/fdouble helper instruction sequences, and
  // the float compares are matched by the .td cmp pseudos and finished in
  // TileExpandPseudo. Everything else (division, square root, conversions to and
  // from integer, the float<->double widen/narrow, remainder and the
  // transcendentals) has no md pattern in GCC and is left to the soft-float
  // libcalls, whose names come from the default libgcc set already wired in
  // RuntimeLibcalls.td. The result links against the MDE libgcc/libm runtime.
  for (MVT VT : {MVT::f32, MVT::f64}) {
    setOperationAction(ISD::FDIV, VT, Expand);
    setOperationAction(ISD::FREM, VT, Expand);
    setOperationAction(ISD::FSQRT, VT, Expand);
    setOperationAction(ISD::FMA, VT, Expand);
    setOperationAction(ISD::FNEG, VT, Expand);
    setOperationAction(ISD::FSIN, VT, Expand);
    setOperationAction(ISD::FCOS, VT, Expand);
    setOperationAction(ISD::FSINCOS, VT, Expand);
    setOperationAction(ISD::FPOW, VT, Expand);
    setOperationAction(ISD::FPOWI, VT, Expand);
    setOperationAction(ISD::FLOG, VT, Expand);
    setOperationAction(ISD::FLOG2, VT, Expand);
    setOperationAction(ISD::FLOG10, VT, Expand);
    setOperationAction(ISD::FEXP, VT, Expand);
    setOperationAction(ISD::FEXP2, VT, Expand);
    setOperationAction(ISD::FCEIL, VT, Expand);
    setOperationAction(ISD::FTRUNC, VT, Expand);
    setOperationAction(ISD::FRINT, VT, Expand);
    setOperationAction(ISD::FNEARBYINT, VT, Expand);
    setOperationAction(ISD::FROUND, VT, Expand);
    setOperationAction(ISD::FROUNDEVEN, VT, Expand);
    setOperationAction(ISD::FFLOOR, VT, Expand);
    setOperationAction(ISD::FMINNUM, VT, Expand);
    setOperationAction(ISD::FMAXNUM, VT, Expand);
    setOperationAction(ISD::BR_CC, VT, Expand);
    setOperationAction(ISD::SELECT_CC, VT, Expand);
    // There is no fp immediate form, and the getNode bitcast-of-constant fold
    // would undo an inline integer materialization, so float literals go to the
    // constant pool (matching GCC-12 and the 3.3 fork).
    setOperationAction(ISD::ConstantFP, VT, Expand);
    // Sign manipulation is a couple of bitfield ops on the integer view.
    setOperationAction(ISD::FABS, VT, Custom);
    setOperationAction(ISD::FCOPYSIGN, VT, Custom);
  }

  // Integer <-> floating point conversions and the float/double widen and
  // narrow are soft-float libcalls (no GCC md pattern). With f32/f64 legal the
  // generic legalizer turns these Expand actions into the right libcall.
  for (MVT IntVT : {MVT::i32, MVT::i64}) {
    setOperationAction(ISD::SINT_TO_FP, IntVT, Expand);
    setOperationAction(ISD::UINT_TO_FP, IntVT, Expand);
    setOperationAction(ISD::FP_TO_SINT, IntVT, Expand);
    setOperationAction(ISD::FP_TO_UINT, IntVT, Expand);
  }
  setOperationAction(ISD::FP_EXTEND, MVT::f64, Expand);
  setOperationAction(ISD::FP_ROUND, MVT::f32, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::f64, MVT::f32, Expand);
  setTruncStoreAction(MVT::f64, MVT::f32, Expand);

  // The fp compare helpers compute the ordered result and the unordered flag in
  // one shot, so the unordered predicates are cheap but the ordered ones are
  // simpler to synthesize as SETO combined with the matching unordered compare.
  for (MVT VT : {MVT::f32, MVT::f64}) {
    setCondCodeAction(ISD::SETOEQ, VT, Expand);
    setCondCodeAction(ISD::SETONE, VT, Expand);
    setCondCodeAction(ISD::SETOLT, VT, Expand);
    setCondCodeAction(ISD::SETOLE, VT, Expand);
    setCondCodeAction(ISD::SETOGT, VT, Expand);
    setCondCodeAction(ISD::SETOGE, VT, Expand);
  }

  setTargetDAGCombine(ISD::SELECT);
  setTargetDAGCombine(ISD::ZERO_EXTEND);

  setMinFunctionAlignment(Align(8));

  // TILE-Gx has no native half-precision support. Treat f16/bf16 as storage
  // only and soft-promote them to f32 (with __extendhfsf2/__truncsfhf2 libcall
  // conversions), mirroring the no-Zfh RISC-V path. softPromoteHalfType() is
  // overridden to true so half values in arguments, returns and phis are
  // softened before ISel; without this, fp16 conversions fail "Cannot select"
  // and half-typed values trip a FunctionLoweringInfo register-init assertion.
  for (MVT ValVT : {MVT::f32, MVT::f64}) {
    setTruncStoreAction(ValVT, MVT::f16, Expand);
    setLoadExtAction(ISD::EXTLOAD, ValVT, MVT::f16, Expand);
    setTruncStoreAction(ValVT, MVT::bf16, Expand);
    setLoadExtAction(ISD::EXTLOAD, ValVT, MVT::bf16, Expand);
  }
  for (MVT ValVT : {MVT::f32, MVT::f64}) {
    setOperationAction(ISD::FP_TO_FP16, ValVT, Expand);
    setOperationAction(ISD::FP16_TO_FP, ValVT, Expand);
    setOperationAction(ISD::STRICT_FP_TO_FP16, ValVT, Expand);
    setOperationAction(ISD::STRICT_FP16_TO_FP, ValVT, Expand);
  }

  // Atomic load/store have no dedicated instruction: an aligned ld/st is
  // already atomic, and ordering is supplied by the fences AtomicExpandPass
  // brackets the access with. Rewrite the atomic node into a plain load/store
  // (keeping the extension and the atomic memory operand) during legalization.
  // The action is keyed on the value type: i32 covers the i8/i16/i32 accesses
  // (their value is promoted to i32) and i64 the doubleword access.
  for (MVT VT : {MVT::i32, MVT::i64}) {
    setOperationAction(ISD::ATOMIC_LOAD, VT, Custom);
    setOperationAction(ISD::ATOMIC_STORE, VT, Custom);
  }

  // The fetch/exchange/compare-exchange instructions cover atomics up to a
  // doubleword; without this the AtomicExpandPass libcall-izes every atomic.
  setMaxAtomicSizeInBitsSupported(64);

  // Wide (i128) shifts are expanded to a pair of doubleword shifts glued by
  // these nodes; synthesize them from the legal 64-bit shifts and a select.
  for (unsigned Op : {ISD::SHL_PARTS, ISD::SRL_PARTS, ISD::SRA_PARTS})
    setOperationAction(Op, MVT::i64, Custom);

  computeRegisterProperties(Subtarget.getRegisterInfo());
}

const char *TileTargetLowering::getTargetNodeName(unsigned Opcode) const {
  switch (Opcode) {
  case TileISD::TLSGD:
    return "TileISD::TLSGD";
  case TileISD::LDTLS:
    return "TileISD::LDTLS";
  case TileISD::TLSRELAXADD0:
    return "TileISD::TLSRELAXADD0";
  case TileISD::TLSRELAXADD1:
    return "TileISD::TLSRELAXADD1";
  case TileISD::MOVE:
    return "TileISD::MOVE";
  case TileISD::SAR16:
    return "TileISD::SAR16";
  case TileISD::JmpLink:
    return "TileISD::JmpLink";
  case TileISD::Ret:
    return "TileISD::Ret";
  case TileISD::MF:
    return "TileISD::MF";
  case TileISD::BFINS:
    return "TileISD::BFINS";
  case TileISD::BFEXTU:
    return "TileISD::BFEXTU";
  case TileISD::BRINDJT:
    return "TileISD::BRINDJT";
  case TileISD::VAARG_SP:
    return "TileISD::VAARG_SP";
  case TileISD::ALLOCA_SP:
    return "TileISD::ALLOCA_SP";
  case TileISD::ALLOCA_ADDR:
    return "TileISD::ALLOCA_ADDR";
  default:
    return nullptr;
  }
}

EVT TileTargetLowering::getSetCCResultType(const DataLayout &, LLVMContext &,
                                           EVT VT) const {
  if (!VT.isVector())
    return MVT::i32;
  return VT.changeVectorElementTypeToInteger();
}

unsigned TileTargetLowering::getJumpTableEncoding() const {
  // Absolute 64-bit block addresses in .rodata; lowerBR_JT loads an entry and
  // jumps to it. The generic AsmPrinter emits these entries directly.
  return MachineJumpTableInfo::EK_BlockAddress;
}

bool TileTargetLowering::isOffsetFoldingLegal(
    const GlobalAddressSDNode *GA) const {
  return false;
}

bool TileTargetLowering::isShuffleMaskLegal(ArrayRef<int> M, EVT VT) const {
  return false;
}

// getGlobalBaseReg pins r51 and marks the function so the AsmPrinter emits the
// PIC header that loads the GOT base into it.
SDValue TileTargetLowering::getGlobalBaseReg(SelectionDAG &DAG) const {
  TileFunctionInfo *FI = DAG.getMachineFunction().getInfo<TileFunctionInfo>();
  return DAG.getRegister(FI->getGlobalBaseReg(), MVT::i64);
}

SDValue TileTargetLowering::lowerGlobalAddress(SDValue Op,
                                               SelectionDAG &DAG) const {
  const GlobalAddressSDNode *GA = cast<GlobalAddressSDNode>(Op);
  SDLoc DL(Op);
  const GlobalValue *GV = GA->getGlobal();
  int64_t Offset = GA->getOffset();

  if (getTargetMachine().getRelocationModel() == Reloc::PIC_) {
    // GOT-relative: build the 32-bit GOT offset (moveli hw1_last_got,
    // shl16insli hw0_got), add the GOT base, then load the entry to get the
    // symbol address. Every symbol is routed through the GOT, which always
    // links; GCC additionally short-circuits locals to a PC-relative form.
    SDValue High =
        DAG.getTargetGlobalAddress(GV, DL, MVT::i64, 0, TileII::MO_HW1_LAST_GOT);
    SDValue Low =
        DAG.getTargetGlobalAddress(GV, DL, MVT::i64, 0, TileII::MO_HW0_GOT);
    SDValue Off = DAG.getNode(TileISD::MOVE, DL, MVT::i64, High);
    Off = DAG.getNode(TileISD::SAR16, DL, MVT::i64, Off, Low);
    SDValue Entry =
        DAG.getNode(ISD::ADD, DL, MVT::i64, getGlobalBaseReg(DAG), Off);
    SDValue Addr =
        DAG.getLoad(MVT::i64, DL, DAG.getEntryNode(), Entry,
                    MachinePointerInfo::getGOT(DAG.getMachineFunction()));
    if (Offset != 0)
      Addr = DAG.getNode(ISD::ADD, DL, MVT::i64, Addr,
                         DAG.getConstant(Offset, DL, MVT::i64));
    return Addr;
  }

  // Absolute 64-bit address: moveli hw2_last, shl16insli hw1, shl16insli hw0.
  SDValue High =
      DAG.getTargetGlobalAddress(GV, DL, MVT::i64, Offset, TileII::MO_HW2_LAST);
  SDValue Middle =
      DAG.getTargetGlobalAddress(GV, DL, MVT::i64, Offset, TileII::MO_HW1);
  SDValue Low =
      DAG.getTargetGlobalAddress(GV, DL, MVT::i64, Offset, TileII::MO_HW0);

  SDValue Addr = DAG.getNode(TileISD::MOVE, DL, MVT::i64, High);
  Addr = DAG.getNode(TileISD::SAR16, DL, MVT::i64, Addr, Middle);
  return DAG.getNode(TileISD::SAR16, DL, MVT::i64, Addr, Low);
}

SDValue TileTargetLowering::lowerGlobalTLSAddress(SDValue Op,
                                                  SelectionDAG &DAG) const {
  const GlobalAddressSDNode *GA = cast<GlobalAddressSDNode>(Op);
  SDLoc DL(Op);
  const GlobalValue *GV = GA->getGlobal();
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  TLSModel::Model Model = getTargetMachine().getTLSModel(GV);

  if (Model == TLSModel::GeneralDynamic || Model == TLSModel::LocalDynamic) {
    // moveli hw1_last_tls_gd / shl16insli hw0_tls_gd build the GOT offset of the
    // tls_index pair; addi tls_add(sym) adds the GOT base; jal tls_gd_call(sym)
    // invokes __tls_get_addr; addi tls_gd_add(sym) finalizes the result.
    SDValue High =
        DAG.getTargetGlobalAddress(GV, DL, PtrVT, 0, TileII::MO_HW1_LAST_TLS_GD);
    SDValue Low =
        DAG.getTargetGlobalAddress(GV, DL, PtrVT, 0, TileII::MO_HW0_TLS_GD);
    SDValue TAdd =
        DAG.getTargetGlobalAddress(GV, DL, PtrVT, 0, TileII::MO_TLS_ADD);
    SDValue TGdAdd =
        DAG.getTargetGlobalAddress(GV, DL, PtrVT, 0, TileII::MO_TLS_GD_ADD);

    SDValue Arg = DAG.getNode(TileISD::MOVE, DL, PtrVT, High);
    Arg = DAG.getNode(TileISD::TLSGD, DL, PtrVT, Arg, Low);
    Arg = DAG.getNode(TileISD::TLSRELAXADD0, DL, PtrVT, getGlobalBaseReg(DAG),
                      TAdd, Arg);

    Type *PtrTy = PointerType::get(*DAG.getContext(), 0);
    ArgListTy Args;
    ArgListEntry Entry;
    Entry.Node = Arg;
    Entry.Ty = PtrTy;
    Args.push_back(Entry);

    TargetLowering::CallLoweringInfo CLI(DAG);
    CLI.setDebugLoc(DL)
        .setChain(DAG.getEntryNode())
        .setLibCallee(CallingConv::C, PtrTy, Op, std::move(Args));
    std::pair<SDValue, SDValue> CallResult = LowerCallTo(CLI);
    return DAG.getNode(TileISD::TLSRELAXADD1, DL, PtrVT, CallResult.first,
                       TGdAdd);
  }

  SDValue Result;
  if (Model == TLSModel::InitialExec) {
    // moveli hw1_last_tls_ie / shl16insli hw0_tls_ie build the GOT offset of the
    // tpoff entry; addi tls_add adds the GOT base; ld_tls tls_ie_load loads the
    // tpoff; add tp yields the address.
    SDValue High =
        DAG.getTargetGlobalAddress(GV, DL, PtrVT, 0, TileII::MO_HW1_LAST_TLS_IE);
    SDValue Low =
        DAG.getTargetGlobalAddress(GV, DL, PtrVT, 0, TileII::MO_HW0_TLS_IE);
    SDValue TAdd =
        DAG.getTargetGlobalAddress(GV, DL, PtrVT, 0, TileII::MO_TLS_ADD);
    SDValue TLoad =
        DAG.getTargetGlobalAddress(GV, DL, PtrVT, 0, TileII::MO_TLS_IE_LOAD);
    Result = DAG.getNode(TileISD::MOVE, DL, PtrVT, High);
    Result = DAG.getNode(TileISD::TLSGD, DL, PtrVT, Result, Low);
    Result = DAG.getNode(TileISD::TLSRELAXADD0, DL, PtrVT, getGlobalBaseReg(DAG),
                         TAdd, Result);
    Result = DAG.getNode(TileISD::LDTLS, DL, PtrVT, Result, TLoad);
  } else if (Model == TLSModel::LocalExec) {
    // moveli hw1_last_tls_le / shl16insli hw0_tls_le build the tpoff directly;
    // add tp yields the address. No GOT.
    SDValue High =
        DAG.getTargetGlobalAddress(GV, DL, PtrVT, 0, TileII::MO_HW1_LAST_TLS_LE);
    SDValue Low =
        DAG.getTargetGlobalAddress(GV, DL, PtrVT, 0, TileII::MO_HW0_TLS_LE);
    Result = DAG.getNode(TileISD::MOVE, DL, PtrVT, High);
    Result = DAG.getNode(TileISD::TLSGD, DL, PtrVT, Result, Low);
  } else {
    report_fatal_error("Tile: unsupported TLS model");
  }

  return DAG.getNode(ISD::ADD, DL, PtrVT, Result,
                     DAG.getRegister(Tile::TP, PtrVT));
}

SDValue TileTargetLowering::lowerJumpTable(SDValue Op, SelectionDAG &DAG) const {
  const JumpTableSDNode *JT = cast<JumpTableSDNode>(Op);
  SDLoc DL(Op);
  EVT PtrVT = Op.getValueType();

  if (getTargetMachine().getRelocationModel() == Reloc::PIC_) {
    SDValue High =
        DAG.getTargetJumpTable(JT->getIndex(), PtrVT, TileII::MO_HW1_LAST_GOT);
    SDValue Low =
        DAG.getTargetJumpTable(JT->getIndex(), PtrVT, TileII::MO_HW0_GOT);
    SDValue Off = DAG.getNode(TileISD::MOVE, DL, PtrVT, High);
    Off = DAG.getNode(TileISD::SAR16, DL, PtrVT, Off, Low);
    SDValue Entry =
        DAG.getNode(ISD::ADD, DL, PtrVT, getGlobalBaseReg(DAG), Off);
    return DAG.getLoad(PtrVT, DL, DAG.getEntryNode(), Entry,
                       MachinePointerInfo::getGOT(DAG.getMachineFunction()));
  }

  SDValue High =
      DAG.getTargetJumpTable(JT->getIndex(), PtrVT, TileII::MO_HW2_LAST);
  SDValue Middle = DAG.getTargetJumpTable(JT->getIndex(), PtrVT, TileII::MO_HW1);
  SDValue Low = DAG.getTargetJumpTable(JT->getIndex(), PtrVT, TileII::MO_HW0);

  SDValue Addr = DAG.getNode(TileISD::MOVE, DL, PtrVT, High);
  Addr = DAG.getNode(TileISD::SAR16, DL, PtrVT, Addr, Middle);
  return DAG.getNode(TileISD::SAR16, DL, PtrVT, Addr, Low);
}

SDValue TileTargetLowering::lowerBR_JT(SDValue Op, SelectionDAG &DAG) const {
  SDValue Chain = Op->getOperand(0);
  SDValue Table = Op->getOperand(1);
  SDValue Index = Op->getOperand(2);
  SDLoc DL(Op);
  EVT PtrVT = getPointerTy(DAG.getDataLayout());

  const JumpTableSDNode *JT = cast<JumpTableSDNode>(Table);
  SDValue JTI = DAG.getTargetJumpTable(JT->getIndex(), PtrVT);

  // Address of the selected entry = Table + Index * 8; load the absolute target
  // it holds and jump there. BRINDJT carries the jump-table index so the
  // indirect branch records its successor blocks.
  Index =
      DAG.getNode(ISD::SHL, DL, PtrVT, Index, DAG.getConstant(3, DL, MVT::i32));
  SDValue Addr = DAG.getNode(ISD::ADD, DL, PtrVT, Index, Table);
  SDValue Target = DAG.getLoad(
      PtrVT, DL, Chain, Addr,
      MachinePointerInfo::getJumpTable(DAG.getMachineFunction()));
  return DAG.getNode(TileISD::BRINDJT, DL, MVT::Other, Target.getValue(1),
                     Target, JTI);
}

SDValue TileTargetLowering::LowerOperation(SDValue Op,
                                           SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  case ISD::BRCOND:
  case ISD::SELECT:
    // Kept as-is and matched by the beqz/bnez and cmoveqz/cmovnez patterns.
    return Op;
  case ISD::MUL:
    // Expanded into a multiply-add sequence during DAG-to-DAG selection.
    return Op;
  case ISD::MULHU:
  case ISD::MULHS:
    // Built from the 32-bit partial products during DAG-to-DAG selection.
    // Kept off the generic expander (which would refold the result back into a
    // multiply-high) by passing the node through unchanged.
    return Op;
  case ISD::GlobalAddress:
    return lowerGlobalAddress(Op, DAG);
  case ISD::GlobalTLSAddress:
    return lowerGlobalTLSAddress(Op, DAG);
  case ISD::JumpTable:
    return lowerJumpTable(Op, DAG);
  case ISD::BR_JT:
    return lowerBR_JT(Op, DAG);
  case ISD::VASTART:
    return lowerVASTART(Op, DAG);
  case ISD::VAARG:
    return lowerVAARG(Op, DAG);
  case ISD::VACOPY:
    return lowerVACOPY(Op, DAG);
  case ISD::DYNAMIC_STACKALLOC:
    return lowerDYNAMIC_STACKALLOC(Op, DAG);
  case ISD::FRAMEADDR:
    return lowerFRAMEADDR(Op, DAG);
  case ISD::RETURNADDR:
    return lowerRETURNADDR(Op, DAG);
  case ISD::ConstantPool:
    return lowerConstantPool(Op, DAG);
  case ISD::FABS:
    return lowerFABS(Op, DAG);
  case ISD::FCOPYSIGN:
    return lowerFCOPYSIGN(Op, DAG);
  case ISD::ATOMIC_LOAD:
    return lowerATOMIC_LOAD(Op, DAG);
  case ISD::ATOMIC_STORE:
    return lowerATOMIC_STORE(Op, DAG);
  case ISD::SHL_PARTS:
    return lowerShiftLeftParts(Op, DAG);
  case ISD::SRL_PARTS:
    return lowerShiftRightParts(Op, DAG, false);
  case ISD::SRA_PARTS:
    return lowerShiftRightParts(Op, DAG, true);
  }
  return SDValue();
}

// The shift-amount operand of the *_PARTS nodes is the target shift-amount type
// (i32), while the value halves are i64; widen it so all of the synthesized
// arithmetic happens in the i64 domain.
SDValue TileTargetLowering::lowerShiftLeftParts(SDValue Op,
                                                SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Lo = Op.getOperand(0);
  SDValue Hi = Op.getOperand(1);
  SDValue Shamt = Op.getOperand(2);
  EVT VT = Lo.getValueType();

  Shamt = DAG.getZExtOrTrunc(Shamt, DL, VT);

  SDValue Zero = DAG.getConstant(0, DL, VT);
  SDValue One = DAG.getConstant(1, DL, VT);
  SDValue MinusLen = DAG.getSignedConstant(-64, DL, VT);
  SDValue LenMinus1 = DAG.getConstant(63, DL, VT);
  SDValue ShamtMinusLen = DAG.getNode(ISD::ADD, DL, VT, Shamt, MinusLen);
  SDValue LenMinus1Shamt = DAG.getNode(ISD::SUB, DL, VT, LenMinus1, Shamt);

  SDValue LoTrue = DAG.getNode(ISD::SHL, DL, VT, Lo, Shamt);
  SDValue ShiftRight1Lo = DAG.getNode(ISD::SRL, DL, VT, Lo, One);
  SDValue ShiftRightLo =
      DAG.getNode(ISD::SRL, DL, VT, ShiftRight1Lo, LenMinus1Shamt);
  SDValue ShiftLeftHi = DAG.getNode(ISD::SHL, DL, VT, Hi, Shamt);
  SDValue HiTrue = DAG.getNode(ISD::OR, DL, VT, ShiftLeftHi, ShiftRightLo);
  SDValue HiFalse = DAG.getNode(ISD::SHL, DL, VT, Lo, ShamtMinusLen);

  SDValue CC = DAG.getSetCC(DL, getSetCCResultType(DAG.getDataLayout(),
                                                   *DAG.getContext(), VT),
                            ShamtMinusLen, Zero, ISD::SETLT);

  Lo = DAG.getNode(ISD::SELECT, DL, VT, CC, LoTrue, Zero);
  Hi = DAG.getNode(ISD::SELECT, DL, VT, CC, HiTrue, HiFalse);

  SDValue Parts[2] = {Lo, Hi};
  return DAG.getMergeValues(Parts, DL);
}

SDValue TileTargetLowering::lowerShiftRightParts(SDValue Op, SelectionDAG &DAG,
                                                 bool IsSRA) const {
  SDLoc DL(Op);
  SDValue Lo = Op.getOperand(0);
  SDValue Hi = Op.getOperand(1);
  SDValue Shamt = Op.getOperand(2);
  EVT VT = Lo.getValueType();

  Shamt = DAG.getZExtOrTrunc(Shamt, DL, VT);

  unsigned ShiftRightOp = IsSRA ? ISD::SRA : ISD::SRL;

  SDValue Zero = DAG.getConstant(0, DL, VT);
  SDValue One = DAG.getConstant(1, DL, VT);
  SDValue MinusLen = DAG.getSignedConstant(-64, DL, VT);
  SDValue LenMinus1 = DAG.getConstant(63, DL, VT);
  SDValue ShamtMinusLen = DAG.getNode(ISD::ADD, DL, VT, Shamt, MinusLen);
  SDValue LenMinus1Shamt = DAG.getNode(ISD::SUB, DL, VT, LenMinus1, Shamt);

  SDValue ShiftRightLo = DAG.getNode(ISD::SRL, DL, VT, Lo, Shamt);
  SDValue ShiftLeftHi1 = DAG.getNode(ISD::SHL, DL, VT, Hi, One);
  SDValue ShiftLeftHi =
      DAG.getNode(ISD::SHL, DL, VT, ShiftLeftHi1, LenMinus1Shamt);
  SDValue LoTrue = DAG.getNode(ISD::OR, DL, VT, ShiftRightLo, ShiftLeftHi);
  SDValue HiTrue = DAG.getNode(ShiftRightOp, DL, VT, Hi, Shamt);
  SDValue LoFalse = DAG.getNode(ShiftRightOp, DL, VT, Hi, ShamtMinusLen);
  SDValue HiFalse =
      IsSRA ? DAG.getNode(ISD::SRA, DL, VT, Hi, LenMinus1) : Zero;

  SDValue CC = DAG.getSetCC(DL, getSetCCResultType(DAG.getDataLayout(),
                                                   *DAG.getContext(), VT),
                            ShamtMinusLen, Zero, ISD::SETLT);

  Lo = DAG.getNode(ISD::SELECT, DL, VT, CC, LoTrue, LoFalse);
  Hi = DAG.getNode(ISD::SELECT, DL, VT, CC, HiTrue, HiFalse);

  SDValue Parts[2] = {Lo, Hi};
  return DAG.getMergeValues(Parts, DL);
}

SDValue TileTargetLowering::lowerATOMIC_LOAD(SDValue Op,
                                             SelectionDAG &DAG) const {
  AtomicSDNode *N = cast<AtomicSDNode>(Op);
  EVT VT = N->getValueType(0);
  EVT MemVT = N->getMemoryVT();
  SDLoc DL(Op);

  if (MemVT == VT)
    return DAG.getLoad(VT, DL, N->getChain(), N->getBasePtr(),
                       N->getMemOperand());

  return DAG.getExtLoad(N->getExtensionType(), DL, VT, N->getChain(),
                        N->getBasePtr(), MemVT, N->getMemOperand());
}

SDValue TileTargetLowering::lowerATOMIC_STORE(SDValue Op,
                                              SelectionDAG &DAG) const {
  AtomicSDNode *N = cast<AtomicSDNode>(Op);
  EVT MemVT = N->getMemoryVT();
  SDValue Val = N->getVal();
  SDLoc DL(Op);

  if (MemVT == Val.getValueType())
    return DAG.getStore(N->getChain(), DL, Val, N->getBasePtr(),
                        N->getMemOperand());

  return DAG.getTruncStore(N->getChain(), DL, Val, N->getBasePtr(), MemVT,
                           N->getMemOperand());
}

Register TileTargetLowering::getExceptionPointerRegister(
    const Constant *PersonalityFn) const {
  return Tile::R12;
}

Register TileTargetLowering::getExceptionSelectorRegister(
    const Constant *PersonalityFn) const {
  return Tile::R13;
}

TargetLowering::AtomicExpansionKind
TileTargetLowering::shouldExpandAtomicRMWInIR(AtomicRMWInst *AI) const {
  switch (AI->getOperation()) {
  case AtomicRMWInst::Add:
  case AtomicRMWInst::And:
  case AtomicRMWInst::Or:
  case AtomicRMWInst::Xchg:
    return AtomicExpansionKind::None;
  default:
    return AtomicExpansionKind::CmpXChg;
  }
}

SDValue TileTargetLowering::lowerConstantPool(SDValue Op,
                                              SelectionDAG &DAG) const {
  const ConstantPoolSDNode *CP = cast<ConstantPoolSDNode>(Op);
  SDLoc DL(Op);
  EVT PtrVT = Op.getValueType();

  auto Entry = [&](unsigned Flag) {
    return DAG.getTargetConstantPool(CP->getConstVal(), PtrVT, CP->getAlign(),
                                     CP->getOffset(), Flag);
  };

  if (getTargetMachine().getRelocationModel() == Reloc::PIC_) {
    SDValue Off = DAG.getNode(TileISD::MOVE, DL, PtrVT,
                              Entry(TileII::MO_HW1_LAST_GOT));
    Off = DAG.getNode(TileISD::SAR16, DL, PtrVT, Off, Entry(TileII::MO_HW0_GOT));
    SDValue GotEntry =
        DAG.getNode(ISD::ADD, DL, PtrVT, getGlobalBaseReg(DAG), Off);
    return DAG.getLoad(PtrVT, DL, DAG.getEntryNode(), GotEntry,
                       MachinePointerInfo::getGOT(DAG.getMachineFunction()));
  }

  // Absolute 64-bit address of the pool entry: moveli hw2_last, shl16insli hw1,
  // shl16insli hw0, same materialization as a global address.
  SDValue Addr = DAG.getNode(TileISD::MOVE, DL, PtrVT, Entry(TileII::MO_HW2_LAST));
  Addr = DAG.getNode(TileISD::SAR16, DL, PtrVT, Addr, Entry(TileII::MO_HW1));
  return DAG.getNode(TileISD::SAR16, DL, PtrVT, Addr, Entry(TileII::MO_HW0));
}

SDValue TileTargetLowering::lowerFABS(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT VT = Op.getValueType();
  EVT IntVT = (VT == MVT::f64) ? MVT::i64 : MVT::i32;
  // Clear the sign bit: keep bits [0 .. width-2] via an unsigned bitfield
  // extract on the integer view.
  SDValue X = DAG.getNode(ISD::BITCAST, DL, IntVT, Op.getOperand(0));
  SDValue Start = DAG.getConstant(0, DL, IntVT);
  SDValue End = DAG.getConstant(VT == MVT::f64 ? 62 : 30, DL, IntVT);
  SDValue Res = DAG.getNode(TileISD::BFEXTU, DL, IntVT, X, Start, End);
  return DAG.getNode(ISD::BITCAST, DL, VT, Res);
}

SDValue TileTargetLowering::lowerFCOPYSIGN(SDValue Op,
                                           SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT VT = Op.getValueType();
  EVT IntVT = (VT == MVT::f64) ? MVT::i64 : MVT::i32;
  uint64_t SignPos = (VT == MVT::f64) ? 63 : 31;
  SDValue X = DAG.getNode(ISD::BITCAST, DL, IntVT, Op.getOperand(0));
  SDValue Y = DAG.getNode(ISD::BITCAST, DL, IntVT, Op.getOperand(1));
  SDValue Pos = DAG.getConstant(SignPos, DL, IntVT);
  // Extract the sign bit from Y and insert it into the sign position of X.
  SDValue Sign = DAG.getNode(TileISD::BFEXTU, DL, IntVT, Y, Pos, Pos);
  SDValue Ins = DAG.getNode(TileISD::BFINS, DL, IntVT, Sign, Pos, Pos, X);
  return DAG.getNode(ISD::BITCAST, DL, VT, Ins);
}

SDValue TileTargetLowering::lowerVASTART(SDValue Op, SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  TileFunctionInfo *FuncInfo = MF.getInfo<TileFunctionInfo>();
  SDLoc dl(Op);
  EVT PtrVT = getPointerTy(DAG.getDataLayout());

  // va_start stores the address of the first variadic slot into va_list.next,
  // and the entry stack pointer into va_list.sp so va_arg can tell when the
  // remaining arguments have spilled past the register save area onto the
  // caller's stack (and must then skip the 16-byte reserve zone).
  SDValue FI = DAG.getFrameIndex(FuncInfo->getVarArgsFrameIndex(), PtrVT);
  const Value *SV = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();
  SDValue Chain = DAG.getStore(Op.getOperand(0), dl, FI, Op.getOperand(1),
                               MachinePointerInfo(SV));

  SDValue OrigSP =
      DAG.getNode(TileISD::VAARG_SP, dl, PtrVT,
                  DAG.getRegister(Tile::SP, PtrVT), DAG.getConstant(0, dl, PtrVT));
  SDValue SPField = DAG.getNode(ISD::ADD, dl, PtrVT, Op.getOperand(1),
                                DAG.getConstant(8, dl, PtrVT));
  return DAG.getStore(Chain, dl, OrigSP, SPField, MachinePointerInfo(SV));
}

SDValue TileTargetLowering::lowerVACOPY(SDValue Op, SelectionDAG &DAG) const {
  // va_list is a 16-byte struct {void *next; void *sp_at_entry;}.
  SDValue Chain = Op.getOperand(0);
  SDValue DstPtr = Op.getOperand(1);
  SDValue SrcPtr = Op.getOperand(2);
  const Value *DstSV = cast<SrcValueSDNode>(Op.getOperand(3))->getValue();
  const Value *SrcSV = cast<SrcValueSDNode>(Op.getOperand(4))->getValue();
  SDLoc dl(Op);

  return DAG.getMemcpy(Chain, dl, DstPtr, SrcPtr, DAG.getIntPtrConstant(16, dl),
                       Align(8), /*isVol=*/false, /*AlwaysInline=*/false,
                       /*CI=*/nullptr, std::nullopt, MachinePointerInfo(DstSV),
                       MachinePointerInfo(SrcSV));
}

SDValue TileTargetLowering::lowerVAARG(SDValue Op, SelectionDAG &DAG) const {
  SDNode *Node = Op.getNode();
  EVT VT = Node->getValueType(0);
  SDValue InChain = Node->getOperand(0);
  SDValue VAListPtr = Node->getOperand(1);
  const Value *SV = cast<SrcValueSDNode>(Node->getOperand(2))->getValue();
  SDLoc dl(Node);
  EVT PtrVT = getPointerTy(DAG.getDataLayout());

  // The bottom 16 bytes of every frame are reserved, so once va_arg walks off
  // the register save area and reaches the entry stack pointer it must hop the
  // reserve zone. va_list.sp (the entry sp) is the boundary marker.
  SDValue SPPtr = DAG.getNode(ISD::ADD, dl, PtrVT, VAListPtr,
                              DAG.getConstant(8, dl, PtrVT));
  SDValue VAList =
      DAG.getLoad(PtrVT, dl, InChain, VAListPtr, MachinePointerInfo(SV));
  SDValue SPSave =
      DAG.getLoad(PtrVT, dl, VAList.getValue(1), SPPtr, MachinePointerInfo(SV));

  SDValue NextPtr = DAG.getNode(ISD::ADD, dl, PtrVT, VAList,
                                DAG.getConstant(8, dl, PtrVT));
  SDValue Cond = DAG.getNode(ISD::XOR, dl, PtrVT, SPSave, NextPtr);
  SDValue AdjPtr = DAG.getNode(ISD::ADD, dl, PtrVT, VAList,
                               DAG.getConstant(24, dl, PtrVT));
  AdjPtr = DAG.getNode(ISD::SELECT, dl, PtrVT, Cond, NextPtr, AdjPtr);

  InChain = DAG.getStore(VAList.getValue(1), dl, AdjPtr, VAListPtr,
                         MachinePointerInfo(SV));
  return DAG.getLoad(VT, dl, InChain, VAList, MachinePointerInfo());
}

SDValue TileTargetLowering::lowerDYNAMIC_STACKALLOC(SDValue Op,
                                                    SelectionDAG &DAG) const {
  SDValue Chain = Op.getOperand(0);
  SDValue Size = Op.getOperand(1);
  SDLoc dl(Op);
  EVT PtrVT = getPointerTy(DAG.getDataLayout());

  // Grow the stack, preserving the two reserved doublewords at the bottom of
  // the frame (saved lr and chained sp) across the move, then hand back a
  // pointer that clears the 16-byte reserve zone.
  SDValue SP = DAG.getCopyFromReg(Chain, dl, Tile::SP, PtrVT);
  SDValue NewSP = DAG.getNode(ISD::SUB, dl, PtrVT, SP, Size);

  SDValue Reserve0 =
      DAG.getLoad(PtrVT, dl, SP.getValue(1), SP, MachinePointerInfo());
  SDValue Reserve1Addr =
      DAG.getNode(ISD::ADD, dl, PtrVT, SP, DAG.getConstant(8, dl, PtrVT));
  SDValue Reserve1 = DAG.getLoad(PtrVT, dl, Reserve0.getValue(1), Reserve1Addr,
                                 MachinePointerInfo());

  SDValue PlaceHolderSP =
      DAG.getNode(TileISD::ALLOCA_SP, dl, PtrVT,
                  DAG.getRegister(Tile::ZERO, PtrVT), NewSP);

  Chain = DAG.getStore(Reserve1.getValue(1), dl, Reserve0, PlaceHolderSP,
                       MachinePointerInfo());
  Reserve1Addr = DAG.getNode(ISD::ADD, dl, PtrVT, PlaceHolderSP,
                             DAG.getConstant(8, dl, PtrVT));
  Chain = DAG.getStore(Chain, dl, Reserve1, Reserve1Addr, MachinePointerInfo());

  Chain = DAG.getCopyToReg(Chain, dl, Tile::SP, NewSP);

  SDValue NewVal = DAG.getNode(TileISD::ALLOCA_ADDR, dl, PtrVT, NewSP,
                               DAG.getConstant(16, dl, PtrVT));
  SDValue Ops[2] = {NewVal, Chain};
  return DAG.getMergeValues(Ops, dl);
}

SDValue TileTargetLowering::lowerFRAMEADDR(SDValue Op, SelectionDAG &DAG) const {
  assert((cast<ConstantSDNode>(Op.getOperand(0))->getZExtValue() == 0) &&
         "Frame address can only be determined for the current frame.");
  MachineFrameInfo &MFI = DAG.getMachineFunction().getFrameInfo();
  MFI.setFrameAddressIsTaken(true);
  EVT VT = Op.getValueType();
  SDLoc dl(Op);
  return DAG.getCopyFromReg(DAG.getEntryNode(), dl, Tile::FP, VT);
}

SDValue TileTargetLowering::lowerRETURNADDR(SDValue Op,
                                            SelectionDAG &DAG) const {
  assert((cast<ConstantSDNode>(Op.getOperand(0))->getZExtValue() == 0) &&
         "Return address can only be determined for the current frame.");
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MVT VT = Op.getSimpleValueType();
  MFI.setReturnAddressIsTaken(true);

  unsigned Reg = MF.addLiveIn(Tile::LR, getRegClassFor(VT));
  return DAG.getCopyFromReg(DAG.getEntryNode(), SDLoc(Op), Reg, VT);
}

static SDValue performSELECTCombine(SDNode *N, SelectionDAG &DAG,
                                    TargetLowering::DAGCombinerInfo &DCI) {
  if (DCI.isBeforeLegalizeOps())
    return SDValue();

  SDValue SetCC = N->getOperand(0);
  if (SetCC.getOpcode() != ISD::SETCC ||
      !SetCC.getOperand(0).getValueType().isInteger())
    return SDValue();

  SDValue False = N->getOperand(2);
  EVT FalseTy = False.getValueType();
  if (!FalseTy.isInteger())
    return SDValue();

  // Fold "select (setcc x), t, 0" into "select (setcc.inverse x), 0, t" so the
  // zero arm lands on the cmovnez false operand a cmoveqz expects.
  ConstantSDNode *CN = dyn_cast<ConstantSDNode>(False);
  if (!CN || CN->getZExtValue())
    return SDValue();

  const SDLoc DL(N);
  ISD::CondCode CC = cast<CondCodeSDNode>(SetCC.getOperand(2))->get();
  SDValue True = N->getOperand(1);

  SetCC = DAG.getSetCC(DL, SetCC.getValueType(), SetCC.getOperand(0),
                       SetCC.getOperand(1), ISD::getSetCCInverse(CC, MVT::i64));

  return DAG.getNode(ISD::SELECT, DL, FalseTy, SetCC, False, True);
}

static SDValue performZEXTCombine(SDNode *N, SelectionDAG &DAG) {
  // A setcc already writes a zero-extended 64-bit value, so a zext of it is
  // redundant: rebuild the setcc directly at i64.
  SDValue SetCC = N->getOperand(0);
  if (SetCC.getOpcode() != ISD::SETCC || N->getValueType(0) != MVT::i64)
    return SDValue();

  // Only fold when the compared operands are a legal type. Forcing the i64
  // result onto a setcc over an illegal wide type (e.g. i128) makes the generic
  // wide-integer setcc expansion produce an i32 sub-boolean that no longer
  // matches this i64 result, tripping ExpandIntOp_SETCC.
  if (!DAG.getTargetLoweringInfo().isTypeLegal(
          SetCC.getOperand(0).getValueType()))
    return SDValue();

  return DAG.getNode(ISD::SETCC, SDLoc(SetCC), MVT::i64, SetCC.getOperand(0),
                     SetCC.getOperand(1), SetCC.getOperand(2));
}

SDValue TileTargetLowering::PerformDAGCombine(SDNode *N,
                                              DAGCombinerInfo &DCI) const {
  switch (N->getOpcode()) {
  default:
    break;
  case ISD::SELECT:
    return performSELECTCombine(N, DCI.DAG, DCI);
  case ISD::ZERO_EXTEND:
    return performZEXTCombine(N, DCI.DAG);
  }
  return SDValue();
}

MachineBasicBlock *
TileTargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                                MachineBasicBlock *BB) const {
  llvm_unreachable("Unexpected instr type to insert");
}

// Add the specified physical register as a live-in value and create a
// corresponding virtual register for it.
static unsigned AddLiveIn(MachineFunction &MF, unsigned PReg,
                          const TargetRegisterClass *RC) {
  assert(RC->contains(PReg) && "Not the correct regclass!");
  unsigned VReg = MF.getRegInfo().createVirtualRegister(RC);
  MF.getRegInfo().addLiveIn(PReg, VReg);
  return VReg;
}

// Spill an incoming byval argument into a frame object so its address can be
// taken. The leading doublewords arrive in argument registers; any tail lives
// on the caller's stack just above the 16-byte reserve zone.
static unsigned CopyTileByValRegs(MachineFunction &MF, SDValue Chain,
                                  const SDLoc &dl,
                                  std::vector<SDValue> &OutChains,
                                  SelectionDAG &DAG, const CCValAssign &VA,
                                  const ISD::ArgFlagsTy &Flags,
                                  MachineFrameInfo &MFI, bool IsRegLoc,
                                  SmallVectorImpl<SDValue> &InVals, EVT PtrTy) {
  const MCPhysReg *Reg = TileIntRegs + TILEGX_AREG_NUM;
  int FOOffset;
  int ByRegSize = 0;
  int ByMemSize = 0;
  Align Alignment = std::max(Flags.getNonZeroByValAlign(), Align(8));
  int ByValSize = alignTo(Flags.getByValSize(), Alignment);

  if (IsRegLoc) {
    Reg = std::find(TileIntRegs, TileIntRegs + TILEGX_AREG_NUM, VA.getLocReg());
    FOOffset = -ByValSize;
    ByRegSize = (TILEGX_AREG_NUM - (Reg - TileIntRegs)) * 8;
    ByMemSize = ByValSize - ByRegSize;
  } else
    FOOffset = VA.getLocMemOffset();

  unsigned LastFI = MFI.CreateFixedObject(ByValSize, FOOffset, true);
  SDValue FIN = DAG.getFrameIndex(LastFI, PtrTy);
  InVals.push_back(FIN);

  for (int I = 0; (Reg != TileIntRegs + TILEGX_AREG_NUM) && (I * 8) < ByValSize;
       ++Reg, ++I) {
    unsigned VReg = AddLiveIn(MF, *Reg, &Tile::CPURegsRegClass);
    SDValue StorePtr =
        DAG.getNode(ISD::ADD, dl, PtrTy, FIN, DAG.getConstant(I * 8, dl, PtrTy));
    SDValue Store = DAG.getStore(Chain, dl, DAG.getRegister(VReg, MVT::i64),
                                 StorePtr, MachinePointerInfo());
    OutChains.push_back(Store);
  }

  if (ByMemSize) {
    SDValue Dst = DAG.getNode(ISD::ADD, dl, PtrTy, FIN,
                              DAG.getConstant(ByRegSize, dl, PtrTy));
    SDValue Src =
        DAG.getNode(ISD::ADD, dl, PtrTy, FIN,
                    DAG.getConstant(ByValSize + TILEGX_BZONE_SIZE, dl, PtrTy));
    Chain = DAG.getMemcpy(Chain, dl, Dst, Src,
                          DAG.getConstant(ByMemSize, dl, PtrTy), Align(8),
                          /*isVol=*/false, /*AlwaysInline=*/false,
                          /*CI=*/nullptr, std::nullopt, MachinePointerInfo(),
                          MachinePointerInfo());
    OutChains.push_back(Chain);
  }

  return LastFI;
}

SDValue TileTargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &dl,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  TileFunctionInfo *TileFI = MF.getInfo<TileFunctionInfo>();
  EVT PtrVT = getPointerTy(DAG.getDataLayout());

  TileFI->setVarArgsFrameIndex(0);

  std::vector<SDValue> OutChains;

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeFormalArguments(Ins, CC_Tile);

  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    CCValAssign &VA = ArgLocs[i];
    EVT ValVT = VA.getValVT();
    ISD::ArgFlagsTy Flags = Ins[i].Flags;
    bool IsRegLoc = VA.isRegLoc();

    if (Flags.isByVal()) {
      assert(Flags.getByValSize() &&
             "ByVal args of size 0 should have been ignored by front-end.");
      CopyTileByValRegs(MF, Chain, dl, OutChains, DAG, VA, Flags, MFI, IsRegLoc,
                        InVals, PtrVT);
      continue;
    }

    if (IsRegLoc) {
      EVT RegVT = VA.getLocVT();
      unsigned ArgReg = VA.getLocReg();
      const TargetRegisterClass *RC;

      if (RegVT == MVT::i32 || RegVT == MVT::f32)
        RC = &Tile::CPU32RegsRegClass;
      else
        RC = &Tile::CPURegsRegClass;

      unsigned Reg = AddLiveIn(MF, ArgReg, RC);
      SDValue ArgValue = DAG.getCopyFromReg(Chain, dl, Reg, RegVT);

      switch (VA.getLocInfo()) {
      default:
        llvm_unreachable("Unknown loc info!");
      case CCValAssign::Full:
        break;
      case CCValAssign::BCvt:
        ArgValue = DAG.getNode(ISD::BITCAST, dl, ValVT, ArgValue);
        break;
      case CCValAssign::SExt:
        ArgValue = DAG.getNode(ISD::AssertSext, dl, RegVT, ArgValue,
                               DAG.getValueType(ValVT));
        ArgValue = DAG.getNode(ISD::TRUNCATE, dl, ValVT, ArgValue);
        break;
      case CCValAssign::ZExt:
        ArgValue = DAG.getNode(ISD::AssertZext, dl, RegVT, ArgValue,
                               DAG.getValueType(ValVT));
        ArgValue = DAG.getNode(ISD::TRUNCATE, dl, ValVT, ArgValue);
        break;
      }

      InVals.push_back(ArgValue);
    } else {
      assert(VA.isMemLoc());
      int FI = MFI.CreateFixedObject(ValVT.getSizeInBits() / 8,
                                     VA.getLocMemOffset(), true);
      SDValue FIN = DAG.getFrameIndex(FI, PtrVT);
      InVals.push_back(DAG.getLoad(ValVT, dl, Chain, FIN,
                                   MachinePointerInfo::getFixedStack(MF, FI)));
    }
  }

  // A function returning a struct by value gets the hidden result pointer in
  // r0; stash it so LowerReturn can place it back in r0 at every exit.
  if (MF.getFunction().hasStructRetAttr()) {
    unsigned Reg = TileFI->getSRetReturnReg();
    if (!Reg) {
      Reg = MF.getRegInfo().createVirtualRegister(getRegClassFor(MVT::i64));
      TileFI->setSRetReturnReg(Reg);
    }
    SDValue Copy = DAG.getCopyToReg(DAG.getEntryNode(), dl, Reg, InVals[0]);
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Copy, Chain);
  }

  if (IsVarArg) {
    const unsigned NumOfRegs = TILEGX_AREG_NUM;
    unsigned Idx = CCInfo.getFirstUnallocated(TileIntRegs);
    const unsigned RegSize = 8;
    const int FirstRegSlotOffset = -80; // Offset of r0's save slot.
    int RegSlotOffset = FirstRegSlotOffset + Idx * RegSize;
    const TargetRegisterClass *RC = &Tile::CPURegsRegClass;

    int FirstVaArgOffset;
    if (Idx == NumOfRegs)
      FirstVaArgOffset =
          alignTo(CCInfo.getStackSize() + TILEGX_BZONE_SIZE, RegSize);
    else
      FirstVaArgOffset = RegSlotOffset;

    int LastFI = MFI.CreateFixedObject(RegSize, FirstVaArgOffset, true);
    TileFI->setVarArgsFrameIndex(LastFI);

    // Spill the still-unused argument registers into the save area so va_arg
    // can sweep them as if they had been passed on the stack.
    for (int StackOffset = RegSlotOffset; Idx < NumOfRegs;
         ++Idx, StackOffset += RegSize) {
      unsigned Reg = AddLiveIn(MF, TileIntRegs[Idx], RC);
      SDValue ArgValue =
          DAG.getCopyFromReg(Chain, dl, Reg, MVT::getIntegerVT(RegSize * 8));
      LastFI = MFI.CreateFixedObject(RegSize, StackOffset, true);
      SDValue PtrOff = DAG.getFrameIndex(LastFI, PtrVT);
      OutChains.push_back(
          DAG.getStore(Chain, dl, ArgValue, PtrOff, MachinePointerInfo()));
    }
  }

  if (!OutChains.empty()) {
    OutChains.push_back(Chain);
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, OutChains);
  }

  return Chain;
}

// Copy a byval argument into the outgoing argument registers and, for the
// overflow, onto the caller stack just above the 16-byte reserve zone.
void TileTargetLowering::passByValArg(
    SDValue Chain, const SDLoc &dl,
    SmallVectorImpl<std::pair<unsigned, SDValue>> &RegsToPass,
    SmallVectorImpl<SDValue> &MemOpChains, SDValue StackPtr,
    MachineFrameInfo &MFI, SelectionDAG &DAG, SDValue Arg,
    const CCValAssign &VA, const ISD::ArgFlagsTy &Flags) const {
  unsigned ByValSize = Flags.getByValSize();
  Align Alignment = std::max(Flags.getNonZeroByValAlign(), Align(8));
  bool IsRegLoc = VA.isRegLoc();
  unsigned Offset = 0;
  unsigned LocMemOffset = TILEGX_BZONE_SIZE;
  unsigned MemCpySize = ByValSize;
  EVT PtrTy = getPointerTy(DAG.getDataLayout());

  if (IsRegLoc) {
    const MCPhysReg *Reg =
        std::find(TileIntRegs, TileIntRegs + TILEGX_AREG_NUM, VA.getLocReg());
    const MCPhysReg *RegEnd = TileIntRegs + TILEGX_AREG_NUM;

    for (; (Reg != RegEnd) && (ByValSize >= Offset + 8); ++Reg, Offset += 8) {
      SDValue LoadPtr = DAG.getNode(ISD::ADD, dl, PtrTy, Arg,
                                    DAG.getConstant(Offset, dl, PtrTy));
      SDValue LoadVal = DAG.getLoad(MVT::i64, dl, Chain, LoadPtr,
                                    MachinePointerInfo(), Alignment);
      MemOpChains.push_back(LoadVal.getValue(1));
      RegsToPass.push_back(std::make_pair(*Reg, LoadVal));
    }

    if (!(MemCpySize = ByValSize - Offset))
      return;

    // Assemble the sub-doubleword tail with shifted zero-extending loads so it
    // can travel in the final argument register.
    if (Reg != RegEnd) {
      assert((ByValSize < Offset + 8) &&
             "Size of the remainder should be smaller than 8-byte.");
      SDValue Val;
      for (unsigned LoadSize = 4; Offset < ByValSize; LoadSize /= 2) {
        unsigned RemSize = ByValSize - Offset;
        if (RemSize < LoadSize)
          continue;

        SDValue LoadPtr = DAG.getNode(ISD::ADD, dl, PtrTy, Arg,
                                      DAG.getConstant(Offset, dl, PtrTy));
        SDValue LoadVal = DAG.getExtLoad(
            ISD::ZEXTLOAD, dl, MVT::i64, Chain, LoadPtr, MachinePointerInfo(),
            MVT::getIntegerVT(LoadSize * 8), Alignment);
        MemOpChains.push_back(LoadVal.getValue(1));

        unsigned Shamt = (Offset % 8) * 8;
        SDValue Shift = DAG.getNode(ISD::SHL, dl, MVT::i64, LoadVal,
                                    DAG.getConstant(Shamt, dl, MVT::i32));
        Val = Val.getNode() ? DAG.getNode(ISD::OR, dl, MVT::i64, Val, Shift)
                            : Shift;
        Offset += LoadSize;
        Alignment = std::min(Alignment, Align(LoadSize));
      }
      RegsToPass.push_back(std::make_pair(*Reg, Val));
      return;
    }
  } else
    LocMemOffset = VA.getLocMemOffset();

  assert(MemCpySize && "MemCpySize must not be zero.");

  SDValue Src =
      DAG.getNode(ISD::ADD, dl, PtrTy, Arg, DAG.getConstant(Offset, dl, PtrTy));
  SDValue Dst = DAG.getNode(ISD::ADD, dl, PtrTy, StackPtr,
                            DAG.getConstant(LocMemOffset, dl, PtrTy));
  Chain = DAG.getMemcpy(Chain, dl, Dst, Src,
                        DAG.getConstant(MemCpySize, dl, PtrTy),
                        std::min(Alignment, Align(8)), /*isVol=*/false,
                        /*AlwaysInline=*/false, /*CI=*/nullptr, std::nullopt,
                        MachinePointerInfo(), MachinePointerInfo());
  MemOpChains.push_back(Chain);
}

SDValue TileTargetLowering::LowerCall(TargetLowering::CallLoweringInfo &CLI,
                                      SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG = CLI.DAG;
  SDLoc &dl = CLI.DL;
  SmallVectorImpl<ISD::OutputArg> &Outs = CLI.Outs;
  SmallVectorImpl<SDValue> &OutVals = CLI.OutVals;
  SmallVectorImpl<ISD::InputArg> &Ins = CLI.Ins;
  SDValue InChain = CLI.Chain;
  SDValue Callee = CLI.Callee;
  CallingConv::ID CallConv = CLI.CallConv;
  bool IsVarArg = CLI.IsVarArg;

  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  bool IsPIC = getTargetMachine().getRelocationModel() == Reloc::PIC_;

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeCallOperands(Outs, CC_Tile);

  unsigned NextStackOffset = CCInfo.getStackSize();

  bool IsTailCall = CLI.IsTailCall &&
                    isEligibleForTailCallOptimization(CCInfo, NextStackOffset,
                                                      CLI, MF);
  CLI.IsTailCall = IsTailCall;

  // A sibling call needs no argument area of its own and is not bracketed by a
  // call-frame sequence; the existing frame is torn down by the epilogue.
  SDValue Chain = InChain;
  if (!IsTailCall)
    Chain = DAG.getCALLSEQ_START(InChain, NextStackOffset, 0, dl);
  SDValue StackPtr = DAG.getCopyFromReg(Chain, dl, Tile::SP, PtrVT);

  SmallVector<std::pair<unsigned, SDValue>, TILEGX_AREG_NUM> RegsToPass;
  SmallVector<SDValue, 8> MemOpChains;

  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    SDValue Arg = OutVals[i];
    CCValAssign &VA = ArgLocs[i];
    MVT ValVT = VA.getValVT(), LocVT = VA.getLocVT();
    ISD::ArgFlagsTy Flags = Outs[i].Flags;

    if (Flags.isByVal()) {
      assert(Flags.getByValSize() &&
             "ByVal args of size 0 should have been ignored by front-end.");
      passByValArg(Chain, dl, RegsToPass, MemOpChains, StackPtr, MFI, DAG, Arg,
                   VA, Flags);
      continue;
    }

    switch (VA.getLocInfo()) {
    default:
      llvm_unreachable("Unknown loc info!");
    case CCValAssign::Full:
      if (VA.isRegLoc()) {
        if ((ValVT == MVT::f32 && LocVT == MVT::i32) ||
            (ValVT == MVT::f64 && LocVT == MVT::i64))
          Arg = DAG.getNode(ISD::BITCAST, dl, LocVT, Arg);
      }
      break;
    case CCValAssign::SExt:
      Arg = DAG.getNode(ISD::SIGN_EXTEND, dl, LocVT, Arg);
      break;
    case CCValAssign::ZExt:
      Arg = DAG.getNode(ISD::ZERO_EXTEND, dl, LocVT, Arg);
      break;
    case CCValAssign::AExt:
      Arg = DAG.getNode(ISD::ANY_EXTEND, dl, LocVT, Arg);
      break;
    }

    if (VA.isRegLoc()) {
      RegsToPass.push_back(std::make_pair(VA.getLocReg(), Arg));
      continue;
    }

    assert(VA.isMemLoc());
    SDValue PtrOff = DAG.getNode(ISD::ADD, dl, PtrVT, StackPtr,
                                 DAG.getIntPtrConstant(VA.getLocMemOffset(), dl));
    MemOpChains.push_back(
        DAG.getStore(Chain, dl, Arg, PtrOff, MachinePointerInfo()));
  }

  if (!MemOpChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, MemOpChains);

  // A direct call references the callee by name; the JUMPOFF relocation on the
  // jal carries it. Under PIC a preemptible callee goes through the PLT, and a
  // general-dynamic TLS callee resolves to __tls_get_addr via tls_gd_call.
  // Indirect callees fall through as a register operand.
  if (Callee.getOpcode() == ISD::GlobalTLSAddress) {
    GlobalAddressSDNode *G = cast<GlobalAddressSDNode>(Callee);
    Callee = DAG.getTargetGlobalAddress(G->getGlobal(), dl, PtrVT, 0,
                                        TileII::MO_TLS_GD_CALL);
  } else if (GlobalAddressSDNode *G = dyn_cast<GlobalAddressSDNode>(Callee)) {
    unsigned Flag = (IsPIC && !G->getGlobal()->hasLocalLinkage())
                        ? TileII::MO_PLT_CALL
                        : TileII::MO_NO_FLAG;
    Callee = DAG.getTargetGlobalAddress(G->getGlobal(), dl, PtrVT, 0, Flag);
  } else if (ExternalSymbolSDNode *S = dyn_cast<ExternalSymbolSDNode>(Callee)) {
    unsigned Flag = IsPIC ? TileII::MO_PLT_CALL : TileII::MO_NO_FLAG;
    Callee = DAG.getTargetExternalSymbol(S->getSymbol(), PtrVT, Flag);
  }

  SDValue InFlag;
  for (unsigned i = 0, e = RegsToPass.size(); i != e; ++i) {
    Chain = DAG.getCopyToReg(Chain, dl, RegsToPass[i].first,
                             RegsToPass[i].second, InFlag);
    InFlag = Chain.getValue(1);
  }

  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
  SmallVector<SDValue, 8> Ops;
  Ops.push_back(Chain);
  Ops.push_back(Callee);

  for (unsigned i = 0, e = RegsToPass.size(); i != e; ++i)
    Ops.push_back(DAG.getRegister(RegsToPass[i].first,
                                  RegsToPass[i].second.getValueType()));

  const TargetRegisterInfo *TRI = Subtarget.getRegisterInfo();
  const uint32_t *Mask = TRI->getCallPreservedMask(MF, CallConv);
  assert(Mask && "Missing call preserved mask for calling convention");
  Ops.push_back(DAG.getRegisterMask(Mask));

  if (InFlag.getNode())
    Ops.push_back(InFlag);

  if (IsTailCall) {
    MFI.setHasTailCall();
    return DAG.getNode(TileISD::TailCall, dl, MVT::Other, Ops);
  }

  Chain = DAG.getNode(TileISD::JmpLink, dl, NodeTys, Ops);
  InFlag = Chain.getValue(1);

  Chain = DAG.getCALLSEQ_END(Chain, NextStackOffset, 0, InFlag, dl);
  InFlag = Chain.getValue(1);

  return LowerCallResult(Chain, InFlag, CallConv, IsVarArg, Ins, dl, DAG,
                         InVals);
}

bool TileTargetLowering::isEligibleForTailCallOptimization(
    const CCState &CCInfo, unsigned NextStackOffset, const CallLoweringInfo &CLI,
    const MachineFunction &MF) const {
  // Be conservative: only direct sibling calls with every argument in a
  // register, matching the C convention on both sides, and no special argument
  // shapes. Anything else falls back to a normal jal.
  if (CLI.IsVarArg || NextStackOffset != 0)
    return false;
  if (CLI.CallConv != CallingConv::C ||
      MF.getFunction().getCallingConv() != CallingConv::C)
    return false;
  if (MF.getFunction().hasStructRetAttr())
    return false;
  // An indirect callee would have to survive the epilogue in a caller-saved
  // register; restrict to symbol callees for now.
  if (!isa<GlobalAddressSDNode>(CLI.Callee) &&
      !isa<ExternalSymbolSDNode>(CLI.Callee))
    return false;
  for (const ISD::OutputArg &Out : CLI.Outs)
    if (Out.Flags.isByVal())
      return false;
  return true;
}

SDValue TileTargetLowering::LowerCallResult(
    SDValue Chain, SDValue InGlue, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &dl,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeCallResult(Ins, RetCC_Tile);

  for (unsigned i = 0, e = RVLocs.size(); i != e; ++i) {
    Chain = DAG.getCopyFromReg(Chain, dl, RVLocs[i].getLocReg(),
                               RVLocs[i].getValVT(), InGlue)
                .getValue(1);
    InGlue = Chain.getValue(2);
    InVals.push_back(Chain.getValue(0));
  }

  return Chain;
}

SDValue
TileTargetLowering::LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                                bool IsVarArg,
                                const SmallVectorImpl<ISD::OutputArg> &Outs,
                                const SmallVectorImpl<SDValue> &OutVals,
                                const SDLoc &dl, SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, RVLocs, *DAG.getContext());
  CCInfo.AnalyzeReturn(Outs, RetCC_Tile);

  SDValue Flag;
  SmallVector<SDValue, 4> RetOps(1, Chain);
  RetOps.push_back(SDValue());

  for (unsigned i = 0; i != RVLocs.size(); ++i) {
    CCValAssign &VA = RVLocs[i];
    assert(VA.isRegLoc() && "Can only return in registers!");
    Chain = DAG.getCopyToReg(Chain, dl, VA.getLocReg(), OutVals[i], Flag);
    Flag = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
  }

  // A struct-returning function feeds the saved hidden result pointer back into
  // r0 at the return so the caller can find it.
  if (MF.getFunction().hasStructRetAttr()) {
    TileFunctionInfo *TileFI = MF.getInfo<TileFunctionInfo>();
    unsigned Reg = TileFI->getSRetReturnReg();
    if (!Reg)
      llvm_unreachable("sret virtual register not created in the entry block");
    EVT PtrVT = getPointerTy(DAG.getDataLayout());
    SDValue Val = DAG.getCopyFromReg(Chain, dl, Reg, PtrVT);
    Chain = DAG.getCopyToReg(Chain, dl, Tile::R0, Val, Flag);
    Flag = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(Tile::R0, PtrVT));
  }

  // Return on Tile is always a "jr $lr".
  RetOps[0] = Chain;
  RetOps[1] = DAG.getRegister(Tile::LR, MVT::i64);

  if (Flag.getNode())
    RetOps.push_back(Flag);

  return DAG.getNode(TileISD::Ret, dl, MVT::Other, RetOps);
}

//===----------------------------------------------------------------------===//
//                           Tile Inline Assembly Support
//===----------------------------------------------------------------------===//

// "R00".."R10" name a single GPR r0-r10; "Z0"/"Z1" are the all-ones low or
// high word immediates. These are the multi-character tilegx constraints.
static int parseRConstraint(StringRef C) {
  if (C.size() != 3 || C[0] != 'R' || C[1] < '0' || C[1] > '9' || C[2] < '0' ||
      C[2] > '9')
    return -1;
  int N = (C[1] - '0') * 10 + (C[2] - '0');
  return N <= 10 ? N : -1;
}

TileTargetLowering::ConstraintType
TileTargetLowering::getConstraintType(StringRef Constraint) const {
  // 'd' : An address register. 'y' : Equivalent to r (compatibility).
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    default:
      break;
    case 'd':
    case 'y':
      return C_RegisterClass;
    }
  }
  if (parseRConstraint(Constraint) >= 0)
    return C_Register;
  if (Constraint == "Z0" || Constraint == "Z1")
    return C_Other;
  return TargetLowering::getConstraintType(Constraint);
}

void TileTargetLowering::LowerAsmOperandForConstraint(
    SDValue Op, StringRef Constraint, std::vector<SDValue> &Ops,
    SelectionDAG &DAG) const {
  if (Constraint == "Z0" || Constraint == "Z1") {
    if (auto *C = dyn_cast<ConstantSDNode>(Op)) {
      uint64_t Want = Constraint == "Z0" ? 0xffffffffULL : 0xffffffff00000000ULL;
      if (C->getZExtValue() == Want) {
        Ops.push_back(DAG.getTargetConstant(Want, SDLoc(Op), MVT::i64));
        return;
      }
    }
    return;
  }
  TargetLowering::LowerAsmOperandForConstraint(Op, Constraint, Ops, DAG);
}

TargetLowering::ConstraintWeight
TileTargetLowering::getSingleConstraintMatchWeight(
    AsmOperandInfo &info, const char *constraint) const {
  ConstraintWeight Weight = CW_Invalid;
  Value *CallOperandVal = info.CallOperandVal;
  if (!CallOperandVal)
    return CW_Default;
  Type *type = CallOperandVal->getType();
  switch (*constraint) {
  default:
    Weight = TargetLowering::getSingleConstraintMatchWeight(info, constraint);
    break;
  case 'd':
  case 'y':
    if (type->isIntegerTy())
      Weight = CW_Register;
    break;
  }
  return Weight;
}

std::pair<unsigned, const TargetRegisterClass *>
TileTargetLowering::getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                                                 StringRef Constraint,
                                                 MVT VT) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    case 'd':
    case 'y':
    case 'r':
      return std::make_pair(0U, &Tile::CPURegsRegClass);
    }
  }
  if (int N = parseRConstraint(Constraint); N >= 0) {
    std::string Reg = ("{r" + Twine(N) + "}").str();
    return TargetLowering::getRegForInlineAsmConstraint(TRI, Reg, VT);
  }
  return TargetLowering::getRegForInlineAsmConstraint(TRI, Constraint, VT);
}
