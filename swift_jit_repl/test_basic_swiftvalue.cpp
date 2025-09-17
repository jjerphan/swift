#include "SwiftJITREPL.h"
#include <iostream>
#include <cassert>

int main() {
    std::cout << "=== Testing Basic SwiftValue Functionality ===" << std::endl;
    
    // Test the SwiftValue class directly
    std::cout << "\n1. Testing SwiftValue class..." << std::endl;
    
    SwiftValue value1("42", "Int");
    assert(value1.isValid());
    assert(value1.getValue() == "42");
    assert(value1.getType() == "Int");
    
    std::cout << "✓ SwiftValue creation and access works" << std::endl;
    
    // Test SwiftValue with different types
    SwiftValue value2("Hello", "String");
    assert(value2.isValid());
    assert(value2.getValue() == "Hello");
    assert(value2.getType() == "String");
    
    std::cout << "✓ SwiftValue with different types works" << std::endl;
    
    // Test SwiftValue invalidation
    SwiftValue value3;
    assert(!value3.isValid());
    
    value3.setValue("3.14", "Double");
    assert(value3.isValid());
    assert(value3.getValue() == "3.14");
    assert(value3.getType() == "Double");
    
    std::cout << "✓ SwiftValue invalidation and re-setting works" << std::endl;
    
    // Test SwiftValue clearing
    value3.clear();
    assert(!value3.isValid());
    
    std::cout << "✓ SwiftValue clearing works" << std::endl;
    
    // Test copy constructor
    SwiftValue value4 = value1;
    assert(value4.isValid());
    assert(value4.getValue() == "42");
    assert(value4.getType() == "Int");
    
    std::cout << "✓ SwiftValue copy constructor works" << std::endl;
    
    // Test assignment operator
    SwiftValue value5;
    value5 = value2;
    assert(value5.isValid());
    assert(value5.getValue() == "Hello");
    assert(value5.getType() == "String");
    
    std::cout << "✓ SwiftValue assignment operator works" << std::endl;
    
    // Test move constructor
    SwiftValue value6 = std::move(value1);
    assert(value6.isValid());
    assert(value6.getValue() == "42");
    assert(value6.getType() == "Int");
    assert(!value1.isValid()); // Should be moved from
    
    std::cout << "✓ SwiftValue move constructor works" << std::endl;
    
    // Test move assignment
    SwiftValue value7;
    value7 = std::move(value2);
    assert(value7.isValid());
    assert(value7.getValue() == "Hello");
    assert(value7.getType() == "String");
    assert(!value2.isValid()); // Should be moved from
    
    std::cout << "✓ SwiftValue move assignment works" << std::endl;
    
    std::cout << "\n=== All Basic SwiftValue Tests Passed! ===" << std::endl;
    
    return 0;
}
