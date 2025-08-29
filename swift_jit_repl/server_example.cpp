#include "SwiftJITREPL.h"
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <chrono>

// Simple request structure
struct EvaluationRequest {
    std::string id;
    std::string expression;
    std::chrono::steady_clock::time_point timestamp;
    
    EvaluationRequest(const std::string& req_id, const std::string& expr)
        : id(req_id), expression(expr), timestamp(std::chrono::steady_clock::now()) {}
};

// Simple response structure
struct EvaluationResponse {
    std::string id;
    bool success;
    std::string result;
    std::string type;
    std::string error_message;
    std::chrono::steady_clock::time_point timestamp;
    double compilation_time_ms;
    double execution_time_ms;
    
    EvaluationResponse(const std::string& req_id)
        : id(req_id), success(false), timestamp(std::chrono::steady_clock::now()),
          compilation_time_ms(0.0), execution_time_ms(0.0) {}
};

// Thread-safe request queue
class RequestQueue {
private:
    std::queue<EvaluationRequest> requests;
    mutable std::mutex mutex;
    std::condition_variable condition;
    
public:
    void push(const EvaluationRequest& request) {
        std::lock_guard<std::mutex> lock(mutex);
        requests.push(request);
        condition.notify_one();
    }
    
    bool pop(EvaluationRequest& request) {
        std::unique_lock<std::mutex> lock(mutex);
        if (requests.empty()) {
            return false;
        }
        request = requests.front();
        requests.pop();
        return true;
    }
    
    bool waitAndPop(EvaluationRequest& request, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex);
        if (condition.wait_for(lock, timeout, [this] { return !requests.empty(); })) {
            request = requests.front();
            requests.pop();
            return true;
        }
        return false;
    }
    
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex);
        return requests.size();
    }
    
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex);
        return requests.empty();
    }
};

// Swift JIT REPL Server
class SwiftJITServer {
private:
    SwiftJITREPL::SwiftJITREPL repl;
    RequestQueue requestQueue;
    std::vector<std::thread> workerThreads;
    std::atomic<bool> running{false};
    std::mutex statsMutex;
    
    // Server statistics
    struct ServerStats {
        size_t total_requests = 0;
        size_t successful_evaluations = 0;
        size_t failed_evaluations = 0;
        double total_response_time_ms = 0.0;
        double total_compilation_time_ms = 0.0;
        double total_execution_time_ms = 0.0;
        std::chrono::steady_clock::time_point start_time;
        
        ServerStats() : start_time(std::chrono::steady_clock::now()) {}
    } stats;
    
public:
    SwiftJITServer(const SwiftJITREPL::REPLConfig& config) : repl(config) {}
    
    bool initialize() {
        std::cout << "Initializing Swift JIT Server...\n";
        
        if (!repl.initialize()) {
            std::cerr << "Failed to initialize REPL: " << repl.getLastError() << "\n";
            return false;
        }
        
        std::cout << "Swift JIT Server initialized successfully!\n";
        return true;
    }
    
    void start(size_t num_workers = 4) {
        if (running.load()) {
            std::cout << "Server is already running\n";
            return;
        }
        
        running.store(true);
        std::cout << "Starting Swift JIT Server with " << num_workers << " worker threads...\n";
        
        // Start worker threads
        for (size_t i = 0; i < num_workers; ++i) {
            workerThreads.emplace_back(&SwiftJITServer::workerThread, this, i);
        }
        
        std::cout << "Server started successfully!\n";
    }
    
    void stop() {
        if (!running.load()) {
            return;
        }
        
        std::cout << "Stopping Swift JIT Server...\n";
        running.store(false);
        
        // Wait for all worker threads to finish
        for (auto& thread : workerThreads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        
        workerThreads.clear();
        std::cout << "Server stopped\n";
    }
    
    void submitRequest(const std::string& id, const std::string& expression) {
        EvaluationRequest request(id, expression);
        requestQueue.push(request);
        
        std::lock_guard<std::mutex> lock(statsMutex);
        stats.total_requests++;
    }
    
    void printStats() {
        std::lock_guard<std::mutex> lock(statsMutex);
        
        auto now = std::chrono::steady_clock::now();
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - stats.start_time).count();
        
        std::cout << "\n=== Server Statistics ===\n";
        std::cout << "Uptime: " << uptime << " seconds\n";
        std::cout << "Total requests: " << stats.total_requests << "\n";
        std::cout << "Successful evaluations: " << stats.successful_evaluations << "\n";
        std::cout << "Failed evaluations: " << stats.failed_evaluations << "\n";
        std::cout << "Success rate: " 
                  << (stats.total_requests > 0 ? (stats.successful_evaluations * 100.0 / stats.total_requests) : 0) 
                  << "%\n";
        std::cout << "Average response time: " 
                  << (stats.total_requests > 0 ? stats.total_response_time_ms / stats.total_requests : 0) 
                  << " ms\n";
        std::cout << "Average compilation time: " 
                  << (stats.total_requests > 0 ? stats.total_compilation_time_ms / stats.total_requests : 0) 
                  << " ms\n";
        std::cout << "Average execution time: " 
                  << (stats.total_requests > 0 ? stats.total_execution_time_ms / stats.total_requests : 0) 
                  << " ms\n";
        std::cout << "Current queue size: " << requestQueue.size() << "\n";
        
        // Print REPL statistics
        auto replStats = repl.getStats();
        std::cout << "\n=== REPL Statistics ===\n";
        std::cout << "Total expressions: " << replStats.total_expressions << "\n";
        std::cout << "Successful compilations: " << replStats.successful_compilations << "\n";
        std::cout << "Failed compilations: " << replStats.failed_compilations << "\n";
    }
    
