#include "MinimalSwiftREPL.h"

#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBTarget.h"
#include "lldb/API/SBProcess.h"
#include "lldb/API/SBExpressionOptions.h"
#include "lldb/API/SBValue.h"
#include "lldb/API/SBError.h"
#include "lldb/API/SBLaunchInfo.h"

#include <iostream>
#include <memory>
#include <mutex>

namespace SwiftMinimalREPL {

/**
 * Private implementation class (PIMPL idiom)
 * Hides LLDB dependencies from the header file
 */
class MinimalSwiftREPL::Impl {
public:
    lldb::SBDebugger debugger;
    lldb::SBTarget target;
    lldb::SBProcess process;
    lldb::SBExpressionOptions options;
    REPLConfig config;
    bool initialized = false;
    std::string lastError;
    
    // Static initialization management
    static std::mutex initMutex;
    static bool lldbInitialized;
    
    explicit Impl(const REPLConfig& cfg) : config(cfg) {
        // Ensure LLDB is initialized (thread-safe)
        std::lock_guard<std::mutex> lock(initMutex);
        if (!lldbInitialized) {
            lldb::SBDebugger::Initialize();
            Impl::lldbInitialized = true;
        }
    }
    
    ~Impl() {
        // Note: We don't call SBDebugger::Terminate() here because
        // other instances might still be using LLDB
    }
    
    bool initialize() {
        try {
            // Create debugger instance
            debugger = lldb::SBDebugger::Create(false);
            if (!debugger.IsValid()) {
                lastError = "Failed to create LLDB debugger instance";
                return false;
            }
            
            // Set debugger to synchronous mode for predictable behavior
            debugger.SetAsync(false);
            
            // Create a target with a simple executable
            target = debugger.CreateTarget("/tmp/test");
            if (!target.IsValid()) {
                lastError = "Failed to create LLDB target";
                return false;
            }
            
            // Create a launch info for the process
            const char* argv[] = {"/tmp/test", nullptr};
            lldb::SBLaunchInfo launch_info(argv);
            launch_info.SetLaunchFlags(lldb::eLaunchFlagStopAtEntry);
            
            // Launch the process to create a proper execution context
            lldb::SBError error;
            process = target.Launch(launch_info, error);
            
            if (!process.IsValid()) {
                lastError = "Failed to launch process: " + std::string(error.GetCString() ? error.GetCString() : "Unknown error");
                return false;
            }
            
            // Stop the process immediately to avoid it running away
            process.Stop();
            
            // Configure expression options for Swift
            options.SetLanguage(lldb::eLanguageTypeSwift);
            options.SetFetchDynamicValue(config.fetch_dynamic_values ? 
                                       lldb::eDynamicCanRunTarget : lldb::eDynamicDontRunTarget);
            options.SetTryAllThreads(config.try_all_threads);
            options.SetUnwindOnError(config.unwind_on_error);
            options.SetIgnoreBreakpoints(config.ignore_breakpoints);
            options.SetGenerateDebugInfo(config.generate_debug_info);
            
            if (config.timeout_usec > 0) {
                options.SetTimeoutInMicroSeconds(config.timeout_usec);
            }
            
            initialized = true;
            return true;
            
        } catch (const std::exception& e) {
            lastError = std::string("Exception during initialization: ") + e.what();
            return false;
        } catch (...) {
            lastError = "Unknown exception during initialization";
            return false;
        }
    }
    
