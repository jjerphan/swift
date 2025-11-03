#include "SwiftJITREPL.h"
#include <gtest/gtest.h>

namespace {

struct REPLFixture : public ::testing::Test {
    SwiftJITREPL::SwiftJITREPL *repl = nullptr;

    void SetUp() override {
        ASSERT_TRUE(SwiftJITREPL::SwiftJITREPL::isAvailable());
        SwiftJITREPL::REPLConfig config;
        config.enable_optimizations = false;
        config.generate_debug_info = false;
        repl = new SwiftJITREPL::SwiftJITREPL(config);
        ASSERT_NE(repl, nullptr);
    }

    void TearDown() override {
        delete repl;
        repl = nullptr;
    }
};

TEST_F(REPLFixture, TopLevelPrint) {
    auto result = repl->evaluate(
        "import Swift\n"
        "print(\"Hello from SIL JIT!\")\n"
    );
    ASSERT_TRUE(result.success) << result.error_message;
    EXPECT_EQ(repl->executeAll(), 0);
}

TEST_F(REPLFixture, StatePersistenceVariable) {
    auto r = repl->evaluate("import Swift\n");
    ASSERT_TRUE(r.success) << r.error_message;
    r = repl->evaluate("var a = 41\n");
    ASSERT_TRUE(r.success) << r.error_message;
    r = repl->evaluate("print(a + 1)\n");
    ASSERT_TRUE(r.success) << r.error_message;
    EXPECT_EQ(repl->executeAll(), 0);
}

TEST_F(REPLFixture, FunctionDefineAndInvoke) {
    auto r = repl->evaluate("import Swift\n");
    ASSERT_TRUE(r.success) << r.error_message;
    r = repl->evaluate("func square(_ x: Int) -> Int { x * x }\n");
    ASSERT_TRUE(r.success) << r.error_message;
    r = repl->evaluate("print(square(5))\n");
    ASSERT_TRUE(r.success) << r.error_message;
    EXPECT_EQ(repl->executeAll(), 0);
}

TEST_F(REPLFixture, ErrorThenResetRecovery) {
    auto r = repl->evaluate("let = broken\n");
    ASSERT_FALSE(r.success);
    ASSERT_TRUE(repl->reset());
    r = repl->evaluate("import Swift\nprint(\"still works\")\n");
    ASSERT_TRUE(r.success) << r.error_message;
    EXPECT_EQ(repl->executeAll(), 0);
}

} // namespace

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}