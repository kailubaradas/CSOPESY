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

// **CONSTANTS MOVED TO TOP**
const int NUM_PROCESSES = 10;
const int PRINTS_PER_PROCESS = 100;
const int MAX_MEM = 16384;
const int MEM_PER_PROC = 4096;
const int MEM_PER_FRAME = 16;

// Memory size validation constants
const int MIN_MEMORY_SIZE = 64;
const int MAX_MEMORY_SIZE = 65536;

// **NEW STRUCTURES FOR PAGE TABLE AND SEGMENTATION**

struct PageEntry {
    int physicalFrame;      // Physical frame number (-1 if not loaded)
    bool isLoaded;          // Whether page is currently in physical memory
    bool isDirty;           // Whether page has been modified
    bool isAccessed;        // Whether page has been accessed recently
    
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
    int startAddress;       // Virtual start address
    int size;               // Size in bytes
    std::string type;       // "symbol_table", "code", "stack", "heap"
    
    MemorySegment(int start, int sz, const std::string& t) 
        : startAddress(start), size(sz), type(t) {}
};

struct ProcessMemoryLayout {
    std::vector<MemorySegment> segments;
    PageTable pageTable;
    int totalMemorySize;
    
    ProcessMemoryLayout(int memSize) : pageTable(0), totalMemorySize(memSize) {
        // Calculate number of pages needed
        int pagesNeeded = (memSize + MEM_PER_FRAME - 1) / MEM_PER_FRAME; // Ceiling division
        pageTable = PageTable(pagesNeeded);
        
        // Initialize segments
        initializeSegments();
    }
    
private:
    void initializeSegments() {
        // First 64 bytes: Symbol table for declared variables
        segments.emplace_back(0, 64, "symbol_table");
        
        // Remaining memory divided into segments
        int remainingMemory = totalMemorySize - 64;
        if (remainingMemory > 0) {
            // Divide remaining memory: 40% code, 30% stack, 30% heap
            int codeSize = (remainingMemory * 40) / 100;
            int stackSize = (remainingMemory * 30) / 100;
            int heapSize = remainingMemory - codeSize - stackSize; // Remainder goes to heap
            
            segments.emplace_back(64, codeSize, "code");
            segments.emplace_back(64 + codeSize, stackSize, "stack");
            segments.emplace_back(64 + codeSize + stackSize, heapSize, "heap");
        }
    }
};

