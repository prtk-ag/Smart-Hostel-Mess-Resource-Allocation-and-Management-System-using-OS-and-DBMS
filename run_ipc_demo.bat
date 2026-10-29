@echo off
rem Phase 8 demo on Windows: generator process -> pipe -> server process -> database.
rem Finds the executables whether they were built by Visual Studio (build\Release) or MinGW (build).
set BIN=build\Release
if not exist %BIN%\shrm_ipc_server.exe set BIN=build
if not exist %BIN%\shrm_ipc_server.exe (echo Run build_windows.bat first. & exit /b 1)

echo === 1) Anonymous pipe: generator ^| server ===
%BIN%\shrm_event_generator.exe --count 300 --bad 4 | %BIN%\shrm_ipc_server.exe --db shrm_ipc.db

echo.
echo === 2) Named pipe: server in its own window, generator here ===
start "SHRM server" cmd /k "%BIN%\shrm_ipc_server.exe --in pipe:demo --db shrm_ipc_named.db"
%BIN%\shrm_event_generator.exe --count 300 --bad 4 --out pipe:demo
echo Generator finished -- see the report in the server window.
