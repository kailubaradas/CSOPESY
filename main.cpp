#include <iostream>
#include <string>
#include <map>
#include <queue>
#include <vector>
#include <fstream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <cstdlib>
#include <atomic>

using Clock = std::chrono::system_clock;

const int NUM_PROCESSES = 10;
const int PRINTS_PER_PROCESS = 100;
const int MAX_MEM = 16384;
const int MEM_PER_PROC = 4096;
const int MEM_PER_FRAME = 16;

const int MIN_MEMORY_SIZE = 64;
const int MAX_MEMORY_SIZE = 65536;

struct PageEntry {
    int physicalFrame;
    bool isLoaded;
    bool isDirty;
    bool isAccessed;        
    
    PageEntry() : physicalFrame(-1), isLoaded(false), isDirty(false), isAccessed(false) {}
};

struct PageTable {
    std::vector<PageEntry> pages;
    int numPages;
    
    PageTable(int pages_needed) : numPages(pages_needed) {
        pages.resize(pages_needed);
    }
};

struct MemorySegment {
    int startAddress;
    int size;
    std::string type;
    
    MemorySegment(int start, int sz, const std::string& t) 
        : startAddress(start), size(sz), type(t) {}
};

struct ProcessMemoryLayout {
    std::vector<MemorySegment> segments;
    PageTable pageTable;
    int totalMemorySize;
    
    ProcessMemoryLayout(int memSize) : pageTable(0), totalMemorySize(memSize) {
        int pagesNeeded = (memSize + MEM_PER_FRAME - 1) / MEM_PER_FRAME; 
        pageTable = PageTable(pagesNeeded);
        initializeSegments();
    }
    
private:
    void initializeSegments() {
        segments.emplace_back(0, 64, "symbol_table");
        int remainingMemory = totalMemorySize - 64;
        if (remainingMemory > 0) {
            int codeSize = (remainingMemory * 40) / 100;
            int stackSize = (remainingMemory * 30) / 100;
            int heapSize = remainingMemory - codeSize - stackSize;
            segments.emplace_back(64, codeSize, "code");
            segments.emplace_back(64 + codeSize, stackSize, "stack");
            segments.emplace_back(64 + codeSize + stackSize, heapSize, "heap");
        }
    }
};

enum class InstructionType {
    DECLARE,
    ADD, SUB, MUL, DIV,
    WRITE, READ,
    PRINT
};

struct Instruction {
    InstructionType type;
    std::vector<std::string> operands;
    
    Instruction(InstructionType t, const std::vector<std::string>& ops) 
        : type(t), operands(ops) {}
};

struct ProcessVariables {
    std::map<std::string, int> variables;
    std::map<int, int> memory;
};

struct Session {
    Clock::time_point start;
    bool finished = false;
    int memorySize = 4096;
    std::unique_ptr<ProcessMemoryLayout> memoryLayout;
    std::vector<Instruction> instructions;
    ProcessVariables variables;
    
    Session() = default;
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = default;
    Session& operator=(Session&&) = default;
};

struct Config {
    int num_cpu;
    std::string scheduler;
    int quantum_cycles;
    int batch_process_freq;
    int min_ins;
    int max_ins;
    int delays_per_exec;
};

Config config;

bool readConfig(const std::string& filename, Config& config) {
    std::ifstream file(filename.c_str());
    if (!file) {
        std::cerr << "Error: Cannot open config file '" << filename << "'\n";
        return false;
    }

    std::string key;
    while (file >> key) {
        if (key == "num-cpu") file >> config.num_cpu;
        else if (key == "scheduler") file >> config.scheduler;
        else if (key == "quantum-cycles") file >> config.quantum_cycles;
        else if (key == "batch-process-freq") file >> config.batch_process_freq;
        else if (key == "min-ins") file >> config.min_ins;
        else if (key == "max-ins") file >> config.max_ins;
        else if (key == "delays-per-exec") file >> config.delays_per_exec;
        else {
            std::string garbage;
            file >> garbage;
            std::cerr << "Warning: Unknown config key '" << key << "'. Skipping.\n";
        }
    }

    return true;
}

void printConfig(const Config& config) {
    std::cout << "Loaded Configuration:\n";
    std::cout << "  num-cpu: " << config.num_cpu << "\n";
    std::cout << "  scheduler: " << config.scheduler << "\n";
    std::cout << "  quantum-cycles: " << config.quantum_cycles << "\n";
    std::cout << "  batch-process-freq: " << config.batch_process_freq << "\n";
    std::cout << "  min-ins: " << config.min_ins << "\n";
    std::cout << "  max-ins: " << config.max_ins << "\n";
    std::cout << "  delays-per-exec: " << config.delays_per_exec << "\n";
}

std::map<int, Session> sessions;
std::map<int, std::string> processNames;
std::atomic<bool> stopScheduler(false);

std::vector<std::queue<int>> coreQueues;
std::vector<std::mutex> coreMutexes;
std::vector<std::condition_variable> coreCVs;

struct MemoryBlock {
    int start;
    int end;
    int pid;  
};

std::vector<MemoryBlock> memoryBlocks;  
std::mutex memoryMutex;
int snapshotCounter = 0;
bool enableSnapshots = false;

