#include <iostream>

int main() {
    int x = 42;
    std::string message = "Hello from LLDB!";
    double pi = 3.14159;
    
    std::cout << "Starting program..." << std::endl;
    
    // This is where we'll set a breakpoint
    x = x + 1;  // Breakpoint here
    
    std::cout << "x = " << x << std::endl;
    std::cout << "message = " << message << std::endl;
    std::cout << "pi = " << pi << std::endl;
    
    return 0;
}
