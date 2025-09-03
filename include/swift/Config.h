// Minimal Config.h for Swift JIT REPL
// This file provides the necessary configuration defines

#ifndef SWIFT_CONFIG_H
#define SWIFT_CONFIG_H

// Basic platform features
#define HAVE_WAIT4 1
#define HAVE_PROC_PID_RUSAGE 1

// Swift features
#define SWIFT_IMPLICIT_CONCURRENCY_IMPORT 1
#define SWIFT_ENABLE_EXPERIMENTAL_DISTRIBUTED 0
#define SWIFT_ENABLE_GLOBAL_ISEL_ARM64 0
#define SWIFT_ENABLE_EXPERIMENTAL_PARSER_VALIDATION 0

#endif // SWIFT_CONFIG_H
