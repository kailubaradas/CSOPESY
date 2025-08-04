#include "config.h"
#include <iostream>
#include <fstream>
#include <string>

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
        else if (key == "num-processes") file >> config.num_processes;
        else if (key == "prints-per-process") file >> config.prints_per_process;
        else if (key == "max-overall-mem") file >> config.max_memory_size;
        else if (key == "mem-per-frame") file >> config.mem_per_frame;
        else if (key == "mem-per-proc") file >> config.mem_per_proc;
        else if (key == "min-memory-size") file >> config.min_memory_size;
        else if (key == "max-memory-size") file >> config.max_memory_size;
        else if (key == "num-frames") file >> config.num_frames;
        else if (key == "backing-store-size") file >> config.backing_store_size;
        else {
            std::string garbage;
            file >> garbage;
            std::cerr << "Warning: Unknown config key '" << key << "'. Skipping.\n";
        }
    }
    config.num_frames = config.max_memory_size / config.mem_per_frame;
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
