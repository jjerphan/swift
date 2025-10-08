#include "SwiftIncrementalExecutor.h"

// Standard library includes
#include <iostream>
#include <memory>

// LLVM includes for JIT functionality
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/Support/Error.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"

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
    
    // Create a resource tracker for this PTU
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
        
        // Move the module to the JIT, but release it from the PTU to avoid double-deletion
        llvm::errs() << "[SwiftIncrementalExecutor] About to add module to JIT...\n";
        llvm::errs() << "[SwiftIncrementalExecutor] Dumping LLVM IR content:\n";
        PTU.TheModule->print(llvm::errs(), nullptr);
        llvm::errs() << "[SwiftIncrementalExecutor] End of LLVM IR dump\n";
        auto result = Jit->addIRModule(RT, {std::move(PTU.TheModule), TSCtx});
        // Release the module from the PTU to prevent double-deletion
        PTU.TheModule.release();
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
    llvm::orc::ResourceTrackerSP RT = std::move(ResourceTrackers[&PTU]);
    if (!RT)
        return llvm::Error::success();

    ResourceTrackers.erase(&PTU);
    if (llvm::Error Err = RT->remove())
        return Err;
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
    
    // Look up and call the swift_jit_main function
    llvm::errs() << "[SwiftIncrementalExecutor::execute] Looking up swift_jit_main function...\n";
    auto mainAddrOrErr = getSymbolAddress("swift_jit_main");
    if (mainAddrOrErr) {
        llvm::errs() << "[SwiftIncrementalExecutor::execute] Found swift_jit_main at address: " 
                    << mainAddrOrErr->getValue() << "\n";
        
        // Cast the address to a function pointer and call it
        using MainFunc = void(*)();
        MainFunc mainFunc = reinterpret_cast<MainFunc>(mainAddrOrErr->getValue());
        llvm::errs() << "[SwiftIncrementalExecutor::execute] Calling swift_jit_main...\n";
        mainFunc();
        llvm::errs() << "[SwiftIncrementalExecutor::execute] swift_jit_main completed\n";
    } else {
        llvm::errs() << "[SwiftIncrementalExecutor::execute] WARNING: Could not find swift_jit_main function: " 
                    << llvm::toString(mainAddrOrErr.takeError()) << "\n";
        // Don't return error, just log the warning - some modules might not have main functions
    }
    
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
    if (!Jit) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), 
                                     "JIT not initialized");
    }
    
    using namespace llvm::orc;
    auto SO = makeJITDylibSearchOrder({&Jit->getMainJITDylib(),
                                       Jit->getPlatformJITDylib().get(),
                                       Jit->getProcessSymbolsJITDylib().get()});

    auto& ES = Jit->getExecutionSession();
    auto SymOrErr = ES.lookup(SO, ES.intern(Name));
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
