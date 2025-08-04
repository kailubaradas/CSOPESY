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

struct PageEntry {
    int physicalFrame;
    bool isLoaded;
    bool isDirty;
    bool isAccessed;        
    
    PageEntry() : physicalFrame(-1), isLoaded(false), isDirty(false), isAccessed(false) {}
};

std::string trim(const std::string &s) {
    auto l = s.find_first_not_of(" \t\r\n");
    if (l == std::string::npos) return "";
    auto r = s.find_last_not_of(" \t\r\n");
    return s.substr(l, r - l + 1);
}

struct Config {
    int num_cpu;
    std::string scheduler;
    int quantum_cycles;
    int batch_process_freq;
    int min_ins;
    int max_ins;
    int delays_per_exec;
    int num_processes = 10;
    int prints_per_process = 100;
    int max_overall_mem;
    int mem_per_frame;
    int mem_per_proc = 4096;
    int min_memory_size = 64;
    int max_memory_size = 65536;
    int num_frames = 1024;
    int backing_store_size = 65536;
};
Config config;

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
        int pagesNeeded = (memSize + config.mem_per_frame - 1) / config.mem_per_frame; 
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
        else if (key == "num-processes") file >> config.num_processes;
        else if (key == "prints-per-process") file >> config.prints_per_process;
        else if (key == "max-overall-mem") file >> config.max_memory_size;
        else if (key == "mem-per-frame") file >> config.mem_per_frame;
        else if (key == "mem-per-proc") file >> config.mem_per_proc;
        else if (key == "min-memory-size") file >> config.min_memory_size;
        else if (key == "max-memory-size") file >> config.max_memory_size;
        else if (key == "num-frames") file >> config.num_frames;
        else if (key == "backing-store-size") file >> config.backing_store_size;
        else {
            std::string garbage;
            file >> garbage;
            std::cerr << "Warning: Unknown config key '" << key << "'. Skipping.\n";
        }
    }
    config.num_frames = config.max_memory_size / config.mem_per_frame;
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

// ===== DEMAND PAGING ALLOCATOR IMPLEMENTATION =====

// Physical Frame Management
struct PhysicalFrame {
    int frameNumber;
    int processId;
    int pageNumber;
    bool isOccupied;
    bool isDirty;
    Clock::time_point lastAccessed;
    
    PhysicalFrame() : frameNumber(-1), processId(-1), pageNumber(-1), 
                     isOccupied(false), isDirty(false), lastAccessed(Clock::now()) {}
    
    PhysicalFrame(int frameNum) : frameNumber(frameNum), processId(-1), pageNumber(-1), 
                                 isOccupied(false), isDirty(false), lastAccessed(Clock::now()) {}
};

void writePageToBackingStore(int processId, int pageNumber, const std::vector<int>& pageData) {
    std::ofstream ofs("csopesy-backing-store.txt", std::ios::app);
    if (!ofs) return;
    ofs << "PID " << processId << " PAGE " << pageNumber << " DATA";
    for (int val : pageData) ofs << " " << val;
    ofs << "\n";
}

std::vector<int> readPageFromBackingStore(int processId, int pageNumber) {
    std::ifstream ifs("csopesy-backing-store.txt");
    std::vector<int> pageData(config.mem_per_frame / sizeof(int), 0);
    if (!ifs) return pageData;
    std::string line;
    while (std::getline(ifs, line)) {
        std::istringstream iss(line);
        std::string pidLabel, pageLabel, dataLabel;
        int pid, page;
        iss >> pidLabel >> pid >> pageLabel >> page >> dataLabel;
        if (pid == processId && page == pageNumber) {
            for (int& val : pageData) {
                if (!(iss >> val)) break;
            }
            break;
        }
    }
    return pageData;
}

// Backing Store Simulation
struct BackingStore {
    std::vector<std::vector<int>> processPages;  // [processId][pageNumber] -> data
    std::mutex backingStoreMutex;
    
    BackingStore() {
        processPages.resize(1000);  // Support up to 1000 processes
        for (auto& pages : processPages) {
            pages.resize(1000, 0);  // Each process can have up to 1000 pages
        }
    }
    
    void storePage(int processId, int pageNumber, const std::vector<int>& pageData) {
        std::lock_guard<std::mutex> lock(backingStoreMutex);
        writePageToBackingStore(processId, pageNumber, pageData);
    }

