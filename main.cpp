#include <iostream>
#include <string>
#include <map>
#include <queue>
#include <vector>
#include <fstream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <cstdlib>
#include <atomic>

using Clock = std::chrono::system_clock;

struct Session {
    Clock::time_point start;
    bool finished = false;
};

struct Config {
    int num_cpu;
    std::string scheduler;
    int quantum_cycles;
    int batch_process_freq;
    int min_ins;
    int max_ins;
    int delays_per_exec;
};

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
        else {
            std::string garbage;
            file >> garbage;
            std::cerr << "Warning: Unknown config key '" << key << "'. Skipping.\n";
        }
    }

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

const int NUM_PROCESSES = 10;
const int PRINTS_PER_PROCESS = 100;

std::map<int, Session> sessions;
std::map<int, std::string> processNames;
std::atomic<bool> stopScheduler(false);

std::vector<std::queue<int>> coreQueues;
std::vector<std::mutex> coreMutexes;
std::vector<std::condition_variable> coreCVs;

std::string formatTimestamp(const Clock::time_point &tp) {
    std::time_t t = Clock::to_time_t(tp);
    std::tm tm = *std::localtime(&t);
    std::ostringstream oss;
    oss << std::setfill('0')
        << std::setw(2) << (tm.tm_mon + 1) << '/'
        << std::setw(2) << tm.tm_mday << '/'
        << (tm.tm_year + 1900) << ", "
        << std::setw(2) << ((tm.tm_hour % 12) ? (tm.tm_hour % 12) : 12)
        << ':' << std::setw(2) << tm.tm_min
        << ':' << std::setw(2) << tm.tm_sec
        << ' ' << (tm.tm_hour >= 12 ? "PM" : "AM");
    return oss.str();
}

void cpuWorker(int coreId) {
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
            }
        }

        if (pid == -1) continue;

        std::string fname = std::string("screen_") + (pid < 10 ? "0" : "") + std::to_string(pid) + ".txt";
        std::ofstream ofs(fname.c_str(), std::ios::trunc);
        for (int i = 0; i < PRINTS_PER_PROCESS; ++i) {
            auto now = Clock::now();
            ofs << "(" << formatTimestamp(now) << ") Core:" << coreId
                << " \"Hello world from " << (processNames.count(pid) ? processNames[pid] : (std::string("screen_") + (pid < 10 ? "0" : "") + std::to_string(pid))) << "!\"\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        ofs.close();

        sessions[pid].finished = true;
    }
}

