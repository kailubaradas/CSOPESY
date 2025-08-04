#include "structures.h"
#include "config.h"

PageTable::PageTable(int pages_needed) : numPages(pages_needed) {
    pages.resize(pages_needed);
}

ProcessMemoryLayout::ProcessMemoryLayout(int memSize) : pageTable(0), totalMemorySize(memSize) {
    int pagesNeeded = (memSize + config.mem_per_frame - 1) / config.mem_per_frame;
    pageTable = PageTable(pagesNeeded);
    initializeSegments();
}

void ProcessMemoryLayout::initializeSegments() {
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

PhysicalFrame::PhysicalFrame() : frameNumber(-1), processId(-1), pageNumber(-1),
                                 isOccupied(false), isDirty(false), lastAccessed(Clock::now()) {}

PhysicalFrame::PhysicalFrame(int frameNum) : frameNumber(frameNum), processId(-1), pageNumber(-1),
                                             isOccupied(false), isDirty(false), lastAccessed(Clock::now()) {}

BackingStore::BackingStore() {
    processPages.resize(1000);  // Support up to 1000 processes
    for (auto& pages : processPages) {
        pages.resize(1000, 0);  // Each process can have up to 1000 pages
    }
}

void BackingStore::storePage(int processId, int pageNumber, const std::vector<int>& pageData) {
    std::lock_guard<std::mutex> lock(backingStoreMutex);
    // Implementation of writePageToBackingStore is needed here
}

std::vector<int> BackingStore::loadPage(int processId, int pageNumber) {
    std::lock_guard<std::mutex> lock(backingStoreMutex);
    // Implementation of readPageFromBackingStore is needed here
    return {};
}
