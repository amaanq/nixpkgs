add_llvm_component_library(LLVMTileInfo
  TileTargetInfo.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  Tile
  )
