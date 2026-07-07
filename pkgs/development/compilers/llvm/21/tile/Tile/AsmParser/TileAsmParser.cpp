//===-- TileAsmParser.cpp - Parse Tile assembly to MCInst -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The textual TILE-Gx assembly parser. It turns .s text (and clang inline asm)
// into the same MCInst the codegen path produces, so the already byte-validated
// MCCodeEmitter encodes it. Register names, immediates, the hw0/hw1/got/tls
// relocation-specifier operand syntax and single-instruction VLIW bundles
// ({ ... }) are accepted.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/TileBaseInfo.h"
#include "MCTargetDesc/TileMCAsmInfo.h"
#include "MCTargetDesc/TileMCTargetDesc.h"
#include "TargetInfo/TileTargetInfo.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCParser/AsmLexer.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>

using namespace llvm;

#define DEBUG_TYPE "tile-asm-parser"

namespace {
class TileOperand;

class TileAsmParser : public MCTargetAsmParser {
  MCAsmParser &Parser;
  const MCRegisterInfo &MRI;

  // A VLIW bundle { a ; b } groups instructions issued in one cycle. Matched
  // instructions inside an open bundle are buffered and flushed as one packet
  // when the closing brace is seen: a single-instruction bundle emits bare
  // (byte-identical to the unbraced form), a multi-instruction bundle emits a
  // BUNDLE the code emitter merges into one 64-bit word.
  bool InBundle = false;
  SmallVector<MCInst *, 2> BundledInsts;

  bool emitOrBuffer(const MCInst &Inst, MCStreamer &Out, SMLoc IDLoc);
  bool flushBundle(MCStreamer &Out, SMLoc IDLoc);

#define GET_ASSEMBLER_HEADER
#include "TileGenAsmMatcher.inc"

  bool matchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                               OperandVector &Operands, MCStreamer &Out,
                               uint64_t &ErrorInfo,
                               bool MatchingInlineAsm) override;
  bool parseRegister(MCRegister &Reg, SMLoc &StartLoc, SMLoc &EndLoc) override;
  ParseStatus tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                               SMLoc &EndLoc) override;
  bool parseInstruction(ParseInstructionInfo &Info, StringRef Name,
                        SMLoc NameLoc, OperandVector &Operands) override;
  ParseStatus parseDirective(AsmToken DirectiveID) override;
  bool tokenIsStartOfStatement(AsmToken::TokenKind Token) override;
  unsigned validateTargetOperandClass(MCParsedAsmOperand &Op,
                                      unsigned Kind) override;

  bool parseOperand(OperandVector &Operands, StringRef Mnemonic);
  bool tryParseRegisterOperand(OperandVector &Operands, StringRef Mnemonic);
  bool tryParseRelocOperand(OperandVector &Operands);
  bool emitSpecialInstruction(StringRef Name, OperandVector &Operands,
                              SMLoc IDLoc, MCStreamer &Out);

  MCRegister matchRegisterName(StringRef Name, bool Is32);
  MCRegister matchRegisterByNumber(unsigned RegNum, bool Is32);
  MCRegister tryParseRegisterNumber(StringRef Mnemonic);

  static bool require32bit(StringRef Mnemonic);
  static uint16_t getSpecifierForName(StringRef Name);

public:
  TileAsmParser(const MCSubtargetInfo &STI, MCAsmParser &Parser,
                const MCInstrInfo &MII, const MCTargetOptions &Options)
      : MCTargetAsmParser(Options, STI, MII), Parser(Parser),
        MRI(*Parser.getContext().getRegisterInfo()) {
    setAvailableFeatures(ComputeAvailableFeatures(STI.getFeatureBits()));
  }
};

// A parsed TILE-Gx operand: a mnemonic/bundle token, a register or an
// immediate/expression (plain numbers, labels and relocation specifiers).
class TileOperand : public MCParsedAsmOperand {
  enum KindTy { k_Token, k_Register, k_Immediate } Kind;