    EvaluationResult evaluate(const std::string& expression) {
        if (!initialized) {
            return EvaluationResult("REPL not initialized");
        }
        
        try {
            // Evaluate the Swift expression
            lldb::SBValue result = target.EvaluateExpression(expression.c_str(), options);
            
            if (result.IsValid() && !result.GetError().Fail()) {
                // Extract value and type information
                std::string value = result.GetValue() ? result.GetValue() : "<no value>";
                std::string type = result.GetTypeName() ? result.GetTypeName() : "<unknown type>";
                
                // If no explicit value, try to get summary or description
                if (value == "<no value>" || value.empty()) {
                    if (result.GetSummary()) {
                        value = result.GetSummary();
                    } else if (result.GetObjectDescription()) {
                        value = result.GetObjectDescription();
                    }
                }
                
                return EvaluationResult(value, type);
            } else {
                // Extract error information
                lldb::SBError error = result.GetError();
                std::string errorMsg = error.GetCString() ? error.GetCString() : "Unknown evaluation error";
                lastError = errorMsg;
                return EvaluationResult(errorMsg);
            }
            
        } catch (const std::exception& e) {
            std::string errorMsg = std::string("Exception during evaluation: ") + e.what();
            lastError = errorMsg;
            return EvaluationResult(errorMsg);
        } catch (...) {
            std::string errorMsg = "Unknown exception during evaluation";
            lastError = errorMsg;
            return EvaluationResult(errorMsg);
        }
    }
    
