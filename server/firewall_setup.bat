@echo off
setlocal

net session >nul 2>&1
if %errorlevel% neq 0 (
  powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath 'cmd.exe' -ArgumentList '/k', '""%~f0""' -Verb RunAs"
  exit /b
)

cd /d "%~dp0"
echo Configurando firewall para o TempMonitor...
echo.

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0firewall_setup.ps1"

echo.
pause
