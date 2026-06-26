/*
 * TempMonitor-ino — Firmware do ESP8266 (Wemos D1 mini)
 * -----------------------------------------------------
 * Versao 2 — Fases 1 + 2:
 *   - Loop NAO-bloqueante (agendamento por millis).
 *   - Registro de dispositivos persistente em LittleFS (/devices.json).
 *   - Tratamento de erros estruturado (textos claros no display).
 *   - [FASE 2] Web server (porta 80) + mDNS (tempmonitor.local) para
 *     adicionar/remover dispositivos por IP e selecionar o ativo pela rede.
 *
 * A Fase 3 adicionara a hospedagem do instalador do servidor Python.
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "secrets.h" // credenciais Wi-Fi (fora do git; veja secrets.h.example)

// ---------- Display ----------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ---------- Identificacao ----------
#define FW_NAME "tempmonitor-ino"
#define FW_VERSION "2.0"

// ---------- Wi-Fi ----------
// As credenciais ficam em secrets.h (fora do controle de versao).
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// ---------- Web server ----------
ESP8266WebServer server(80);
const char* MDNS_HOST = "tempmonitor"; // acesso por http://tempmonitor.local

// ---------- Tempos (ms) ----------
const unsigned long POLL_INTERVAL_MS = 3000;         // intervalo entre leituras
const unsigned long WIFI_RETRY_MS = 5000;            // intervalo de reconexao
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000; // espera maxima no boot
const uint16_t HTTP_TIMEOUT_MS = 1000;               // timeout por requisicao

// ---------- Registro de dispositivos ----------
static const uint8_t MAX_DEVICES = 8;
const char* DEVICES_FILE = "/devices.json";

struct Device {
  String name;
  String ip;
  uint16_t port;
  bool enabled;
};

Device devices[MAX_DEVICES];
uint8_t deviceCount = 0;
int8_t activeIndex = -1; // -1 = nenhum dispositivo ativo

// ---------- Resultado estruturado de leitura ----------
enum class ReadStatus {
  OK,
  SEM_DISPOSITIVO, // nenhum dispositivo cadastrado/ativo
  SEM_WIFI,        // ESP fora do Wi-Fi
  OFFLINE,         // nao conectou no dispositivo (IP errado / PC desligado)
  TIMEOUT,         // requisicao estourou o tempo
  HTTP_ERRO,       // respondeu, mas com codigo != 200 (ex.: 501, 404)
  JSON_ERRO,       // resposta nao e um JSON valido
  SEM_CHAVE        // JSON ok, mas sem a chave esperada
};

struct TempResult {
  ReadStatus status;
  float value;
  int httpCode;
};

TempResult cpuResult { ReadStatus::SEM_DISPOSITIVO, 0.0f, 0 };
TempResult gpuResult { ReadStatus::SEM_DISPOSITIVO, 0.0f, 0 };

// ---------- Agendamento ----------
unsigned long lastPoll = 0;
unsigned long lastWifiAttempt = 0;

// ---------- Prototipos ----------
bool loadDevices();
bool saveDevices();
void seedDefaultDevices();
int addDevice(const String& name, const String& ip, uint16_t port);
bool removeDevice(uint8_t index);
bool selectDevice(uint8_t index);
TempResult obterTemperatura(const Device& dev, const char* path, const char* jsonKey);
void pollActiveDevice();
void renderDisplay();
void maintainWifi();
const char* statusCurto(const TempResult& r);
String statusLabel(const TempResult& r);
bool isValidIp(const String& ip);
void requestImmediatePoll();
void setupWebServer();

// ========================================================================
//  Registro de dispositivos (LittleFS)
// ========================================================================

bool loadDevices() {
  if (!LittleFS.exists(DEVICES_FILE)) return false;

  File f = LittleFS.open(DEVICES_FILE, "r");
  if (!f) return false;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err) {
    Serial.printf("Erro lendo %s: %s\n", DEVICES_FILE, err.c_str());
    return false;
  }

  deviceCount = 0;
  activeIndex = -1;

  for (JsonObject o : doc["devices"].as<JsonArray>()) {
    if (deviceCount >= MAX_DEVICES) break;

    const char* nm = o["name"] | "";
    const char* ipv = o["ip"] | "";
    if (strlen(ipv) == 0) continue; // ignora entrada invalida

    Device& d = devices[deviceCount];
    d.name = nm;
    d.ip = ipv;
    d.port = o["port"] | 5000;
    d.enabled = o["enabled"] | true;
    deviceCount++;
  }

  int a = doc["active"] | -1;
  if (a >= 0 && a < deviceCount) activeIndex = (int8_t)a;
  else if (deviceCount > 0) activeIndex = 0;

  return true;
}

bool saveDevices() {
  JsonDocument doc;
  doc["active"] = activeIndex;

  JsonArray arr = doc["devices"].to<JsonArray>();
  for (uint8_t i = 0; i < deviceCount; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["name"] = devices[i].name;
    o["ip"] = devices[i].ip;
    o["port"] = devices[i].port;
    o["enabled"] = devices[i].enabled;
  }

  File f = LittleFS.open(DEVICES_FILE, "w");
  if (!f) {
    Serial.println(F("Falha ao abrir devices.json para escrita"));
    return false;
  }
  serializeJson(doc, f);
  f.close();
  return true;
}

void seedDefaultDevices() {
  deviceCount = 0;
  activeIndex = -1;
  // Mantem o PC que ja era usado na v1, para o painel funcionar de imediato.
  addDevice("PC01", "192.168.0.119", 5000);
}

int addDevice(const String& name, const String& ip, uint16_t port) {
  if (deviceCount >= MAX_DEVICES) return -1;
  if (ip.length() == 0) return -1;

  Device& d = devices[deviceCount];
  d.name = name.length() ? name : ip;
  d.ip = ip;
  d.port = port ? port : 5000;
  d.enabled = true;

  int idx = deviceCount;
  deviceCount++;
  if (activeIndex < 0) activeIndex = (int8_t)idx;

  saveDevices();
  return idx;
}

bool removeDevice(uint8_t index) {
  if (index >= deviceCount) return false;

  for (uint8_t i = index; (uint8_t)(i + 1) < deviceCount; i++) {
    devices[i] = devices[i + 1];
  }
  deviceCount--;

  if (deviceCount == 0) {
    activeIndex = -1;
  } else if (index < activeIndex) {
    activeIndex--;
  } else if (index == activeIndex && activeIndex >= deviceCount) {
    activeIndex = deviceCount - 1;
  }

  saveDevices();
  return true;
}

bool selectDevice(uint8_t index) {
  if (index >= deviceCount) return false;
  activeIndex = (int8_t)index;
  saveDevices();
  return true;
}

// ========================================================================
//  Leitura de temperatura via API REST
// ========================================================================

TempResult obterTemperatura(const Device& dev, const char* path, const char* jsonKey) {
  TempResult r { ReadStatus::OFFLINE, 0.0f, 0 };

  WiFiClient client;
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);

  String url = "http://" + dev.ip + ":" + String(dev.port) + path;
  Serial.printf("[HTTP] GET %s ... ", url.c_str());

  if (!http.begin(client, url)) {
    Serial.println(F("falha ao iniciar conexao"));
    r.status = ReadStatus::OFFLINE;
    return r;
  }

  int code = http.GET();
  r.httpCode = code;

  if (code == HTTP_CODE_OK) {
    String payload = http.getString();
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);

    if (err) {
      r.status = ReadStatus::JSON_ERRO;
    } else if (doc[jsonKey].isNull()) {
      r.status = ReadStatus::SEM_CHAVE;
    } else {
      r.value = doc[jsonKey].as<float>();
      r.status = ReadStatus::OK;
    }
  } else if (code < 0) {
    // Erros de conexao da HTTPClient (codigos negativos).
    r.status = (code == HTTPC_ERROR_READ_TIMEOUT) ? ReadStatus::TIMEOUT : ReadStatus::OFFLINE;
  } else {
    // Respondeu com codigo HTTP de erro (501 = sensor indisponivel, etc.).
    r.status = ReadStatus::HTTP_ERRO;
  }

  Serial.printf("http=%d status=%d\n", code, (int)r.status);
  http.end();
  return r;
}

void pollActiveDevice() {
  if (WiFi.status() != WL_CONNECTED) {
    cpuResult.status = ReadStatus::SEM_WIFI;
    gpuResult.status = ReadStatus::SEM_WIFI;
    return;
  }

  if (activeIndex < 0 || activeIndex >= deviceCount) {
    cpuResult.status = ReadStatus::SEM_DISPOSITIVO;
    gpuResult.status = ReadStatus::SEM_DISPOSITIVO;
    return;
  }

  const Device& dev = devices[activeIndex];
  Serial.printf("\n--- Lendo %s (%s:%u) ---\n", dev.name.c_str(), dev.ip.c_str(), dev.port);

  gpuResult = obterTemperatura(dev, "/gpu-temp", "temperatura_gpu");
  yield();
  cpuResult = obterTemperatura(dev, "/cpu-temp", "temperatura_cpu");
}

void requestImmediatePoll() {
  // Faz o proximo loop ler imediatamente (sem esperar o intervalo).
  lastPoll = millis() - POLL_INTERVAL_MS - 1;
}

// ========================================================================
//  Display
// ========================================================================

const char* statusCurto(const TempResult& r) {
  switch (r.status) {
    case ReadStatus::OK:              return "";
    case ReadStatus::OFFLINE:         return "OFF";
    case ReadStatus::TIMEOUT:         return "T/O";
    case ReadStatus::HTTP_ERRO:       return (r.httpCode == 501) ? "N/D" : "ERR";
    case ReadStatus::JSON_ERRO:       return "JSON";
    case ReadStatus::SEM_CHAVE:       return "?";
    case ReadStatus::SEM_WIFI:        return "WIFI";
    case ReadStatus::SEM_DISPOSITIVO: return "---";
    default:                          return "--";
  }
}

String statusLabel(const TempResult& r) {
  switch (r.status) {
    case ReadStatus::OK:              return String(r.value, 1) + " C";
    case ReadStatus::OFFLINE:         return "Offline";
    case ReadStatus::TIMEOUT:         return "Timeout";
    case ReadStatus::HTTP_ERRO:       return (r.httpCode == 501) ? "Sensor indisponivel (501)" : ("Erro HTTP " + String(r.httpCode));
    case ReadStatus::JSON_ERRO:       return "Resposta invalida";
    case ReadStatus::SEM_CHAVE:       return "Sem dado";
    case ReadStatus::SEM_WIFI:        return "Sem Wi-Fi";
    case ReadStatus::SEM_DISPOSITIVO: return "Sem dispositivo";
    default:                          return "--";
  }
}

void renderDisplay() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Estado: sem Wi-Fi
  if (WiFi.status() != WL_CONNECTED) {
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println(F("Sem Wi-Fi"));
    display.println(F("Reconectando..."));
    display.print(F("SSID: "));
    display.println(ssid);
    display.display();
    return;
  }

  // Estado: nenhum dispositivo cadastrado/ativo
  if (activeIndex < 0 || deviceCount == 0) {
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println(F("Sem dispositivo"));
    display.println(F("Cadastre em:"));
    display.print(F("http://"));
    display.print(MDNS_HOST);
    display.println(F(".local"));
    display.println(WiFi.localIP());
    display.display();
    return;
  }

  const Device& dev = devices[activeIndex];

  // Cabecalho: nome do dispositivo ativo
  display.setTextSize(1);
  display.setCursor(0, 0);
  String header = dev.name;
  if (header.length() > 21) header = header.substring(0, 21);
  display.println(header);

  // CPU
  display.setTextSize(2);
  display.setCursor(0, 16);
  display.print(F("CPU:"));
  if (cpuResult.status == ReadStatus::OK) {
    display.print(cpuResult.value, 1);
    display.print(F("C"));
  } else {
    display.print(statusCurto(cpuResult));
  }

  // GPU
  display.setCursor(0, 40);
  display.print(F("GPU:"));
  if (gpuResult.status == ReadStatus::OK) {
    display.print(gpuResult.value, 1);
    display.print(F("C"));
  } else {
    display.print(statusCurto(gpuResult));
  }

  display.display();
}

// ========================================================================
//  Wi-Fi (reconexao nao-bloqueante)
// ========================================================================

void maintainWifi() {
  if (WiFi.status() == WL_CONNECTED) return;

  unsigned long now = millis();
  if (now - lastWifiAttempt >= WIFI_RETRY_MS) {
    lastWifiAttempt = now;
    Serial.println(F("Wi-Fi desconectado. Tentando reconectar..."));
    WiFi.reconnect();
  }
}

// ========================================================================
//  Web server (Fase 2)
// ========================================================================

// Valida um IPv4 simples no formato a.b.c.d (0-255 por octeto).
bool isValidIp(const String& ip) {
  int parts = 0;
  int val = -1;
  bool any = false;

  for (uint16_t i = 0; i <= ip.length(); i++) {
    char c = (i < ip.length()) ? ip[i] : '.'; // '.' sintetico no fim flush do ultimo octeto
    if (c == '.') {
      if (!any || val < 0 || val > 255) return false;
      parts++;
      val = -1;
      any = false;
      if (parts > 4) return false;
    } else if (c >= '0' && c <= '9') {
      val = (val < 0 ? 0 : val) * 10 + (c - '0');
      any = true;
      if (val > 255) return false;
    } else {
      return false;
    }
  }
  return parts == 4;
}

// Pagina web (HTML + CSS + JS) servida da flash.
const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="pt-br">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>TempMonitor</title>
<style>
  body{font-family:system-ui,Arial,sans-serif;margin:0;background:#0f172a;color:#e2e8f0}
  .wrap{max-width:560px;margin:0 auto;padding:16px}
  h1{font-size:1.3rem;margin:.2rem 0}
  h2{font-size:1rem;margin:.2rem 0}
  .card{background:#1e293b;border-radius:10px;padding:14px;margin:12px 0}
  .big{font-size:1.5rem;font-weight:700}
  table{width:100%;border-collapse:collapse}
  td{padding:6px;border-bottom:1px solid #334155;font-size:.9rem;vertical-align:middle}
  tr.active{background:#334155}
  button{cursor:pointer;border:0;border-radius:6px;padding:6px 10px;font-size:.85rem}
  .sel{background:#2563eb;color:#fff}
  .del{background:#dc2626;color:#fff}
  input{width:100%;box-sizing:border-box;padding:8px;border-radius:6px;border:1px solid #475569;background:#0f172a;color:#e2e8f0;margin:4px 0}
  .row{display:flex;gap:8px}
  .add{background:#16a34a;color:#fff;width:100%;padding:10px;margin-top:6px}
  .muted{color:#94a3b8;font-size:.8rem}
</style>
</head>
<body>
<div class="wrap">
  <h1>TempMonitor</h1>

  <div class="card">
    <div class="muted" id="devName">--</div>
    <div class="big">CPU: <span id="cpu">--</span></div>
    <div class="big">GPU: <span id="gpu">--</span></div>
  </div>

  <div class="card">
    <h2>Dispositivos</h2>
    <table><tbody id="list"></tbody></table>
  </div>

  <div class="card">
    <h2>Adicionar dispositivo</h2>
    <input id="name" placeholder="Nome (ex: PC da sala)">
    <div class="row">
      <input id="ip" placeholder="IP (ex: 192.168.0.119)">
      <input id="port" value="5000" style="max-width:90px">
    </div>
    <button class="add" onclick="add()">Adicionar</button>
    <div class="muted" id="msg"></div>
  </div>

  <div class="card">
    <h2>Adicionar um PC novo</h2>
    <p class="muted">Num PowerShell <b>como administrador</b> no PC a monitorar, cole e rode:</p>
    <code id="cmd" style="display:block;background:#0f172a;padding:8px;border-radius:6px;word-break:break-all;font-size:.8rem">...</code>
    <button class="add" onclick="copyCmd()">Copiar comando</button>
    <div class="muted" id="msg2"></div>
  </div>
</div>
<script>
async function getJSON(u){const r=await fetch(u);return r.json()}
async function post(u,d){const r=await fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(d)});return r.json()}
async function refresh(){
  try{
    const s=await getJSON('/api/status');
    document.getElementById('devName').textContent=s.name?(s.name+' ('+s.ip+')'):'Nenhum dispositivo selecionado';
    document.getElementById('cpu').textContent=s.cpu.label;
    document.getElementById('gpu').textContent=s.gpu.label;
    const ip=s.panelIp||location.host;
    document.getElementById('cmd').textContent='irm http://'+ip+'/download/setup.ps1 | iex';
    const data=await getJSON('/api/devices');
    const tb=document.getElementById('list');tb.innerHTML='';
    if(!data.devices.length){tb.innerHTML='<tr><td class="muted">Nenhum cadastrado.</td></tr>';return}
    data.devices.forEach(x=>{
      const tr=document.createElement('tr');
      if(x.i===data.active)tr.className='active';
      tr.innerHTML='<td>'+x.name+'<br><span class="muted">'+x.ip+':'+x.port+'</span></td>'+
        '<td style="text-align:right">'+
        (x.i===data.active?'<span class="muted">ativo</span> ':'<button class="sel" onclick="sel('+x.i+')">Selecionar</button> ')+
        '<button class="del" onclick="del('+x.i+')">X</button></td>';
      tb.appendChild(tr);
    });
  }catch(e){}
}
async function add(){
  const name=document.getElementById('name').value;
  const ip=document.getElementById('ip').value;
  const port=document.getElementById('port').value||5000;
  const r=await post('/api/add',{name,ip,port});
  document.getElementById('msg').textContent=r.ok?'Adicionado!':('Erro: '+(r.erro||'?'));
  if(r.ok){document.getElementById('name').value='';document.getElementById('ip').value=''}
  refresh();
}
async function sel(i){await post('/api/select',{i});refresh()}
async function del(i){if(confirm('Remover este dispositivo?')){await post('/api/remove',{i});refresh()}}
function copyCmd(){const t=document.getElementById('cmd').textContent;try{navigator.clipboard.writeText(t);document.getElementById('msg2').textContent='Copiado!'}catch(e){document.getElementById('msg2').textContent='Selecione o texto e copie.'}}
refresh();setInterval(refresh,3000);
</script>
</body>
</html>
)HTML";

void handleRoot() {
  server.send_P(200, PSTR("text/html"), INDEX_HTML);
}

void handleApiDevices() {
  JsonDocument doc;
  doc["active"] = activeIndex;
  JsonArray arr = doc["devices"].to<JsonArray>();
  for (uint8_t i = 0; i < deviceCount; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["i"] = i;
    o["name"] = devices[i].name;
    o["ip"] = devices[i].ip;
    o["port"] = devices[i].port;
    o["enabled"] = devices[i].enabled;
  }
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleApiStatus() {
  JsonDocument doc;
  doc["active"] = activeIndex;
  doc["wifi"] = (WiFi.status() == WL_CONNECTED);
  doc["panelIp"] = WiFi.localIP().toString();
  if (activeIndex >= 0 && activeIndex < deviceCount) {
    doc["name"] = devices[activeIndex].name;
    doc["ip"] = devices[activeIndex].ip;
  }

  JsonObject c = doc["cpu"].to<JsonObject>();
  c["ok"] = (cpuResult.status == ReadStatus::OK);
  c["value"] = cpuResult.value;
  c["label"] = statusLabel(cpuResult);

  JsonObject g = doc["gpu"].to<JsonObject>();
  g["ok"] = (gpuResult.status == ReadStatus::OK);
  g["value"] = gpuResult.value;
  g["label"] = statusLabel(gpuResult);

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleApiAdd() {
  String name = server.arg("name");
  String ip = server.arg("ip");
  uint16_t port = (uint16_t) server.arg("port").toInt();

  if (!isValidIp(ip)) {
    server.send(400, "application/json", "{\"erro\":\"IP invalido\"}");
    return;
  }
  int idx = addDevice(name, ip, port);
  if (idx < 0) {
    server.send(400, "application/json", "{\"erro\":\"Lista cheia (max 8)\"}");
    return;
  }
  requestImmediatePoll();
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleApiRemove() {
  int i = server.arg("i").toInt();
  if (i < 0 || !removeDevice((uint8_t)i)) {
    server.send(400, "application/json", "{\"erro\":\"Indice invalido\"}");
    return;
  }
  requestImmediatePoll();
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleApiSelect() {
  int i = server.arg("i").toInt();
  if (i < 0 || !selectDevice((uint8_t)i)) {
    server.send(400, "application/json", "{\"erro\":\"Indice invalido\"}");
    return;
  }
  requestImmediatePoll();
  server.send(200, "application/json", "{\"ok\":true}");
}

// Serve o setup.ps1 da flash, injetando o endereco do painel no marcador.
void handleDownloadSetup() {
  File f = LittleFS.open("/setup.ps1", "r");
  if (!f) {
    server.send(404, "text/plain", "setup.ps1 ausente (rode 'pio run -t uploadfs')");
    return;
  }
  String body = f.readString();
  f.close();
  body.replace("__BASE_URL__", "http://" + WiFi.localIP().toString());
  server.send(200, "text/plain; charset=utf-8", body);
}

// Serve o main.py da flash (download direto).
void handleDownloadMain() {
  File f = LittleFS.open("/main.py", "r");
  if (!f) {
    server.send(404, "text/plain", "main.py ausente (rode 'pio run -t uploadfs')");
    return;
  }
  server.streamFile(f, "text/plain; charset=utf-8");
  f.close();
}

void handleNotFound() {
  server.send(404, "text/plain", "Nao encontrado");
}

void setupWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/devices", HTTP_GET, handleApiDevices);
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/add", HTTP_POST, handleApiAdd);
  server.on("/api/remove", HTTP_POST, handleApiRemove);
  server.on("/api/select", HTTP_POST, handleApiSelect);
  server.on("/download/setup.ps1", HTTP_GET, handleDownloadSetup);
  server.on("/download/main.py", HTTP_GET, handleDownloadMain);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println(F("Web server iniciado na porta 80"));

  if (WiFi.status() == WL_CONNECTED && MDNS.begin(MDNS_HOST)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("mDNS ativo: http://%s.local\n", MDNS_HOST);
  }
}

// ========================================================================
//  setup / loop
// ========================================================================

void setup() {
  Serial.begin(115200);
  delay(10);
  Serial.println();
  Serial.println(F("== " FW_NAME " v" FW_VERSION " =="));

  // Display
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("Falha ao iniciar o display SSD1306 (seguindo mesmo assim)."));
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F(FW_NAME));
  display.setTextSize(2);
  display.setCursor(0, 20);
  display.println(F("v" FW_VERSION));
  display.setTextSize(1);
  display.setCursor(0, 52);
  display.print(F("Iniciando..."));
  display.display();
  delay(3000);

  // LittleFS
  if (!LittleFS.begin()) {
    Serial.println(F("Falha ao montar LittleFS. Formatando..."));
    LittleFS.format();
    LittleFS.begin();
  }

  // Carrega dispositivos salvos ou cria o padrao na primeira vez.
  if (!loadDevices() || deviceCount == 0) {
    Serial.println(F("Nenhum dispositivo salvo. Criando padrao."));
    seedDefaultDevices();
  }
  Serial.printf("Dispositivos: %u | ativo: %d\n", deviceCount, activeIndex);

  // Wi-Fi (espera limitada no boot; o loop continua reconectando)
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(F("Conectando Wi-Fi"));
  display.println(ssid);
  display.display();

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
    Serial.print('.');
    display.print('.');
    display.display();
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nWi-Fi conectado. IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println(F("\nWi-Fi nao conectou no tempo limite (segue tentando)."));
  }

  // Web server + mDNS
  setupWebServer();

  // Primeira leitura + render imediatos
  pollActiveDevice();
  renderDisplay();
  lastPoll = millis();
}

void loop() {
  server.handleClient(); // atende requisicoes web (precisa ser chamado sempre)
  MDNS.update();
  maintainWifi();

  unsigned long now = millis();
  if (now - lastPoll >= POLL_INTERVAL_MS) {
    lastPoll = now;
    pollActiveDevice();
    renderDisplay();
    Serial.println(F("---------------------------------"));
  }

  delay(5);
}