  struct TokOp {
    const char *Data;
    unsigned Length;
  };
  struct RegOp {
    MCRegister Reg;
  };
  struct ImmOp {
    const MCExpr *Val;
  };

  union {
    struct TokOp Tok;
    struct RegOp Reg;
    struct ImmOp Imm;
  };

  SMLoc StartLoc, EndLoc;

public:
  TileOperand(KindTy K) : Kind(K) {}

  bool isToken() const override { return Kind == k_Token; }
  bool isReg() const override { return Kind == k_Register; }
  bool isImm() const override { return Kind == k_Immediate; }
  bool isMem() const override { return false; }

  StringRef getToken() const {
    assert(Kind == k_Token && "not a token");
    return StringRef(Tok.Data, Tok.Length);
  }
  MCRegister getReg() const override {
    assert(Kind == k_Register && "not a register");
    return Reg.Reg;
  }
  const MCExpr *getImm() const {
    assert(Kind == k_Immediate && "not an immediate");
    return Imm.Val;
  }

  SMLoc getStartLoc() const override { return StartLoc; }
  SMLoc getEndLoc() const override { return EndLoc; }

  void addExpr(MCInst &Inst, const MCExpr *Expr) const {
    if (const auto *CE = dyn_cast<MCConstantExpr>(Expr))
      Inst.addOperand(MCOperand::createImm(CE->getValue()));
    else
      Inst.addOperand(MCOperand::createExpr(Expr));
  }

  void addRegOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "invalid operand count");
    Inst.addOperand(MCOperand::createReg(getReg()));
  }
  void addImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "invalid operand count");
    addExpr(Inst, getImm());
  }

  void print(raw_ostream &OS, const MCAsmInfo &MAI) const override {
    switch (Kind) {
    case k_Token:
      OS << "Token:" << getToken();
      break;
    case k_Register:
      OS << "Reg:" << getReg().id();
      break;
    case k_Immediate:
      OS << "Imm:";
      MAI.printExpr(OS, *getImm());
      break;
    }
  }

  static std::unique_ptr<TileOperand> createToken(StringRef Str, SMLoc S) {
    auto Op = std::make_unique<TileOperand>(k_Token);
    Op->Tok.Data = Str.data();
    Op->Tok.Length = Str.size();
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }
  static std::unique_ptr<TileOperand> createReg(MCRegister Reg, SMLoc S,
                                                SMLoc E) {
    auto Op = std::make_unique<TileOperand>(k_Register);
    Op->Reg.Reg = Reg;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }
  static std::unique_ptr<TileOperand> createImm(const MCExpr *Val, SMLoc S,
                                                SMLoc E) {
    auto Op = std::make_unique<TileOperand>(k_Immediate);
    Op->Imm.Val = Val;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }
};
} // namespace

#define GET_REGISTER_MATCHER
#define GET_MATCHER_IMPLEMENTATION
#include "TileGenAsmMatcher.inc"

// The 32-bit forms reuse the same encoding as the 64-bit register; selecting
// the CPU32Regs alias keeps the matched register class consistent with the
// 32-bit instruction's operand class (the encoded bits are identical either
// way).
bool TileAsmParser::require32bit(StringRef Mnemonic) {
  return StringSwitch<bool>(Mnemonic)
      .Cases("st4", "addx", "subx", "shlx", "shlxi", "shrux", "shruxi", true)
      .Cases("addxi", "addxli", "fetchadd4", "fetchand4", "fetchor4", true)
      .Cases("exch4", "mulx", true)
      .Default(false);
}

