#include "SwiftIncrementalExecutor.h"

// Standard library includes
#include <memory>

// LLVM includes for JIT functionality
#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
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
    llvm::errs() << "[SwiftIncrementalExecutor] addModule called\n";
    
    if (!Jit) {
        llvm::errs() << "[SwiftIncrementalExecutor] ERROR: JIT not initialized\n";
        return llvm::createStringError(llvm::inconvertibleErrorCode(), 
                                     "JIT not initialized");
    }
    
    llvm::errs() << "[SwiftIncrementalExecutor] JIT is initialized, creating resource tracker\n";
    
    // Create a resource tracker for this PTU (following Clang's pattern)
    llvm::orc::ResourceTrackerSP RT =
        Jit->getMainJITDylib().createResourceTracker();
    ResourceTrackers[&PTU] = RT;
    
    llvm::errs() << "[SwiftIncrementalExecutor] Resource tracker created for PTU\n";
    
    // If the PTU has an LLVM module, add it to the JIT
    if (PTU.TheModule) {
        llvm::errs() << "[SwiftIncrementalExecutor] PTU has LLVM module, adding to JIT\n";
        llvm::errs() << "[SwiftIncrementalExecutor] Module name: " << PTU.TheModule->getName() << "\n";
        llvm::errs() << "[SwiftIncrementalExecutor] Module functions: ";
        for (auto& F : *PTU.TheModule) {
            llvm::errs() << F.getName() << " ";
        }
        llvm::errs() << "\n";
        
        // Add module to JIT using the resource tracker (following Clang's pattern)
        llvm::errs() << "[SwiftIncrementalExecutor] About to add module to JIT...\n";
        llvm::errs() << "[SwiftIncrementalExecutor] Dumping LLVM IR content:\n";
        PTU.TheModule->print(llvm::errs(), nullptr);
        llvm::errs() << "[SwiftIncrementalExecutor] End of LLVM IR dump\n";
        
        // Use ThreadSafeModule wrapper like Clang does
        llvm::orc::ThreadSafeModule TSM(std::move(PTU.TheModule), TSCtx);
        auto result = Jit->addIRModule(RT, std::move(TSM));
        
        if (result) {
            llvm::errs() << "[SwiftIncrementalExecutor] ERROR: Failed to add IR module to JIT: " 
                        << llvm::toString(std::move(result)) << "\n";
        } else {
            llvm::errs() << "[SwiftIncrementalExecutor] Successfully added IR module to JIT\n";
            llvm::errs() << "[SwiftIncrementalExecutor] JIT module addition completed\n";
        }
        return result;
    }
    
    // If no LLVM module, we need to compile the Swift code to LLVM IR first
    llvm::errs() << "[SwiftIncrementalExecutor] PTU has no LLVM module\n";
    llvm::errs() << "[SwiftIncrementalExecutor] Input code: " << PTU.InputCode << "\n";
    llvm::errs() << "[SwiftIncrementalExecutor] Module part: " << (PTU.ModulePart ? "present" : "null") << "\n";    
    llvm::errs() << "[SwiftIncrementalExecutor] Returning success (no LLVM module to add)\n";
    return llvm::Error::success();
}

llvm::Error SwiftIncrementalExecutor::removeModule(SwiftPartialTranslationUnit& PTU) {
    llvm::errs() << "[SwiftIncrementalExecutor] removeModule called\n";
    
    llvm::orc::ResourceTrackerSP RT = std::move(ResourceTrackers[&PTU]);
    if (!RT) {
        llvm::errs() << "[SwiftIncrementalExecutor] No resource tracker found for PTU\n";
        return llvm::Error::success();
    }

    ResourceTrackers.erase(&PTU);
    llvm::errs() << "[SwiftIncrementalExecutor] Removing resource tracker...\n";
    if (llvm::Error Err = RT->remove()) {
        llvm::errs() << "[SwiftIncrementalExecutor] ERROR: Failed to remove resource tracker: " 
                    << llvm::toString(std::move(Err)) << "\n";
        return Err;
    }
    llvm::errs() << "[SwiftIncrementalExecutor] Resource tracker removed successfully\n";
    return llvm::Error::success();
}

llvm::Error SwiftIncrementalExecutor::execute() {
    llvm::errs() << "[SwiftIncrementalExecutor::execute] Starting execution\n";
    
    if (!Jit) {
        llvm::errs() << "[SwiftIncrementalExecutor::execute] ERROR: JIT not initialized\n";
        return llvm::createStringError(llvm::inconvertibleErrorCode(), 
                                     "JIT not initialized");
    }
    
    // Check if we have any modules to execute
    if (ResourceTrackers.empty()) {
        llvm::errs() << "[SwiftIncrementalExecutor::execute] No modules to execute\n";
        return llvm::Error::success();
    }
    
    llvm::errs() << "[SwiftIncrementalExecutor::execute] Found " << ResourceTrackers.size() << " modules to execute\n";
    
    // Initialize the JIT dylib only once (equivalent to Clang's runCtors)
    if (!Initialized) {
        llvm::errs() << "[SwiftIncrementalExecutor::execute] Initializing JIT dylib...\n";
        auto& MainJITDylib = Jit->getMainJITDylib();
        if (auto Err = Jit->initialize(MainJITDylib)) {
            llvm::errs() << "[SwiftIncrementalExecutor::execute] ERROR: Failed to initialize JIT dylib\n";
            return Err;
        }
        Initialized = true;
        llvm::errs() << "[SwiftIncrementalExecutor::execute] JIT dylib initialized successfully\n";
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
            llvm::errs() << "[SwiftIncrementalExecutor::execute] Found entry '" << entryName << "' at address: " 
                        << mainAddr->getValue() << "\n";
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
            llvm::errs() << "[SwiftIncrementalExecutor::execute] Found fallback entry '" << entryName << "' at address: " 
                        << mainAddr->getValue() << "\n";
        } else {
            llvm::errs() << "[SwiftIncrementalExecutor::execute] WARNING: Could not find any entry function: " 
                        << llvm::toString(addrOrErr.takeError()) << "\n";
            // Don't return error, just log the warning - some modules might not have main functions
            return llvm::Error::success();
        }
    }
    
    // Cast the address to a function pointer and call it
    using MainFunc = void(*)();
    MainFunc mainFunc = reinterpret_cast<MainFunc>(mainAddr->getValue());
    llvm::errs() << "[SwiftIncrementalExecutor::execute] Calling " << entryName << "...\n";
    mainFunc();
    llvm::errs() << "[SwiftIncrementalExecutor::execute] " << entryName << " completed\n";
    
    return llvm::Error::success();
}

llvm::Error SwiftIncrementalExecutor::runCtors() const {
    llvm::errs() << "[SwiftIncrementalExecutor::runCtors] Running global constructors\n";
    
    if (!Jit) {
        llvm::errs() << "[SwiftIncrementalExecutor::runCtors] ERROR: JIT not initialized\n";
        return llvm::createStringError(llvm::inconvertibleErrorCode(), 
                                     "JIT not initialized");
    }
    
    // Initialize the JIT dylib to run global constructors (like Clang IncrementalExecutor)
    auto& MainJITDylib = Jit->getMainJITDylib();
    if (auto Err = Jit->initialize(MainJITDylib)) {
        llvm::errs() << "[SwiftIncrementalExecutor::runCtors] ERROR: Failed to initialize JIT dylib\n";
        return Err;
    }
    
    llvm::errs() << "[SwiftIncrementalExecutor::runCtors] Global constructors executed successfully\n";
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
