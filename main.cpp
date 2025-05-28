#include <iostream>
#include <string>
#include <map>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <cstdlib>

// Session placeholder for "screen" layouts
typedef std::chrono::system_clock Clock;
struct Session {
    Clock::time_point start;
    int currentLine;
    int totalLines;
};

// Print ASCII header
void printHeader() {
    std::cout
      << "||======================================||\n"
      << "||            CSOPESY CLI v0.1          ||\n"
      << "||======================================||\n";
}

// Cross-platform clear
void clearScreen() {
#if defined(_WIN32) || defined(_WIN64)
    std::system("cls");
#else
    std::system("clear");
#endif
}

// Trim whitespace
std::string trim(const std::string &s) {
    auto l = s.find_first_not_of(" \t\r\n");
    if (l == std::string::npos) return "";
    auto r = s.find_last_not_of(" \t\r\n");
    return s.substr(l, r - l + 1);
}

// Format timestamp MM/DD/YYYY, HH:MM:SS AM/PM
std::string formatTimestamp(const Clock::time_point &tp) {
    std::time_t t = Clock::to_time_t(tp);
    std::tm tm = *std::localtime(&t);
    std::ostringstream oss;
    oss << std::setfill('0')
        << std::setw(2) << (tm.tm_mon + 1) << '/'
        << std::setw(2) << tm.tm_mday       << '/'
        << (tm.tm_year + 1900) << ", "
        << ((tm.tm_hour % 12) ? (tm.tm_hour % 12) : 12)
        << ':' << std::setw(2) << tm.tm_min
        << ':' << std::setw(2) << tm.tm_sec
        << ' '  << (tm.tm_hour >= 12 ? "PM" : "AM");
    return oss.str();
}

// Show session details
void displaySession(const std::string &name, const Session &s) {
    std::cout << "\n=== Screen: " << name << " ===\n"
              << "Process name: " << name << "\n"
              << "Line: " << s.currentLine << " / " << s.totalLines << "\n"
              << "Created: " << formatTimestamp(s.start) << "\n\n";
}

int main() {
    std::map<std::string, Session> sessions;
    bool inSession = false;
    bool initialized = false;
    std::string current;

    clearScreen();
    printHeader();

    std::string line;
    while (true) {
        // show prompt
        std::cout << (inSession ? current + "> " : "Main> ");
        if (!std::getline(std::cin, line)) break;
        std::string cmd = trim(line);
        if (cmd.empty()) continue;

        // exit always available
        if (cmd == "exit") {
            if (inSession) {
                inSession = false;
                clearScreen(); printHeader();
                continue;
            }
            break;
        }

        // require initialize first
        if (!initialized) {
            if (cmd == "initialize") {
                initialized = true;
                std::cout << "Initialization successful.\n";
                clearScreen(); printHeader();
            } else {
                std::cout << "Please run 'initialize' before any other command.\n";
            }
            continue;
        }

        if (!inSession) {
            // simple utility commands
            if (cmd == "initialize") {
                std::cout << "Already initialized.\n";
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
                clearScreen(); printHeader();
            }
            // screen -s <name>: create new session
            else if (cmd.rfind("screen -s ", 0) == 0) {
                std::string name = trim(cmd.substr(9));
                if (name.empty()) {
                    std::cout << "Usage: screen -s <name>\n";
                } else if (sessions.count(name)) {
                    std::cout << "Session '" << name << "' already exists.\n";
                } else {
                    Session s{ Clock::now(), 1, 100 };
                    sessions[name] = s;
                    current = name;
                    inSession = true;
                    displaySession(name, s);
                }
            }
            // screen -r <name>: reattach existing session
            else if (cmd.rfind("screen -r ", 0) == 0) {
                std::string name = trim(cmd.substr(9));
                if (name.empty()) {
                    std::cout << "Usage: screen -r <name>\n";
                } else {
                    auto it = sessions.find(name);
                    if (it == sessions.end()) {
                        std::cout << "No such session: '" << name << "'\n";
                    } else {
                        Session &s = it->second;
                        if (s.currentLine < s.totalLines) s.currentLine++;
                        current = name;
                        inSession = true;
                        displaySession(name, s);
                    }
                }
            }
            else {
                std::cout << "Unknown command: '" << cmd << "'. Available: initialize, scheduler-test, scheduler-stop, report-util, clear, exit, screen -s <name>, screen -r <name>.\n";
            }
        } else {
            // inside a session: only exit returns
            std::cout << "(In '" << current << "') type 'exit' to return to main menu.\n";
        }
    }
    return 0;
}