// Map a relocation-specifier operator name to the TileII machine-operand flag
// that the code emitter decodes into a fixup. This is the inverse of
// Tile::getSpecifierName. hw0/hw1_last are disambiguated by the caller: a
// symbol-minus-label difference (the GOT-base self-computation) takes the
// PC-relative variant.
uint16_t TileAsmParser::getSpecifierForName(StringRef Name) {
  return StringSwitch<uint16_t>(Name)
      .Case("hw0", TileII::MO_HW0)
      .Case("hw1", TileII::MO_HW1)
      .Case("hw1_last", TileII::MO_HW1_LAST)
      .Case("hw2_last", TileII::MO_HW2_LAST)
      .Case("hw0_got", TileII::MO_HW0_GOT)
      .Case("hw1_last_got", TileII::MO_HW1_LAST_GOT)
      .Case("plt", TileII::MO_PLT_CALL)
      .Case("hw0_tls_gd", TileII::MO_HW0_TLS_GD)
      .Case("hw1_last_tls_gd", TileII::MO_HW1_LAST_TLS_GD)
      .Case("hw0_tls_ie", TileII::MO_HW0_TLS_IE)
      .Case("hw1_last_tls_ie", TileII::MO_HW1_LAST_TLS_IE)
      .Case("hw0_tls_le", TileII::MO_HW0_TLS_LE)
      .Case("hw1_last_tls_le", TileII::MO_HW1_LAST_TLS_LE)
      .Case("tls_add", TileII::MO_TLS_ADD)
      .Case("tls_gd_add", TileII::MO_TLS_GD_ADD)
      .Case("tls_gd_call", TileII::MO_TLS_GD_CALL)
      .Case("tls_ie_load", TileII::MO_TLS_IE_LOAD)
      .Default(0);
}

MCRegister TileAsmParser::matchRegisterName(StringRef Name, bool Is32) {
  MCRegister Reg = StringSwitch<MCRegister>(Name)
                       .Case("fp", Tile::FP)
                       .Case("tp", Tile::TP)
                       .Case("sp", Tile::SP)
                       .Case("lr", Tile::LR)
                       .Case("zero", Tile::ZERO)
                       .Default(MCRegister());
  if (!Reg)
    return MCRegister();
  if (Is32)
    return matchRegisterByNumber(MRI.getEncodingValue(Reg), true);
  return Reg;
}

MCRegister TileAsmParser::matchRegisterByNumber(unsigned RegNum, bool Is32) {
  if (RegNum > 63)
    return MCRegister();
  const MCRegisterClass &RC = MRI.getRegClass(
      Is32 ? Tile::CPU32RegsRegClassID : Tile::CPURegsRegClassID);
  for (MCRegister Reg : RC)
    if (MRI.getEncodingValue(Reg) == RegNum)
      return Reg;
  return MCRegister();
}

MCRegister TileAsmParser::tryParseRegisterNumber(StringRef Mnemonic) {
  const AsmToken &Tok = Parser.getTok();
  if (!Tok.is(AsmToken::Identifier))
    return MCRegister();

  std::string Lower = Tok.getString().lower();
  bool Is32 = require32bit(Mnemonic);
  if (Lower[0] == 'r') {
    unsigned Value;
    if (!StringRef(Lower).substr(1).getAsInteger(10, Value))
      return matchRegisterByNumber(Value, Is32);
    return MCRegister();
  }
  return matchRegisterName(Lower, Is32);
}

bool TileAsmParser::parseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                  SMLoc &EndLoc) {
  if (!tryParseRegister(Reg, StartLoc, EndLoc).isSuccess())
    return Error(StartLoc, "invalid register name");
  return false;
}

ParseStatus TileAsmParser::tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                            SMLoc &EndLoc) {
  const AsmToken &Tok = Parser.getTok();
  StartLoc = Tok.getLoc();
  EndLoc = Tok.getEndLoc();
  Reg = tryParseRegisterNumber("");
  if (!Reg)
    return ParseStatus::NoMatch;
  return ParseStatus::Success;
}

bool TileAsmParser::tryParseRegisterOperand(OperandVector &Operands,
                                            StringRef Mnemonic) {
  SMLoc S = Parser.getTok().getLoc();
  MCRegister Reg = tryParseRegisterNumber(Mnemonic);
  if (!Reg)
    return true;
  Operands.push_back(TileOperand::createReg(Reg, S, Parser.getTok().getEndLoc()));
  Parser.Lex();
  return false;
}