    bool reset() {
        if (!initialized) {
            return false;
        }
        
        try {
            // Kill the current process if it's still running
            if (process.IsValid()) {
                process.Kill();
            }
            
            // Create a new target and process to reset the evaluation context
            target = debugger.CreateTarget("/tmp/test");
            if (!target.IsValid()) {
                return false;
            }
            
            const char* argv[] = {"/tmp/test", nullptr};
            lldb::SBLaunchInfo launch_info(argv);
            launch_info.SetLaunchFlags(lldb::eLaunchFlagStopAtEntry);
            
            lldb::SBError error;
            process = target.Launch(launch_info, error);
            
            if (!process.IsValid()) {
                return false;
            }
            
            process.Stop();
            return true;
        } catch (...) {
            lastError = "Failed to reset REPL context";
            return false;
        }
    }
};

// Static member definitions
std::mutex MinimalSwiftREPL::Impl::initMutex;
bool MinimalSwiftREPL::Impl::lldbInitialized = false;

// MinimalSwiftREPL implementation

MinimalSwiftREPL::MinimalSwiftREPL(const REPLConfig& config) 
    : pImpl(std::make_unique<Impl>(config)) {
}

MinimalSwiftREPL::~MinimalSwiftREPL() = default;

MinimalSwiftREPL::MinimalSwiftREPL(MinimalSwiftREPL&& other) noexcept 
    : pImpl(std::move(other.pImpl)) {
}

MinimalSwiftREPL& MinimalSwiftREPL::operator=(MinimalSwiftREPL&& other) noexcept {
    if (this != &other) {
        pImpl = std::move(other.pImpl);
    }
    return *this;
}

bool MinimalSwiftREPL::initialize() {
    return pImpl ? pImpl->initialize() : false;
}

bool MinimalSwiftREPL::isInitialized() const {
    return pImpl && pImpl->initialized;
}

EvaluationResult MinimalSwiftREPL::evaluate(const std::string& expression) {
    if (!pImpl) {
        return EvaluationResult("REPL instance is invalid");
    }
    return pImpl->evaluate(expression);
}

std::vector<EvaluationResult> MinimalSwiftREPL::evaluateMultiple(const std::vector<std::string>& expressions) {
    std::vector<EvaluationResult> results;
    results.reserve(expressions.size());
    
    for (const auto& expr : expressions) {
        results.push_back(evaluate(expr));
    }
    
    return results;
}

bool MinimalSwiftREPL::reset() {
    return pImpl ? pImpl->reset() : false;
}

std::string MinimalSwiftREPL::getLastError() const {
    return pImpl ? pImpl->lastError : "REPL instance is invalid";
}

bool MinimalSwiftREPL::isSwiftSupportAvailable() {
    try {
        std::cout << "[DEBUG] Starting Swift REPL availability check..." << std::endl;
        
        // Try to initialize LLDB and check if Swift language is supported
        std::lock_guard<std::mutex> lock(Impl::initMutex);
        if (!Impl::lldbInitialized) {
            std::cout << "[DEBUG] Initializing LLDB..." << std::endl;
            lldb::SBDebugger::Initialize();
            Impl::lldbInitialized = true;
            std::cout << "[DEBUG] LLDB initialized successfully" << std::endl;
        } else {
            std::cout << "[DEBUG] LLDB already initialized" << std::endl;
        }
        
        std::cout << "[DEBUG] Creating LLDB debugger..." << std::endl;
        lldb::SBDebugger debugger = lldb::SBDebugger::Create(false);
        if (!debugger.IsValid()) {
            std::cout << "[ERROR] Failed to create LLDB debugger" << std::endl;
            return false;
        }
        std::cout << "[DEBUG] LLDB debugger created successfully" << std::endl;
        
        std::cout << "[DEBUG] Creating LLDB target..." << std::endl;
        // Use a simple executable to avoid crashes
        lldb::SBTarget target = debugger.CreateTarget("/tmp/test");
        if (!target.IsValid()) {
            std::cout << "[ERROR] Failed to create LLDB target" << std::endl;
            return false;
        }
        std::cout << "[DEBUG] LLDB target created successfully" << std::endl;
        
        // Create a launch info and launch the process
        std::cout << "[DEBUG] Launching process..." << std::endl;
        const char* argv[] = {"/tmp/test", nullptr};
        lldb::SBLaunchInfo launch_info(argv);
        launch_info.SetLaunchFlags(lldb::eLaunchFlagStopAtEntry);
        
        lldb::SBError error;
        lldb::SBProcess process = target.Launch(launch_info, error);
        
        if (!process.IsValid()) {
            std::cout << "[ERROR] Failed to launch process: " << (error.GetCString() ? error.GetCString() : "Unknown error") << std::endl;
            return false;
        }
        
        // Stop the process immediately
        process.Stop();
        std::cout << "[DEBUG] Process launched and stopped successfully" << std::endl;
        
        // Try a simple Swift expression to test support
        std::cout << "[DEBUG] Setting up Swift expression options..." << std::endl;
        lldb::SBExpressionOptions options;
        options.SetLanguage(lldb::eLanguageTypeSwift);
        options.SetTimeoutInMicroSeconds(100000); // 0.1 second timeout
        std::cout << "[DEBUG] Expression options configured for Swift language" << std::endl;
        
        std::cout << "[DEBUG] Attempting to evaluate Swift expression: '1 + 1'" << std::endl;
        lldb::SBValue result = target.EvaluateExpression("1 + 1", options);
        
        if (result.IsValid()) {
            std::cout << "[DEBUG] Swift expression evaluation succeeded - result is valid" << std::endl;
            if (!result.GetError().Fail()) {
                std::cout << "[DEBUG] No errors reported - Swift REPL is AVAILABLE!" << std::endl;
                return true;
            } else {
                std::cout << "[ERROR] Swift expression evaluation failed with error: " 
                          << (result.GetError().GetCString() ? result.GetError().GetCString() : "Unknown error") << std::endl;
                return false;
            }
        } else {
            std::cout << "[ERROR] Swift expression evaluation failed - result is invalid" << std::endl;
            return false;
        }
        
    } catch (const std::exception& e) {
        std::cout << "[ERROR] Exception during Swift REPL check: " << e.what() << std::endl;
        return false;
    } catch (...) {
        std::cout << "[ERROR] Unknown exception during Swift REPL check" << std::endl;
        return false;
    }
}

// Convenience functions

EvaluationResult evaluateSwiftExpression(const std::string& expression, const REPLConfig& config) {
    MinimalSwiftREPL repl(config);
    if (!repl.initialize()) {
        return EvaluationResult("Failed to initialize Swift REPL: " + repl.getLastError());
    }
    return repl.evaluate(expression);
}

bool isSwiftREPLAvailable() {
    return MinimalSwiftREPL::isSwiftSupportAvailable();
}

} // namespace SwiftMinimalREPL
