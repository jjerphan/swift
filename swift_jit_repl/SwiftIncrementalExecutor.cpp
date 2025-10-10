#include "SwiftIncrementalExecutor.h"

// Standard library includes
#include <memory>

// LLVM includes for JIT functionality
#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/ExecutionEngine/Orc/EPCDynamicLibrarySearchGenerator.h"
#include "llvm/Support/Error.h"

namespace SwiftJITREPL {

SwiftIncrementalExecutor::SwiftIncrementalExecutor(llvm::orc::ThreadSafeContext& TSC, 
                                                   std::unique_ptr<llvm::orc::LLJIT> JIT)
    : TSCtx(TSC), Jit(std::move(JIT)) {
}

SwiftIncrementalExecutor::~SwiftIncrementalExecutor() {
    // Clean up all resource trackers
    for (auto& [ptu, tracker] : ResourceTrackers) {
        if (tracker) {
            auto Err = tracker->remove();
            if (Err) {
                // Log the error but don't throw in destructor
                llvm::consumeError(std::move(Err));
            }
        }
    }
}

llvm::Error SwiftIncrementalExecutor::addModule(SwiftPartialTranslationUnit& PTU) {
    if (!Jit) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), 
                                     "JIT not initialized");
    }
    
    // Create a resource tracker for this PTU (following Clang's pattern)
    llvm::orc::ResourceTrackerSP RT =
        Jit->getMainJITDylib().createResourceTracker();
    ResourceTrackers[&PTU] = RT;
    
    // If the PTU has an LLVM module, add it to the JIT
    if (PTU.TheModule) {
        
        // Use ThreadSafeModule wrapper like Clang does
        llvm::orc::ThreadSafeModule TSM(std::move(PTU.TheModule), TSCtx);
        auto result = Jit->addIRModule(RT, std::move(TSM));
        
        if (result) {
            // Error occurred
        }
        return result;
    }
    
    // If no LLVM module, we need to compile the Swift code to LLVM IR first
    return llvm::Error::success();
}

llvm::Error SwiftIncrementalExecutor::removeModule(SwiftPartialTranslationUnit& PTU) {
    llvm::orc::ResourceTrackerSP RT = std::move(ResourceTrackers[&PTU]);
    if (!RT) {
        return llvm::Error::success();
    }

    ResourceTrackers.erase(&PTU);
    if (llvm::Error Err = RT->remove()) {
        return Err;
    }
    return llvm::Error::success();
}

llvm::Error SwiftIncrementalExecutor::execute() {
    if (!Jit) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), 
                                     "JIT not initialized");
    }
    
    // Check if we have any modules to execute
    if (ResourceTrackers.empty()) {
        return llvm::Error::success();
    }
    
    // Initialize the JIT dylib only once (equivalent to Clang's runCtors)
    if (!Initialized) {
        auto& MainJITDylib = Jit->getMainJITDylib();
        if (auto Err = Jit->initialize(MainJITDylib)) {
            return Err;
        }
        Initialized = true;
    }
    
    // Look up and call the latest PTU's unique entry function
    // Try to find the most recent entry function by looking for the highest numbered module
    std::string entryName;
    std::optional<llvm::orc::ExecutorAddr> mainAddr;
    
    // Search for entry functions in reverse order (most recent first)
    for (int i = 9; i >= 0; --i) {
        std::string candidate = std::string("swift_jit_main_Evaluation_") + std::to_string(i);
        auto addrOrErr = getSymbolAddress(candidate);
        if (addrOrErr) {
            entryName = candidate;
            mainAddr = *addrOrErr;
            break;
        }
        llvm::consumeError(addrOrErr.takeError());
    }
    
    // Fallback to generic main function
    if (!mainAddr) {
        auto addrOrErr = getSymbolAddress("swift_jit_main");
        if (addrOrErr) {
            entryName = "swift_jit_main";
            mainAddr = *addrOrErr;
        } else {
            // Don't return error, just log the warning - some modules might not have main functions
            return llvm::Error::success();
        }
    }
    
    // Cast the address to a function pointer and call it
    using MainFunc = void(*)();
    MainFunc mainFunc = reinterpret_cast<MainFunc>(mainAddr->getValue());
    mainFunc();
    
    return llvm::Error::success();
}

llvm::Error SwiftIncrementalExecutor::runCtors() const {
    if (!Jit) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), 
                                     "JIT not initialized");
    }
    
    // Initialize the JIT dylib to run global constructors (like Clang IncrementalExecutor)
    auto& MainJITDylib = Jit->getMainJITDylib();
    if (auto Err = Jit->initialize(MainJITDylib)) {
        return Err;
    }
    return llvm::Error::success();
}

llvm::Expected<llvm::orc::ExecutorAddr> SwiftIncrementalExecutor::getSymbolAddress(llvm::StringRef Name) const {
    return getSymbolAddress(Name, LinkerName);
}

llvm::Expected<llvm::orc::ExecutorAddr> SwiftIncrementalExecutor::getSymbolAddress(llvm::StringRef Name, SymbolNameKind NameKind) const {
    if (!Jit) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), 
                                     "JIT not initialized");
    }
    
    using namespace llvm::orc;
    
    // Create search order following Clang's pattern: MainJITDylib, PlatformJITDylib, ProcessSymbolsJITDylib
    auto SO = makeJITDylibSearchOrder({&Jit->getMainJITDylib(),
                                       Jit->getPlatformJITDylib().get(),
                                       Jit->getProcessSymbolsJITDylib().get()});

    auto& ES = Jit->getExecutionSession();
    
    // Use intern for linker names, mangleAndIntern for mangled names (following Clang's pattern)
    auto SymOrErr = ES.lookup(SO, (NameKind == LinkerName) ? ES.intern(Name)
                                                           : Jit->mangleAndIntern(Name));
    if (auto Err = SymOrErr.takeError())
        return std::move(Err);
    return SymOrErr->getAddress();
}

llvm::Error SwiftIncrementalExecutor::cleanUp() {
    if (!Jit) {
        return llvm::Error::success();
    }
    
    // This calls the global dtors of registered modules
    return Jit->deinitialize(Jit->getMainJITDylib());
}

} // namespace SwiftJITREPL
