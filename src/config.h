#ifndef CONFIG_H
#define CONFIG_H

#include "structures.h"
#include <string>

extern Config config;

bool readConfig(const std::string& filename, Config& config);
void printConfig(const Config& config);

#endif // CONFIG_H
