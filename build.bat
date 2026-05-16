@echo off
setlocal

:: Check if ESP-IDF environment is already set up
if "%IDF_PATH%"=="" (
    echo [ERROR] IDF_PATH is not set. Please run export.bat from your ESP-IDF directory.
    pause
    exit /b 1
)

:: Option 1: Modern CMake-based build (Recommended)
echo [INFO] Building with idf.py...
call idf.py build

:: Option 2: Legacy Make-based build (Fallback)
:: if errorlevel 1 (
::     echo [INFO] idf.py failed, trying legacy make...
::     make -j4
:: )

if errorlevel 1 (
    echo [ERROR] Build failed!
    pause
    exit /b 1
)

echo [SUCCESS] Build complete.
echo [INFO] To flash, run: build_and_flash.bat
pause
