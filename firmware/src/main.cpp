#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <ESP8266Ping.h>

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const char* ssid = "SUA_REDE_WIFI";
const char* password = "SUA_SENHA_WIFI";

const char* cpuEndpointUrlPc01 = "http://192.168.0.119:5000/cpu-temp";
const char* gpuEndpointUrlPc01 = "http://192.168.0.119:5000/gpu-temp";
const char* cpuEndpointUrlPc02 = "http://192.168.0.119:5000/cpu-temp";
const char* gpuEndpointUrlPc02 = "http://192.168.0.119:5000/gpu-temp";

const IPAddress pc01IP(192, 168, 0, 119);
const IPAddress pc02IP(192, 168, 0, 119);

float temperaturaCpu = 0.0;
float temperaturaGpu = 0.0;

bool pc01Online = false;
bool pc02Online = false;

const char* currentGpuEndpointUrl = "";
const char* currentCpuEndpointUrl = "";

const char* gpuJsonKey = "temperatura_gpu";
const char* cpuJsonKey = "temperatura_cpu";

float obterTemperatura(const char* url, const char* jsonKey);
bool endpointDisponivel(const char* url);
bool pingHost(IPAddress ipAddress);
void writeTextOnDisplay(float temperaturaCpu, float temperaturaGpu);

void setup() {
  Serial.begin(115200);
  delay(10);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    while (true) {
      delay(1000);
    }
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("Iniciando..."));
  display.display();
  delay(1000);

  Serial.println();
  Serial.print(F("Conectando-se a rede Wi-Fi: "));
  Serial.println(ssid);

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(F("Conectando Wi-Fi"));
  display.println(ssid);
  display.display();

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    display.print(".");
    display.display();
  }

  Serial.println();
  Serial.println(F("Wi-Fi conectado!"));
  Serial.print(F("Endereco IP: "));
  Serial.println(WiFi.localIP());

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(F("Wi-Fi conectado!"));
  display.println(F("IP:"));
  display.println(WiFi.localIP());
  display.display();
  delay(1000);

  pc01Online = pingHost(pc01IP) || endpointDisponivel(cpuEndpointUrlPc01);
  pc02Online = pingHost(pc02IP) || endpointDisponivel(cpuEndpointUrlPc02);

  display.clearDisplay();
  display.setCursor(0, 0);

  if (pc01Online) {
    currentGpuEndpointUrl = gpuEndpointUrlPc01;
    currentCpuEndpointUrl = cpuEndpointUrlPc01;
    display.println(F("PC01 online!"));
  } else if (pc02Online) {
    currentGpuEndpointUrl = gpuEndpointUrlPc02;
    currentCpuEndpointUrl = cpuEndpointUrlPc02;
    display.println(F("PC02 online!"));
  } else {
    Serial.println(F("Nenhum computador online."));
    display.println(F("Nenhum PC online."));
    delay(60000);
  }

  display.display();
  Serial.println(currentGpuEndpointUrl);
  Serial.println(currentCpuEndpointUrl);
  delay(1000);
}

void loop() {
  if (WiFi.status() == WL_CONNECTED && currentGpuEndpointUrl[0] != '\0' && currentCpuEndpointUrl[0] != '\0') {
    temperaturaGpu = obterTemperatura(currentGpuEndpointUrl, gpuJsonKey);

    if (temperaturaGpu != -999.0) {
      Serial.print(F(">>> Temperatura da GPU atualizada: "));
      Serial.print(temperaturaGpu);
      Serial.println(F(" C"));
    } else {
      Serial.println(F(">>> Falha ao obter a temperatura da GPU."));
    }

    delay(500);

    temperaturaCpu = obterTemperatura(currentCpuEndpointUrl, cpuJsonKey);

    if (temperaturaCpu != -999.0) {
      Serial.print(F(">>> Temperatura da CPU atualizada: "));
      Serial.print(temperaturaCpu);
      Serial.println(F(" C"));
    } else {
      Serial.println(F(">>> Falha ao obter a temperatura da CPU."));
    }
  } else {
    Serial.println(F("Erro na conexao com os dispositivos!"));
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println(F("Erro na conexao"));
    display.println(F("com dispositivos"));
    display.display();
  }

  Serial.println(F("\nAguardando 1 segundo para a proxima consulta..."));
  Serial.println(F("---------------------------------"));
  writeTextOnDisplay(temperaturaCpu, temperaturaGpu);
  delay(1000);
}

float obterTemperatura(const char* url, const char* jsonKey) {
  WiFiClient client;
  HTTPClient http;
  float temperatura = -999.0;

  Serial.printf("\n[HTTP] Iniciando requisicao para: %s\n", url);

  if (http.begin(client, url)) {
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();

      StaticJsonDocument<128> doc;
      DeserializationError error = deserializeJson(doc, payload);

      if (error) {
        Serial.print(F("Falha ao analisar o JSON: "));
        Serial.println(error.c_str());
      } else if (doc.containsKey(jsonKey)) {
        temperatura = doc[jsonKey];
      } else {
        Serial.printf("Erro: A chave '%s' nao foi encontrada no JSON.\n", jsonKey);
      }
    } else {
      Serial.printf("[HTTP] Falha na requisicao, codigo de erro: %d\n", httpCode);
    }

    http.end();
  } else {
    Serial.printf("[HTTP] Nao foi possivel iniciar a conexao para %s\n", url);
  }

  return temperatura;
}

bool endpointDisponivel(const char* url) {
  WiFiClient client;
  HTTPClient http;
  bool disponivel = false;

  Serial.printf("Testando endpoint %s... ", url);

  if (http.begin(client, url)) {
    int httpCode = http.GET();
    disponivel = (httpCode == HTTP_CODE_OK);
    Serial.printf("HTTP %d\n", httpCode);
    http.end();
  } else {
    Serial.println(F("falha ao iniciar HTTP"));
  }

  return disponivel;
}

bool pingHost(IPAddress ipAddress) {
  Serial.printf("Tentando pingar %s... ", ipAddress.toString().c_str());

  bool success = Ping.ping(ipAddress, 3);

  if (success) {
    Serial.println(F("Online!"));
  } else {
    Serial.println(F("Offline."));
  }

  return success;
}

void writeTextOnDisplay(float temperaturaCpu, float temperaturaGpu) {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.print(F("CPU:"));
  display.print(temperaturaCpu, 1);
  display.print(F("C"));

  display.setCursor(0, 40);
  display.print(F("GPU:"));
  display.print(temperaturaGpu, 1);
  display.print(F("C"));

  display.display();
}