    std::vector<int> loadPage(int processId, int pageNumber) {
        std::lock_guard<std::mutex> lock(backingStoreMutex);
        return readPageFromBackingStore(processId, pageNumber);
    }
};

// Global Memory Management
class DemandPagingAllocator {
private:
    std::vector<PhysicalFrame> physicalFrames;
    std::queue<int> freeFrames;
    std::queue<int> fifoQueue;
    BackingStore backingStore;
    std::mutex framesMutex;
    int pageFaultCount;
    int pageReplacementCount;
    
    // LRU Page Replacement Algorithm
    int findLRUFrame() {
        int lruFrame = -1;
        Clock::time_point oldestTime = Clock::now();
        
        for (int i = 0; i < config.num_frames; ++i) {
            if (physicalFrames[i].isOccupied && physicalFrames[i].lastAccessed < oldestTime) {
                oldestTime = physicalFrames[i].lastAccessed;
                lruFrame = i;
            }
        }
        return lruFrame;
    }
    
    void evictPage(int frameNumber) {
        PhysicalFrame& frame = physicalFrames[frameNumber];
        
        if (frame.isDirty) {
            // Write page back to backing store
            std::vector<int> pageData(config.mem_per_frame / sizeof(int), frameNumber); // Simplified data
            backingStore.storePage(frame.processId, frame.pageNumber, pageData);
        }
        
        // Update the page table entry for the evicted page
        if (sessions.find(frame.processId) != sessions.end() && 
            sessions[frame.processId].memoryLayout) {
            auto& pageTable = sessions[frame.processId].memoryLayout->pageTable;
            if (frame.pageNumber < pageTable.numPages) {
                pageTable.pages[frame.pageNumber].isLoaded = false;
                pageTable.pages[frame.pageNumber].physicalFrame = -1;
                pageTable.pages[frame.pageNumber].isDirty = frame.isDirty;
            }
        }
        
        // Clear frame
        frame.processId = -1;
        frame.pageNumber = -1;
        frame.isOccupied = false;
        frame.isDirty = false;
        
        pageReplacementCount++;
    }
    
public:
    DemandPagingAllocator() : pageFaultCount(0), pageReplacementCount(0) {
        physicalFrames.resize(config.num_frames);
        for (int i = 0; i < config.num_frames; ++i) {
            physicalFrames[i] = PhysicalFrame(i);
            freeFrames.push(i);
        }
    }
    
    // Handle page fault - load page into physical memory
    bool handlePageFault(int processId, int pageNumber) {
        std::lock_guard<std::mutex> lock(framesMutex);
        
        pageFaultCount++;
        
        // Check if process exists
        if (sessions.find(processId) == sessions.end() || 
            !sessions[processId].memoryLayout) {
            return false;
        }
        
        auto& pageTable = sessions[processId].memoryLayout->pageTable;
        if (pageNumber >= pageTable.numPages) {
            return false;
        }
        
        int frameNumber = -1;
        
        // Try to get a free frame
       if (!freeFrames.empty()) {
    frameNumber = freeFrames.front();
    freeFrames.pop();
    fifoQueue.push(frameNumber);
} else {
    // evict oldest in FIFO
    frameNumber = fifoQueue.front();
    fifoQueue.pop();
    evictPage(frameNumber);
    fifoQueue.push(frameNumber);
    pageReplacementCount++;
}
        
        // Load page from backing store
        std::vector<int> pageData = backingStore.loadPage(processId, pageNumber);
        
        // Update physical frame
        PhysicalFrame& frame = physicalFrames[frameNumber];
        frame.processId = processId;
        frame.pageNumber = pageNumber;
        frame.isOccupied = true;
        frame.isDirty = false;
        frame.lastAccessed = Clock::now();
        
        // Update page table entry
        PageEntry& pageEntry = pageTable.pages[pageNumber];
        pageEntry.physicalFrame = frameNumber;
        pageEntry.isLoaded = true;
        pageEntry.isAccessed = true;
        pageEntry.isDirty = false;
        
        return true;
    }
    
