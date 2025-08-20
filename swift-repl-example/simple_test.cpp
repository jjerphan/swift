#include <iostream>
#include <string>

int main() {
    // This is where we want to stop and evaluate expressions
    int x = 42;
    std::string message = "Hello from LLDB!";
    double pi = 3.14159;
    
    std::cout << "Variables initialized:" << std::endl;
    std::cout << "x = " << x << std::endl;
    std::cout << "message = " << message << std::endl;
    std::cout << "pi = " << pi << std::endl;
    
    // Simple program that just prints and exits
    std::cout << "Program completed successfully!" << std::endl;
    return 0;
}
