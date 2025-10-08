#pragma once

#include "Common.h"
#include "SwiftPartialTranslationUnit.h"

namespace SwiftJITREPL {

/**
 * Swift incremental executor (inspired by Clang's IncrementalExecutor)
 * Manages JIT execution of partial translation units
 */
class SwiftIncrementalExecutor {
private:
    std::unique_ptr<llvm::orc::LLJIT> Jit;
    llvm::orc::ThreadSafeContext& TSCtx;
    std::map<const SwiftPartialTranslationUnit*, llvm::orc::ResourceTrackerSP> ResourceTrackers;
    bool Initialized = false;
    
public:
    SwiftIncrementalExecutor(llvm::orc::ThreadSafeContext& TSC, 
                            std::unique_ptr<llvm::orc::LLJIT> JIT);
    ~SwiftIncrementalExecutor();
    
    // Add a partial translation unit to the JIT
    llvm::Error addModule(SwiftPartialTranslationUnit& PTU);
    
    // Remove a partial translation unit from the JIT
    llvm::Error removeModule(SwiftPartialTranslationUnit& PTU);
    
    // Execute the JIT'd code
    llvm::Error execute();
    
    // Run global constructors (like Clang IncrementalExecutor)
    llvm::Error runCtors() const;
    
    // Get symbol address
    llvm::Expected<llvm::orc::ExecutorAddr> getSymbolAddress(llvm::StringRef Name) const;
    
    // Clean up the JIT instance
    llvm::Error cleanUp();
    
    // Get the execution engine
    llvm::orc::LLJIT& GetExecutionEngine() { return *Jit; }
};

} // namespace SwiftJITREPL