    // Access memory address (triggers page fault if needed)
    bool accessMemory(int processId, int virtualAddress, bool isWrite = false) {
        if (sessions.find(processId) == sessions.end() || 
            !sessions[processId].memoryLayout) {
            return false;
        }
        
        auto& pageTable = sessions[processId].memoryLayout->pageTable;
        int pageNumber = virtualAddress / config.mem_per_frame;
        int offset = virtualAddress % config.mem_per_frame;
        
        if (pageNumber >= pageTable.numPages) {
            return false; // Invalid address
        }
        
        PageEntry& pageEntry = pageTable.pages[pageNumber];
        
        // Check if page is loaded
        if (!pageEntry.isLoaded) {
            // Page fault occurred
            if (!handlePageFault(processId, pageNumber)) {
                return false;
            }
        }
        
        // Update access information
        {
            std::lock_guard<std::mutex> lock(framesMutex);
            if (pageEntry.physicalFrame >= 0 && pageEntry.physicalFrame < config.num_frames) {
                physicalFrames[pageEntry.physicalFrame].lastAccessed = Clock::now();
                if (isWrite) {
                    physicalFrames[pageEntry.physicalFrame].isDirty = true;
                    pageEntry.isDirty = true;
                }
            }
        }
        
        pageEntry.isAccessed = true;
        
        return true;
    }
    
    // Free all pages for a process
    void freeProcessPages(int processId) {
        std::lock_guard<std::mutex> lock(framesMutex);
        
        for (int i = 0; i < config.num_frames; ++i) {
            if (physicalFrames[i].isOccupied && physicalFrames[i].processId == processId) {
                physicalFrames[i].processId = -1;
                physicalFrames[i].pageNumber = -1;
                physicalFrames[i].isOccupied = false;
                physicalFrames[i].isDirty = false;
                freeFrames.push(i);
            }
        }
    }
    
    // Get statistics
    void getStatistics(int& pageFaults, int& pageReplacements, int& framesUsed) {
        std::lock_guard<std::mutex> lock(framesMutex);
        pageFaults = pageFaultCount;
        pageReplacements = pageReplacementCount;
        framesUsed = config.num_frames - freeFrames.size();
    }
    
    // Display frame table
    void displayFrameTable() {
        std::lock_guard<std::mutex> lock(framesMutex);
        
        std::cout << "\n===== PHYSICAL FRAME TABLE =====\n";
        std::cout << "Frame# | Process ID | Page# | Occupied | Dirty | Last Accessed\n";
        std::cout << "-------|------------|-------|----------|-------|---------------\n";
        
        for (int i = 0; i < config.num_frames; ++i) {
            const auto& frame = physicalFrames[i];
            std::cout << std::setw(6) << i << " | ";
            
            if (frame.isOccupied) {
                std::cout << std::setw(10) << frame.processId << " | ";
                std::cout << std::setw(5) << frame.pageNumber << " | ";
                std::cout << std::setw(8) << "Yes" << " | ";
                std::cout << std::setw(5) << (frame.isDirty ? "Yes" : "No") << " | ";
                
                // Fixed time formatting - use the same method as formatTimestamp
                auto time_t_val = Clock::to_time_t(frame.lastAccessed);
                std::tm* tm = std::localtime(&time_t_val);
                std::cout << std::setfill('0')
                        << std::setw(2) << tm->tm_hour << ':'
                        << std::setw(2) << tm->tm_min << ':'
                        << std::setw(2) << tm->tm_sec;
            } else {
                std::cout << std::setw(10) << "N/A" << " | ";
                std::cout << std::setw(5) << "N/A" << " | ";
                std::cout << std::setw(8) << "No" << " | ";
                std::cout << std::setw(5) << "N/A" << " | ";
                std::cout << "N/A";
            }
            std::cout << "\n";
        }
        
        int pageFaults, pageReplacements, framesUsed;
        getStatistics(pageFaults, pageReplacements, framesUsed);
        
        std::cout << "\nSTATISTICS:\n";
        std::cout << "  Total Page Faults: " << pageFaults << "\n";
        std::cout << "  Page Replacements: " << pageReplacements << "\n";
        std::cout << "  Frames Used: " << framesUsed << "/" << config.num_frames << "\n";
        std::cout << "  Free Frames: " << (config.num_frames - framesUsed) << "\n\n";
    }

};

// Convert hexadecimal string to integer
int hexToInt(const std::string& hexStr) {
    return std::stoi(hexStr, nullptr, 16);
}

// Global demand paging allocator instance
DemandPagingAllocator demandPagingAllocator;

// Memory access functions with demand paging
bool readMemory(int processId, int virtualAddress, int& value) {
    if (demandPagingAllocator.accessMemory(processId, virtualAddress, false)) {
        // Simulate reading value (simplified)
        value = virtualAddress % 1000; // Dummy value based on address
        return true;
    }
    return false;
}

