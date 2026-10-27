@echo off
rem Build with MinGW-w64 (gcc). On Windows no third-party libraries are needed:
rem the HTTPS client uses the WinHTTP API that ships with Windows.
gcc -std=c11 -O2 -Wall -Wextra -Iinclude src\*.c -o translator.exe -lwinhttp -lws2_32
if errorlevel 1 (
    echo Build failed.
    exit /b 1
)
echo Built translator.exe