// **ENHANCED SESSION STRUCTURE**
struct Session {
    Clock::time_point start;
    bool finished = false;
    int memorySize = 4096; // Default memory size in bytes
    std::unique_ptr<ProcessMemoryLayout> memoryLayout; // NEW: Memory layout with page table
    
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

// **NEW FUNCTIONS FOR PAGE TABLE MANAGEMENT**

// Helper function to create process memory layout
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

// Function to display page table information
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

// Function to display memory segments
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

// Test function to validate page table creation
void testPageTableCreation() {
    std::cout << "\n=== PAGE TABLE CREATION TESTS ===\n";
    
    // Test case 1: Standard 4096 byte process
    std::cout << "Test 1: Standard 4096 byte process\n";
    int testPid1 = 9999;
    sessions[testPid1].memorySize = 4096;
    processNames[testPid1] = "test_standard";
    createProcessMemoryLayout(testPid1, 4096);
    
    // Test case 2: Small 128 byte process
    std::cout << "\nTest 2: Small 128 byte process\n";
    int testPid2 = 9998;
    sessions[testPid2].memorySize = 128;
    processNames[testPid2] = "test_small";
    createProcessMemoryLayout(testPid2, 128);
    
    // Test case 3: Large 8192 byte process
    std::cout << "\nTest 3: Large 8192 byte process\n";
    int testPid3 = 9997;
    sessions[testPid3].memorySize = 8192;
    processNames[testPid3] = "test_large";
    createProcessMemoryLayout(testPid3, 8192);
    
    // Display page tables
    std::cout << "\n=== PAGE TABLE DETAILS ===\n";
    displayPageTable(testPid1);
    displayPageTable(testPid2);
    displayPageTable(testPid3);
    
    // Display memory segments
    std::cout << "=== MEMORY SEGMENTS ===\n";
    displayMemorySegments(testPid1);
    displayMemorySegments(testPid2);
    displayMemorySegments(testPid3);
    
    // Clean up test processes
    sessions.erase(testPid1);
    sessions.erase(testPid2);
    sessions.erase(testPid3);
    processNames.erase(testPid1);
    processNames.erase(testPid2);
    processNames.erase(testPid3);
    
    std::cout << "=== TESTS COMPLETED ===\n\n";
}

// Helper function to check if a number is a power of 2
bool isPowerOfTwo(int n) {
    return n > 0 && (n & (n - 1)) == 0;
}

// Helper function to validate memory size
bool isValidMemorySize(int size) {
    return size >= MIN_MEMORY_SIZE && size <= MAX_MEMORY_SIZE && isPowerOfTwo(size);
}

// Helper function to parse screen -s command arguments
bool parseScreenCommand(const std::string& cmd, std::string& processName, int& memorySize) {
    // Remove "screen -s " prefix
    std::string args = cmd.substr(10);
    
    // Find the last space to separate process name and memory size
    size_t lastSpace = args.find_last_of(' ');
    
    if (lastSpace == std::string::npos) {
        // No memory size provided, use default
        processName = args;
        memorySize = MEM_PER_PROC; // Default 4096 bytes
        return true;
    }
    
    processName = args.substr(0, lastSpace);
    std::string memorySizeStr = args.substr(lastSpace + 1);
    
    // Trim whitespace
    processName.erase(0, processName.find_first_not_of(" \t"));
    processName.erase(processName.find_last_not_of(" \t") + 1);
    memorySizeStr.erase(0, memorySizeStr.find_first_not_of(" \t"));
    memorySizeStr.erase(memorySizeStr.find_last_not_of(" \t") + 1);
    
    // Check if process name is empty
    if (processName.empty()) {
        return false;
    }
    
    // Try to parse memory size
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

    // Merge adjacent free blocks
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

void snapshotMemory() {
    std::lock_guard<std::mutex> lock(memoryMutex);
    std::ofstream ofs("memory_stamp_" + std::to_string(snapshotCounter++) + ".txt");

    // Timestamp
    auto now = Clock::now();
    std::time_t t = Clock::to_time_t(now);
    std::tm tm = *std::localtime(&t);
    char buffer[64];
    strftime(buffer, sizeof(buffer), "%m/%d/%Y %I:%M:%S%p", &tm);
    ofs << "Timestamp: (" << buffer << ")\n";

    // Count processes and fragmentation
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

    // Reverse print to match "bottom-up" layout
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

void schedulerThread() {
    if (config.scheduler == "rr") {
        // Round Robin Scheduling
        std::queue<int> readyQueue;

        for (int pid = 1; pid <= NUM_PROCESSES; ++pid) {
            readyQueue.push(pid);
            Session s;
            s.start = Clock::now();
            s.finished = false;
            s.memorySize = MEM_PER_PROC; // Default for scheduler-test
            sessions[pid] = std::move(s);
            processNames[pid] = std::string("screen_") + (pid < 10 ? "0" : "") + std::to_string(pid);
            
            // **NEW: Create memory layout for each process**
            createProcessMemoryLayout(pid, MEM_PER_PROC);
        }

        int currentCore = 0;

        while (!readyQueue.empty()) {
            // === Quantum Cycle Start ===

            int pid = readyQueue.front();
            readyQueue.pop();

            bool allocated = allocateMemory(pid);
            if (!allocated) {
                // If memory allocation fails, still simulate quantum cycle
                readyQueue.push(pid);
                std::this_thread::sleep_for(std::chrono::milliseconds(config.quantum_cycles));
                snapshotMemory(); // ✅ Snapshot still required this cycle
                continue;
            }

            // Allocate CPU core and push process
            {
                std::lock_guard<std::mutex> lock(coreMutexes[currentCore]);
                coreQueues[currentCore].push(pid);
            }

            coreCVs[currentCore].notify_one();
            std::this_thread::sleep_for(std::chrono::milliseconds(config.quantum_cycles));
            snapshotMemory(); // ✅ Snapshot for this quantum cycle

            // Requeue if not yet done
            if (!sessions[pid].finished) {
                readyQueue.push(pid);
            } else {
                freeMemory(pid);
            }

            currentCore = (currentCore + 1) % config.num_cpu;

            // === Quantum Cycle End ===
        }

        stopScheduler = true;
        for (int i = 0; i < config.num_cpu; ++i)
            coreCVs[i].notify_all();

    } else {
        // Default: Static assignment (non-RR)
        for (int pid = 1; pid <= NUM_PROCESSES; ++pid) {
            int assignedCore = (pid - 1) % config.num_cpu;
            {
                std::lock_guard<std::mutex> lock(coreMutexes[assignedCore]);
                coreQueues[assignedCore].push(pid);

                Session s;
                s.start = Clock::now();
                s.finished = false;
                s.memorySize = MEM_PER_PROC; // Default for scheduler-test
                sessions[pid] = std::move(s);
                processNames[pid] = std::string("screen_") + (pid < 10 ? "0" : "") + std::to_string(pid);
                
                // **NEW: Create memory layout for each process**
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

std::string trim(const std::string &s) {
    auto l = s.find_first_not_of(" \t\r\n");
    if (l == std::string::npos) return "";
    auto r = s.find_last_not_of(" \t\r\n");
    return s.substr(l, r - l + 1);
}

int main() {
    bool initialized = false;
    std::string line;
    std::thread scheduler;
    std::vector<std::thread> workers;

    int next_pid = 1; // Start at 1 for screen_01
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
                // printConfig(config);
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
        // **NEW COMMAND: Test page table creation**
        else if (cmd == "test-pagetable") {
            testPageTableCreation();
        }
        // **NEW COMMAND: Display page table for a specific process**
        else if (cmd.rfind("pagetable ", 0) == 0) {
            try {
                int pid = std::stoi(cmd.substr(10));
                displayPageTable(pid);
            } catch (const std::exception& e) {
                std::cout << "Error: Invalid process ID. Usage: pagetable <pid>\n";
            }
        }
        // **NEW COMMAND: Display memory segments for a specific process**
        else if (cmd.rfind("segments ", 0) == 0) {
            try {
                int pid = std::stoi(cmd.substr(9));
                displayMemorySegments(pid);
            } catch (const std::exception& e) {
                std::cout << "Error: Invalid process ID. Usage: segments <pid>\n";
            }
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
            processNames[pid] = pname; // Store the user-supplied name
            int assignedCore = round_robin_core++ % config.num_cpu;
            {
                std::lock_guard<std::mutex> lock(coreMutexes[assignedCore]);
                coreQueues[assignedCore].push(pid);
                Session s;
                s.start = Clock::now();
                s.finished = false;
                s.memorySize = memorySize; // Store the specified memory size
                sessions[pid] = std::move(s);
                
                // **NEW: Create memory layout for the process**
                createProcessMemoryLayout(pid, memorySize);
            }
            coreCVs[assignedCore].notify_one();

            std::cout << "Process '" << pname << "' created with " << memorySize << " bytes of memory.\n";

            // Enter process screen
            while (true) {
                clearScreen();
                std::cout << "Process name: " << processNames[pid] << "\n";
                std::cout << "ID: " << pid << "\n";
                std::cout << "Memory size: " << sessions[pid].memorySize << " bytes\n";
                
                // **NEW: Display page table info**
                if (sessions[pid].memoryLayout) {
                    std::cout << "Pages needed: " << sessions[pid].memoryLayout->pageTable.numPages << "\n";
                }
                
                std::cout << "Logs:\n";

                // Print logs from file
                std::string fname = std::string("screen_") + (pid < 10 ? "0" : "") + std::to_string(pid) + ".txt";
                std::ifstream ifs(fname);
                std::string logline;
                int log_count = 0;
                while (std::getline(ifs, logline)) {
                    std::cout << logline << "\n";
                    log_count++;
                }
                ifs.close();

                // Show dummy instruction info
                int current_line = log_count;
                int total_lines = PRINTS_PER_PROCESS;
                std::cout << "\nCurrent instruction line: " << current_line << "\n";
                std::cout << "Lines of code: " << total_lines << "\n";

                // If finished, print Finished!
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
                // **NEW COMMAND: Show page table within process screen**
                else if (proc_cmd == "pagetable") {
                    displayPageTable(pid);
                    std::cout << "Press Enter to continue...";
                    std::cin.get();
                }
                // **NEW COMMAND: Show memory segments within process screen**
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
        // **NEW COMMAND: Help command to show available commands**
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