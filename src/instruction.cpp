#include "instruction.h"
#include "utils.h"
#include "globals.h"
#include "memory_manager.h"
#include <iostream>
#include <vector>
#include <string>

bool isValidVariableName(const std::string& name) {
    if (name.empty() || !std::isalpha(name[0])) {
        return false;
    }
    
    for (char c : name) {
        if (!std::isalnum(c) && c != '_') {
            return false;
        }
    }
    
    return true;
}

bool isValidAddress(const std::string& addr) {
    if (addr.length() < 3 || addr.substr(0, 2) != "0x") {
        return false;
    }
    
    for (size_t i = 2; i < addr.length(); ++i) {
        if (!std::isxdigit(addr[i])) {
            return false;
        }
    }
    
    return true;
}

bool parseInstruction(const std::string& instrStr, Instruction& instruction) {
    std::vector<std::string> tokens = split(instrStr, ' ');
    
    if (tokens.empty()) {
        return false;
    }
    
    std::string command = tokens[0];
    
    if (command == "DECLARE") {
        if (tokens.size() != 3) {
            std::cerr << "Error: DECLARE requires exactly 2 arguments: DECLARE <variable> <value>\n";
            return false;
        }
        
        if (!isValidVariableName(tokens[1])) {
            std::cerr << "Error: Invalid variable name '" << tokens[1] << "'\n";
            return false;
        }
        
        try {
            std::stoi(tokens[2]);
        } catch (const std::exception&) {
            std::cerr << "Error: DECLARE value must be a number\n";
            return false;
        }
        
        instruction = Instruction(InstructionType::DECLARE, {tokens[1], tokens[2]});
        return true;
    }
    
    else if (command == "ADD" || command == "SUB" || command == "MUL" || command == "DIV") {
        if (tokens.size() != 4) {
            std::cerr << "Error: " << command << " requires exactly 3 arguments: " 
                      << command << " <result> <operand1> <operand2>\n";
            return false;
        }
        
        if (!isValidVariableName(tokens[1])) {
            std::cerr << "Error: Invalid result variable name '" << tokens[1] << "'\n";
            return false;
        }
        
        InstructionType type;
        if (command == "ADD") type = InstructionType::ADD;
        else if (command == "SUB") type = InstructionType::SUB;
        else if (command == "MUL") type = InstructionType::MUL;
        else type = InstructionType::DIV;
        
        instruction = Instruction(type, {tokens[1], tokens[2], tokens[3]});
        return true;
    }
    
    else if (command == "WRITE") {
        if (tokens.size() != 3) {
            std::cerr << "Error: WRITE requires exactly 2 arguments: WRITE <address> <variable>\n";
            return false;
        }
        
        if (!isValidAddress(tokens[1])) {
            std::cerr << "Error: Invalid address format '" << tokens[1] << "'. Use 0xABCD format.\n";
            return false;
        }
        
        if (!isValidVariableName(tokens[2])) {
            std::cerr << "Error: Invalid variable name '" << tokens[2] << "'\n";
            return false;
        }
        
        instruction = Instruction(InstructionType::WRITE, {tokens[1], tokens[2]});
        return true;
    }
    
    else if (command == "READ") {
        if (tokens.size() != 3) {
            std::cerr << "Error: READ requires exactly 2 arguments: READ <variable> <address>\n";
            return false;
        }
        
        if (!isValidVariableName(tokens[1])) {
            std::cerr << "Error: Invalid variable name '" << tokens[1] << "'\n";
            return false;
        }
        
        if (!isValidAddress(tokens[2])) {
            std::cerr << "Error: Invalid address format '" << tokens[2] << "'. Use 0xABCD format.\n";
            return false;
        }
        
        instruction = Instruction(InstructionType::READ, {tokens[1], tokens[2]});
        return true;
    }
    
    else if (command == "PRINT") {
        if (tokens.size() < 2) {
            std::cerr << "Error: PRINT requires at least 1 argument\n";
            return false;
        }
        
        std::string printArg = instrStr.substr(instrStr.find("PRINT") + 5);
        printArg = trim(printArg);
        
        if (printArg.empty() || printArg.front() != '(' || printArg.back() != ')') {
            std::cerr << "Error: PRINT argument must be enclosed in parentheses\n";
            return false;
        }
        
        printArg = printArg.substr(1, printArg.length() - 2);
        instruction = Instruction(InstructionType::PRINT, {printArg});
        return true;
    }

    else if (command == "PRINT" || command.substr(0, 6) == "PRINT(") {
        std::string printArg;
        
        if (command == "PRINT") {
            if (tokens.size() < 2) {
                std::cerr << "Error: PRINT requires at least 1 argument\n";
                return false;
            }
            
            printArg = instrStr.substr(instrStr.find("PRINT") + 5);
            printArg = trim(printArg);
        } else {
            printArg = instrStr.substr(5);
        }
        
        if (printArg.empty() || printArg.front() != '(' || printArg.back() != ')') {
            std::cerr << "Error: PRINT argument must be enclosed in parentheses\n";
            return false;
        }
        
        printArg = printArg.substr(1, printArg.length() - 2);
        instruction = Instruction(InstructionType::PRINT, {printArg});
        return true;
    }
    
    else {
        std::cerr << "Error: Unknown instruction '" << command << "'\n";
        return false;
    }
}

