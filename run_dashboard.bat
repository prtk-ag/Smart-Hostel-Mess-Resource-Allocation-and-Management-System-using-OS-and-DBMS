@echo off
rem Starts the SHRM dashboard, then open http://127.0.0.1:8080 in your browser.
rem Needs Python 3 (python.org, tick "Add to PATH") and the project built (build_windows.bat).
where py >nul 2>nul && (py dashboard\server.py & exit /b)
where python >nul 2>nul && (python dashboard\server.py & exit /b)
echo Python not found. Install it from python.org and tick "Add Python to PATH".
