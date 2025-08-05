#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <iomanip>
#include "src/config.h"
#include "src/utils.h"
#include "src/globals.h"
#include "src/memory_manager.h"
#include "src/instruction.h"
#include "src/process.h"
#include "src/scheduler.h"
#include "src/reports.h"
#include <fstream>

void displayProcessSmi() {

    auto now = Clock::now();
    std::time_t t = Clock::to_time_t(now);
    std::tm* localtm = std::localtime(&t);
    char datetime[100];
    std::strftime(datetime, sizeof(datetime), "%a %b %d %H:%M:%S %Y", localtm);
    
    int totalProcesses = 0;
    int runningProcesses = 0;
    int finishedProcesses = 0;
    int totalMemoryUsed = 0;
    
    for (const auto& entry : sessions) {
        totalProcesses++;
        if (entry.second.finished) {
            finishedProcesses++;
        } else {
            runningProcesses++;
            totalMemoryUsed += entry.second.memorySize;
        }
    }
    
    int memoryPercentage = (totalMemoryUsed * 100) / config.max_memory_size;
    
    int cpuUtil = (runningProcesses > 0) ? std::min(100, (runningProcesses * 100) / config.num_cpu) : 0;
    
    std::cout << datetime << "\n";
    std::cout << "+-----------------------------------------------------------------------------------------+\n";
    std::cout << "| CSOPESY-SMI 1.0                   Driver Version: 1.0           CSOPESY Version: 0.1    |\n";
    std::cout << "|-----------------------------------------+------------------------+----------------------+\n";
    std::cout << "| CPU  Name                  Architecture | Cores Available        | Process Scheduling   |\n";
    std::cout << "| Util Processes   Active    Memory Usage |           Memory-Total | Scheduler     Mode   |\n";
    std::cout << "|                                         |                        |                      |\n";
    std::cout << "|=========================================+========================+======================|\n";
    std::cout << "|   0  CSOPESY Virtual CPU        x86_64  |   " << std::setw(2) << config.num_cpu << " cores            |                  N/A |\n";
    std::cout << "| " << std::setw(3) << cpuUtil << "%  " << std::setw(3) << totalProcesses << " procs  " 
              << std::setw(3) << runningProcesses << " active  " 
              << std::setw(5) << (totalMemoryUsed/1024) << "KB / " << std::setw(5) << (config.max_memory_size/1024) << "KB |    "
              << std::setw(5) << (totalMemoryUsed/1024) << "KB / " << std::setw(7) << (config.max_memory_size/1024) << "KB | "
              << std::setw(5) << config.scheduler << "        Default |\n";
    std::cout << "|                                         |                        |                  N/A |\n";
    std::cout << "+-----------------------------------------+------------------------+----------------------+\n";
    
    int pageFaults, pageReplacements, framesUsed;
    demandPagingAllocator.getStatistics(pageFaults, pageReplacements, framesUsed);
    
    std::cout << "\n";
    std::cout << "+-----------------------------------------------------------------------------------------+\n";
    std::cout << "| Processes:                                                                              |\n";
    std::cout << "|  CPU   Core  PID     Status   Process name                              Memory Usage   |\n";
    std::cout << "|                                                                          (KB)           |\n";
    std::cout << "|=========================================================================================|\n";
    
    for (const auto& entry : sessions) {
        int pid = entry.first;
        const auto& session = entry.second;
        std::string processName = processNames[pid];
        std::string status = session.finished ? "Done" : "Run ";
        int assignedCore = (pid - 1) % config.num_cpu;
        int memoryKB = session.memorySize / 1024;
        
        if (processName.length() > 30) {
            processName = "..." + processName.substr(processName.length() - 27);
        }
        
        std::cout << "|   0  " 
                  << std::setw(4) << assignedCore
                  << std::setw(6) << pid
                  << std::setw(9) << status
                  << std::setw(3) << " "
                  << std::left << std::setw(33) << processName
                  << std::right << std::setw(17) << memoryKB
                  << "   |" << std::endl;
    }
    
    std::cout << "+-----------------------------------------------------------------------------------------+\n";
    
    std::cout << "\n";
    std::cout << "Memory Statistics:\n";
    std::cout << "  Total Memory: " << config.max_memory_size << " bytes (" << config.max_memory_size/1024 << " KB)\n";
    std::cout << "  Used Memory: " << totalMemoryUsed << " bytes (" << totalMemoryUsed/1024 << " KB)\n";
    std::cout << "  Free Memory: " << (config.max_memory_size - totalMemoryUsed) << " bytes (" 
              << (config.max_memory_size - totalMemoryUsed)/1024 << " KB)\n";
    std::cout << "  Page Faults: " << pageFaults << "\n";
    std::cout << "  Page Replacements: " << pageReplacements << "\n";
    std::cout << "  Frames Used: " << framesUsed << "/" << config.num_frames << "\n";
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
                std::cout << "  - Between " << config.min_memory_size << " and " << config.max_memory_size << " bytes\n";
                std::cout << "  - A power of 2 (e.g., 64, 128, 256, 512, 1024, 2048, 4096, ...)\n";
                continue;
            }
            
            std::vector<Instruction> instructions;
            if (!parseInstructions(instructionString, instructions)) {
                std::cout << "Error: Failed to parse instructions.\n";
                continue;
            }
            
            if (instructions.size() < 1 || instructions.size() > 50) {
                std::cout << "Error: Number of instructions must be between 1 and 50. Found: " 
                          << instructions.size() << "\n";
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
                s.instructions = instructions;
                sessions[pid] = std::move(s);
                
                createProcessMemoryLayout(pid, memorySize);
            }
            coreCVs[assignedCore].notify_one();

            std::cout << "Process '" << pname << "' created successfully!\n";
            std::cout << "  Memory size: " << memorySize << " bytes\n";
            std::cout << "  Instructions: " << instructions.size() << " parsed successfully\n";
            std::cout << "  Assigned to core: " << assignedCore << "\n\n";
            
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
                              << " @ " << formatTimestamp(entry.second.start)
                              << " [" << entry.second.memorySize << " bytes, " << pages << " pages]\n";
                }
            }
            std::cout << "Running:\n";
            for (const auto& entry : sessions) {
                if (!entry.second.finished) {
                    int pages = entry.second.memoryLayout ? entry.second.memoryLayout->pageTable.numPages : 0;
                    std::cout << "  " << processNames[entry.first]
                              << " (screen_" << (entry.first < 10 ? "0" : "") << entry.first << ")"
                              << " @ " << formatTimestamp(entry.second.start)
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
            generateUtilizationReport();
        } else if (cmd == "report-mem") {
            generateMemoryReport();
        } else if (cmd == "vmstat") {
            std::cout << "\n===== VMSTAT =====\n";
            std::cout << "Total CPU Active Ticks: " << total_cpu_active_ticks << "\n";
            std::cout << "Total CPU Idle Ticks: " << total_cpu_idle_ticks << "\n";
            std::cout << "\nPer-process CPU Ticks:\n";
            for (const auto& entry : sessions) {
                int pid = entry.first;
                const Session& s = entry.second;
                std::cout << "PID " << pid << " (" << processNames[pid] << ")"
                          << ": Active Ticks = " << s.cpu_active_ticks
                          << ", Idle Ticks = " << s.cpu_idle_ticks
                          << (s.finished ? " [Finished]" : " [Running]")
                          << "\n";
            }
            std::cout << "===================\n\n";
        }
        else if (cmd == "test-pagetable") {
            int testPid = next_pid++;
            processNames[testPid] = "test_process";
            Session s;
            s.start = Clock::now();
            s.finished = false;
            s.memorySize = 1024;
            sessions[testPid] = std::move(s);
            
            createProcessMemoryLayout(testPid, 1024);
            
            std::cout << "\nSimulating memory accesses...\n";
            writeMemory(testPid, 0x0, 42);
            writeMemory(testPid, 0x10, 123);
            writeMemory(testPid, 0x20, 456);
            int value;
            readMemory(testPid, 0x0, value);
            
            std::cout << "\nPage Table after memory accesses:\n";
            displayPageTable(testPid);
        }
        else if (cmd == "frametable") {
            demandPagingAllocator.displayFrameTable();
        } 
        else if (cmd == "process-smi") {
            displayProcessSmi();
        }
        else if (cmd.rfind("screen -r ", 0) == 0) {
            std::string targetStr = trim(cmd.substr(9));
            int targetPid = -1;
            
            // Try to interpret as PID first
            try {
                targetPid = std::stoi(targetStr);
            } catch (const std::exception&) {
                // If not a number, search by process name
                for (const auto& pair : processNames) {
                    if (pair.second == targetStr) {
                        targetPid = pair.first;
                        break;
                    }
                }
            }
            
            if (targetPid == -1 || sessions.find(targetPid) == sessions.end()) {
                std::cout << "Error: No such process found.\n";
                std::cout << "Usage: screen -r <pid|name>\n";
                continue;
            }
            
            // Now we have a valid PID, show the process information and output
            std::cout << "Process name: " << processNames[targetPid] << "\n";
            std::cout << "ID: " << targetPid << "\n";
            std::cout << "Memory size: " << sessions[targetPid].memorySize << " bytes\n";
            
            if (sessions[targetPid].memoryLayout) {
                std::cout << "Pages needed: " << sessions[targetPid].memoryLayout->pageTable.numPages << "\n";
            }
            
            std::cout << "\nProcess output:\n";
            std::string fname = std::string("screen_") + (targetPid < 10 ? "0" : "") + std::to_string(targetPid) + ".txt";
            std::ifstream ifs(fname);
            std::string line;
            while (std::getline(ifs, line)) {
                std::cout << line << "\n";
            }
            ifs.close();
        }
        else if (cmd == "help") {
            std::cout << "\nAvailable Commands:\n";
            std::cout << "  initialize                    - Initialize the system\n";
            std::cout << "  scheduler-test               - Start the scheduler test\n";
            std::cout << "  scheduler-stop               - Stop the scheduler\n";
            std::cout << "  screen -s <name> [mem_size]  - Create a new process\n";
            std::cout << "  screen -c <name> <mem> \"ins\" - Create a new process with instructions\n";
            std::cout << "  screen -ls                   - List all processes\n";
            std::cout << "  pagetable <pid>              - Show page table for process\n";
            std::cout << "  segments <pid>               - Show memory segments for process\n";
            std::cout << "  test-pagetable               - Run page table creation tests\n";
            std::cout << "  frametable                   - Display physical frame table\n";
            std::cout << "  report-util                  - Generate utilization report\n";
            std::cout << "  report-mem                   - Generate memory report\n";
            std::cout << "  vmstat                       - Show CPU tick statistics (active/idle, per-process)\n";
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
