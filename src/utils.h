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
void clearScreen();
void printHeader();
int hexToInt(const std::string& hexStr);

#endif // UTILS_H
