Swift JIT REPL
===============

A minimal Swift JIT-based REPL built directly on Swift compiler components and LLVM ORC JIT. It parses small Swift inputs incrementally, lowers them to SIL, generates LLVM IR, and executes via a lightweight JIT. No debugger integration is involved.

Overview
--------
- Uses `swift::CompilerInvocation` to configure immediate/JIT mode
- Builds per-input `swift::ModuleDecl` instances for isolation
- Lowers to SIL and then to LLVM IR
- Loads IR into an `llvm::orc::LLJIT` instance and executes module initializers

Quick Start
-----------
1) Build prerequisites
- Clang/LLVM toolchain (use clang as the C++ compiler)
- A Swift toolchain installed with runtime and SDK paths available

2) Environment
- Ensure the following directories exist on your system and point to your Swift toolchain:
  - `SWIFT_RUNTIME_LIBRARY_PATHS`
  - `SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_1`
  - `SWIFT_RUNTIME_LIBRARY_IMPORT_PATHS_2`
  - `SWIFT_RUNTIME_RESOURCE_PATH`
  - `SWIFT_SDK_PATH`
- These are compiled in with defaults in `SwiftJITREPL.cpp`. Adjust your build to override if needed.

3) Build (example)
- Integrate `swift_jit_repl` into your build system and compile with clang.

4) Run the example
- Binary prints availability checks, evaluates a few expressions, and prints simple stats.

Current Capabilities
--------------------
- Creates a fresh `ModuleDecl` per input in a shared `ASTContext`
- Type-checks each input and generates SIL/LLVM IR
- JITs IR into the ORC main dylib and runs global constructors
- Basic evaluation loop with simple success/error tracking

What’s Left To Implement (not exhaustive)
------------------------
The following items are planned or partially scaffolded but not complete:
 - Understand the double `free` which is observed and see whether this is
 due to the comment of the call to `verify` we have done in the past.
 - Resolve the circular import resolution when this call to `verify` is present.

Repository Layout (subset)
--------------------------
- `swift_jit_repl/SwiftJITREPL.h/.cpp`: Public API and implementation
- `swift_jit_repl/example.cpp`: Minimal example of usage

Notes
-----
- The implementation avoids debugger-specific integration and focuses on direct JIT execution.
- Paths to the Swift runtime, resources, and SDK must be correct for your toolchain.
- Undo/reset semantics will evolve alongside cross-input state reuse.


