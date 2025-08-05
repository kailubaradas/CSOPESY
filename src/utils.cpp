#include "utils.h"
#include "globals.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstdlib>

std::string trim(const std::string &s) {
    auto l = s.find_first_not_of(" \t\r\n");
    if (l == std::string::npos) return "";
    auto r = s.find_last_not_of(" \t\r\n");
    return s.substr(l, r - l + 1);
}

std::vector<std::string> split(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    
    while (std::getline(ss, token, delimiter)) {
        tokens.push_back(trim(token));
    }
    
    return tokens;
}

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

std::string formatCrashTime(const Clock::time_point &tp) {
    std::time_t t = Clock::to_time_t(tp);
    std::tm tm = *std::localtime(&t);
    std::ostringstream oss;
    oss << std::setfill('0')
        << std::setw(2) << tm.tm_hour << ':'
        << std::setw(2) << tm.tm_min << ':'
        << std::setw(2) << tm.tm_sec;
    return oss.str();
}

void recordCrash(int processId, const std::string& address, const std::string& error) {
    if (sessions.find(processId) != sessions.end()) {
        sessions[processId].crashInfo.hasCrashed = true;
        sessions[processId].crashInfo.crashTime = Clock::now();
        sessions[processId].crashInfo.invalidAddress = address;
        sessions[processId].crashInfo.errorMessage = error;
        sessions[processId].finished = true;
        
        std::cout << "\n[SYSTEM] Process " << processId << " (" 
                  << (processNames.count(processId) ? processNames[processId] : "unknown")
                  << ") crashed due to memory access violation.\n";
    }
}

void clearScreen() {
#if defined(_WIN32) || defined(_WIN64)
    std::system("cls");
#else
    std::system("clear");
#endif
}

void printHeader() {
    std::cout<<"||======================================||\n"
             <<"||            CSOPESY CLI v0.1          ||\n"
             <<"||======================================||\n";
}

int hexToInt(const std::string& hexStr) {
    return std::stoi(hexStr, nullptr, 16);
}
