@echo off
rem Starts the simulation with the web interface and opens it in the default browser.
cd /d "%~dp0"
start "" cmd /c "timeout /t 2 /nobreak >nul & start http://127.0.0.1:8080/"
Elevator.exe --web
