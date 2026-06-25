add_llvm_library( LLVMLoopFissionPass MODULE
    LoopFissionPass.cpp

    PLUGIN_TOOL
    opt
    )
