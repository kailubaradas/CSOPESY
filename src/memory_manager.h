#ifndef MEMORY_MANAGER_H
#define MEMORY_MANAGER_H

#include "structures.h"
#include <vector>
#include <queue>
#include <mutex>

class DemandPagingAllocator {
private:
    std::vector<PhysicalFrame> physicalFrames;
    std::queue<int> freeFrames;
    std::queue<int> fifoQueue;
    BackingStore backingStore;
    std::mutex framesMutex;
    int pageFaultCount;
    int pageReplacementCount;

    int findLRUFrame();
    void swapPageOut(int frameNumber);
    int swapPageIn(int processId, int pageNumber);

public:
    DemandPagingAllocator();
    bool handlePageFault(int processId, int pageNumber);
    bool accessMemory(int processId, int virtualAddress, bool isWrite = false);
    void freeProcessPages(int processId);
    void getStatistics(int& pageFaults, int& pageReplacements, int& framesUsed);
    void displayFrameTable();
};

extern DemandPagingAllocator demandPagingAllocator;

void writePageToBackingStore(int processId, int pageNumber, const std::vector<int>& pageData);
std::vector<int> readPageFromBackingStore(int processId, int pageNumber);
bool readMemory(int processId, int virtualAddress, int& value);
bool writeMemory(int processId, int virtualAddress, int value);
void createProcessMemoryLayout(int pid, int memorySize);
void displayPageTable(int pid);
void displayMemorySegments(int pid);

#endif // MEMORY_MANAGER_H
