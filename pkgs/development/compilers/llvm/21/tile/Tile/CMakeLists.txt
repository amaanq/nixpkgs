add_llvm_component_group(Tile)

set(LLVM_TARGET_DEFINITIONS Tile.td)

tablegen(LLVM TileGenRegisterInfo.inc -gen-register-info)
tablegen(LLVM TileGenInstrInfo.inc -gen-instr-info)
tablegen(LLVM TileGenMCCodeEmitter.inc -gen-emitter)
tablegen(LLVM TileGenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM TileGenAsmMatcher.inc -gen-asm-matcher)
tablegen(LLVM TileGenDAGISel.inc -gen-dag-isel)
tablegen(LLVM TileGenCallingConv.inc -gen-callingconv)
tablegen(LLVM TileGenSubtargetInfo.inc -gen-subtarget)
tablegen(LLVM TileGenDFAPacketizer.inc -gen-dfa-packetizer)

add_public_tablegen_target(TileCommonTableGen)

add_llvm_target(TileCodeGen
  TileAsmPrinter.cpp
  TileExpandPseudo.cpp
  TileFrameLowering.cpp
  TileInstrInfo.cpp
  TileISelDAGToDAG.cpp
  TileISelLowering.cpp
  TileMachineFunction.cpp
  TileMCInstLower.cpp
  TileRegisterInfo.cpp
  TileSelectionDAGInfo.cpp
  TileSubtarget.cpp
  TileTargetMachine.cpp
  TileVLIWPacketizer.cpp

  LINK_COMPONENTS
  Analysis
  AsmPrinter
  CodeGen
  CodeGenTypes
  Core
  MC
  SelectionDAG
  Support
  Target
  TargetParser
  TileDesc
  TileInfo
  TransformUtils

  ADD_TO_COMPONENT
  Tile
  )

add_subdirectory(AsmParser)
add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
