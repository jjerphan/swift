# Swift Minimal REPL

A minimal C++ library for evaluating Swift expressions programmatically without requiring stdin/stdout interaction. Perfect for servers, applications, and services that need to evaluate Swift code on demand.

## Features

- **Programmatic API**: Evaluate Swift expressions through a clean C++ API
- **No stdin/stdout**: Designed for server applications and background services
- **Thread-safe**: Multiple REPL instances can be used concurrently
- **Error handling**: Comprehensive error reporting and handling
- **Batch evaluation**: Evaluate multiple expressions in sequence
- **Context management**: Reset REPL context to clear variables and state
- **Configurable**: Customizable timeout, debug settings, and evaluation options
- **Modern C++**: Uses C++17 features, RAII, and move semantics

## Requirements

- LLDB with Swift support
- CMake 3.16 or later
- C++17 compatible compiler
- Swift toolchain (for Swift language support in LLDB)

## Building

```bash
cd swift_minimal_repl
mkdir build
cd build
cmake ..
make
```

You may need to adjust the LLDB paths in `CMakeLists.txt` based on your LLDB installation.

## Quick Start

### Basic Usage

```cpp
#include "MinimalSwiftREPL.h"
#include <iostream>

int main() {
    // Check if Swift REPL is available
    if (!SwiftMinimalREPL::isSwiftREPLAvailable()) {
        std::cerr << "Swift REPL not available" << std::endl;
        return 1;
    }
    
    // Create and initialize REPL
    SwiftMinimalREPL::MinimalSwiftREPL repl;
    if (!repl.initialize()) {
        std::cerr << "Failed to initialize REPL" << std::endl;
        return 1;
    }
    
    // Evaluate Swift expressions
    auto result = repl.evaluate("let x = 42; x * 2");
    if (result.success) {
        std::cout << "Result: " << result.value << " (type: " << result.type << ")" << std::endl;
    } else {
        std::cout << "Error: " << result.error_message << std::endl;
    }
    
    return 0;
}
```

### Convenience Function

For one-off evaluations:

```cpp
auto result = SwiftMinimalREPL::evaluateSwiftExpression("\"Hello, Swift!\".count");
if (result.success) {
    std::cout << "Length: " << result.value << std::endl;
}
```

### Server Usage Example

```cpp
class SwiftEvaluationServer {
    SwiftMinimalREPL::MinimalSwiftREPL repl;
    
public:
    bool initialize() {
        return repl.initialize();
    }
    
    std::string handleRequest(const std::string& expression) {
        auto result = repl.evaluate(expression);
        if (result.success) {
            return "Result: " + result.value + " (type: " + result.type + ")";
        } else {
            return "Error: " + result.error_message;
        }
    }
};
```

## API Reference

### MinimalSwiftREPL Class

#### Constructor
```cpp
MinimalSwiftREPL(const REPLConfig& config = REPLConfig{});
```

#### Methods
- `bool initialize()` - Initialize the REPL instance
- `bool isInitialized() const` - Check if REPL is initialized
- `EvaluationResult evaluate(const std::string& expression)` - Evaluate a single expression
- `std::vector<EvaluationResult> evaluateMultiple(const std::vector<std::string>& expressions)` - Evaluate multiple expressions
- `bool reset()` - Reset REPL context (clears all variables)
- `std::string getLastError() const` - Get last error message
- `static bool isSwiftSupportAvailable()` - Check if Swift support is available

### EvaluationResult Structure

```cpp
struct EvaluationResult {
    bool success;           // Whether evaluation succeeded
    std::string value;      // String representation of result value
    std::string type;       // Type name of the result
    std::string error_message; // Error message if evaluation failed
};
```

### REPLConfig Structure

```cpp
struct REPLConfig {
    bool fetch_dynamic_values = true;
    bool allow_jit = true;
    bool ignore_breakpoints = true;
    bool unwind_on_error = true;
    bool generate_debug_info = false;
    bool try_all_threads = false;
    int timeout_usec = 500000; // 0.5 seconds default timeout
};
```

### Convenience Functions

```cpp
// One-shot evaluation
EvaluationResult evaluateSwiftExpression(const std::string& expression, 
                                       const REPLConfig& config = REPLConfig{});

// Check availability
bool isSwiftREPLAvailable();
```

## Examples

The project includes several example programs:

1. **`example.cpp`** - Basic usage examples demonstrating all major features
2. **`server_example.cpp`** - Server-like usage showing how to handle client requests
3. **`test.cpp`** - Comprehensive test suite

Build and run them:

```bash
./swift_repl_example    # Basic examples
./swift_repl_server     # Server simulation
./swift_repl_test       # Test suite
```

## Use Cases

### Server Applications
Perfect for web servers that need to evaluate Swift expressions from client requests:

```cpp
// HTTP endpoint handler
std::string handleSwiftEval(const std::string& code) {
    auto result = repl.evaluate(code);
    return result.success ? result.value : result.error_message;
}
```

### Data Processing
Evaluate Swift expressions for data transformation:

```cpp
// Process data with Swift expressions
auto result = repl.evaluate("let data = [1,2,3,4,5]; data.map { $0 * 2 }.reduce(0, +)");
```

### Configuration and Scripting
Use Swift expressions for dynamic configuration:

```cpp
// Dynamic configuration
auto result = repl.evaluate("let config = [\"timeout\": 30, \"retries\": 3]; config[\"timeout\"]");
```

## Thread Safety

- Multiple `MinimalSwiftREPL` instances can be used concurrently from different threads
- Each instance maintains its own evaluation context
- LLDB initialization is handled thread-safely
- Individual instances are not thread-safe - don't share an instance between threads without synchronization

## Error Handling

The library provides comprehensive error handling:

- **Initialization errors**: Failed to create LLDB debugger or target
- **Evaluation errors**: Swift compilation or runtime errors
- **Timeout errors**: Expression evaluation exceeds configured timeout
- **System errors**: LLDB or system-level failures

Always check the `success` field of `EvaluationResult` and handle errors appropriately.

## Limitations

- Requires LLDB with Swift support to be installed
- Swift expressions are evaluated in a sandboxed environment
- No persistent state between REPL instances (unless explicitly managed)
- Performance overhead of LLDB initialization and Swift compilation
- Limited to Swift expressions that don't require external dependencies

## Performance Considerations

- REPL initialization has overhead - reuse instances when possible
- Complex Swift expressions may take time to compile and execute
- Use timeouts to prevent hanging on problematic expressions
- Consider connection pooling for server applications

## Troubleshooting

### "Swift REPL not available"
- Ensure LLDB is built with Swift support
- Check that Swift toolchain is properly installed
- Verify LLDB can find Swift runtime libraries

### Compilation errors
- Check that LLDB include paths are correct in CMakeLists.txt
- Ensure LLDB library paths are accessible
- Verify C++17 compiler support

### Runtime errors
- Check LLDB library compatibility
- Ensure proper linking with all required libraries
- Verify Swift runtime is available at runtime

## Contributing

Contributions are welcome! Please ensure:

1. Code follows the existing style
2. New features include tests
3. Documentation is updated
4. Examples demonstrate new functionality

## License

This project follows the same license as the Swift project.
