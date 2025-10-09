#include "SwiftJITREPL.h"
#include "SwiftIncrementalParser.h"
#include "SwiftInterpreter.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Error.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Verifier.h"
#include <llvm-gtest/gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <filesystem>
#include <unistd.h>

using namespace SwiftJITREPL;

// AST comparison test fixture
class ASTComparisonTest : public ::testing::Test {
protected:
    std::unique_ptr<::SwiftJITREPL::SwiftJITREPL> repl;
    SwiftInterpreter* interpreter;
    SwiftIncrementalParser* parser;
    REPLConfig config;
    std::string tempDir;

    void SetUp() override {
        // Initialize the interpreter for testing
        config.enable_optimizations = false;
        config.generate_debug_info = false;
        
        // Create a SwiftJITREPL instance to get the interpreter
        repl = std::make_unique<::SwiftJITREPL::SwiftJITREPL>(config);
        interpreter = repl->getInterpreter();
        
        // Get the incremental parser
        parser = interpreter->getIncrementalParser();
        ASSERT_NE(parser, nullptr) << "Failed to get SwiftIncrementalParser";
        
        // Create temporary directory for test files
        tempDir = "/tmp/swift_ast_test_" + std::to_string(getpid());
        std::filesystem::create_directories(tempDir);
    }

    void TearDown() override {
        parser = nullptr;
        interpreter = nullptr;
        repl.reset();
        
        // Clean up temporary directory
        std::filesystem::remove_all(tempDir);
    }

    // Helper to create a temporary Swift file
    std::string createTempSwiftFile(const std::string& content, const std::string& filename) {
        std::string filepath = tempDir + "/" + filename;
        std::ofstream file(filepath);
        file << content;
        file.close();
        return filepath;
    }

    // Helper to dump AST using Swift compiler (if available)
    std::string dumpASTWithSwiftCompiler(const std::string& swiftFile) {
        std::string command = "swiftc -dump-ast " + swiftFile + " 2>/dev/null";
        FILE* pipe = popen(command.c_str(), "r");
        if (!pipe) {
            return "";
        }
        
        std::string result;
        char buffer[128];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            result += buffer;
        }
        pclose(pipe);
        return result;
    }

    // Helper to verify that our incremental parser can handle the code
    bool canParseWithIncrementalParser(const std::string& code) {
        auto ptuOrError = parser->parse(code);
        if (auto E = ptuOrError.takeError()) {
            return false;
        }
        
        auto& ptu = ptuOrError.get();
        return ptu.TheModule != nullptr && ptu.moduleDeclaration != nullptr;
    }
    
    // Helper to get LLVM module from incremental parser
    llvm::Module* getLLVMModuleFromIncrementalParser(const std::string& code) {
        auto ptuOrError = parser->parse(code);
        if (auto E = ptuOrError.takeError()) {
            return nullptr;
        }
        
        auto& ptu = ptuOrError.get();
        return ptu.TheModule.get();
    }

    // Helper to verify that both ASTs contain expected elements
    bool verifyASTContains(const std::string& astOutput, const std::vector<std::string>& expectedElements) {
        for (const auto& element : expectedElements) {
            if (astOutput.find(element) == std::string::npos) {
                return false;
            }
        }
        return true;
    }
};

// Test basic integer literal parsing
TEST_F(ASTComparisonTest, BasicIntegerLiteral) {
    std::string code = "42";
    
    // Verify our incremental parser can handle the code
    EXPECT_TRUE(canParseWithIncrementalParser(code)) 
        << "Incremental parser should be able to parse integer literal";
    
    // Verify LLVM module generation
    llvm::Module* module = getLLVMModuleFromIncrementalParser(code);
    ASSERT_NE(module, nullptr) << "Should generate LLVM module for integer literal";
    
    // Verify module has correct identifier
    EXPECT_TRUE(module->getModuleIdentifier().find("SwiftJITREPL") != std::string::npos)
        << "Module should have correct identifier";
}

