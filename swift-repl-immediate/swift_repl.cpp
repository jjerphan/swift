//===--- swift-repl.cpp - swift-repl - the Swift REPL --------------------===//
//
// Part of the Swift Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://swift.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements a REPL tool on top of Swift JIT (immediate style).
//
//===----------------------------------------------------------------------===//

#include "SwiftJITREPL.h"
#include "llvm/Support/ManagedStatic.h" // llvm_shutdown
#include "llvm/Support/Signals.h"
#include "llvm/Support/TargetSelect.h"
#include <iostream>
#include <string>
#include <termios.h>
#include <unistd.h>

static void LLVMErrorHandler(void *UserData, const char *Message,
                             bool GenCrashDiag) {
  std::cerr << "Swift REPL Error: " << Message << "\n";
  llvm::sys::RunInterruptHandlers();
  exit(1);
}

namespace {
struct RawModeGuard {
  bool enabled{false};
  termios orig{};
  RawModeGuard() {
    if (!isatty(STDIN_FILENO)) return;
    if (tcgetattr(STDIN_FILENO, &orig) == -1) return;
    termios raw = orig;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_oflag &= ~(OPOST);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) return;
    enabled = true;
  }
  ~RawModeGuard() {
    if (enabled) tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);
  }
};

// Reads multi-line input where:
// - Enter inserts a newline
// - Ctrl+E submits (chosen due to poor terminal support for Ctrl+Enter)
// - Ctrl+C clears current buffer
// - Ctrl+D on empty buffer exits (signals quit)
// Returns false if the caller should quit.
bool readMultiline(std::string &outBuffer) {
  RawModeGuard guard;
  outBuffer.clear();
  const std::string prompt = "swift-repl  > ";
  const std::string cont   = "swift-repl... ";
  std::cout << prompt << std::flush;
  bool firstLine = true;
  while (true) {
    unsigned char ch = 0;
    ssize_t n = ::read(STDIN_FILENO, &ch, 1);
    if (n <= 0) return false; // treat as quit
    if (ch == '\n' || ch == '\r') { // Enter inserts newline
      outBuffer.push_back('\n');
      // Always start a new continuation prompt at column 0
      std::cout << "\n\r" << cont << std::flush;
      firstLine = false;
      continue;
    }
    if (ch == 0x05) { // Ctrl+E -> submit
      std::cout << "\n" << std::flush;
      return true;
    }
    if (ch == 0x03) { // Ctrl+C -> clear current buffer
      outBuffer.clear();
      std::cout << "^C\n" << prompt << std::flush;
      firstLine = true;
      continue;
    }
    if (ch == 0x04) { // Ctrl+D -> quit if buffer empty, else ignore
      if (outBuffer.empty()) return false;
      continue;
    }
    // Basic backspace handling
    if (ch == 0x7F || ch == 0x08) { // DEL or BS
      if (!outBuffer.empty()) {
        if (outBuffer.back() == '\n') {
          // naive: just erase without moving cursor across lines
          // user can use Ctrl+C to clear if needed
          outBuffer.pop_back();
        } else {
          outBuffer.pop_back();
          std::cout << "\b \b" << std::flush;
        }
      }
      continue;
    }
    // Echo printable ASCII
    if (ch >= 0x20 && ch <= 0x7E) {
      outBuffer.push_back(static_cast<char>(ch));
      std::cout << static_cast<char>(ch) << std::flush;
      continue;
    }
    // Ignore other control bytes
  }
}
} // namespace

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
    std::cout << "Welcome to Swift REPL (immediate)!\n";
    std::cout << "Controls:\n";
    std::cout << "  Enter     -> insert newline\n";
    std::cout << "  Ctrl+E    -> submit evaluation (Ctrl+Enter is not reliably detectable in TTY)\n";
    std::cout << "  Ctrl+C    -> clear current input\n";
    std::cout << "  Ctrl+D    -> quit (when input is empty)\n\n";

    while (true) {
      std::string buffer;
      if (!readMultiline(buffer)) break; // quit requested

      // Trim trailing newlines to avoid accidental blank lines at end
      while (!buffer.empty() && buffer.back() == '\n') buffer.pop_back();

      if (buffer == "quit" || buffer == "exit")
        break;
      if (buffer == "help") {
        std::cout << "Swift REPL Commands:\n";
        std::cout << "  quit, exit    - Exit the REPL\n";
        std::cout << "  help          - Show this help message\n";
        std::cout << "Controls:\n";
        std::cout << "  Enter     -> insert newline\n";
        std::cout << "  Ctrl+E    -> submit evaluation\n";
        std::cout << "  Ctrl+C    -> clear current input\n";
        std::cout << "  Ctrl+D    -> quit (when input is empty)\n";
        std::cout << "\n";
        continue;
      }
      if (buffer.empty())
        continue;

      auto result = repl.evaluate(buffer);
      if (!result.success) {
        std::cerr << "Error: " << result.error_message << "\n";
        HasError = true;
      }
    }
  }

  return HasError ? 1 : 0;
}


