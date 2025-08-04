#include "process.h"
#include "utils.h"
#include "config.h"
#include <iostream>

bool isPowerOfTwo(int n) {
    return n > 0 && (n & (n - 1)) == 0;
}

bool isValidMemorySize(int size) {
    return size >= config.min_memory_size && size <= config.max_memory_size && isPowerOfTwo(size);
}

bool parseScreenCommandWithInstructions(const std::string& cmd, std::string& processName, 
                                       int& memorySize, std::string& instructions) {
    if (cmd.substr(0, 10) != "screen -c ") {
        return false;
    }
    
    std::string args = cmd.substr(10); 
    
    size_t quoteStart = args.find('"');
    if (quoteStart == std::string::npos) {
        std::cerr << "Error: Instructions must be enclosed in double quotes\n";
        return false;
    }
    
    size_t quoteEnd = args.find('"', quoteStart + 1);
    if (quoteEnd == std::string::npos) {
        std::cerr << "Error: Missing closing quote for instructions\n";
        return false;
    }
    
    instructions = args.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
    
    std::string beforeInstructions = trim(args.substr(0, quoteStart));
    std::vector<std::string> parts = split(beforeInstructions, ' ');
    
    if (parts.size() != 2) {
        std::cerr << "Error: Expected format: screen -c <process_name> <memory_size> \"<instructions>\"\n";
        return false;
    }
    
    processName = parts[0];
    
    if (processName.empty()) {
        std::cerr << "Error: Process name cannot be empty\n";
        return false;
    }
    
    try {
        memorySize = std::stoi(parts[1]);
    } catch (const std::exception&) {
        std::cerr << "Error: Invalid memory size format\n";
        return false;
    }
    
    return true;
}

bool parseScreenCommand(const std::string& cmd, std::string& processName, int& memorySize) {
    std::string args = cmd.substr(10);
    size_t lastSpace = args.find_last_of(' ');
    
    if (lastSpace == std::string::npos) {
        processName = args;
        memorySize = config.mem_per_proc;
        return true;
    }
    
    processName = args.substr(0, lastSpace);
    std::string memorySizeStr = args.substr(lastSpace + 1);
    
    processName.erase(0, processName.find_first_not_of(" \t"));
    processName.erase(processName.find_last_not_of(" \t") + 1);
    memorySizeStr.erase(0, memorySizeStr.find_first_not_of(" \t"));
    memorySizeStr.erase(memorySizeStr.find_last_not_of(" \t") + 1);
    
    if (processName.empty()) {
        return false;
    }
    
    try {
        memorySize = std::stoi(memorySizeStr);
    } catch (const std::exception& e) {
        return false;
    }
    
    return true;
}
