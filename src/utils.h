#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <vector>
#include <chrono>
#include "structures.h"

using Clock = std::chrono::system_clock;

std::string trim(const std::string &s);
std::vector<std::string> split(const std::string& str, char delimiter);
std::string formatTimestamp(const Clock::time_point &tp);
std::string formatCrashTime(const Clock::time_point &tp);
void recordCrash(int processId, const std::string& address, const std::string& error);
void clearScreen();
void printHeader();
int hexToInt(const std::string& hexStr);


#endif // UTILS_H