bool writeMemory(int processId, int virtualAddress, int value) {
    return demandPagingAllocator.accessMemory(processId, virtualAddress, true);
}

// Execute instruction with memory access simulation
bool executeInstructionWithPaging(int processId, const Instruction& instruction) {
			auto& pageTable = sessions[processId].memoryLayout->pageTable;
		
		// If this instruction reads or writes memory, compute its page:
		if (instruction.type == InstructionType::READ ||
		    instruction.type == InstructionType::WRITE) 
		{
		    int vAddr   = hexToInt(instruction.operands[1 - (instruction.type==InstructionType::READ ? 0 : 0)]); 
		    if (!pageTable.pages[vAddr / config.mem_per_frame].isLoaded) {
		        // This will load it (or evict via FIFO) before we go on:
		        demandPagingAllocator.handlePageFault(processId, vAddr / config.mem_per_frame);
		    }
		}
	
    auto& variables = sessions[processId].variables;
    
    try {
        switch (instruction.type) {
            case InstructionType::DECLARE: {
                std::string varName = instruction.operands[0];
                int value = std::stoi(instruction.operands[1]);
                variables.variables[varName] = value;
                
                // Simulate memory access for variable storage
                int address = std::hash<std::string>{}(varName) % sessions[processId].memorySize;
                writeMemory(processId, address, value);
                
                std::cout << "Process " << processId << " declared " << varName << " = " << value << "\n";
                break;
            }
            
            case InstructionType::READ: {
                std::string varName = instruction.operands[0];
                int address = hexToInt(instruction.operands[1]);
                
                if (address >= sessions[processId].memorySize) {
                    std::cerr << "Error: Address " << instruction.operands[1] << " out of bounds\n";
                    return false;
                }
                
                int value;
                if (readMemory(processId, address, value)) {
                    variables.variables[varName] = value;
                    std::cout << "Process " << processId << " read " << varName << " = " << value 
                              << " from " << instruction.operands[1] << "\n";
                } else {
                    std::cerr << "Error: Failed to read memory at address " << instruction.operands[1] << "\n";
                    return false;
                }
                break;
            }
            
            case InstructionType::WRITE: {
                int address = hexToInt(instruction.operands[0]);
                std::string varName = instruction.operands[1];
                
                if (address >= sessions[processId].memorySize) {
                    std::cerr << "Error: Address " << instruction.operands[0] << " out of bounds\n";
                    return false;
                }
                
                if (variables.variables.find(varName) == variables.variables.end()) {
                    std::cerr << "Error: Variable '" << varName << "' not declared\n";
                    return false;
                }
                
                int value = variables.variables[varName];
                if (writeMemory(processId, address, value)) {
                    std::cout << "Process " << processId << " wrote " << varName << " (" << value 
                              << ") to " << instruction.operands[0] << "\n";
                } else {
                    std::cerr << "Error: Failed to write memory at address " << instruction.operands[0] << "\n";
                    return false;
                }
                break;
            }
            
            case InstructionType::ADD:
            case InstructionType::SUB:
            case InstructionType::MUL:
            case InstructionType::DIV: {
                std::string resultVar = instruction.operands[0];
                
                // Get operand values
                int op1, op2;
                
                // First operand - can be variable or constant
                if (variables.variables.find(instruction.operands[1]) != variables.variables.end()) {
                    op1 = variables.variables[instruction.operands[1]];
                } else {
                    try {
                        op1 = std::stoi(instruction.operands[1]);
                    } catch (const std::exception&) {
                        std::cerr << "Error: Invalid operand '" << instruction.operands[1] << "'\n";
                        return false;
                    }
                }
                
                // Second operand - can be variable or constant
                if (variables.variables.find(instruction.operands[2]) != variables.variables.end()) {
                    op2 = variables.variables[instruction.operands[2]];
                } else {
                    try {
                        op2 = std::stoi(instruction.operands[2]);
                    } catch (const std::exception&) {
                        std::cerr << "Error: Invalid operand '" << instruction.operands[2] << "'\n";
                        return false;
                    }
                }
                
                // Perform operation
                int result;
                std::string op_str;
                switch (instruction.type) {
                    case InstructionType::ADD: result = op1 + op2; op_str = "+"; break;
                    case InstructionType::SUB: result = op1 - op2; op_str = "-"; break;
                    case InstructionType::MUL: result = op1 * op2; op_str = "*"; break;
                    case InstructionType::DIV: 
                        if (op2 == 0) {
                            std::cerr << "Error: Division by zero\n";
                            return false;
                        }
                        result = op1 / op2; 
                        op_str = "/";
                        break;
                    default: return false;
                }
                
                variables.variables[resultVar] = result;
                
                // Simulate memory access for result storage
                int address = std::hash<std::string>{}(resultVar) % sessions[processId].memorySize;
                writeMemory(processId, address, result);
                
                std::cout << "Process " << processId << " computed " << resultVar << " = " 
                          << op1 << " " << op_str << " " << op2 << " = " << result << "\n";
                break;
            }
            
            case InstructionType::PRINT: {
                std::string content = instruction.operands[0];
                
                // Handle simple variable printing
                if (variables.variables.find(content) != variables.variables.end()) {
                    std::cout << "Process " << processId << " prints: " << variables.variables[content] << "\n";
                } else {
                    // Handle string literals and basic string concatenation
                    std::string output = content;
                    
                    // Simple string + variable handling (basic implementation)
                    size_t plusPos = content.find(" + ");
                    if (plusPos != std::string::npos) {
                        std::string leftPart = trim(content.substr(0, plusPos));
                        std::string rightPart = trim(content.substr(plusPos + 3));
                        
                        // Remove quotes from string literals
                        if (leftPart.front() == '"' && leftPart.back() == '"') {
                            leftPart = leftPart.substr(1, leftPart.length() - 2);
                        }
                        
                        if (variables.variables.find(rightPart) != variables.variables.end()) {
                            output = leftPart + std::to_string(variables.variables[rightPart]);
                        } else {
                            output = leftPart + rightPart;
                        }
                    } else if (content.front() == '"' && content.back() == '"') {
                        // Remove quotes from string literals
                        output = content.substr(1, content.length() - 2);
                    }
                    
                    std::cout << "Process " << processId << " prints: " << output << "\n";
                }
                break;
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error executing instruction: " << e.what() << "\n";
        return false;
    }
}

// ===== END DEMAND PAGING ALLOCATOR IMPLEMENTATION =====

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
    std::cout << "Page Size: " << config.mem_per_frame << " bytes\n\n";
    
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
    return size >= config.min_memory_size && size <= config.max_memory_size && isPowerOfTwo(size);
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

void cpuWorkerWithInstructions(int coreId) {
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

        // Check if process has custom instructions
        if (!sessions[pid].instructions.empty()) {
            std::cout << "\nExecuting custom instructions for process " << pid 
                      << " (" << processNames[pid] << "):\n";
            
            // Execute each instruction
            for (size_t i = 0; i < sessions[pid].instructions.size(); ++i) {
                const auto& instruction = sessions[pid].instructions[i];
                std::cout << "Instruction " << (i + 1) << "/" << sessions[pid].instructions.size() << ": ";
                
                if (!executeInstructionWithPaging(pid, instruction)) {
                    std::cerr << "Failed to execute instruction " << (i + 1) << " for process " << pid << "\n";
                    break;
                }
                
                // Add delay between instructions
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            std::cout << "Process " << pid << " (" << processNames[pid] << ") completed all instructions.\n\n";
        } else {
            // Original behavior for processes without custom instructions
            std::string fname = std::string("screen_") + (pid < 10 ? "0" : "") + std::to_string(pid) + ".txt";
            std::ofstream ofs(fname.c_str(), std::ios::trunc);
            for (int i = 0; i < config.prints_per_process; ++i) {
                auto now = Clock::now();
                ofs << "(" << formatTimestamp(now) << ") Core:" << coreId
                    << " \"Hello world from " << (processNames.count(pid) ? processNames[pid] : (std::string("screen_") + (pid < 10 ? "0" : "") + std::to_string(pid))) << "!\"\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            ofs.close();
        }

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
        else if ((block.end - block.start + 1) < config.mem_per_proc)
            externalFrag += (block.end - block.start + 1);
    }

    ofs << "Number of processes in memory: " << procCount << "\n";
    ofs << "Total external fragmentation in KB: " << externalFrag / 1024 << "\n\n";

    ofs << "----end---- = " << config.max_overall_mem << "\n\n";

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

    int totalMemory = config.max_overall_mem;
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
            if (blockSize < config.mem_per_proc) {
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
    ofs << "----end---- = " << config.max_memory_size << "\n\n";

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

        for (int pid = 1; pid <= config.num_processes; ++pid) {
            readyQueue.push(pid);
            Session s;
            s.start = Clock::now();
            s.finished = false;
            s.memorySize = config.mem_per_proc;
            sessions[pid] = std::move(s);
            processNames[pid] = std::string("screen_") + (pid < 10 ? "0" : "") + std::to_string(pid);

            createProcessMemoryLayout(pid, config.mem_per_proc);
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
        for (int pid = 1; pid <= config.num_processes; ++pid) {
            int assignedCore = (pid - 1) % config.num_cpu;
            {
                std::lock_guard<std::mutex> lock(coreMutexes[assignedCore]);
                coreQueues[assignedCore].push(pid);

                Session s;
                s.start = Clock::now();
                s.finished = false;
                s.memorySize = config.mem_per_proc;
                sessions[pid] = std::move(s);
                processNames[pid] = std::string("screen_") + (pid < 10 ? "0" : "") + std::to_string(pid);
                
                createProcessMemoryLayout(pid, config.mem_per_proc);
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
        memoryBlocks.push_back({0, config.max_memory_size - 1, -1}); 

        if (cmd == "scheduler-test") {
            stopScheduler = false;
            sessions.clear();
            processNames.clear();
            for (int i = 0; i < config.num_cpu; ++i) {
                std::queue<int> empty;
                std::swap(coreQueues[i], empty);
            }

            for (int i = 0; i < config.num_cpu; ++i)
                workers.emplace_back(cpuWorkerWithInstructions, i);
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
    
    // Parse the command
    if (!parseScreenCommandWithInstructions(cmd, pname, memorySize, instructionString)) {
        std::cout << "Error: Invalid command format.\n";
        std::cout << "Usage: screen -c <process_name> <memory_size> \"<instructions>\"\n";
        std::cout << "Example: screen -c myprocess 1024 \"DECLARE x 10; ADD result x 5; PRINT(result)\"\n";
        continue;
    }
    
    // Validate process name
    if (pname.empty()) {
        std::cout << "Error: Process name cannot be empty.\n";
        continue;
    }
    
    // Validate memory size
    if (!isValidMemorySize(memorySize)) {
        std::cout << "Error: Invalid memory size (" << memorySize << " bytes).\n";
        std::cout << "Memory size must be:\n";
        std::cout << "  - Between " << config.min_memory_size << " and " << config.max_memory_size << " bytes\n";
        std::cout << "  - A power of 2 (e.g., 64, 128, 256, 512, 1024, 2048, 4096, ...)\n";
        continue;
    }
    
    // Parse and validate instructions
    std::vector<Instruction> instructions;
    if (!parseInstructions(instructionString, instructions)) {
        std::cout << "Error: Failed to parse instructions.\n";
        continue;
    }
    
    // Validate instruction count
    if (instructions.size() < 1 || instructions.size() > 50) {
        std::cout << "Error: Number of instructions must be between 1 and 50. Found: " 
                  << instructions.size() << "\n";
        continue;
    }
    
    // Create the process
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

    std::cout << "Process '" << pname << "' created successfully!\n";
    std::cout << "  Memory size: " << memorySize << " bytes\n";
    std::cout << "  Instructions: " << instructions.size() << " parsed successfully\n";
    std::cout << "  Assigned to core: " << assignedCore << "\n\n";
    
    // Display parsed instructions for confirmation
    printInstructions(instructions);
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
                std::cout << "  - Between " << config.min_memory_size << " and " << config.max_memory_size << " bytes\n";
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
                int total_lines = config.prints_per_process;
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
        else if (cmd == "test-pagetable") {
            // Create a test process
            int testPid = next_pid++;
            processNames[testPid] = "test_process";
            Session s;
            s.start = Clock::now();
            s.finished = false;
            s.memorySize = 1024; // 1KB for testing
            sessions[testPid] = std::move(s);
            
            createProcessMemoryLayout(testPid, 1024);
            
            // Simulate some memory accesses
            std::cout << "\nSimulating memory accesses...\n";
            writeMemory(testPid, 0x0, 42);    // First page
            writeMemory(testPid, 0x10, 123);  // Still first page
            writeMemory(testPid, 0x20, 456);  // Second page
            int value;
            readMemory(testPid, 0x0, value);  // Read from first page
            
            std::cout << "\nPage Table after memory accesses:\n";
            displayPageTable(testPid);
        }
        else if (cmd == "frametable") {
            demandPagingAllocator.displayFrameTable();
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
            std::cout << "  frametable                   - Display physical frame table\n";
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
