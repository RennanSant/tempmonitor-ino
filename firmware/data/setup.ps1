<#
  setup.ps1 - Instalador do servidor TempMonitor para um PC novo.

  Hospedado pelo ESP8266. Ao servir este arquivo, o firmware substitui o
  marcador __BASE_URL__ pelo endereco do painel (ex.: http://192.168.0.50).

  Uso (num PowerShell COMO ADMINISTRADOR no PC a ser monitorado):
      irm http://<ip-do-painel>/download/setup.ps1 | iex
#>

$ErrorActionPreference = 'Stop'
$BASE = '__BASE_URL__'
$DEST = Join-Path $env:LOCALAPPDATA 'TempMonitor'

function Info($m) { Write-Host "[TempMonitor] $m" -ForegroundColor Cyan }
function Refresh-Path {
  $env:Path = [Environment]::GetEnvironmentVariable('Path','Machine') + ';' +
              [Environment]::GetEnvironmentVariable('Path','User')
}

Info "Instalador iniciado."
Info "Painel:  $BASE"
Info "Destino: $DEST"

# 1) Precisa ser administrador (firewall + driver exigem)
$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
         ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) {
  Write-Warning "Rode este comando num PowerShell COMO ADMINISTRADOR e tente de novo."
  return
}

# 2) Python (instala via winget se faltar)
if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
  Info "Python nao encontrado. Instalando via winget..."
  winget install -e --id Python.Python.3.12 --accept-source-agreements --accept-package-agreements
  Refresh-Path
}
if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
  Write-Warning "Python ainda nao esta no PATH. Feche e reabra o PowerShell (admin) e rode o comando de novo."
  return
}

# 3) Dependencias Python
Info "Instalando dependencias Python (Flask, pythonnet, wmi)..."
python -m pip install --upgrade --quiet pip
python -m pip install --quiet Flask pythonnet wmi

# 4) PawnIO (driver assinado que le os sensores da CPU; compativel com a
#    Integridade de Memoria)
if (-not (Get-CimInstance Win32_SystemDriver -Filter "Name='PawnIO'" -ErrorAction SilentlyContinue)) {
  Info "Instalando PawnIO (driver de sensores)..."
  winget install -e --id namazso.PawnIO --accept-source-agreements --accept-package-agreements
} else {
  Info "PawnIO ja instalado."
}

# 5) Baixa o main.py do painel (ESP)
New-Item -ItemType Directory -Force -Path $DEST | Out-Null
Info "Baixando main.py do painel..."
Invoke-WebRequest "$BASE/download/main.py" -OutFile (Join-Path $DEST 'main.py') -UseBasicParsing

# 6) Baixa as DLLs do LibreHardwareMonitor (release oficial v0.9.6, build .NET
#    Framework) e instala em vendor/LibreHardwareMonitor/
$vendor = Join-Path $DEST 'vendor\LibreHardwareMonitor'
if (-not (Test-Path (Join-Path $vendor 'LibreHardwareMonitorLib.dll'))) {
  Info "Baixando LibreHardwareMonitor (GitHub oficial)..."
  $zip = Join-Path $env:TEMP 'tm_lhm.zip'
  $tmp = Join-Path $env:TEMP 'tm_lhm_extract'
  Invoke-WebRequest 'https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/releases/download/v0.9.6/LibreHardwareMonitor.zip' -OutFile $zip -UseBasicParsing
  if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force }
  Expand-Archive -Path $zip -DestinationPath $tmp -Force
  $dll = Get-ChildItem $tmp -Recurse -Filter 'LibreHardwareMonitorLib.dll' | Select-Object -First 1
  if (-not $dll) { Write-Warning "Nao encontrei a DLL dentro do zip."; return }
  New-Item -ItemType Directory -Force -Path $vendor | Out-Null
  Copy-Item (Join-Path $dll.DirectoryName '*') $vendor -Recurse -Force
  Remove-Item $zip, $tmp -Recurse -Force
} else {
  Info "LibreHardwareMonitor ja presente."
}

# 7) Firewall: porta 5000 (API) + ping (ICMPv4)
Info "Configurando firewall (porta 5000 + ping)..."
if (-not (Get-NetFirewallRule -DisplayName 'TempMonitor Flask TCP 5000' -ErrorAction SilentlyContinue)) {
  New-NetFirewallRule -DisplayName 'TempMonitor Flask TCP 5000' -Direction Inbound -Action Allow -Protocol TCP -LocalPort 5000 -Profile Any | Out-Null
}
if (-not (Get-NetFirewallRule -DisplayName 'TempMonitor ICMPv4 Ping' -ErrorAction SilentlyContinue)) {
  New-NetFirewallRule -DisplayName 'TempMonitor ICMPv4 Ping' -Direction Inbound -Action Allow -Protocol ICMPv4 -IcmpType 8 -Profile Any | Out-Null
}

Write-Host ""
Info "Instalacao concluida!"

$mainPy = Join-Path $DEST 'main.py'
Write-Host ""
Write-Host "O que deseja fazer agora?" -ForegroundColor Yellow
Write-Host "  [1] Iniciar o servidor agora"
Write-Host "  [2] Fechar (iniciar depois)"
$opcao = Read-Host "Escolha [1/2]"

if ($opcao -eq '1') {
  Info "Iniciando o servidor... (Ctrl+C para parar)"
  Write-Host "Cadastre o IP deste PC no painel do ESP (descubra com 'ipconfig')." -ForegroundColor Green
  Write-Host ""
  python "$mainPy"
} else {
  Write-Host ""
  Write-Host "Ok. Para iniciar depois, rode num PowerShell ADMINISTRADOR:" -ForegroundColor Green
  Write-Host "    python `"$mainPy`"" -ForegroundColor Green
  Write-Host "E cadastre o IP deste PC no painel do ESP (descubra com 'ipconfig')." -ForegroundColor Green
}