// Parse a relocation-specifier operand: <op>(expr), e.g. hw1_last_got(g) or the
// GOT-base hw0(_GLOBAL_OFFSET_TABLE_-.Llabel). Produces an MCSpecifierExpr whose
// specifier is the TileII flag, identical to what TileMCInstLower emits.
bool TileAsmParser::tryParseRelocOperand(OperandVector &Operands) {
  const AsmToken &Tok = Parser.getTok();
  SMLoc S = Tok.getLoc();
  uint16_t Spec = getSpecifierForName(Tok.getString().lower());
  if (!Spec)
    return true;

  Parser.Lex(); // Eat the specifier keyword.
  if (getLexer().isNot(AsmToken::LParen))
    return Error(getLexer().getLoc(), "expected '(' after relocation operator");
  Parser.Lex(); // Eat '('.

  const MCExpr *Sub;
  SMLoc EndLoc;
  if (getParser().parseParenExpression(Sub, EndLoc))
    return true;

  // The bare hw0/hw1_last operators select the PC-relative variant when applied
  // to a symbol difference (the GOT-base self-computation), matching GNU as,
  // which derives the PCREL relocation from the subtraction rather than a
  // distinct operator.
  if (const auto *BE = dyn_cast<MCBinaryExpr>(Sub)) {
    if (BE->getOpcode() == MCBinaryExpr::Sub) {
      if (Spec == TileII::MO_HW0)
        Spec = TileII::MO_HW0_PIC;
      else if (Spec == TileII::MO_HW1_LAST)
        Spec = TileII::MO_HW1_LAST_PIC;
    }
  }

  const MCExpr *Res = MCSpecifierExpr::create(Sub, Spec, getContext());
  SMLoc E = SMLoc::getFromPointer(Parser.getTok().getLoc().getPointer() - 1);
  Operands.push_back(TileOperand::createImm(Res, S, E));
  return false;
}

bool TileAsmParser::parseOperand(OperandVector &Operands, StringRef Mnemonic) {
  if (getLexer().is(AsmToken::Identifier)) {
    if (!tryParseRegisterOperand(Operands, Mnemonic))
      return false;
    if (!tryParseRelocOperand(Operands))
      return false;
    if (getParser().hasPendingError())
      return true;
    // Otherwise fall through: a plain symbol (label) parses as an expression.
  }

  switch (getLexer().getKind()) {
  default:
    return Error(Parser.getTok().getLoc(), "unexpected token in operand");
  case AsmToken::Identifier:
  case AsmToken::LParen:
  case AsmToken::Minus:
  case AsmToken::Plus:
  case AsmToken::Tilde:
  case AsmToken::Integer:
  case AsmToken::Dot:
  case AsmToken::String: {
    const MCExpr *Expr;
    SMLoc S = Parser.getTok().getLoc();
    if (getParser().parseExpression(Expr))
      return true;
    SMLoc E = SMLoc::getFromPointer(Parser.getTok().getLoc().getPointer() - 1);
    Operands.push_back(TileOperand::createImm(Expr, S, E));
    return false;
  }
  }
}

bool TileAsmParser::tokenIsStartOfStatement(AsmToken::TokenKind Token) {
  // VLIW bundle delimiters open a statement so they reach parseInstruction.
  return Token == AsmToken::LCurly || Token == AsmToken::RCurly;
}

