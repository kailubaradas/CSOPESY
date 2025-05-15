#include <iostream>
#include <string>
#include <algorithm>
#include <cstdlib>   

// Print your ASCII header
void printHeader() {
    std::cout
      << "||======================================||\n"
      << "||            CSOPESY CLI v0.1          ||\n"
      << "||======================================||\n";
}

// Cross-platform clear-screen
void clearScreen() {
#if defined(_WIN32) || defined(_WIN64)
    std::system("cls");
#else
    std::system("clear");
#endif
}

int main() {
    printHeader();

    std::string line;
    while (true) {
        std::cout << "Enter a command: ";
        if (!std::getline(std::cin, line)) 
            break;  // EOF or error

        // Trim whitespace
        auto l = line.find_first_not_of(" \t\r\n");
        if (l == std::string::npos) continue;
        auto r = line.find_last_not_of(" \t\r\n");
        std::string cmd = line.substr(l, r - l + 1);

        // Normalize to lowercase
        std::transform(cmd.begin(), cmd.end(), cmd.begin(),
                       [](unsigned char c){ return std::tolower(c); });

        if (cmd == "initialize") {
            std::cout << "initialize command recognized. Doing something.\n";
        }
        else if (cmd == "screen") {
            std::cout << "screen command recognized. Doing something.\n";
        }
        else if (cmd == "scheduler-test") {
            std::cout << "scheduler-test command recognized. Doing something.\n";
        }
        else if (cmd == "scheduler-stop") {
            std::cout << "scheduler-stop command recognized. Doing something.\n";
        }
        else if (cmd == "report-util") {
            std::cout << "report-util command recognized. Doing something.\n";
        }
        else if (cmd == "clear") {
            clearScreen();
            printHeader();
        }
        else if (cmd == "exit") {
            break;  // quit immediately
        }
        else {
            std::cout << "Unknown command: " << cmd << "\n";
        }
    }

    return 0;
}

