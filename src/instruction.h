#ifndef INSTRUCTION_H
#define INSTRUCTION_H

#include "structures.h"
#include <string>
#include <vector>

bool parseInstruction(const std::string& instrStr, Instruction& instruction);
bool parseInstructions(const std::string& instructionString, std::vector<Instruction>& instructions);
bool executeInstructionWithPaging(int processId, const Instruction& instruction);
void printInstructions(const std::vector<Instruction>& instructions);

#endif // INSTRUCTION_H
