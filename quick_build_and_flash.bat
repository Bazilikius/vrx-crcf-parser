@echo off
:: This script builds and flashes the project.
:: It must be run from an ESP-IDF Command Prompt.

echo [1/2] Building project...
call idf.py build

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] Build failed. Make sure you are in the ESP-IDF Command Prompt.
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo [2/2] Flashing project...
echo (If multiple ESP32s are connected, use: idf.py -p COMx flash)
call idf.py flash monitor

if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Flashing failed.
    pause
    exit /b %ERRORLEVEL%
)
