@echo off
REM NexusD Demo Cluster - Start Script (Windows)
REM Starts a local cluster of NexusD instances for testing

setlocal EnableDelayedExpansion

set SCRIPT_DIR=%~dp0
set CONFIG_FILE=%SCRIPT_DIR%cluster.conf
set PID_FILE=%SCRIPT_DIR%.cluster.pids

REM Default configuration
set CLUSTER_NAME=demo-cluster
set CLUSTER_NODE_COUNT=3
set BASE_APP_PORT=5672
set BASE_MESH_PORT=5680
set BASE_DISCOVERY_PORT=5690
set MULTICAST_GROUP=239.255.77.77
set MULTICAST_PORT=5670
set LOG_LEVEL=info
set LOG_DIR=logs
set DATA_DIR=data
set NEXUSD_BIN=..\..\build\Release\nexusd.exe
set STARTUP_DELAY=1

REM Load configuration
if exist "%CONFIG_FILE%" (
    for /f "usebackq tokens=1,2 delims==" %%a in ("%CONFIG_FILE%") do (
        if not "%%a"=="" if not "%%b"=="" (
            set "%%a=%%b"
        )
    )
)

REM Resolve binary path
set NEXUSD_PATH=%SCRIPT_DIR%%NEXUSD_BIN%
if not exist "%NEXUSD_PATH%" (
    set NEXUSD_PATH=%SCRIPT_DIR%..\..\build\Debug\nexusd.exe
)
if not exist "%NEXUSD_PATH%" (
    echo Error: NexusD binary not found
    echo Please build NexusD first:
    echo   cd ..\..\build
    echo   cmake ..
    echo   cmake --build . --config Release
    exit /b 1
)

REM Create directories
if not exist "%SCRIPT_DIR%%LOG_DIR%" mkdir "%SCRIPT_DIR%%LOG_DIR%"
if not exist "%SCRIPT_DIR%%DATA_DIR%" mkdir "%SCRIPT_DIR%%DATA_DIR%"

REM Clear PID file
if exist "%PID_FILE%" del "%PID_FILE%"

echo.
echo ================================================================
echo           NexusD Demo Cluster - Starting
echo ================================================================
echo.
echo   Cluster Name:  %CLUSTER_NAME%
echo   Node Count:    %CLUSTER_NODE_COUNT%
echo   Base App Port: %BASE_APP_PORT%
echo.

REM Build peer list
set PEER_LIST=
for /l %%i in (1,1,%CLUSTER_NODE_COUNT%) do (
    set /a MESH_PORT=%BASE_MESH_PORT% + %%i - 1
    if defined PEER_LIST (
        set PEER_LIST=!PEER_LIST!,localhost:!MESH_PORT!
    ) else (
        set PEER_LIST=localhost:!MESH_PORT!
    )
)

REM Start nodes
for /l %%i in (1,1,%CLUSTER_NODE_COUNT%) do (
    set /a APP_PORT=%BASE_APP_PORT% + %%i - 1
    set /a MESH_PORT=%BASE_MESH_PORT% + %%i - 1
    set /a DISCOVERY_PORT=%BASE_DISCOVERY_PORT% + %%i - 1
    set NODE_ID=%CLUSTER_NAME%-node-%%i
    set NODE_DATA_DIR=%SCRIPT_DIR%%DATA_DIR%\node-%%i
    set NODE_LOG_FILE=%SCRIPT_DIR%%LOG_DIR%\node-%%i.log
    
    if not exist "!NODE_DATA_DIR!" mkdir "!NODE_DATA_DIR!"
    
    echo   Starting node %%i...
    echo     ID:        !NODE_ID!
    echo     App Port:  !APP_PORT!
    echo     Mesh Port: !MESH_PORT!
    
    start /b "" "%NEXUSD_PATH%" ^
        --id "!NODE_ID!" ^
        --app-port !APP_PORT! ^
        --mesh-port !MESH_PORT! ^
        --discovery-port !DISCOVERY_PORT! ^
        --multicast-group %MULTICAST_GROUP% ^
        --multicast-port %MULTICAST_PORT% ^
        --peers "%PEER_LIST%" ^
        --data-dir "!NODE_DATA_DIR!" ^
        --log-level %LOG_LEVEL% ^
        > "!NODE_LOG_FILE!" 2>&1
    
    echo.
    timeout /t %STARTUP_DELAY% /nobreak > nul
)

echo   Waiting for nodes to initialize...
timeout /t 2 /nobreak > nul

echo.
echo ================================================================
echo   Cluster started!
echo ================================================================
echo.
echo   Logs: %SCRIPT_DIR%%LOG_DIR%
echo.
echo Use the CLI to explore the cluster:
echo.
echo   nexusd-cli DISCOVER
echo       Discover all running instances
echo.
echo   nexusd-cli -n 1
echo       Connect to first discovered instance
echo.
echo   nexusd-cli -p %BASE_APP_PORT% INFO
echo       Get info from node 1
echo.
echo To stop the cluster:
echo   stop.bat
echo.

endlocal