void createProcessMemoryLayout(int pid, int memorySize) {
    sessions[pid].memoryLayout = std::make_unique<ProcessMemoryLayout>(memorySize);
    
    std::cout << "Created memory layout for process " << pid << ":\n";
    std::cout << "  Total memory: " << memorySize << " bytes\n";
    std::cout << "  Pages needed: " << sessions[pid].memoryLayout->pageTable.numPages << "\n";
    std::cout << "  Memory segments:\n";
    
    for (const auto& segment : sessions[pid].memoryLayout->segments) {
        std::cout << "    " << segment.type << ": " 
                  << segment.startAddress << "-" 
                  << (segment.startAddress + segment.size - 1) 
                  << " (" << segment.size << " bytes)\n";
    }
}

void displayPageTable(int pid) {
    if (sessions.find(pid) == sessions.end() || !sessions[pid].memoryLayout) {
        std::cout << "Process " << pid << " not found or has no memory layout.\n";
        return;
    }
    
    const auto& pageTable = sessions[pid].memoryLayout->pageTable;
    std::cout << "Page Table for Process " << pid << " (" << processNames[pid] << "):\n";
    std::cout << "Total Pages: " << pageTable.numPages << "\n";
    std::cout << "Page Size: " << MEM_PER_FRAME << " bytes\n\n";
    
    std::cout << "Page# | Physical Frame | Loaded | Dirty | Accessed\n";
    std::cout << "------|----------------|--------|-------|----------\n";
    
    for (int i = 0; i < pageTable.numPages; ++i) {
        const auto& page = pageTable.pages[i];
        std::cout << std::setw(5) << i << " | ";
        
        if (page.physicalFrame == -1) {
            std::cout << std::setw(14) << "N/A" << " | ";
        } else {
            std::cout << std::setw(14) << page.physicalFrame << " | ";
        }
        
        std::cout << std::setw(6) << (page.isLoaded ? "Yes" : "No") << " | ";
        std::cout << std::setw(5) << (page.isDirty ? "Yes" : "No") << " | ";
        std::cout << std::setw(8) << (page.isAccessed ? "Yes" : "No") << "\n";
    }
    std::cout << "\n";
}

void displayMemorySegments(int pid) {
    if (sessions.find(pid) == sessions.end() || !sessions[pid].memoryLayout) {
        std::cout << "Process " << pid << " not found or has no memory layout.\n";
        return;
    }
    
    const auto& segments = sessions[pid].memoryLayout->segments;
    std::cout << "Memory Segments for Process " << pid << " (" << processNames[pid] << "):\n";
    std::cout << "Segment Type  | Start Address | End Address | Size (bytes)\n";
    std::cout << "--------------|---------------|-------------|-------------\n";
    
    for (const auto& segment : segments) {
        std::cout << std::setw(12) << segment.type << " | ";
        std::cout << std::setw(13) << segment.startAddress << " | ";
        std::cout << std::setw(11) << (segment.startAddress + segment.size - 1) << " | ";
        std::cout << std::setw(11) << segment.size << "\n";
    }
    std::cout << "\n";
}

std::string trim(const std::string &s) {
    auto l = s.find_first_not_of(" \t\r\n");
    if (l == std::string::npos) return "";
    auto r = s.find_last_not_of(" \t\r\n");
    return s.substr(l, r - l + 1);
}

std::vector<std::string> split(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    
    while (std::getline(ss, token, delimiter)) {
        tokens.push_back(trim(token));
    }
    
    return tokens;
}

bool isValidVariableName(const std::string& name) {
    if (name.empty() || !std::isalpha(name[0])) {
        return false;
    }
    
    for (char c : name) {
        if (!std::isalnum(c) && c != '_') {
            return false;
        }
    }
    
    return true;
}

bool isValidAddress(const std::string& addr) {
    if (addr.length() < 3 || addr.substr(0, 2) != "0x") {
        return false;
    }
    
    for (size_t i = 2; i < addr.length(); ++i) {
        if (!std::isxdigit(addr[i])) {
            return false;
        }
    }
    
    return true;
}

int hexToInt(const std::string& hexStr) {
    return std::stoi(hexStr, nullptr, 16);
}

