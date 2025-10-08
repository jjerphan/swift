#pragma once

#include "Common.h"

namespace SwiftJITREPL {

/**
 * Swift-specific partial translation unit (inspired by Clang's PartialTranslationUnit)
 * Represents a piece of Swift code that has been parsed and compiled incrementally
 * Each PTU owns its own ModuleDecl for true state reuse via module imports
 */
struct SwiftPartialTranslationUnit {
    swift::ModuleDecl* ModulePart = nullptr;  // Reference to ModuleDecl (owned by ASTContext)
    std::unique_ptr<llvm::Module> TheModule;
    std::string InputCode;
    swift::ASTContext* SharedASTContext = nullptr;  // Reference to shared context
    
    SwiftPartialTranslationUnit() = default;
    
    bool operator==(const SwiftPartialTranslationUnit &other) const {
        return other.ModulePart == ModulePart && other.TheModule == TheModule;
    }
};

} // namespace SwiftJITREPL

