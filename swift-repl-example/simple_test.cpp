#include <iostream>
#include <thread>
#include <chrono>

int main() {
    int x = 42;
    std::string message = "Hello from LLDB!";
    double pi = 3.14159;
    bool running = true;
    
    std::cout << "Starting program with infinite loop..." << std::endl;
    std::cout << "Variables initialized:" << std::endl;
    std::cout << "  x = " << x << std::endl;
    std::cout << "  message = " << message << std::endl;
    std::cout << "  pi = " << pi << std::endl;
    std::cout << "  running = " << (running ? "true" : "false") << std::endl;
    
    // This is where we'll set a breakpoint
    std::cout << "Entering infinite loop - set breakpoint here!" << std::endl;
    
    while (running) {
        // This loop will run forever, providing execution context
        x = x + 1;
        
        // Add some delay to make it easier to attach debugger
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        
        // Print current values every few iterations
        if (x % 10 == 0) {
            std::cout << "Loop iteration: x = " << x << std::endl;
        }
        
        // This will never be true, keeping the loop infinite
        if (x > 1000000) {
            running = false;  // This will never happen
        }
    }
    
    std::cout << "This will never be reached" << std::endl;
    return 0;
}
