#include "MinimalSwiftREPL.h"

#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBTarget.h"
#include "lldb/API/SBProcess.h"
#include "lldb/API/SBExpressionOptions.h"
#include "lldb/API/SBValue.h"
#include "lldb/API/SBError.h"
#include "lldb/API/SBLaunchInfo.h"
#include "lldb/API/SBPlatform.h"
#include "lldb/API/SBCommandInterpreter.h"

#include <iostream>
#include <memory>
#include <mutex>
#include <unistd.h>

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
        // Set Swift resource directory environment variable before initializing LLDB
        std::cout << "[DEBUG] Setting Swift resource directory environment variable..." << std::endl;
        std::string swift_root = "/home/jjerphan/dev/build/Ninja-RelWithDebInfoAssert/swift-linux-x86_64";
        setenv("SWIFT_RESOURCE_DIR", swift_root.c_str(), 1);
        std::cout << "[DEBUG] Set SWIFT_RESOURCE_DIR=" << getenv("SWIFT_RESOURCE_DIR") << std::endl;
        
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
            
            // Try to explicitly load Swift libraries into the target process
            // This might help with symbol resolution
            std::cout << "[DEBUG] Attempting to preload Swift libraries..." << std::endl;
            
            // Set environment variables that might help with Swift library discovery
            std::string swift_root = "/home/jjerphan/dev/build/Ninja-RelWithDebInfoAssert/swift-linux-x86_64";
            std::string swift_lib_path = swift_root + "/lib/swift/linux";
            std::string swift_lib_x86_64_path = swift_root + "/lib/swift/linux/x86_64";
            
            // Try to explicitly load Swift modules using LLDB's module loading
            std::cout << "[DEBUG] Attempting to load Swift modules..." << std::endl;
            try {
                // Try to load the Swift standard library module
                lldb::SBError module_error;
                
                // Try to load libswiftCore.so explicitly
                std::string swift_core_path = swift_lib_path + "/libswiftCore.so";
                std::cout << "[DEBUG] Trying to load: " << swift_core_path << std::endl;
                
                // Use LLDB's module loading mechanism
                if (access(swift_core_path.c_str(), F_OK) == 0) {
                    std::cout << "[DEBUG] Swift core library file exists" << std::endl;
                    
                    // Try to add the module to the target using the correct API
                    // Note: This is experimental and might not work
                    lldb::SBModule module = target.AddModule(swift_core_path.c_str(), nullptr, nullptr);
                    if (module.IsValid()) {
                        std::cout << "[DEBUG] Successfully loaded Swift core module" << std::endl;
                    } else {
                        std::cout << "[DEBUG] Failed to load Swift core module" << std::endl;
                    }
                } else {
                    std::cout << "[DEBUG] Swift core library file does not exist: " << swift_core_path << std::endl;
                }
                
                // Try to also load the Swift standard library module using Swift-specific paths
                std::string swift_module_path = swift_lib_path + "/Swift.swiftmodule";
                std::cout << "[DEBUG] Trying to find Swift module directory: " << swift_module_path << std::endl;
                
                if (access(swift_module_path.c_str(), F_OK) == 0) {
                    std::cout << "[DEBUG] Swift module directory exists" << std::endl;
                    
                    // Look for the actual .swiftmodule file
                    std::string swiftmodule_file = swift_module_path + "/x86_64-unknown-linux-gnu.swiftmodule";
                    if (access(swiftmodule_file.c_str(), F_OK) == 0) {
                        std::cout << "[DEBUG] Found Swift.swiftmodule file: " << swiftmodule_file << std::endl;
                        
                                        // Try to add this as a module
                lldb::SBModule swift_module = target.AddModule(swiftmodule_file.c_str(), nullptr, nullptr);
                if (swift_module.IsValid()) {
                    std::cout << "[DEBUG] Successfully loaded Swift.swiftmodule" << std::endl;
                } else {
                    std::cout << "[DEBUG] Failed to load Swift.swiftmodule" << std::endl;
                }
                
                // Try to use LLDB's process to load the Swift libraries
                std::cout << "[DEBUG] Attempting to load Swift libraries via process..." << std::endl;
                try {
                    // Try to load libswiftCore.so into the target process
                    lldb::SBError process_error;
                    std::string swift_core_lib = "libswiftCore.so";
                    
                    // Use the process to load the library
                    uint32_t token = process.LoadImage(swift_core_lib.c_str(), nullptr, process_error);
                    if (token != 0) {
                        std::cout << "[DEBUG] Successfully loaded libswiftCore.so via process, token: " << token << std::endl;
                    } else {
                        std::cout << "[DEBUG] Failed to load libswiftCore.so via process: " << process_error.GetCString() << std::endl;
                    }
                } catch (const std::exception& e) {
                    std::cout << "[DEBUG] Exception during process loading: " << e.what() << std::endl;
                } catch (...) {
                    std::cout << "[DEBUG] Unknown exception during process loading" << std::endl;
                }
                    } else {
                        std::cout << "[DEBUG] Swift.swiftmodule file not found: " << swiftmodule_file << std::endl;
                    }
                } else {
                    std::cout << "[DEBUG] Swift module directory does not exist: " << swift_module_path << std::endl;
                }
                
                            // Try a different approach - set environment variables that might help LLDB find Swift modules
            std::cout << "[DEBUG] Setting Swift module environment variables..." << std::endl;
            
            // Set environment variables in the current process that might help LLDB
            setenv("SWIFT_ROOT", swift_root.c_str(), 1);
            setenv("SWIFT_LIBRARY_PATH", swift_lib_path.c_str(), 1);
            setenv("SWIFT_MODULE_PATH", swift_module_path.c_str(), 1);
            
            std::cout << "[DEBUG] Set environment variables: SWIFT_ROOT=" << getenv("SWIFT_ROOT") 
                          << ", SWIFT_LIBRARY_PATH=" << getenv("SWIFT_LIBRARY_PATH")
                          << ", SWIFT_MODULE_PATH=" << getenv("SWIFT_MODULE_PATH") << std::endl;
            
            // Set Swift module search paths on the target
            std::cout << "[DEBUG] Setting Swift module search paths on target..." << std::endl;
            try {
                // Use LLDB's command interpreter to set the Swift module search paths
                lldb::SBCommandInterpreter interpreter = debugger.GetCommandInterpreter();
                if (interpreter.IsValid()) {
                    std::string command = "settings set target.swift-module-search-paths " + swift_lib_path;
                    std::cout << "[DEBUG] Executing command: " << command << std::endl;
                    
                    lldb::SBCommandReturnObject result;
                    interpreter.HandleCommand(command.c_str(), result);
                    
                    if (result.Succeeded()) {
                        std::cout << "[DEBUG] Successfully set Swift module search paths" << std::endl;
                    } else {
                        std::cout << "[DEBUG] Failed to set Swift module search paths: " << result.GetError() << std::endl;
                    }
                    
                    // Try to set additional Swift-specific settings
                    std::cout << "[DEBUG] Setting additional Swift settings..." << std::endl;
                    
                    // Try to set Swift framework search paths
                    std::string framework_command = "settings set target.swift-framework-search-paths " + swift_lib_path;
                    interpreter.HandleCommand(framework_command.c_str(), result);
                    if (result.Succeeded()) {
                        std::cout << "[DEBUG] Successfully set Swift framework search paths" << std::endl;
                    } else {
                        std::cout << "[DEBUG] Failed to set Swift framework search paths: " << result.GetError() << std::endl;
                    }
                    
                    // Try to set Swift extra clang flags to include our paths
                    std::string clang_flags = "-I" + swift_root + "/include -L" + swift_lib_path;
                    std::string clang_command = "settings set target.swift-extra-clang-flags " + clang_flags;
                    interpreter.HandleCommand(clang_command.c_str(), result);
                    if (result.Succeeded()) {
                        std::cout << "[DEBUG] Successfully set Swift extra clang flags" << std::endl;
                    } else {
                        std::cout << "[DEBUG] Failed to set Swift extra clang flags: " << result.GetError() << std::endl;
                    }
                    
                } else {
                    std::cout << "[DEBUG] Command interpreter is not valid" << std::endl;
                }
            } catch (const std::exception& e) {
                std::cout << "[DEBUG] Exception during setting Swift module search paths: " << e.what() << std::endl;
            } catch (...) {
                std::cout << "[DEBUG] Unknown exception during setting Swift module search paths" << std::endl;
            }
                
            } catch (const std::exception& e) {
                std::cout << "[DEBUG] Exception during module loading: " << e.what() << std::endl;
            } catch (...) {
                std::cout << "[DEBUG] Unknown exception during module loading" << std::endl;
            }
            
            // Configure expression options for Swift
            options.SetLanguage(lldb::eLanguageTypeSwift);
            options.SetFetchDynamicValue(config.fetch_dynamic_values ? 
                                       lldb::eDynamicCanRunTarget : lldb::eDynamicDontRunTarget);
            options.SetTryAllThreads(config.try_all_threads);
            options.SetUnwindOnError(config.unwind_on_error);
            options.SetIgnoreBreakpoints(config.ignore_breakpoints);
            options.SetGenerateDebugInfo(config.generate_debug_info);
            
            // Try to set Swift-specific options that might help with module resolution
            std::cout << "[DEBUG] Configuring Swift expression options..." << std::endl;
            
            // Set additional Swift-specific options if available
            try {
                // Try to set Swift module search paths
                // Note: These options might not be available in all LLDB versions
                if (options.GetSuppressPersistentResult()) {
                    std::cout << "[DEBUG] Swift options support detected" << std::endl;
                }
                
                // Try to set Swift framework search paths
                std::string swift_framework_path = swift_root + "/lib/swift/linux";
                std::cout << "[DEBUG] Swift framework path: " << swift_framework_path << std::endl;
                
            } catch (...) {
                std::cout << "[DEBUG] Advanced Swift options not available" << std::endl;
            }
            
            // Try to force the Swift standard library to be imported by evaluating a simple import
            std::cout << "[DEBUG] Attempting to force Swift standard library import..." << std::endl;
            try {
                // Try to evaluate a simple import statement to force the Swift standard library to be loaded
                lldb::SBValue import_result = target.EvaluateExpression("import Swift", options);
                if (import_result.IsValid() && !import_result.GetError().Fail()) {
                    std::cout << "[DEBUG] Successfully imported Swift standard library" << std::endl;
                } else {
                    std::cout << "[DEBUG] Failed to import Swift standard library: " 
                              << (import_result.GetError().GetCString() ? import_result.GetError().GetCString() : "Unknown error") << std::endl;
                }
            } catch (const std::exception& e) {
                std::cout << "[DEBUG] Exception during Swift import: " << e.what() << std::endl;
            } catch (...) {
                std::cout << "[DEBUG] Unknown exception during Swift import" << std::endl;
            }
            
            // Try to override the Swift resource directory by setting environment variables
            // that the Swift compiler might recognize
            std::cout << "[DEBUG] Setting Swift compiler environment variables..." << std::endl;
            
            // Set environment variables that might be recognized by the Swift compiler
            setenv("SWIFT_RESOURCE_DIR", swift_root.c_str(), 1);
            setenv("SWIFT_LIBRARY_PATH", swift_lib_path.c_str(), 1);
            setenv("SWIFT_MODULE_PATH", (swift_lib_path + "/Swift.swiftmodule").c_str(), 1);
            
            // Try to set additional Swift compiler paths
            std::string swift_include_path = swift_root + "/include/swift";
            std::string swift_lib_path_full = swift_root + "/lib";
            
            setenv("SWIFT_INCLUDE_PATH", swift_include_path.c_str(), 1);
            setenv("SWIFT_LIB_PATH", swift_lib_path_full.c_str(), 1);
            
            std::cout << "[DEBUG] Set Swift compiler environment variables:" << std::endl;
            std::cout << "[DEBUG]   SWIFT_RESOURCE_DIR=" << getenv("SWIFT_RESOURCE_DIR") << std::endl;
            std::cout << "[DEBUG]   SWIFT_LIBRARY_PATH=" << getenv("SWIFT_LIBRARY_PATH") << std::endl;
            std::cout << "[DEBUG]   SWIFT_MODULE_PATH=" << getenv("SWIFT_MODULE_PATH") << std::endl;
            std::cout << "[DEBUG]   SWIFT_INCLUDE_PATH=" << getenv("SWIFT_INCLUDE_PATH") << std::endl;
            std::cout << "[DEBUG]   SWIFT_LIB_PATH=" << getenv("SWIFT_LIB_PATH") << std::endl;
            
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
