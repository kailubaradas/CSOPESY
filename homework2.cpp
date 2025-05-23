#include <iostream>
#include <string>
#include <map>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <cstdlib>

// A session stores its creation time and placeholder progress
struct Session {
    std::chrono::system_clock::time_point start;
    int currentLine;
    int totalLines;
};

// Draw the main header/banner
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

// Trim whitespace from both ends
std::string trim(const std::string& s) {
    auto left = s.find_first_not_of(" \t\r\n");
    if (left == std::string::npos) return "";
    auto right = s.find_last_not_of(" \t\r\n");
    return s.substr(left, right - left + 1);
}

// Format a timestamp as MM/DD/YYYY, HH:MM:SS AM/PM
std::string formatTimestamp(const std::chrono::system_clock::time_point& tp) {
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    // Use std::localtime (not thread-safe but fine for single-threaded CLI)
    std::tm local_tm = *std::localtime(&t);

    std::ostringstream oss;
    oss << std::setfill('0')
        << std::setw(2) << (local_tm.tm_mon + 1) << '/'  // Month [01-12]
        << std::setw(2) << local_tm.tm_mday       << '/'  // Day   [01-31]
        << (local_tm.tm_year + 1900) << ", "             // Year
        // 12-hour clock
        << ((local_tm.tm_hour % 12) == 0 ? 12 : (local_tm.tm_hour % 12))
        << ':' << std::setw(2) << local_tm.tm_min
        << ':' << std::setw(2) << local_tm.tm_sec
        << ' ' << (local_tm.tm_hour >= 12 ? "PM" : "AM");
    return oss.str();
}

// Display the placeholder console for a session
void displaySession(const std::string& name, const Session& s) {
    std::cout << "\n=== Screen: " << name << " ===\n"
              << "Process name: " << name << "\n"
              << "Line: " << s.currentLine << " / " << s.totalLines << "\n"
              << "Created: " << formatTimestamp(s.start) << "\n\n";
}

int main() {
    std::map<std::string, Session> sessions;
    bool inSession = false;
    std::string current;

    clearScreen();
    printHeader();

    std::string input;
    while (true) {
        // Prompt shows context
        if (inSession)
            std::cout << current << "> ";
        else
            std::cout << "Main> ";

        if (!std::getline(std::cin, input))
            break;  // EOF

        std::string cmd = trim(input);
        if (cmd.empty())
            continue;

        if (!inSession) {
            // Create a new screen: screen -s <name>
            const std::string startTok = "screen -s ";
            const std::string reattTok = "screen -r ";

            if (cmd.rfind(startTok, 0) == 0) {
                std::string name = trim(cmd.substr(startTok.size()));
                if (name.empty()) {
                    std::cout << "Usage: screen -s <name>\n";
                } else if (sessions.count(name)) {
                    std::cout << "Session '" << name << "' already exists.\n";
                } else {
                    // Initialize placeholder session
                    Session s;
                    s.start = std::chrono::system_clock::now();
                    s.currentLine = 1;
                    s.totalLines = 100;
                    sessions[name] = s;

                    // Enter session
                    current = name;
                    inSession = true;
                    displaySession(current, sessions[current]);
                }
            }
            // Reattach to existing screen: screen -r <name>
            else if (cmd.rfind(reattTok, 0) == 0) {
                std::string name = trim(cmd.substr(reattTok.size()));
                auto it = sessions.find(name);
                if (name.empty()) {
                    std::cout << "Usage: screen -r <name>\n";
                } else if (it == sessions.end()) {
                    std::cout << "No such session: '" << name << "'\n";
                } else {
                    // Optionally simulate progress
                    Session &s = it->second;
                    if (s.currentLine < s.totalLines) s.currentLine++;

                    current = name;
                    inSession = true;
                    displaySession(current, s);
                }
            }
            else if (cmd == "exit") {
                // Quit the entire application
                break;
            }
            else {
                std::cout << "Unknown command in main menu: '" << cmd << "'\n";
                std::cout << "Available: screen -s <name>, screen -r <name>, exit\n";
            }
        }
        else {
            // Inside a screen context: only 'exit' returns to main
            if (cmd == "exit") {
                inSession = false;
                clearScreen();
                printHeader();
            } else {
                std::cout << "(Inside '" << current << "') type 'exit' to return to main menu.\n";
            }
        }
    }

    return 0;
}