void schedulerThread() {
    if (config.scheduler == "rr") {
        // Round Robin Scheduling
        std::queue<int> readyQueue;
        for (int pid = 1; pid <= NUM_PROCESSES; ++pid) {
            readyQueue.push(pid);
            Session s;
            s.start = Clock::now();
            s.finished = false;
            sessions[pid] = s;
            processNames[pid] = std::string("screen_") + (pid < 10 ? "0" : "") + std::to_string(pid);
        }
        int currentCore = 0;
        while (!readyQueue.empty()) {
            int pid = readyQueue.front();
            readyQueue.pop();
            {
                std::lock_guard<std::mutex> lock(coreMutexes[currentCore]);
                coreQueues[currentCore].push(pid);
            }
            coreCVs[currentCore].notify_one();
            std::this_thread::sleep_for(std::chrono::milliseconds(config.quantum_cycles));
            // If not finished, requeue
            if (!sessions[pid].finished) {
                readyQueue.push(pid);
            }
            currentCore = (currentCore + 1) % config.num_cpu;
        }
        stopScheduler = true;
        for (int i = 0; i < config.num_cpu; ++i)
            coreCVs[i].notify_all();
    } else {
        // Default: static assignment
        for (int pid = 1; pid <= NUM_PROCESSES; ++pid) {
            int assignedCore = (pid - 1) % config.num_cpu;
            {
                std::lock_guard<std::mutex> lock(coreMutexes[assignedCore]);
                coreQueues[assignedCore].push(pid);
                Session s;
                s.start = Clock::now();
                s.finished = false;
                sessions[pid] = s;
                processNames[pid] = std::string("screen_") + (pid < 10 ? "0" : "") + std::to_string(pid);
            }
            coreCVs[assignedCore].notify_one();
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        stopScheduler = true;
        for (int i = 0; i < config.num_cpu; ++i)
            coreCVs[i].notify_all();
    }
}

std::string fmtTime(const Clock::time_point &tp) {
    std::time_t t = Clock::to_time_t(tp);
    char buf[64];
    strftime(buf, sizeof(buf), "%m/%d/%Y, %I:%M:%S %p", localtime(&t));
    return buf;
}

void printHeader() {
    std::cout<<"||======================================||\n"
             <<"||            CSOPESY CLI v0.1          ||\n"
             <<"||======================================||\n";
}

void clearScreen() {
#if defined(_WIN32) || defined(_WIN64)
    std::system("cls");
#else
    std::system("clear");
#endif
}

std::string trim(const std::string &s) {
    auto l = s.find_first_not_of(" \t\r\n");
    if (l == std::string::npos) return "";
    auto r = s.find_last_not_of(" \t\r\n");
    return s.substr(l, r - l + 1);
}

int main() {
    bool initialized = false;
    std::string line;
    std::thread scheduler;
    std::vector<std::thread> workers;

    int next_pid = 1; // Start at 1 for screen_01
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
                // printConfig(config);
            } else {
                std::cout << "Run 'initialize' first.\n";
            }
            continue;
        }

        if (cmd == "scheduler-test") {
            stopScheduler = false;
            sessions.clear();
            processNames.clear();
            for (int i = 0; i < config.num_cpu; ++i) {
                std::queue<int> empty;
                std::swap(coreQueues[i], empty);
            }

            for (int i = 0; i < config.num_cpu; ++i)
                workers.emplace_back(cpuWorker, i);
            scheduler = std::thread(schedulerThread);
            std::cout << "Started scheduling. Run 'screen -ls' every 1-2s.\n";
        }
        else if (cmd.rfind("screen -s ", 0) == 0) {
            std::string pname = trim(cmd.substr(10));
            if (pname.empty()) {
                std::cout << "Usage: screen -s <process name>\n";
                continue;
            }
            int pid = next_pid++;
            processNames[pid] = pname; // Store the user-supplied name
            int assignedCore = round_robin_core++ % config.num_cpu;
            {
                std::lock_guard<std::mutex> lock(coreMutexes[assignedCore]);
                coreQueues[assignedCore].push(pid);
                Session s;
                s.start = Clock::now();
                s.finished = false;
                sessions[pid] = s;
            }
            coreCVs[assignedCore].notify_one();

            // Enter process screen
            while (true) {
                clearScreen();
                std::cout << "Process name: " << processNames[pid] << "\n";
                std::cout << "ID: " << pid << "\n";
                std::cout << "Logs:\n";

                // Print logs from file
                std::string fname = std::string("screen_") + (pid < 10 ? "0" : "") + std::to_string(pid) + ".txt";
                std::ifstream ifs(fname);
                std::string logline;
                int log_count = 0;
                while (std::getline(ifs, logline)) {
                    std::cout << logline << "\n";
                    log_count++;
                }
                ifs.close();

                // Show dummy instruction info
                int current_line = log_count;
                int total_lines = PRINTS_PER_PROCESS;
                std::cout << "\nCurrent instruction line: " << current_line << "\n";
                std::cout << "Lines of code: " << total_lines << "\n";

                // If finished, print Finished!
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
                } else {
                    std::cout << "Unknown command: '" << proc_cmd << "'\n";
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
                if (entry.second.finished)
                    std::cout << "  " << processNames[entry.first]
                              << " (screen_" << (entry.first < 10 ? "0" : "") << entry.first << ")"
                              << " @ " << fmtTime(entry.second.start) << "\n";
            }
            std::cout << "Running:\n";
            for (const auto& entry : sessions) {
                if (!entry.second.finished)
                    std::cout << "  " << processNames[entry.first]
                              << " (screen_" << (entry.first < 10 ? "0" : "") << entry.first << ")"
                              << " @ " << fmtTime(entry.second.start) << "\n";
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

            std::ofstream ofs("csopesy-log.txt");

            if (!ofs) {
                std::cerr << "Failed to write report to csopesy-log.txt\n";
                continue;
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
            ofs << "------------------------------------------\n";
            ofs << "Running processes:\n";
            for (const auto& entry : sessions) {
                if (!entry.second.finished) {
                    int pid = entry.first;
                    const Session& s = entry.second;
                    std::string name = processNames[pid];
                    ofs << name << "  (" << fmtTime(s.start) << ")"
                        << "   Core: " << (pid - 1) % config.num_cpu
                        << "   " << 0 << " / ????" << "\n"; // TEMPORARY
                }
            }

            ofs << "\nFinished processes:\n";
            for (const auto& entry : sessions) {
                if (entry.second.finished) {
                    int pid = entry.first;
                    const Session& s = entry.second;
                    std::string name = processNames[pid];
                    ofs << name << "  (" << fmtTime(s.start) << ")"
                        << "   Finished   "
                        << "???? / ????" << "\n"; // TEMPORARY
                }
            }

            ofs << "------------------------------------------\n";
            ofs.close();
            std::cout << "Report generated at C:/csopesy-log.txt!\n";
        } else {
            std::cout << "Unknown cmd: '" << cmd << "'\n";
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