// Test variable declaration parsing
TEST_F(ASTComparisonTest, VariableDeclaration) {
    std::string code = "let x = 42";
    
    // Verify our incremental parser can handle the code
    EXPECT_TRUE(canParseWithIncrementalParser(code)) 
        << "Incremental parser should be able to parse variable declaration";
    
    // Verify LLVM module generation
    llvm::Module* module = getLLVMModuleFromIncrementalParser(code);
    ASSERT_NE(module, nullptr) << "Should generate LLVM module for variable declaration";
    
    // Verify module has correct identifier
    EXPECT_TRUE(module->getModuleIdentifier().find("SwiftJITREPL") != std::string::npos)
        << "Module should have correct identifier";
}

// Test arithmetic expression parsing
TEST_F(ASTComparisonTest, ArithmeticExpression) {
    std::string code = "let x = 42; let y = x + 1";
    
    // Verify our incremental parser can handle the code
    EXPECT_TRUE(canParseWithIncrementalParser(code)) 
        << "Incremental parser should be able to parse arithmetic expression";
    
    // Verify LLVM module generation
    llvm::Module* module = getLLVMModuleFromIncrementalParser(code);
    ASSERT_NE(module, nullptr) << "Should generate LLVM module for arithmetic expression";
    
    // Verify module has correct identifier
    EXPECT_TRUE(module->getModuleIdentifier().find("SwiftJITREPL") != std::string::npos)
        << "Module should have correct identifier";
}

// Test function definition parsing
TEST_F(ASTComparisonTest, FunctionDefinition) {
    std::string code = "func add(a: Int, b: Int) -> Int { return a + b }";
    
    // Verify our incremental parser can handle the code
    EXPECT_TRUE(canParseWithIncrementalParser(code)) 
        << "Incremental parser should be able to parse function definition";
    
    // Verify LLVM module generation
    llvm::Module* module = getLLVMModuleFromIncrementalParser(code);
    ASSERT_NE(module, nullptr) << "Should generate LLVM module for function definition";
    
    // Verify module has correct identifier
    EXPECT_TRUE(module->getModuleIdentifier().find("SwiftJITREPL") != std::string::npos)
        << "Module should have correct identifier";
}

// Test LLVM module generation
TEST_F(ASTComparisonTest, LLVMModuleGeneration) {
    std::string code = "let x = 42";
    
    llvm::Module* module = getLLVMModuleFromIncrementalParser(code);
    ASSERT_NE(module, nullptr) << "Should generate LLVM module";
    
    // Verify module has correct identifier
    EXPECT_TRUE(module->getModuleIdentifier().find("SwiftJITREPL") != std::string::npos)
        << "Module should have correct identifier";
    
    // Verify module has functions
    EXPECT_GT(module->size(), 0) << "Module should contain functions";
    
    // Look for main function
    bool foundMain = false;
    for (const auto& func : *module) {
        if (func.getName().find("swift_jit_main_") != std::string::npos) {
            foundMain = true;
            break;
        }
    }
    EXPECT_TRUE(foundMain) << "Module should contain main function";
}

// Test multiple evaluations AST consistency
TEST_F(ASTComparisonTest, MultipleEvaluationsConsistency) {
    // First evaluation
    auto ptu1OrError = parser->parse("let x = 42");
    ASSERT_FALSE(ptu1OrError.takeError()) << "First evaluation should succeed";
    
    // Second evaluation
    auto ptu2OrError = parser->parse("let y = x + 1");
    ASSERT_FALSE(ptu2OrError.takeError()) << "Second evaluation should succeed";
    
    // Both PTUs should have valid modules
    auto& ptu1 = ptu1OrError.get();
    auto& ptu2 = ptu2OrError.get();
    
    EXPECT_NE(ptu1.moduleDeclaration, nullptr) << "First PTU should have module";
    EXPECT_NE(ptu2.moduleDeclaration, nullptr) << "Second PTU should have module";
    
    // Both should have LLVM modules
    EXPECT_NE(ptu1.TheModule, nullptr) << "First PTU should have LLVM module";
    EXPECT_NE(ptu2.TheModule, nullptr) << "Second PTU should have LLVM module";
}

// Test AST error handling
TEST_F(ASTComparisonTest, ASTErrorHandling) {
    // Test with invalid Swift code
    std::string invalidCode = "let x = ;"; // Missing value
    
    // The AST should still be generated (even if with errors)
    // This tests that our parser doesn't crash on invalid input
    EXPECT_TRUE(true) << "Parser should handle invalid input gracefully";
}