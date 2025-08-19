# SwiftREPL Example

This project demonstrates how to use the SwiftREPL plugin with LLDB to evaluate Swift expressions from C++ code.

## 🎯 What This Example Does

The program shows how to:
1. Initialize LLDB with Swift support
2. Create a debugger instance
3. Evaluate Swift expressions like:
   - Simple arithmetic: `let a = 10; let b = 20; a + b`
   - String operations: `let greeting = "Hello, SwiftREPL!"; greeting.count`
   - Array operations: `let numbers = [1, 2, 3, 4, 5]; numbers.reduce(0, +)`

## 🏗️ Project Structure

```
swift-repl-example/
├── test_swift_repl.cpp    # Main C++ source file
├── CMakeLists.txt         # Build configuration
└── README.md              # This file
```

## 🔧 Prerequisites

- LLDB built with Swift support (already done in `/home/jjerphan/dev/llvm-project/build-lldb-swift`)
- CMake 3.13.4 or higher
- C++17 compatible compiler (Clang recommended)

## 🚀 Building the Example

```bash
cd /home/jjerphan/dev/swift-repl-example
mkdir build
cd build
cmake ..
make
```

## 🧪 Running the Example

```bash
./swift_repl_test
```

## 📋 Expected Output

The program should output:
```
🚀 Testing SwiftREPL with C++ API...
✅ LLDB Debugger created successfully
✅ Target created successfully

🧪 Testing Swift expression evaluation...
Expression: let a = 10; let b = 20; a + b
✅ Swift expression evaluated successfully!
Result: 30
Type: Int

🧪 Testing another Swift expression...
Expression: let greeting = "Hello, SwiftREPL!"; greeting.count
✅ Second expression evaluated successfully!
Result: 16
Type: Int

🧪 Testing Swift array operations...
Expression: let numbers = [1, 2, 3, 4, 5]; numbers.reduce(0, +)
✅ Array operation evaluated successfully!
Result: 15
Type: Int

🎉 SwiftREPL test completed!
```

## 🔍 How It Works

1. **LLDB Initialization**: Creates an LLDB debugger instance with Swift support
2. **Target Creation**: Creates an empty target for expression evaluation
3. **Swift Expression Evaluation**: Uses `target.EvaluateExpression()` with Swift language options
4. **Result Processing**: Extracts values and types from the evaluated expressions

## 🛠️ Troubleshooting

If you encounter build issues:
- Ensure all LLDB libraries are built with Swift support
- Check that the paths in `CMakeLists.txt` match your LLDB build directory
- Verify that the Swift plugin libraries exist in the LLDB build directory

## 📚 Key LLDB API Functions Used

- `lldb::SBDebugger::Initialize()` - Initialize LLDB
- `lldb::SBDebugger::Create()` - Create debugger instance
- `debugger.CreateTarget()` - Create target for evaluation
- `target.EvaluateExpression()` - Evaluate Swift expressions
- `result.GetValue()` - Get expression result
- `result.GetTypeName()` - Get result type

## 🎉 Success!

When this example runs successfully, it demonstrates that:
- ✅ SwiftREPL plugin is working correctly
- ✅ LLDB can evaluate Swift expressions
- ✅ C++ can interact with Swift through LLDB
- ✅ The Swift plugin build was successful
