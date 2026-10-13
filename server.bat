@echo off

cd /d "%~dp0backend"

taskkill /IM agap.exe /F >nul 2>&1

g++ main.cpp storage.cpp auth.cpp staff_storage.cpp pdf_report.cpp announcement_storage.cpp complaint_storage.cpp file_util.cpp named_list_storage.cpp -o agap.exe -std=c++17 -lws2_32
if errorlevel 1 (
    echo.
    echo Build failed. Fix the errors above, then run this again.
    pause
    exit /b 1
)

echo Starting AGAP at http://localhost:8080
agap.exe
pause
