@echo off
setlocal
title TempMonitor API

set "SCRIPT_DIR=%~dp0"
set "LOG_FILE=%SCRIPT_DIR%tempmonitor.log"

if /i not "%~1"=="--elevated" (
  net session >nul 2>&1
  if errorlevel 1 (
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath 'cmd.exe' -ArgumentList '/k', '""%~f0"" --elevated' -Verb RunAs"
    exit /b
  )
)

cd /d "%SCRIPT_DIR%"

echo ================================================== > "%LOG_FILE%"
echo TempMonitor iniciado em %date% %time% >> "%LOG_FILE%"
echo Pasta: %SCRIPT_DIR% >> "%LOG_FILE%"
echo ================================================== >> "%LOG_FILE%"
echo.
echo Iniciando TempMonitor...
echo Log: "%LOG_FILE%"
echo.

echo Configurando firewall...
powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%firewall_setup.ps1" >> "%LOG_FILE%" 2>&1
if errorlevel 1 (
  echo [AVISO] Nao foi possivel configurar a regra de firewall.
  echo         O servidor sera iniciado assim mesmo, mas o acesso pela rede
  echo         a partir do ESP8266 pode estar bloqueado. Detalhes no log.
  echo.
)

echo.
echo Iniciando servidor Python...
echo.
python -u "%SCRIPT_DIR%main.py" 2>&1 | powershell -NoProfile -Command "$input | Tee-Object -FilePath '%LOG_FILE%' -Append"

echo.
echo O servidor encerrou ou ocorreu um erro.
echo Veja o log em: "%LOG_FILE%"
echo.
pause
