 add_llvm_library( LLVMLoopFusionPass MODULE
    LoopFusionPass.cpp

    PLUGIN_TOOL
    opt
    )