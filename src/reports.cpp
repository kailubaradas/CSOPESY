#include "reports.h"
#include "globals.h"
#include "config.h"
#include "utils.h"
#include <iostream>
#include <fstream>
#include <iomanip>

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

void generateUtilizationReport() {
    std::ofstream ofs("csopesy-log.txt");

    if (!ofs) {
        std::cerr << "Failed to write report to csopesy-log.txt\n";
        return;
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
    ofs << "Total CPU Active Ticks: " << total_cpu_active_ticks << "\n";
    ofs << "Total CPU Idle Ticks: " << total_cpu_idle_ticks << "\n\n";
    ofs << "------------------------------------------\n";
    ofs << "Running processes:\n";
    for (const auto& entry : sessions) {
        if (!entry.second.finished) {
            int pid = entry.first;
            const Session& s = entry.second;
            std::string name = processNames[pid];
            int pages = s.memoryLayout ? s.memoryLayout->pageTable.numPages : 0;
            ofs << name << "  (" << formatTimestamp(s.start) << ")"
                << "   Core: " << (pid - 1) % config.num_cpu
                << "   Active Ticks: " << s.cpu_active_ticks
                << "   Idle Ticks: " << s.cpu_idle_ticks
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
            ofs << name << "  (" << formatTimestamp(s.start) << ")"
                << "   Finished   "
                << "   Active Ticks: " << s.cpu_active_ticks
                << "   Idle Ticks: " << s.cpu_idle_ticks
                << "   [" << s.memorySize << " bytes, " << pages << " pages]" << "\n";
        }
    }

    ofs << "------------------------------------------\n";
    ofs.close();
    std::cout << "Report generated at C:/csopesy-log.txt!\n";
}
