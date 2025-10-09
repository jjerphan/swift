#pragma once

#include "Common.h"
#include "SwiftPartialTranslationUnit.h"

namespace SwiftJITREPL {

/**
 * Swift incremental parser (inspired by Clang's IncrementalParser)
 * Handles incremental parsing of Swift code using multiple ModuleDecl instances
 * Each Parse() call creates a new ModuleDecl with imports to previous modules for state reuse
 */
class SwiftIncrementalParser {
private:
    swift::ASTContext* sharedASTContext;
    std::vector<swift::ModuleDecl*>* modules;  // Raw pointers (owned by ASTContext)
    llvm::orc::ThreadSafeContext* TSCtx;
    swift::CompilerInstance* sharedCompilerInstance;  // Reference to shared CompilerInstance
    swift::CompilerInvocation* compilerInvocation;  // Reference to shared CompilerInvocation
    std::list<SwiftPartialTranslationUnit> PTUs;
    unsigned InputCount = 0;
    
public:
    SwiftIncrementalParser(swift::ASTContext* sharedASTContext, 
                          std::vector<swift::ModuleDecl*>* modules,
                          llvm::orc::ThreadSafeContext* TSCtx,
                          swift::CompilerInstance* sharedCompilerInstance,
                          swift::CompilerInvocation* compilerInvocation);
    ~SwiftIncrementalParser();
    
    // Parse incremental Swift input and return a partial translation unit
    llvm::Expected<SwiftPartialTranslationUnit&> parse(llvm::StringRef Input);
    
    // Get all parsed translation units
    std::list<SwiftPartialTranslationUnit>& getPTUs() { return PTUs; }
    
    // Clean up a specific PTU
    void cleanUpPTU(SwiftPartialTranslationUnit& PTU);
    
    // Get the shared AST context
    swift::ASTContext* getASTContext() const { return sharedASTContext; }
    
private:
    std::string accumulateAllCode(const std::string& newInput);
    swift::ImplicitImportInfo createImplicitImports();
};

} // namespace SwiftJITREPL

