@echo off
REM Compile all .cpp files in src and main.cpp to main.exe using g++
g++ src\*.cpp main.cpp -o main.exe -std=c++17 -pthread
REM Run the compiled main.exe file
main.exe
