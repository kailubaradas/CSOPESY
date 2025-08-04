#ifndef GLOBALS_H
#define GLOBALS_H

#include "structures.h"
#include <map>
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

extern std::map<int, Session> sessions;
extern std::map<int, std::string> processNames;
extern std::atomic<bool> stopScheduler;

extern std::vector<std::queue<int>> coreQueues;
extern std::vector<std::mutex> coreMutexes;
extern std::vector<std::condition_variable> coreCVs;

extern std::vector<MemoryBlock> memoryBlocks;
extern std::mutex memoryMutex;
extern int snapshotCounter;
extern bool enableSnapshots;

#endif // GLOBALS_H
