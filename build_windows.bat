@echo off
rem Builds SHRM on Windows (Release). Run from the project folder.
rem Needs CMake plus a C++ compiler: Visual Studio Build Tools ("Desktop development with C++")
rem or MinGW/MSYS2. SQLite is found automatically, or downloaded once if missing.
where cmake >nul 2>nul || (echo CMake not found. Install it from cmake.org and tick "Add to PATH". & exit /b 1)
cmake -S . -B build || exit /b 1
cmake --build build --config Release -j || exit /b 1
echo.
echo Build done. Try:  run_ipc_demo.bat