bool TileAsmParser::parseInstruction(ParseInstructionInfo &Info, StringRef Name,
                                     SMLoc NameLoc, OperandVector &Operands) {
  if (Name == "{") {
    InBundle = true;
    BundledInsts.clear();
    return false;
  }
  if (Name == "}") {
    InBundle = false;
    return false;
  }

  Operands.push_back(TileOperand::createToken(Name, NameLoc));

  if (getLexer().isNot(AsmToken::EndOfStatement) &&
      getLexer().isNot(AsmToken::RCurly)) {
    if (parseOperand(Operands, Name)) {
      SMLoc Loc = getLexer().getLoc();
      Parser.eatToEndOfStatement();
      return Error(Loc, "unexpected token in argument list");
    }
    while (getLexer().is(AsmToken::Comma)) {
      Parser.Lex();
      if (parseOperand(Operands, Name)) {
        SMLoc Loc = getLexer().getLoc();
        Parser.eatToEndOfStatement();
        return Error(Loc, "unexpected token in argument list");
      }
    }
  }

  if (getLexer().isNot(AsmToken::EndOfStatement) &&
      getLexer().isNot(AsmToken::RCurly)) {
    SMLoc Loc = getLexer().getLoc();
    Parser.eatToEndOfStatement();
    return Error(Loc, "unexpected token in argument list");
  }

  // Consume the end-of-statement, but leave a bundle-closing '}' for the next
  // statement so it dispatches back through parseInstruction.
  if (getLexer().is(AsmToken::EndOfStatement))
    Parser.Lex();
  return false;
}

bool TileAsmParser::emitSpecialInstruction(StringRef Name,
                                           OperandVector &Operands, SMLoc IDLoc,
                                           MCStreamer &Out) {
  auto Op = [&](unsigned I) -> TileOperand & {
    return static_cast<TileOperand &>(*Operands[I]);
  };
  auto addImm = [&](MCInst &Inst, unsigned I) -> bool {
    if (I >= Operands.size() || !Op(I).isImm())
      return Error(IDLoc, "invalid operand for instruction");
    const MCExpr *E = Op(I).getImm();
    if (const auto *CE = dyn_cast<MCConstantExpr>(E))
      Inst.addOperand(MCOperand::createImm(CE->getValue()));
    else
      Inst.addOperand(MCOperand::createExpr(E));
    return false;
  };
  auto addReg = [&](MCInst &Inst, unsigned I) -> bool {
    if (I >= Operands.size() || !Op(I).isReg())
      return Error(IDLoc, "invalid operand for instruction");
    Inst.addOperand(MCOperand::createReg(Op(I).getReg()));
    return false;
  };

  MCInst Inst;
  if (Name == "lnk") {
    if (Operands.size() != 2)
      return Error(IDLoc, "expected 'lnk <reg>'");
    Inst.setOpcode(Tile::LNK);
    if (addReg(Inst, 1))
      return true;
    // The second operand models the PC anchor and is always zero.
    Inst.addOperand(MCOperand::createImm(0));
  } else {
    if (Operands.size() != 4)
      return Error(IDLoc, "expected 'ld_tls <rd>, <rs>, <imm>'");
    Inst.setOpcode(Tile::LD_TLS);
    if (addReg(Inst, 1) || addReg(Inst, 2) || addImm(Inst, 3))
      return true;
  }
  Inst.setLoc(IDLoc);
  return emitOrBuffer(Inst, Out, IDLoc);
}

bool TileAsmParser::emitOrBuffer(const MCInst &Inst, MCStreamer &Out,
                                 SMLoc IDLoc) {
  if (!InBundle) {
    Out.emitInstruction(Inst, getSTI());
    return false;
  }

  // Loads and stores use the Y2 bundle mode (different layout, no fixed FNOP
  // slot) and cannot be slot-merged; keep them out of multi-op bundles.
  if (!TileII::isBundleMergeable(MII.get(Inst.getOpcode()).TSFlags))
    return Error(IDLoc, "TILE-Gx load/store uses the Y2 bundle mode and cannot "
                        "be packed with another instruction");
  if (BundledInsts.size() >= 2)
    return Error(IDLoc, "TILE-Gx X-format bundle holds at most two "
                        "instructions");

  MCInst *Heap = getContext().createMCInst();
  *Heap = Inst;
  BundledInsts.push_back(Heap);
  return false;
}

