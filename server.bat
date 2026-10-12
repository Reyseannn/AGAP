@echo off
REM Compiles and runs AGAP in one step (Windows + MinGW g++).
REM Double-click it, or run it from the project folder.

cd /d "%~dp0backend"

REM Stop any copy of the server that is still running (it would lock agap.exe).
taskkill /IM agap.exe /F >nul 2>&1

g++ main.cpp storage.cpp auth.cpp staff_storage.cpp pdf_report.cpp announcement_storage.cpp complaint_storage.cpp file_util.cpp -o agap.exe -std=c++17 -lws2_32
if errorlevel 1 (
    echo.
    echo Build failed. Fix the errors above, then run this again.
    pause
    exit /b 1
)

echo Starting AGAP at http://localhost:8080
agap.exe
pause
