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

const int NUM_CORES = 4;
const int NUM_PROCESSES = 10;
const int PRINTS_PER_PROCESS = 100;

std::map<int, Session> sessions;
std::atomic<bool> stopScheduler(false);

// One queue per core
std::queue<int> coreQueues[NUM_CORES];
std::mutex coreMutexes[NUM_CORES];
std::condition_variable coreCVs[NUM_CORES];

// Format timestamp with zero-padded hour
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

// Each core only works on its own queue
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

        std::string fname = (pid < 10 ? "screen_0" : "screen_") + std::to_string(pid) + ".txt";
        std::ofstream ofs(fname, std::ios::trunc);
        for (int i = 0; i < PRINTS_PER_PROCESS; ++i) {
            auto now = Clock::now();
            ofs << "(" << formatTimestamp(now) << ") Core:" << coreId
                << " \"Hello world from screen_" << (pid < 10 ? "0" : "") << pid << "!\"\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        ofs.close();

        sessions[pid].finished = true;
    }
}

void schedulerThread() {
    for (int pid = 1; pid <= NUM_PROCESSES; ++pid) {
        int assignedCore = (pid - 1) % NUM_CORES;
        {
            std::lock_guard<std::mutex> lock(coreMutexes[assignedCore]);
            coreQueues[assignedCore].push(pid);
            sessions[pid] = {Clock::now(), false};
        }
        coreCVs[assignedCore].notify_one();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    stopScheduler = true;
    for (int i = 0; i < NUM_CORES; ++i)
        coreCVs[i].notify_all();
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

    clearScreen(); printHeader();

    while (true) {
        std::cout << "Main> ";
        if (!std::getline(std::cin, line)) break;
        auto cmd = trim(line);
        if (cmd.empty()) continue;

        if (cmd == "exit") break;

        if (!initialized) {
            if (cmd == "initialize") {
                initialized = true;
                clearScreen(); printHeader();
            } else {
                std::cout << "Run 'initialize' first.\n";
            }
            continue;
        }

        if (cmd == "scheduler-test") {
            stopScheduler = false;
            sessions.clear();
            for (int i = 0; i < NUM_CORES; ++i) {
                std::queue<int> empty;
                std::swap(coreQueues[i], empty);
            }

            for (int i = 0; i < NUM_CORES; ++i)
                workers.emplace_back(cpuWorker, i);
            scheduler = std::thread(schedulerThread);
            std::cout << "Started scheduling. Run 'screen -ls' every 1-2s.\n";
        }
        else if (cmd == "screen -ls") {
            std::cout << "Finished:\n";
            for (int pid = 1; pid <= NUM_PROCESSES; ++pid) {
                if (sessions[pid].finished)
                    std::cout << "  screen_" << (pid < 10 ? "0" : "") << pid
                              << " @ " << fmtTime(sessions[pid].start) << "\n";
            }
            std::cout << "Running:\n";
            for (int pid = 1; pid <= NUM_PROCESSES; ++pid) {
                if (!sessions[pid].finished && sessions.count(pid))
                    std::cout << "  screen_" << (pid < 10 ? "0" : "") << pid
                              << " @ " << fmtTime(sessions[pid].start) << "\n";
            }
        }
        else if (cmd == "scheduler-stop") {
            stopScheduler = true;
            for (int i = 0; i < NUM_CORES; ++i)
                coreCVs[i].notify_all();

            if (scheduler.joinable()) scheduler.join();
            for (auto &t : workers)
                if (t.joinable()) t.join();
            workers.clear();
            std::cout << "Scheduler stopped.\n";
        }
        else {
            std::cout << "Unknown cmd: '" << cmd << "'\n";
        }
    }

    stopScheduler = true;
    for (int i = 0; i < NUM_CORES; ++i)
        coreCVs[i].notify_all();
    if (scheduler.joinable()) scheduler.join();
    for (auto &t : workers)
        if (t.joinable()) t.join();

    return 0;
}