bool parseInstructions(const std::string& instructionString, std::vector<Instruction>& instructions) {
    instructions.clear();
    
    if (instructionString.empty()) {
        std::cerr << "Error: Instruction string cannot be empty\n";
        return false;
    }
    
    std::vector<std::string> instrList = split(instructionString, ';');
    
    if (instrList.size() < 1 || instrList.size() > 50) {
        std::cerr << "Error: Number of instructions must be between 1 and 50. Found: " 
                  << instrList.size() << "\n";
        return false;
    }
    
    for (const std::string& instrStr : instrList) {
        std::string trimmedInstr = trim(instrStr);
        if (trimmedInstr.empty()) continue;
        
        Instruction instruction(InstructionType::DECLARE, {});
        if (!parseInstruction(trimmedInstr, instruction)) {
            return false;
        }
        
        instructions.push_back(instruction);
    }
    
    return true;
}

bool executeInstructionWithPaging(int processId, const Instruction& instruction) {
    auto& variables = sessions[processId].variables;
    
    try {
        switch (instruction.type) {
            case InstructionType::DECLARE: {
                std::string varName = instruction.operands[0];
                int value = std::stoi(instruction.operands[1]);
                variables.variables[varName] = value;
                
                int address = std::hash<std::string>{}(varName) % sessions[processId].memorySize;
                writeMemory(processId, address, value);
                
                std::cout << "Process " << processId << " declared " << varName << " = " << value << "\n";
                break;
            }
            
            case InstructionType::READ: {
                std::string varName = instruction.operands[0];
                int address = hexToInt(instruction.operands[1]);
                
                if (address >= sessions[processId].memorySize) {
                    std::cerr << "Error: Address " << instruction.operands[1] << " out of bounds\n";
                    return false;
                }
                
                int value;
                if (readMemory(processId, address, value)) {
                    variables.variables[varName] = value;
                    std::cout << "Process " << processId << " read " << varName << " = " << value 
                              << " from " << instruction.operands[1] << "\n";
                } else {
                    std::cerr << "Error: Failed to read memory at address " << instruction.operands[1] << "\n";
                    return false;
                }
                break;
            }
            
            case InstructionType::WRITE: {
                int address = hexToInt(instruction.operands[0]);
                std::string varName = instruction.operands[1];
                
                if (address >= sessions[processId].memorySize) {
                    std::cerr << "Error: Address " << instruction.operands[0] << " out of bounds\n";
                    return false;
                }
                
                if (variables.variables.find(varName) == variables.variables.end()) {
                    std::cerr << "Error: Variable '" << varName << "' not declared\n";
                    return false;
                }
                
                int value = variables.variables[varName];
                if (writeMemory(processId, address, value)) {
                    std::cout << "Process " << processId << " wrote " << varName << " (" << value 
                              << ") to " << instruction.operands[0] << "\n";
                } else {
                    std::cerr << "Error: Failed to write memory at address " << instruction.operands[0] << "\n";
                    return false;
                }
                break;
            }
            
            case InstructionType::ADD:
            case InstructionType::SUB:
            case InstructionType::MUL:
            case InstructionType::DIV: {
                std::string resultVar = instruction.operands[0];
                
                int op1, op2;
                
                if (variables.variables.find(instruction.operands[1]) != variables.variables.end()) {
                    op1 = variables.variables[instruction.operands[1]];
                } else {
                    try {
                        op1 = std::stoi(instruction.operands[1]);
                    } catch (const std::exception&) {
                        std::cerr << "Error: Invalid operand '" << instruction.operands[1] << "'\n";
                        return false;
                    }
                }
                
                if (variables.variables.find(instruction.operands[2]) != variables.variables.end()) {
                    op2 = variables.variables[instruction.operands[2]];
                } else {
                    try {
                        op2 = std::stoi(instruction.operands[2]);
                    } catch (const std::exception&) {
                        std::cerr << "Error: Invalid operand '" << instruction.operands[2] << "'\n";
                        return false;
                    }
                }
                
                int result;
                std::string op_str;
                switch (instruction.type) {
                    case InstructionType::ADD: result = op1 + op2; op_str = "+"; break;
                    case InstructionType::SUB: result = op1 - op2; op_str = "-"; break;
                    case InstructionType::MUL: result = op1 * op2; op_str = "*"; break;
                    case InstructionType::DIV: 
                        if (op2 == 0) {
                            std::cerr << "Error: Division by zero\n";
                            return false;
                        }
                        result = op1 / op2; 
                        op_str = "/";
                        break;
                    default: return false;
                }
                
                variables.variables[resultVar] = result;
                
                int address = std::hash<std::string>{}(resultVar) % sessions[processId].memorySize;
                writeMemory(processId, address, result);
                
                std::cout << "Process " << processId << " computed " << resultVar << " = " 
                          << op1 << " " << op_str << " " << op2 << " = " << result << "\n";
                break;
            }
            
            case InstructionType::PRINT: {
                std::string content = instruction.operands[0];
                
                if (variables.variables.find(content) != variables.variables.end()) {
                    std::cout << "Process " << processId << " prints: " << variables.variables[content] << "\n";
                } else {
                    std::string output = content;
                    
                    size_t plusPos = content.find(" + ");
                    if (plusPos != std::string::npos) {
                        std::string leftPart = trim(content.substr(0, plusPos));
                        std::string rightPart = trim(content.substr(plusPos + 3));
                        
                        if (leftPart.front() == '"' && leftPart.back() == '"') {
                            leftPart = leftPart.substr(1, leftPart.length() - 2);
                        }
                        
                        if (variables.variables.find(rightPart) != variables.variables.end()) {
                            output = leftPart + std::to_string(variables.variables[rightPart]);
                        } else {
                            output = leftPart + rightPart;
                        }
                    } else if (content.front() == '"' && content.back() == '"') {
                        output = content.substr(1, content.length() - 2);
                    }
                    
                    std::cout << "Process " << processId << " prints: " << output << "\n";
                }
                break;
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error executing instruction: " << e.what() << "\n";
        return false;
    }
}

void printInstructions(const std::vector<Instruction>& instructions) {
    std::cout << "Parsed Instructions (" << instructions.size() << " total):\n";
    for (size_t i = 0; i < instructions.size(); ++i) {
        std::cout << "  " << (i + 1) << ". ";
        
        switch (instructions[i].type) {
            case InstructionType::DECLARE:
                std::cout << "DECLARE " << instructions[i].operands[0] 
                          << " " << instructions[i].operands[1];
                break;
            case InstructionType::ADD:
                std::cout << "ADD " << instructions[i].operands[0] 
                          << " " << instructions[i].operands[1] 
                          << " " << instructions[i].operands[2];
                break;
            case InstructionType::SUB:
                std::cout << "SUB " << instructions[i].operands[0] 
                          << " " << instructions[i].operands[1] 
                          << " " << instructions[i].operands[2];
                break;
            case InstructionType::MUL:
                std::cout << "MUL " << instructions[i].operands[0] 
                          << " " << instructions[i].operands[1] 
                          << " " << instructions[i].operands[2];
                break;
            case InstructionType::DIV:
                std::cout << "DIV " << instructions[i].operands[0] 
                          << " " << instructions[i].operands[1] 
                          << " " << instructions[i].operands[2];
                break;
            case InstructionType::WRITE:
                std::cout << "WRITE " << instructions[i].operands[0] 
                          << " " << instructions[i].operands[1];
                break;
            case InstructionType::READ:
                std::cout << "READ " << instructions[i].operands[0] 
                          << " " << instructions[i].operands[1];
                break;
            case InstructionType::PRINT:
                std::cout << "PRINT(" << instructions[i].operands[0] << ")";
                break;
        }
        std::cout << "\n";
    }
}