bool parseInstruction(const std::string& instrStr, Instruction& instruction) {
    std::vector<std::string> tokens = split(instrStr, ' ');
    
    if (tokens.empty()) {
        return false;
    }
    
    std::string command = tokens[0];
    
    if (command == "DECLARE") {
        if (tokens.size() != 3) {
            std::cerr << "Error: DECLARE requires exactly 2 arguments: DECLARE <variable> <value>\n";
            return false;
        }
        
        if (!isValidVariableName(tokens[1])) {
            std::cerr << "Error: Invalid variable name '" << tokens[1] << "'\n";
            return false;
        }
        
        try {
            std::stoi(tokens[2]);
        } catch (const std::exception&) {
            std::cerr << "Error: DECLARE value must be a number\n";
            return false;
        }
        
        instruction = Instruction(InstructionType::DECLARE, {tokens[1], tokens[2]});
        return true;
    }
    
    else if (command == "ADD" || command == "SUB" || command == "MUL" || command == "DIV") {
        if (tokens.size() != 4) {
            std::cerr << "Error: " << command << " requires exactly 3 arguments: " 
                      << command << " <result> <operand1> <operand2>\n";
            return false;
        }
        
        if (!isValidVariableName(tokens[1])) {
            std::cerr << "Error: Invalid result variable name '" << tokens[1] << "'\n";
            return false;
        }
        
        InstructionType type;
        if (command == "ADD") type = InstructionType::ADD;
        else if (command == "SUB") type = InstructionType::SUB;
        else if (command == "MUL") type = InstructionType::MUL;
        else type = InstructionType::DIV;
        
        instruction = Instruction(type, {tokens[1], tokens[2], tokens[3]});
        return true;
    }
    
    else if (command == "WRITE") {
        if (tokens.size() != 3) {
            std::cerr << "Error: WRITE requires exactly 2 arguments: WRITE <address> <variable>\n";
            return false;
        }
        
        if (!isValidAddress(tokens[1])) {
            std::cerr << "Error: Invalid address format '" << tokens[1] << "'. Use 0xABCD format.\n";
            return false;
        }
        
        if (!isValidVariableName(tokens[2])) {
            std::cerr << "Error: Invalid variable name '" << tokens[2] << "'\n";
            return false;
        }
        
        instruction = Instruction(InstructionType::WRITE, {tokens[1], tokens[2]});
        return true;
    }
    
    else if (command == "READ") {
        if (tokens.size() != 3) {
            std::cerr << "Error: READ requires exactly 2 arguments: READ <variable> <address>\n";
            return false;
        }
        
        if (!isValidVariableName(tokens[1])) {
            std::cerr << "Error: Invalid variable name '" << tokens[1] << "'\n";
            return false;
        }
        
        if (!isValidAddress(tokens[2])) {
            std::cerr << "Error: Invalid address format '" << tokens[2] << "'. Use 0xABCD format.\n";
            return false;
        }
        
        instruction = Instruction(InstructionType::READ, {tokens[1], tokens[2]});
        return true;
    }
    
    else if (command == "PRINT") {
        if (tokens.size() < 2) {
            std::cerr << "Error: PRINT requires at least 1 argument\n";
            return false;
        }
        
        std::string printArg = instrStr.substr(instrStr.find("PRINT") + 5);
        printArg = trim(printArg);
        
        if (printArg.empty() || printArg.front() != '(' || printArg.back() != ')') {
            std::cerr << "Error: PRINT argument must be enclosed in parentheses\n";
            return false;
        }
        
        printArg = printArg.substr(1, printArg.length() - 2);
        instruction = Instruction(InstructionType::PRINT, {printArg});
        return true;
    }

    else if (command == "PRINT" || command.substr(0, 6) == "PRINT(") {
        std::string printArg;
        
        if (command == "PRINT") {
            if (tokens.size() < 2) {
                std::cerr << "Error: PRINT requires at least 1 argument\n";
                return false;
            }
            
            printArg = instrStr.substr(instrStr.find("PRINT") + 5);
            printArg = trim(printArg);
        } else {
            printArg = instrStr.substr(5);
        }
        
        if (printArg.empty() || printArg.front() != '(' || printArg.back() != ')') {
            std::cerr << "Error: PRINT argument must be enclosed in parentheses\n";
            return false;
        }
        
        printArg = printArg.substr(1, printArg.length() - 2);
        instruction = Instruction(InstructionType::PRINT, {printArg});
        return true;
    }
    
    else {
        std::cerr << "Error: Unknown instruction '" << command << "'\n";
        return false;
    }
}

bool parseInstructions(const std::string& instructionString, std::vector<Instruction>& instructions) {
    instructions.clear();
    
    if (instructionString.empty()) {
        std::cerr << "Error: Instruction string cannot be empty\n";
        return false;
    }
    
    std::vector<std::string> instrList = split(instructionString, ';');
    
    if (instrList.size() < 1 || instrList.size() > 50) {
        std::cerr << "Error: Number of instructions must be between 1 and 50. Found: " 
                  << instrList.size() << "\n";
        return false;
    }
    
    for (const std::string& instrStr : instrList) {
        std::string trimmedInstr = trim(instrStr);
        if (trimmedInstr.empty()) continue;
        
        Instruction instruction(InstructionType::DECLARE, {});
        if (!parseInstruction(trimmedInstr, instruction)) {
            return false;
        }
        
        instructions.push_back(instruction);
    }
    
    return true;
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
    
    try {
        memorySize = std::stoi(parts[1]);
    } catch (const std::exception&) {
        std::cerr << "Error: Invalid memory size\n";
        return false;
    }
    
    return true;
}

