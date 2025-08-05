#ifndef STRUCTURES_H
#define STRUCTURES_H

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <queue>
#include <chrono>
#include <memory>
#include <atomic>
#include <mutex>

using Clock = std::chrono::system_clock;

struct PageEntry {
    int physicalFrame;
    bool isLoaded;
    bool isDirty;
    bool isAccessed;

    PageEntry() : physicalFrame(-1), isLoaded(false), isDirty(false), isAccessed(false) {}
};

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

struct PageTable {
    std::vector<PageEntry> pages;
    int numPages;

    PageTable(int pages_needed = 0);
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

    ProcessMemoryLayout(int memSize);

private:
    void initializeSegments();
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
    int cpu_active_ticks = 0;
    int cpu_idle_ticks = 0;

    Session() = default;
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = default;
    Session& operator=(Session&&) = default;
};

struct MemoryBlock {
    int start;
    int end;
    int pid;
};

struct PhysicalFrame {
    int frameNumber;
    int processId;
    int pageNumber;
    bool isOccupied;
    bool isDirty;
    Clock::time_point lastAccessed;

    PhysicalFrame();
    PhysicalFrame(int frameNum);
};

struct BackingStore {
    std::vector<std::vector<int>> processPages;
    std::mutex backingStoreMutex;

    BackingStore();
    void storePage(int processId, int pageNumber, const std::vector<int>& pageData);
    std::vector<int> loadPage(int processId, int pageNumber);
};

#endif // STRUCTURES_H
