@echo off
setlocal

if "%IDF_PATH%"=="" (
    echo [ERROR] IDF_PATH is not set. Please run export.bat from your ESP-IDF directory.
    pause
    exit /b 1
)

set /p port="Enter COM port (e.g. COM3): "

echo [INFO] Flashing to %port%...
call idf.py -p %port% flash monitor

if errorlevel 1 (
    echo [ERROR] Flashing failed!
    pause
    exit /b 1
)
