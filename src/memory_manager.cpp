#include "memory_manager.h"
#include "structures.h"
#include "config.h"
#include "globals.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>

DemandPagingAllocator demandPagingAllocator;

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

DemandPagingAllocator::DemandPagingAllocator() : pageFaultCount(0), pageReplacementCount(0) {
    physicalFrames.resize(config.num_frames);
    for (int i = 0; i < config.num_frames; ++i) {
        physicalFrames[i] = PhysicalFrame(i);
        freeFrames.push(i);
    }
}

int DemandPagingAllocator::findLRUFrame() {
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

void DemandPagingAllocator::swapPageOut(int frameNumber) {
    PhysicalFrame& frame = physicalFrames[frameNumber];
    
    if (frame.isDirty) {
        std::cout << "[Memory Manager] Swapping out dirty page " << frame.pageNumber 
                  << " of process " << frame.processId << " from frame " << frameNumber << " to backing store.\n";
        std::vector<int> pageData(config.mem_per_frame / sizeof(int), frameNumber); // Simplified data
        backingStore.storePage(frame.processId, frame.pageNumber, pageData);
    } else {
        std::cout << "[Memory Manager] Evicting clean page " << frame.pageNumber 
                  << " of process " << frame.processId << " from frame " << frameNumber << ".\n";
    }
    
    if (sessions.count(frame.processId) && sessions[frame.processId].memoryLayout) {
        auto& pageTable = sessions[frame.processId].memoryLayout->pageTable;
        if (frame.pageNumber < pageTable.numPages) {
            pageTable.pages[frame.pageNumber].isLoaded = false;
            pageTable.pages[frame.pageNumber].physicalFrame = -1;
            pageTable.pages[frame.pageNumber].isDirty = frame.isDirty;
        }
    }
    
    frame.processId = -1;
    frame.pageNumber = -1;
    frame.isOccupied = false;
    frame.isDirty = false;
    
    pageReplacementCount++;
}

int DemandPagingAllocator::swapPageIn(int processId, int pageNumber) {
    int frameNumber = -1;
    
    if (!freeFrames.empty()) {
        frameNumber = freeFrames.front();
        freeFrames.pop();
        fifoQueue.push(frameNumber);
    } else {
        if (fifoQueue.empty()) {
            std::cerr << "Error: No frames to evict in FIFO queue.\n";
            return -1;
        }
        frameNumber = fifoQueue.front();
        fifoQueue.pop();
        swapPageOut(frameNumber);
        fifoQueue.push(frameNumber);
    }
    
    std::cout << "[Memory Manager] Swapping in page " << pageNumber 
              << " of process " << processId << " into frame " << frameNumber << " from backing store.\n";
    std::vector<int> pageData = backingStore.loadPage(processId, pageNumber);
    
    PhysicalFrame& frame = physicalFrames[frameNumber];
    frame.processId = processId;
    frame.pageNumber = pageNumber;
    frame.isOccupied = true;
    frame.isDirty = false;
    frame.lastAccessed = Clock::now();
    
    auto& pageTable = sessions[processId].memoryLayout->pageTable;
    PageEntry& pageEntry = pageTable.pages[pageNumber];
    pageEntry.physicalFrame = frameNumber;
    pageEntry.isLoaded = true;
    pageEntry.isAccessed = true;
    pageEntry.isDirty = false;

    return frameNumber;
}

bool DemandPagingAllocator::handlePageFault(int processId, int pageNumber) {
    std::lock_guard<std::mutex> lock(framesMutex);
    
    pageFaultCount++;
    std::cout << "[Memory Manager] Page fault for process " << processId 
              << ", page " << pageNumber << ". Total faults: " << pageFaultCount << "\n";
    
    if (sessions.find(processId) == sessions.end() || !sessions[processId].memoryLayout) {
        std::cerr << "Error: Process " << processId << " not found for page fault handling.\n";
        return false;
    }
    
    auto& pageTable = sessions[processId].memoryLayout->pageTable;
    if (pageNumber >= pageTable.numPages) {
        std::cerr << "Error: Invalid page number " << pageNumber << " for process " << processId << ".\n";
        return false;
    }
    
    int frameNumber = swapPageIn(processId, pageNumber);
    
    return frameNumber != -1;
}

bool DemandPagingAllocator::accessMemory(int processId, int virtualAddress, bool isWrite) {
    if (sessions.find(processId) == sessions.end() || 
        !sessions[processId].memoryLayout) {
        return false;
    }
    
    auto& pageTable = sessions[processId].memoryLayout->pageTable;
    int pageNumber = virtualAddress / config.mem_per_frame;
    
    if (pageNumber >= pageTable.numPages) {
        return false;
    }
    
    PageEntry& pageEntry = pageTable.pages[pageNumber];
    
    if (!pageEntry.isLoaded) {
        if (!handlePageFault(processId, pageNumber)) {
            return false;
        }
    }
    
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

void DemandPagingAllocator::freeProcessPages(int processId) {
    std::lock_guard<std::mutex> lock(framesMutex);
    
    std::queue<int> newFifoQueue;
    while(!fifoQueue.empty()){
        int frameIdx = fifoQueue.front();
        fifoQueue.pop();
        if(physicalFrames[frameIdx].processId != processId){
            newFifoQueue.push(frameIdx);
        }
    }
    fifoQueue = newFifoQueue;

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

void DemandPagingAllocator::getStatistics(int& pageFaults, int& pageReplacements, int& framesUsed) {
    std::lock_guard<std::mutex> lock(framesMutex);
    pageFaults = pageFaultCount;
    pageReplacements = pageReplacementCount;
    framesUsed = config.num_frames - freeFrames.size();
}

void DemandPagingAllocator::displayFrameTable() {
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
            
            auto time_t_val = Clock::to_time_t(frame.lastAccessed);
            std::tm* tm = std::localtime(&time_t_val);
            std::cout << std::setfill('0')
                    << std::setw(2) << tm->tm_hour << ':'
                    << std::setw(2) << tm->tm_min << ':'
                    << std::setw(2) << tm->tm_sec;
            std::cout << std::setfill(' ');
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

bool readMemory(int processId, int virtualAddress, int& value) {
    if (demandPagingAllocator.accessMemory(processId, virtualAddress, false)) {
        value = virtualAddress % 1000;
        return true;
    }
    return false;
}

bool writeMemory(int processId, int virtualAddress, int value) {
    return demandPagingAllocator.accessMemory(processId, virtualAddress, true);
}

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
