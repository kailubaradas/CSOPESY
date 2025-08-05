#include "scheduler.h"
#include "globals.h"
#include "config.h"
#include "instruction.h"
#include "memory_manager.h"
#include "utils.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <fstream>

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
            } else {
                // Core is idle for this tick
                total_cpu_idle_ticks++;
                continue;
            }
        }

        // Core is active for this tick
        total_cpu_active_ticks++;
        {
            std::lock_guard<std::mutex> lock(sessionMutex);
            sessions[pid].cpu_active_ticks++;
        }

        if (pid == -1) continue;

        if (!sessions[pid].instructions.empty()) {
            {
                std::lock_guard<std::mutex> lock(sessionMutex);
                std::cout << "\nExecuting custom instructions for process " << pid 
                          << " (" << processNames[pid] << "):\n";
            }
            
            for (size_t i = 0; i < sessions[pid].instructions.size(); ++i) {
                const auto& instruction = sessions[pid].instructions[i];
                std::cout << "Instruction " << (i + 1) << "/" << sessions[pid].instructions.size() << ": ";
                
                if (!executeInstructionWithPaging(pid, instruction)) {
                    std::cerr << "Failed to execute instruction " << (i + 1) << " for process " << pid << "\n";
                    break;
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            {
                std::lock_guard<std::mutex> lock(sessionMutex);
                std::cout << "Process " << pid << " (" << processNames[pid] << ") completed all instructions.\n\n";
            }
        } else {
            std::string fname = std::string("screen_") + (pid < 10 ? "0" : "") + std::to_string(pid) + ".txt";
            std::ofstream ofs(fname.c_str(), std::ios::trunc);
            for (int i = 0; i < config.prints_per_process; ++i) {
                auto now = Clock::now();
                {
                    std::lock_guard<std::mutex> lock(sessionMutex);
                    ofs << "(" << formatTimestamp(now) << ") Core:" << coreId
                        << " \"Hello world from " << (processNames.count(pid) ? processNames[pid] : (std::string("screen_") + (pid < 10 ? "0" : "") + std::to_string(pid))) << "!\"\n";
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            ofs.close();
        }

        {
            std::lock_guard<std::mutex> lock(sessionMutex);
            sessions[pid].finished = true;
        }
    }
}

void schedulerThread() {
    if (config.scheduler == "rr") {
        std::queue<int> readyQueue;

        for (int pid = 1; pid <= config.num_processes; ++pid) {
            readyQueue.push(pid);
            {
                std::lock_guard<std::mutex> lock(sessionMutex);
                Session s;
                s.start = Clock::now();
                s.finished = false;
                s.memorySize = config.mem_per_proc;
                sessions[pid] = std::move(s);
                processNames[pid] = std::string("screen_") + (pid < 10 ? "0" : "") + std::to_string(pid);

                createProcessMemoryLayout(pid, config.mem_per_proc);
            }
        }

        int currentCore = 0;

        while (!readyQueue.empty()) {

            int pid = readyQueue.front();
            readyQueue.pop();

            // Memory allocation logic needs to be here

            {
                std::lock_guard<std::mutex> lock(coreMutexes[currentCore]);
                coreQueues[currentCore].push(pid);
            }

            coreCVs[currentCore].notify_one();
            std::this_thread::sleep_for(std::chrono::milliseconds(config.quantum_cycles));
            // snapshotMemory(); // This function needs to be available

            {
                std::lock_guard<std::mutex> lock(sessionMutex);
                if (!sessions[pid].finished) {
                    readyQueue.push(pid);
                } else {
                    // freeMemory(pid); // This function needs to be available
                    demandPagingAllocator.freeProcessPages(pid);
                }
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

                {
                    std::lock_guard<std::mutex> lock(sessionMutex);
                    Session s;
                    s.start = Clock::now();
                    s.finished = false;
                    s.memorySize = config.mem_per_proc;
                    sessions[pid] = std::move(s);
                    processNames[pid] = std::string("screen_") + (pid < 10 ? "0" : "") + std::to_string(pid);
                    
                    createProcessMemoryLayout(pid, config.mem_per_proc);
                }
            }
            coreCVs[assignedCore].notify_one();
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        stopScheduler = true;
        for (int i = 0; i < config.num_cpu; ++i)
            coreCVs[i].notify_all();
    }
}