    ~SwiftJITServer() {
        stop();
    }

private:
    void workerThread(size_t thread_id) {
        std::cout << "Worker thread " << thread_id << " started\n";
        
        while (running.load()) {
            EvaluationRequest request("", "");
            
            // Wait for requests with timeout
            if (requestQueue.waitAndPop(request, std::chrono::milliseconds(100))) {
                processRequest(request, thread_id);
            }
        }
        
        std::cout << "Worker thread " << thread_id << " stopped\n";
    }
    
    void processRequest(const EvaluationRequest& request, size_t thread_id) {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        std::cout << "Thread " << thread_id << " processing request " << request.id 
                  << ": " << request.expression << "\n";
        
        // Evaluate the expression
        auto result = repl.evaluate(request.expression);
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto response_time = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count() / 1000.0;
        
        // Create response
        EvaluationResponse response(request.id);
        response.success = result.success;
        response.result = result.value;
        response.type = result.type;
        response.error_message = result.error_message;
        response.compilation_time_ms = response_time; // Simplified for this example
        
        // Update statistics
        {
            std::lock_guard<std::mutex> lock(statsMutex);
            if (result.success) {
                stats.successful_evaluations++;
            } else {
                stats.failed_evaluations++;
            }
            stats.total_response_time_ms += response_time;
            stats.total_compilation_time_ms += response.compilation_time_ms;
            stats.total_execution_time_ms += response.execution_time_ms;
        }
        
        // Print result
        if (result.success) {
            std::cout << "Thread " << thread_id << " completed request " << request.id 
                      << " successfully: " << result.value << " (type: " << result.type << ")\n";
        } else {
            std::cout << "Thread " << thread_id << " failed request " << request.id 
                      << ": " << result.error_message << "\n";
        }
    }
};

// Simulate client requests
void simulateClientRequests(SwiftJITServer& server, int num_requests) {
    std::vector<std::string> sample_expressions = {
        "42",
        "3.14 * 2",
        "1 + 2 + 3 + 4 + 5",
        "let x = 10; x * x",
        "\"Hello, Swift!\".count",
        "Array(1...10).reduce(0, +)",
        "let numbers = [1, 2, 3, 4, 5]; numbers.map { $0 * 2 }.reduce(0, +)",
        "let factorial = { (n: Int) -> Int in n <= 1 ? 1 : n * factorial(n - 1) }; factorial(5)"
    };
    
    std::cout << "Simulating " << num_requests << " client requests...\n";
    
    for (int i = 0; i < num_requests; ++i) {
        std::string id = "req_" + std::to_string(i);
        std::string expression = sample_expressions[i % sample_expressions.size()];
        
        server.submitRequest(id, expression);
        
        // Small delay between requests
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "Finished submitting " << num_requests << " requests\n";
}

int main() {
    std::cout << "=== Swift JIT REPL Server Example ===\n\n";
    
    // Configure the server
    SwiftJITREPL::REPLConfig config;
    config.enable_optimizations = true;
    config.generate_debug_info = false;
    config.lazy_compilation = true;
    config.timeout_ms = 5000;
    
    // Create and initialize server
    SwiftJITServer server(config);
    
    if (!server.initialize()) {
        std::cerr << "Failed to initialize server\n";
        return 1;
    }
    
    // Start the server
    server.start(4); // 4 worker threads
    
    // Simulate client requests
    simulateClientRequests(server, 20);
    
    // Wait for all requests to be processed
    std::cout << "Waiting for all requests to be processed...\n";
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    // Print statistics
    server.printStats();
    
    // Stop the server
    server.stop();
    
    std::cout << "\n=== Server Example Completed ===\n";
    return 0;
}
