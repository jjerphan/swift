//===--- swift-repl.cpp - swift-repl - the Swift REPL --------------------===//
//
// Part of the Swift Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://swift.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements a REPL tool on top of Swift JIT.
//
//===----------------------------------------------------------------------===//

#include "SwiftJITREPL.h"
#include "llvm/Support/ManagedStatic.h" // llvm_shutdown
#include "llvm/Support/Signals.h"
#include "llvm/Support/TargetSelect.h"
#include <iostream>
#include <string>

static void LLVMErrorHandler(void *UserData, const char *Message,
                             bool GenCrashDiag) {
  std::cerr << "Swift REPL Error: " << Message << "\n";
  llvm::sys::RunInterruptHandlers();
  exit(1);
}

std::string readLine(const std::string& prompt) {
  std::cout << prompt;
  std::cout.flush();
  std::string line;
  std::getline(std::cin, line);
  return line;
}

int main(int argc, const char **argv) {
  llvm::llvm_shutdown_obj Y; // Call llvm_shutdown() on exit.

  // Create Swift JIT REPL instance
  SwiftJITREPL::SwiftJITREPL repl;

  bool HasError = false;

  // Execute any command-line inputs
  for (int i = 1; i < argc; i++) {
    auto result = repl.evaluate(argv[i]);
    if (!result.success) {
      std::cerr << "Error: " << result.error_message << "\n";
      HasError = true;
    }
  }

  // Interactive REPL mode
  if (argc == 1) {
    std::string Input;
    
    std::cout << "Welcome to Swift REPL!\n";
    std::cout << "Type 'quit' or 'exit' to exit, 'help' for help.\n\n";
    
    while (true) {
      std::string Line = readLine("swift-repl> ");
      
      // Handle multi-line input with backslash continuation
      if (!Line.empty() && Line.back() == '\\') {
        Input += Line.substr(0, Line.length() - 1);
        std::cout << "swift-repl...   ";
        continue;
      }

      Input += Line;
      
      // Handle special commands
      if (Input == "quit" || Input == "exit") {
        break;
      }
      if (Input == "help") {
        std::cout << "Swift REPL Commands:\n";
        std::cout << "  quit, exit    - Exit the REPL\n";
        std::cout << "  help          - Show this help message\n";
        std::cout << "  \\             - Continue input on next line\n";
        std::cout << "\n";
        std::cout << "Examples:\n";
        std::cout << "  let x = 42\n";
        std::cout << "  x + 1\n";
        std::cout << "  print(\"Hello, Swift!\")\n";
        std::cout << "\n";
      } else if (!Input.empty()) {
        // Evaluate Swift code
        auto result = repl.evaluate(Input);
        if (!result.success) {
          std::cerr << "Error: " << result.error_message << "\n";
          HasError = true;
        }
      }

      Input = "";
    }
  }

  return HasError ? 1 : 0;
}