void printInstructions(const std::vector<Instruction>& instructions) {
    std::cout << "Parsed Instructions (" << instructions.size() << " total):\n";
    for (size_t i = 0; i < instructions.size(); ++i) {
        std::cout << "  " << (i + 1) << ". ";
        
        switch (instructions[i].type) {
            case InstructionType::DECLARE:
                std::cout << "DECLARE " << instructions[i].operands[0] 
                          << " " << instructions[i].operands[1];
                break;
            case InstructionType::ADD:
                std::cout << "ADD " << instructions[i].operands[0] 
                          << " " << instructions[i].operands[1] 
                          << " " << instructions[i].operands[2];
                break;
            case InstructionType::SUB:
                std::cout << "SUB " << instructions[i].operands[0] 
                          << " " << instructions[i].operands[1] 
                          << " " << instructions[i].operands[2];
                break;
            case InstructionType::MUL:
                std::cout << "MUL " << instructions[i].operands[0] 
                          << " " << instructions[i].operands[1] 
                          << " " << instructions[i].operands[2];
                break;
            case InstructionType::DIV:
                std::cout << "DIV " << instructions[i].operands[0] 
                          << " " << instructions[i].operands[1] 
                          << " " << instructions[i].operands[2];
                break;
            case InstructionType::WRITE:
                std::cout << "WRITE " << instructions[i].operands[0] 
                          << " " << instructions[i].operands[1];
                break;
            case InstructionType::READ:
                std::cout << "READ " << instructions[i].operands[0] 
                          << " " << instructions[i].operands[1];
                break;
            case InstructionType::PRINT:
                std::cout << "PRINT(" << instructions[i].operands[0] << ")";
                break;
        }
        std::cout << "\n";
    }
}

bool isPowerOfTwo(int n) {
    return n > 0 && (n & (n - 1)) == 0;
}

bool isValidMemorySize(int size) {
    return size >= MIN_MEMORY_SIZE && size <= MAX_MEMORY_SIZE && isPowerOfTwo(size);
}

