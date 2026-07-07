add_llvm_component_library(LLVMTileAsmParser
  TileAsmParser.cpp

  LINK_COMPONENTS
  MC
  MCParser
  TileDesc
  TileInfo
  Support
  TargetParser

  ADD_TO_COMPONENT
  Tile
  )
