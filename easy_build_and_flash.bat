@echo off
setlocal enabledelayedexpansion

echo ========================================
echo   CRSF Backpack Bridge Build Tool
echo ========================================

:: 1. Check if already in an IDF environment
where idf.py >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    echo [INFO] IDF environment detected.
    goto :run_build
)

:: 2. Try to find common ESP-IDF locations
set "SEARCH_PATHS=%USERPROFILE%\esp\esp-idf C:\Espressif\frameworks"
for %%P in (%SEARCH_PATHS%) do (
    if exist "%%P" (
        for /d %%D in ("%%P\esp-idf*") do (
            if exist "%%D\export.bat" (
                echo [INFO] Found ESP-IDF at %%D
                echo [INFO] Initializing environment...
                call "%%D\export.bat"
                goto :run_build
            )
        )
    )
)

:: 3. If not found, ask the user
echo [!] ESP-IDF environment not found.
echo.
echo Please enter the path to your ESP-IDF folder
echo (e.g. C:\Espressif\frameworks\esp-idf-v5.1)
set /p "IDF_DIR=Path: "

if not exist "!IDF_DIR!\export.bat" (
    echo [ERROR] export.bat not found in !IDF_DIR!
    pause
    exit /b 1
)

call "!IDF_DIR!\export.bat"

:run_build
echo.
echo [1/2] Building...
call idf.py build
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Build failed.
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo [2/2] Flashing...
echo (Ensure your ESP32 is connected)
call idf.py flash monitor
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Flash failed.
    pause
    exit /b %ERRORLEVEL%
)

echo [SUCCESS] Done.
pause
