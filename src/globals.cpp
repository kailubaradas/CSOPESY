#include "globals.h"

std::map<int, Session> sessions;
std::map<int, std::string> processNames;
std::atomic<bool> stopScheduler(false);

std::vector<std::queue<int>> coreQueues;
std::vector<std::mutex> coreMutexes;
std::vector<std::condition_variable> coreCVs;

std::vector<MemoryBlock> memoryBlocks;
std::mutex memoryMutex;
int snapshotCounter = 0;
bool enableSnapshots = false;

int total_cpu_active_ticks = 0;
int total_cpu_idle_ticks = 0;
