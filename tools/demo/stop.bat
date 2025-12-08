@echo off
REM NexusD Demo Cluster - Stop Script (Windows)
REM Stops all cluster nodes

setlocal EnableDelayedExpansion

set SCRIPT_DIR=%~dp0

echo.
echo ================================================================
echo           NexusD Demo Cluster - Stopping
echo ================================================================
echo.

REM Find and kill nexusd processes
echo   Looking for NexusD demo cluster processes...

for /f "tokens=2" %%p in ('tasklist /fi "imagename eq nexusd.exe" /fo list 2^>nul ^| findstr "PID:"') do (
    echo   Stopping process PID: %%p
    taskkill /pid %%p /f > nul 2>&1
)

REM Clean up PID file
if exist "%SCRIPT_DIR%.cluster.pids" del "%SCRIPT_DIR%.cluster.pids"

echo.
echo ================================================================
echo   Cluster stopped!
echo ================================================================
echo.

if "%1"=="--clean" (
    echo   Cleaning up data and logs...
    if exist "%SCRIPT_DIR%data" rmdir /s /q "%SCRIPT_DIR%data"
    if exist "%SCRIPT_DIR%logs" rmdir /s /q "%SCRIPT_DIR%logs"
    echo   Done!
    echo.
)

endlocal
