#ifndef PROCESS_H
#define PROCESS_H

#include <string>
#include <vector>
#include "structures.h"

bool parseScreenCommandWithInstructions(const std::string& cmd, std::string& processName, 
                                       int& memorySize, std::string& instructions);
bool parseScreenCommand(const std::string& cmd, std::string& processName, int& memorySize);
bool isValidMemorySize(int size);

#endif // PROCESS_H
