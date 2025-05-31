#include <iostream>
#include <windows.h>
#include <vector>
#include <string>
#include <conio.h>
#include <iomanip>  // for std::setw

// Define structure to hold dummy process data
struct Process {
    int pid;
    std::string type;
    std::string name;
    int gpuMemoryUsage; // In MiB
};

// Function to set the console text color
void setConsoleColor(WORD color) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, color);
}

// Helper function to truncate process names
std::string truncateProcessName(const std::string& name) {
    if (name.length() <= 33) {
        return name;
    } else {
        return "..." + name.substr(name.length() - 30);
    }
}


std::string formatMemory(int miB) {
    std::ostringstream oss;
    if (miB >= 1024 * 1024) {  // TiB
        double tiB = miB / (1024.0 * 1024.0);
        oss << std::fixed << std::setprecision(1) << tiB << "TiB";
    } else if (miB >= 1024) {  // GiB
        double giB = miB / 1024.0;
        oss << std::fixed << std::setprecision(1) << giB << "GiB";
    } else {
        oss << miB << "MiB";
    }
    return oss.str();
}

// Function to print simplified GPU process info
void processSMI() {
    system("cls");

    std::vector<Process> processes = {
        {1,    "C+G", "C:\\System\\Core\\UI\\Handlers\\uxhost_controller_service.exe", 0},
        {2216, "--", "C:\\Applications\\Network\\Diagnostics\\NetTools\\bin\\network_monitor_util.exe", 128},
        {8216, "C+G", "C:\\Windows\\SysApps\\InteractiveShell\\Widgets\\DockBar\\ui_shell_launcher.exe", 64},
        {8552, "C+G", "C:\\Users\\Public\\AppData\\Local\\Temp\\SyncService\\cache\\autosync_worker.exe", 2048},
        {9348, "C+G", "C:\\Development\\Environments\\Toolchains\\C++\\Build\\v1.4\\custom_compiler_exec.exe", 1023},
        {9348, "C+G", "C:\\Development\\Environments\\Toolchains\\C++\\Build\\v1.4\\custom_compiler_exec.exe", 10929993},
    };

    // Time and GPU summary header
    std::cout << "Sat May 31 18:16:42 2025\n";
    std::cout << "+-----------------------------------------------------------------------------------------+\n";
    std::cout << "| NVIDIA-SMI 576.52                 Driver Version: 576.52         CUDA Version: 12.9     |\n";
    std::cout << "|-----------------------------------------+------------------------+----------------------|\n";
    std::cout << "| GPU  Name                  Driver-Model | Bus-Id          Disp.A | Volatile Uncorr. ECC |\n";
    std::cout << "| Fan  Temp   Perf          Pwr:Usage/Cap |           Memory-Usage | GPU-Util  Compute M. |\n";
    std::cout << "|                                         |                        |               MIG M. |\n";
    std::cout << "|=========================================+========================+======================|\n";
    std::cout << "|   0  NVIDIA GeForce RTX 3050 ...  WDDM  |   00000000:01:00.0  On |                  N/A |\n";
    std::cout << "| N/A   62C    P8              7W /   75W |     741MiB /   4096MiB |      1%      Default |\n";
    std::cout << "|                                         |                        |                  N/A |\n";
    std::cout << "+-----------------------------------------+------------------------+----------------------+\n";

    // Header for processes table
    std::cout << "+-----------------------------------------------------------------------------------------+\n";
    std::cout << "| Processes:                                                                   GPU Memory |\n";
    std::cout << "|     PID   Type   Process Name                                                     Usage |\n";
    std::cout << "|=========================================================================================|\n";

    // Print each process with formatting
    for (const auto& p : processes) {
        std::string truncatedName = truncateProcessName(p.name);
        std::string memStr = formatMemory(p.gpuMemoryUsage);

        std::cout << "|";
        std::cout << std::setw(8) << std::right << p.pid << "    ";
        std::cout << std::setw(3) << std::left << p.type << "   ";
        std::cout << std::setw(60) << std::left << truncatedName << "   ";
        std::cout << std::setw(7) << std::right << memStr << " |\n";
    }

    // Footer border for processes
    std::cout << "+-----------------------------------------------------------------------------------------+\n";

    // Reset color
    setConsoleColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
}