bool TileAsmParser::flushBundle(MCStreamer &Out, SMLoc IDLoc) {
  if (BundledInsts.empty())
    return false;
  if (BundledInsts.size() == 1) {
    Out.emitInstruction(*BundledInsts.front(), getSTI());
    BundledInsts.clear();
    return false;
  }

  // Choose the X0/X1 slot for each op (the same assignment the code emitter uses
  // when packetized codegen lowers a bundle), rewriting the op placed in X0 to
  // its twin. A contradictory pair has no legal encoding and errors loudly.
  if (const char *Err =
          TileII::assignTwoSlotBundle(*BundledInsts[0], *BundledInsts[1], MII))
    return Error(IDLoc, Err);

  MCInst Bundle;
  Bundle.setOpcode(Tile::BUNDLE);
  for (MCInst *Sub : BundledInsts)
    Bundle.addOperand(MCOperand::createInst(Sub));
  Out.emitInstruction(Bundle, getSTI());
  BundledInsts.clear();
  return false;
}

bool TileAsmParser::matchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                                            OperandVector &Operands,
                                            MCStreamer &Out, uint64_t &ErrorInfo,
                                            bool MatchingInlineAsm) {
  // A bundle delimiter ({ or }) pushes no operands. The closing brace (which
  // leaves InBundle clear) is where a buffered packet is flushed.
  if (Operands.empty()) {
    if (!InBundle)
      return flushBundle(Out, IDLoc);
    return false;
  }

  // lnk and ld_tls are codegen-only (lnk is even printed via a custom operand
  // method with no mnemonic in its AsmString), so the generated matcher never
  // sees them. They appear textually in the GOT-base and TLS-IE sequences, so
  // build their MCInst directly to keep the round-trip closed.
  StringRef Mnemonic = static_cast<TileOperand &>(*Operands[0]).getToken();
  if (Mnemonic == "lnk" || Mnemonic == "ld_tls")
    return emitSpecialInstruction(Mnemonic, Operands, IDLoc, Out);

  MCInst Inst;
  unsigned MatchResult =
      MatchInstructionImpl(Operands, Inst, ErrorInfo, MatchingInlineAsm);
  switch (MatchResult) {
  case Match_Success:
    Inst.setLoc(IDLoc);
    return emitOrBuffer(Inst, Out, IDLoc);
  case Match_MissingFeature:
    return Error(IDLoc, "instruction requires a CPU feature not currently enabled");
  case Match_InvalidOperand: {
    SMLoc ErrorLoc = IDLoc;
    if (ErrorInfo != ~0ULL) {
      if (ErrorInfo >= Operands.size())
        return Error(IDLoc, "too few operands for instruction");
      ErrorLoc = ((TileOperand &)*Operands[ErrorInfo]).getStartLoc();
      if (ErrorLoc == SMLoc())
        ErrorLoc = IDLoc;
    }
    return Error(ErrorLoc, "invalid operand for instruction");
  }
  case Match_MnemonicFail:
    return Error(IDLoc, "invalid instruction mnemonic");
  }
  llvm_unreachable("unknown match result type");
}

ParseStatus TileAsmParser::parseDirective(AsmToken DirectiveID) {
  return ParseStatus::NoMatch;
}

// The register operands parse generically (the .td sets
// ShouldEmitMatchRegisterName = 0); accept any GPR for the integer register
// classes since the encoding is the register number regardless of class.
unsigned TileAsmParser::validateTargetOperandClass(MCParsedAsmOperand &AsmOp,
                                                   unsigned Kind) {
  TileOperand &Op = static_cast<TileOperand &>(AsmOp);
  if (!Op.isReg())
    return Match_InvalidOperand;
  if (Kind == MCK_CPURegs || Kind == MCK_CPU32Regs)
    return Match_Success;
  return Match_InvalidOperand;
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeTileAsmParser() {
  RegisterMCAsmParser<TileAsmParser> X(getTheTileTarget());
}
