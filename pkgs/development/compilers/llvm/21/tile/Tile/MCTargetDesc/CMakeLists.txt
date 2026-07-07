add_llvm_component_library(LLVMTileDesc
  TileAsmBackend.cpp
  TileELFObjectWriter.cpp
  TileInstPrinter.cpp
  TileMCAsmInfo.cpp
  TileMCCodeEmitter.cpp
  TileMCTargetDesc.cpp

  LINK_COMPONENTS
  MC
  Support
  TargetParser
  TileInfo

  ADD_TO_COMPONENT
  Tile
  )