bool parseScreenCommand(const std::string& cmd, std::string& processName, int& memorySize) {
    std::string args = cmd.substr(10);
    size_t lastSpace = args.find_last_of(' ');
    
    if (lastSpace == std::string::npos) {
        processName = args;
        memorySize = MEM_PER_PROC;
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

std::string formatTimestamp(const Clock::time_point &tp) {
    std::time_t t = Clock::to_time_t(tp);
    std::tm tm = *std::localtime(&t);
    std::ostringstream oss;
    oss << std::setfill('0')
        << std::setw(2) << (tm.tm_mon + 1) << '/'
        << std::setw(2) << tm.tm_mday << '/'
        << (tm.tm_year + 1900) << ", "
        << std::setw(2) << ((tm.tm_hour % 12) ? (tm.tm_hour % 12) : 12)
        << ':' << std::setw(2) << tm.tm_min
        << ':' << std::setw(2) << tm.tm_sec
        << ' ' << (tm.tm_hour >= 12 ? "PM" : "AM");
    return oss.str();
}

void cpuWorker(int coreId) {
    while (!stopScheduler || !coreQueues[coreId].empty()) {
        int pid = -1;
        {
            std::unique_lock<std::mutex> lock(coreMutexes[coreId]);
            coreCVs[coreId].wait(lock, [&] {
                return !coreQueues[coreId].empty() || stopScheduler;
            });
            if (!coreQueues[coreId].empty()) {
                pid = coreQueues[coreId].front();
                coreQueues[coreId].pop();
            }
        }

        if (pid == -1) continue;

        std::string fname = std::string("screen_") + (pid < 10 ? "0" : "") + std::to_string(pid) + ".txt";
        std::ofstream ofs(fname.c_str(), std::ios::trunc);
        for (int i = 0; i < PRINTS_PER_PROCESS; ++i) {
            auto now = Clock::now();
            ofs << "(" << formatTimestamp(now) << ") Core:" << coreId
                << " \"Hello world from " << (processNames.count(pid) ? processNames[pid] : (std::string("screen_") + (pid < 10 ? "0" : "") + std::to_string(pid))) << "!\"\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        ofs.close();

        sessions[pid].finished = true;
    }
}

bool allocateMemory(int pid) {
    std::lock_guard<std::mutex> lock(memoryMutex);
    int requiredMemory = sessions[pid].memorySize;
    
    for (auto it = memoryBlocks.begin(); it != memoryBlocks.end(); ++it) {
        if (it->pid == -1 && (it->end - it->start + 1) >= requiredMemory) {
            int oldEnd = it->end;
            it->end = it->start + requiredMemory - 1;
            it->pid = pid;

            if (it->end < oldEnd) {
                memoryBlocks.insert(it + 1, {it->end + 1, oldEnd, -1});
            }
            return true;
        }
    }
    return false;
}

void freeMemory(int pid) {
    std::lock_guard<std::mutex> lock(memoryMutex);
    for (auto& block : memoryBlocks) {
        if (block.pid == pid) {
            block.pid = -1;
        }
    }

    for (auto it = memoryBlocks.begin(); it != memoryBlocks.end(); ) {
        if (it != memoryBlocks.begin()) {
            auto prev = it - 1;
            if (prev->pid == -1 && it->pid == -1) {
                prev->end = it->end;
                it = memoryBlocks.erase(it);
                continue;
            }
        }
        ++it;
    }
}

auto lastSnapshotTime = Clock::now();
const int SNAPSHOT_INTERVAL_SECONDS = 1;

void snapshotMemory() {
    auto now = Clock::now();
    auto timeSinceLastSnapshot = std::chrono::duration_cast<std::chrono::seconds>(now - lastSnapshotTime);
    
    if (timeSinceLastSnapshot.count() < SNAPSHOT_INTERVAL_SECONDS) {
        return;
    }
    
    lastSnapshotTime = now;
    
    std::lock_guard<std::mutex> lock(memoryMutex);
    std::ofstream ofs("memory_stamp_" + std::to_string(snapshotCounter++) + ".txt");

    std::time_t t = Clock::to_time_t(now);
    std::tm tm = *std::localtime(&t);
    char buffer[64];
    strftime(buffer, sizeof(buffer), "%m/%d/%Y %I:%M:%S%p", &tm);
    ofs << "Timestamp: (" << buffer << ")\n";

    int procCount = 0;
    int externalFrag = 0;
    for (const auto& block : memoryBlocks) {
        if (block.pid != -1) ++procCount;
        else if ((block.end - block.start + 1) < MEM_PER_PROC)
            externalFrag += (block.end - block.start + 1);
    }

    ofs << "Number of processes in memory: " << procCount << "\n";
    ofs << "Total external fragmentation in KB: " << externalFrag / 1024 << "\n\n";

    ofs << "----end---- = " << MAX_MEM << "\n\n";

    for (auto it = memoryBlocks.rbegin(); it != memoryBlocks.rend(); ++it) {
        const auto& block = *it;
        if (block.pid != -1) {
            ofs << block.end << "\n";
            ofs << "P" << block.pid << "\n";
            ofs << block.start << "\n\n";
        }
    }

    ofs << "----start----- = 0\n";
    ofs.close();
}

void generateMemoryReport() {
    std::lock_guard<std::mutex> lock(memoryMutex);
    
    std::ofstream ofs("memory_report.txt");
    if (!ofs) {
        std::cerr << "Error: Could not create memory_report.txt\n";
        return;
    }

    auto now = Clock::now();
    std::time_t t = Clock::to_time_t(now);
    std::tm tm = *std::localtime(&t);
    char buffer[64];
    strftime(buffer, sizeof(buffer), "%m/%d/%Y %I:%M:%S%p", &tm);

    ofs << "||======================================||\n";
    ofs << "||         MEMORY USAGE REPORT          ||\n";
    ofs << "||======================================||\n\n";
    ofs << "Generated: " << buffer << "\n\n";

    int totalMemory = MAX_MEM;
    int usedMemory = 0;
    int freeMemory = 0;
    int externalFrag = 0;
    int processCount = 0;

    for (const auto& block : memoryBlocks) {
        if (block.pid != -1) {
            usedMemory += (block.end - block.start + 1);
            processCount++;
        } else {
            int blockSize = block.end - block.start + 1;
            freeMemory += blockSize;
            if (blockSize < MEM_PER_PROC) {
                externalFrag += blockSize;
            }
        }
    }

    ofs << "MEMORY STATISTICS:\n";
    ofs << "  Total Memory: " << totalMemory << " bytes (" << totalMemory/1024 << " KB)\n";
    ofs << "  Used Memory: " << usedMemory << " bytes (" << usedMemory/1024 << " KB)\n";
    ofs << "  Free Memory: " << freeMemory << " bytes (" << freeMemory/1024 << " KB)\n";
    ofs << "  Memory Utilization: " << (usedMemory * 100 / totalMemory) << "%\n";
    ofs << "  External Fragmentation: " << externalFrag << " bytes (" << externalFrag/1024 << " KB)\n";
    ofs << "  Number of Processes: " << processCount << "\n\n";

    ofs << "PROCESS DETAILS:\n";
    ofs << "PID | Process Name     | Memory (bytes) | Pages | Status\n";
    ofs << "----|------------------|----------------|-------|--------\n";
    
    for (const auto& session : sessions) {
        int pid = session.first;
        const auto& s = session.second;
        std::string name = processNames.count(pid) ? processNames[pid] : "unknown";
        int pages = s.memoryLayout ? s.memoryLayout->pageTable.numPages : 0;
        std::string status = s.finished ? "Finished" : "Running";
        
        ofs << std::setw(3) << pid << " | ";
        ofs << std::setw(16) << std::left << name << " | ";
        ofs << std::setw(14) << std::right << s.memorySize << " | ";
        ofs << std::setw(5) << pages << " | ";
        ofs << status << "\n";
    }

    ofs << "\nMEMORY LAYOUT:\n";
    ofs << "----end---- = " << MAX_MEM << "\n\n";

    for (auto it = memoryBlocks.rbegin(); it != memoryBlocks.rend(); ++it) {
        const auto& block = *it;
        if (block.pid != -1) {
            ofs << block.end << "\n";
            ofs << "P" << block.pid;
            if (processNames.count(block.pid)) {
                ofs << " (" << processNames[block.pid] << ")";
            }
            ofs << "\n" << block.start << "\n\n";
        } else {
            ofs << block.end << "\n";
            ofs << "FREE (" << (block.end - block.start + 1) << " bytes)\n";
            ofs << block.start << "\n\n";
        }
    }

    ofs << "----start----- = 0\n";
    ofs.close();
    
    std::cout << "Memory report generated: memory_report.txt\n";
}

void schedulerThread() {
    if (config.scheduler == "rr") {
        std::queue<int> readyQueue;

        for (int pid = 1; pid <= NUM_PROCESSES; ++pid) {
            readyQueue.push(pid);
            Session s;
            s.start = Clock::now();
            s.finished = false;
            s.memorySize = MEM_PER_PROC;
            sessions[pid] = std::move(s);
            processNames[pid] = std::string("screen_") + (pid < 10 ? "0" : "") + std::to_string(pid);

            createProcessMemoryLayout(pid, MEM_PER_PROC);
        }

        int currentCore = 0;

        while (!readyQueue.empty()) {

            int pid = readyQueue.front();
            readyQueue.pop();

            bool allocated = allocateMemory(pid);
            if (!allocated) {
                readyQueue.push(pid);
                std::this_thread::sleep_for(std::chrono::milliseconds(config.quantum_cycles));
                snapshotMemory();
                continue;
            }

            {
                std::lock_guard<std::mutex> lock(coreMutexes[currentCore]);
                coreQueues[currentCore].push(pid);
            }

            coreCVs[currentCore].notify_one();
            std::this_thread::sleep_for(std::chrono::milliseconds(config.quantum_cycles));
            snapshotMemory();

            if (!sessions[pid].finished) {
                readyQueue.push(pid);
            } else {
                freeMemory(pid);
            }

            currentCore = (currentCore + 1) % config.num_cpu;
        }

        stopScheduler = true;
        for (int i = 0; i < config.num_cpu; ++i)
            coreCVs[i].notify_all();

    } else {
        for (int pid = 1; pid <= NUM_PROCESSES; ++pid) {
            int assignedCore = (pid - 1) % config.num_cpu;
            {
                std::lock_guard<std::mutex> lock(coreMutexes[assignedCore]);
                coreQueues[assignedCore].push(pid);

                Session s;
                s.start = Clock::now();
                s.finished = false;
                s.memorySize = MEM_PER_PROC;
                sessions[pid] = std::move(s);
                processNames[pid] = std::string("screen_") + (pid < 10 ? "0" : "") + std::to_string(pid);
                
                createProcessMemoryLayout(pid, MEM_PER_PROC);
            }
            coreCVs[assignedCore].notify_one();
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        stopScheduler = true;
        for (int i = 0; i < config.num_cpu; ++i)
            coreCVs[i].notify_all();
    }
}

std::string fmtTime(const Clock::time_point &tp) {
    std::time_t t = Clock::to_time_t(tp);
    char buf[64];
    strftime(buf, sizeof(buf), "%m/%d/%Y, %I:%M:%S %p", localtime(&t));
    return buf;
}

void printHeader() {
    std::cout<<"||======================================||\n"
             <<"||            CSOPESY CLI v0.1          ||\n"
             <<"||======================================||\n";
}

void clearScreen() {
#if defined(_WIN32) || defined(_WIN64)
    std::system("cls");
#else
    std::system("clear");
#endif
}

int main() {
    bool initialized = false;
    std::string line;
    std::thread scheduler;
    std::vector<std::thread> workers;

    int next_pid = 1;
    int round_robin_core = 0;

    clearScreen(); printHeader();

    while (true) {
        std::cout << "Main> ";
        if (!std::getline(std::cin, line)) break;
        auto cmd = trim(line);
        if (cmd.empty()) continue;

        if (cmd == "exit") break;

        if (!initialized) {
            if (cmd == "initialize") {
                if (!readConfig("config.txt", config)) {
                    std::cerr << "Initialization failed. Please check config.txt.\n";
                    continue;
                }

                coreQueues = std::vector<std::queue<int>>(config.num_cpu);
                coreMutexes = std::vector<std::mutex>(config.num_cpu);
                coreCVs = std::vector<std::condition_variable>(config.num_cpu);

                initialized = true;
                clearScreen(); printHeader();
            } else {
                std::cout << "Run 'initialize' first.\n";
            }
            continue;
        }

        memoryBlocks.clear();
        memoryBlocks.push_back({0, MAX_MEM - 1, -1}); 

        if (cmd == "scheduler-test") {
            stopScheduler = false;
            sessions.clear();
            processNames.clear();
            for (int i = 0; i < config.num_cpu; ++i) {
                std::queue<int> empty;
                std::swap(coreQueues[i], empty);
            }

            for (int i = 0; i < config.num_cpu; ++i)
                workers.emplace_back(cpuWorker, i);
            scheduler = std::thread(schedulerThread);
            std::cout << "Started scheduling. Run 'screen -ls' every 1-2s.\n";
        }
        else if (cmd.rfind("pagetable ", 0) == 0) {
            try {
                int pid = std::stoi(cmd.substr(10));
                displayPageTable(pid);
            } catch (const std::exception& e) {
                std::cout << "Error: Invalid process ID. Usage: pagetable <pid>\n";
            }
        }
        else if (cmd.rfind("segments ", 0) == 0) {
            try {
                int pid = std::stoi(cmd.substr(9));
                displayMemorySegments(pid);
            } catch (const std::exception& e) {
                std::cout << "Error: Invalid process ID. Usage: segments <pid>\n";
            }
        }
        else if (cmd.rfind("screen -c ", 0) == 0) {
            std::string pname, instructionString;
            int memorySize;
            
            if (!parseScreenCommandWithInstructions(cmd, pname, memorySize, instructionString)) {
                std::cout << "Error: Invalid command format.\n";
                std::cout << "Usage: screen -c <process_name> <memory_size> \"<instructions>\"\n";
                std::cout << "Example: screen -c myprocess 1024 \"DECLARE x 10; ADD result x 5; PRINT(result)\"\n";
                continue;
            }
            
            if (pname.empty()) {
                std::cout << "Error: Process name cannot be empty.\n";
                continue;
            }
            
            if (!isValidMemorySize(memorySize)) {
                std::cout << "Error: Invalid memory size (" << memorySize << " bytes).\n";
                std::cout << "Memory size must be:\n";
                std::cout << "  - Between " << MIN_MEMORY_SIZE << " and " << MAX_MEMORY_SIZE << " bytes\n";
                std::cout << "  - A power of 2 (e.g., 64, 128, 256, 512, 1024, 2048, 4096, ...)\n";
                continue;
            }
            
            // Parse instructions
            std::vector<Instruction> instructions;
            if (!parseInstructions(instructionString, instructions)) {
                std::cout << "Error: Failed to parse instructions.\n";
                continue;
            }
            
            int pid = next_pid++;
            processNames[pid] = pname;
            int assignedCore = round_robin_core++ % config.num_cpu;
            
            {
                std::lock_guard<std::mutex> lock(coreMutexes[assignedCore]);
                coreQueues[assignedCore].push(pid);
                Session s;
                s.start = Clock::now();
                s.finished = false;
                s.memorySize = memorySize;
                s.instructions = instructions;  // Store the parsed instructions
                sessions[pid] = std::move(s);
                
                createProcessMemoryLayout(pid, memorySize);
            }
            coreCVs[assignedCore].notify_one();

            std::cout << "Process '" << pname << "' created with " << memorySize << " bytes of memory.\n";
            std::cout << "Instructions parsed successfully (" << instructions.size() << " instructions).\n";
            printInstructions(instructions);
            
            // Rest of the screen interaction code remains the same...
            // [Continue with existing screen interaction loop]
        }

        else if (cmd.rfind("screen -s ", 0) == 0) {
            std::string pname;
            int memorySize;
            
            if (!parseScreenCommand(cmd, pname, memorySize)) {
                std::cout << "Error: Invalid command format.\n";
                std::cout << "Usage: screen -s <process_name> [memory_size]\n";
                std::cout << "Memory size must be a number between 64 and 65536 bytes.\n";
                continue;
            }
            
            if (pname.empty()) {
                std::cout << "Error: Process name cannot be empty.\n";
                std::cout << "Usage: screen -s <process_name> [memory_size]\n";
                continue;
            }
            
            if (!isValidMemorySize(memorySize)) {
                std::cout << "Error: Invalid memory size (" << memorySize << " bytes).\n";
                std::cout << "Memory size must be:\n";
                std::cout << "  - Between " << MIN_MEMORY_SIZE << " and " << MAX_MEMORY_SIZE << " bytes\n";
                std::cout << "  - A power of 2 (e.g., 64, 128, 256, 512, 1024, 2048, 4096, ...)\n";
                continue;
            }
            
            int pid = next_pid++;
            processNames[pid] = pname; 
            int assignedCore = round_robin_core++ % config.num_cpu;
            {
                std::lock_guard<std::mutex> lock(coreMutexes[assignedCore]);
                coreQueues[assignedCore].push(pid);
                Session s;
                s.start = Clock::now();
                s.finished = false;
                s.memorySize = memorySize; 
                sessions[pid] = std::move(s);
                
                createProcessMemoryLayout(pid, memorySize);
            }
            coreCVs[assignedCore].notify_one();

            std::cout << "Process '" << pname << "' created with " << memorySize << " bytes of memory.\n";

            while (true) {
                clearScreen();
                std::cout << "Process name: " << processNames[pid] << "\n";
                std::cout << "ID: " << pid << "\n";
                std::cout << "Memory size: " << sessions[pid].memorySize << " bytes\n";
                
                if (sessions[pid].memoryLayout) {
                    std::cout << "Pages needed: " << sessions[pid].memoryLayout->pageTable.numPages << "\n";
                }
                
                std::cout << "Logs:\n";

                std::string fname = std::string("screen_") + (pid < 10 ? "0" : "") + std::to_string(pid) + ".txt";
                std::ifstream ifs(fname);
                std::string logline;
                int log_count = 0;
                while (std::getline(ifs, logline)) {
                    std::cout << logline << "\n";
                    log_count++;
                }
                ifs.close();

                int current_line = log_count;
                int total_lines = PRINTS_PER_PROCESS;
                std::cout << "\nCurrent instruction line: " << current_line << "\n";
                std::cout << "Lines of code: " << total_lines << "\n";

                if (sessions[pid].finished) {
                    std::cout << "\nFinished!\n";
                }

                std::cout << "\nroot:\\> ";
                std::string proc_cmd;
                if (!std::getline(std::cin, proc_cmd)) break;
                proc_cmd = trim(proc_cmd);

                if (proc_cmd == "exit") break;
                else if (proc_cmd == "process-smi") {
                    continue;
                }
                else if (proc_cmd == "pagetable") {
                    displayPageTable(pid);
                    std::cout << "Press Enter to continue...";
                    std::cin.get();
                }
                else if (proc_cmd == "segments") {
                    displayMemorySegments(pid);
                    std::cout << "Press Enter to continue...";
                    std::cin.get();
                } else {
                    std::cout << "Unknown command: '" << proc_cmd << "'\n";
                    std::cout << "Available commands: exit, process-smi, pagetable, segments\n";
                    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                }
            }
            clearScreen();
            printHeader();
            continue;
        }
        else if (cmd == "screen -ls") {
            std::cout << "Finished:\n";
            for (const auto& entry : sessions) {
                if (entry.second.finished) {
                    int pages = entry.second.memoryLayout ? entry.second.memoryLayout->pageTable.numPages : 0;
                    std::cout << "  " << processNames[entry.first]
                              << " (screen_" << (entry.first < 10 ? "0" : "") << entry.first << ")"
                              << " @ " << fmtTime(entry.second.start)
                              << " [" << entry.second.memorySize << " bytes, " << pages << " pages]\n";
                }
            }
            std::cout << "Running:\n";
            for (const auto& entry : sessions) {
                if (!entry.second.finished) {
                    int pages = entry.second.memoryLayout ? entry.second.memoryLayout->pageTable.numPages : 0;
                    std::cout << "  " << processNames[entry.first]
                              << " (screen_" << (entry.first < 10 ? "0" : "") << entry.first << ")"
                              << " @ " << fmtTime(entry.second.start)
                              << " [" << entry.second.memorySize << " bytes, " << pages << " pages]\n";
                }
            }
        }
        else if (cmd == "scheduler-stop") {
            stopScheduler = true;
            for (int i = 0; i < config.num_cpu; ++i)
                coreCVs[i].notify_all();

            if (scheduler.joinable()) scheduler.join();
            for (auto &t : workers)
                if (t.joinable()) t.join();
            workers.clear();
            std::cout << "Scheduler stopped.\n";
        } else if (cmd == "report-util") {

            std::ofstream ofs("csopesy-log.txt");

            if (!ofs) {
                std::cerr << "Failed to write report to csopesy-log.txt\n";
                continue;
            }

            ofs << "||======================================||\n"
                << "||         CSOPESY CPU UTIL REPORT      ||\n"
                << "||======================================||\n\n";

            int coresUsed = config.num_cpu;
            int running = 0, finished = 0;
            for (const auto& entry : sessions) {
                if (entry.second.finished) ++finished;
                else ++running;
            }

            ofs << "CPU utilization: " << (coresUsed > 0 ? "100%" : "0%") << "\n";
            ofs << "Cores used: " << coresUsed << "\n";
            ofs << "Cores available: " << 0 << "\n\n";
            ofs << "------------------------------------------\n";
            ofs << "Running processes:\n";
            for (const auto& entry : sessions) {
                if (!entry.second.finished) {
                    int pid = entry.first;
                    const Session& s = entry.second;
                    std::string name = processNames[pid];
                    int pages = s.memoryLayout ? s.memoryLayout->pageTable.numPages : 0;
                    ofs << name << "  (" << fmtTime(s.start) << ")"
                        << "   Core: " << (pid - 1) % config.num_cpu
                        << "   " << 0 << " / ????" 
                        << "   [" << s.memorySize << " bytes, " << pages << " pages]" << "\n";
                }
            }

            ofs << "\nFinished processes:\n";
            for (const auto& entry : sessions) {
                if (entry.second.finished) {
                    int pid = entry.first;
                    const Session& s = entry.second;
                    std::string name = processNames[pid];
                    int pages = s.memoryLayout ? s.memoryLayout->pageTable.numPages : 0;
                    ofs << name << "  (" << fmtTime(s.start) << ")"
                        << "   Finished   "
                        << "???? / ????" 
                        << "   [" << s.memorySize << " bytes, " << pages << " pages]" << "\n";
                }
            }

            ofs << "------------------------------------------\n";
            ofs.close();
            std::cout << "Report generated at C:/csopesy-log.txt!\n";
        } 
        else if (cmd == "help") {
            std::cout << "\nAvailable Commands:\n";
            std::cout << "  initialize                    - Initialize the system\n";
            std::cout << "  scheduler-test               - Start the scheduler test\n";
            std::cout << "  scheduler-stop               - Stop the scheduler\n";
            std::cout << "  screen -s <name> [mem_size]  - Create a new process\n";
            std::cout << "  screen -ls                   - List all processes\n";
            std::cout << "  pagetable <pid>              - Show page table for process\n";
            std::cout << "  segments <pid>               - Show memory segments for process\n";
            std::cout << "  test-pagetable               - Run page table creation tests\n";
            std::cout << "  report-util                  - Generate utilization report\n";
            std::cout << "  help                         - Show this help message\n";
            std::cout << "  exit                         - Exit the program\n\n";
        }
        else {
            std::cout << "Unknown cmd: '" << cmd << "'. Type 'help' for available commands.\n";
        }
    }
    
    stopScheduler = true;
    for (int i = 0; i < config.num_cpu; ++i)
        coreCVs[i].notify_all();
    if (scheduler.joinable()) scheduler.join();
    for (auto &t : workers)
        if (t.joinable()) t.join();

    return 0;
}