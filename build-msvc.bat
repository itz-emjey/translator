@echo off
rem Build with Microsoft Visual C++ (Developer Command Prompt).
cl /nologo /std:c17 /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /Iinclude src\*.c /Fe:translator.exe ws2_32.lib winhttp.lib
if errorlevel 1 (
    echo Build failed.
    exit /b 1
)
echo Built translator.exe
