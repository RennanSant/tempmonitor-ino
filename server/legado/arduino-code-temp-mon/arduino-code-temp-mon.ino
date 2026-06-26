// Inclui as bibliotecas necessárias
#include <ESP8266WiFi.h>        // Para conectar ao Wi-Fi (se usar ESP32, mude para <WiFi.h>)
#include <ESP8266HTTPClient.h>  // Para fazer a requisição HTTP (se usar ESP32, mude para <HTTPClient.h>)
#include <WiFiClient.h>         // Dependência da biblioteca HTTPClient
#include <ArduinoJson.h>        // Para analisar (parse) a resposta JSON
#include "ESP8266Ping.h"        // Para a função de ping

// Bibliotecas para display 
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);


// --- INÍCIO DAS CONFIGURAÇÕES DO USUÁRIO ---
const char* ssid = "SUA_REDE_WIFI";       // Coloque o nome da sua rede Wi-Fi aqui
const char* password = "SUA_SENHA_WIFI"; // Coloque a senha da sua rede aqui
const char* cpuEndpointUrlPc01 = "http://192.168.0.119:5000/cpu-temp"; // O endpoint API para temperatura da cpu do pc 01
const char* gpuEndpointUrlPc01 = "http://192.168.0.119:5000/gpu-temp"; // O endpoint API para temperatura da gpu do pc 01
const char* cpuEndpointUrlPc02 = "http://192.168.0.119:5000/cpu-temp"; 
const char* gpuEndpointUrlPc02 = "http://192.168.0.119:5000/gpu-temp"; 
const IPAddress pc01IP(192, 168, 0, 119);
const IPAddress pc02IP(192, 168, 0, 119);
// --- FIM DAS CONFIGURAÇÕES DO USUÁRIO ---

// Variável global para armazenar a temperatura da GPU
float temperaturaCpu = 0.0;
float temperaturaGpu = 0.0;

// Variáveis para receber a verificação do ping
bool pc01Online = false;
bool pc02Online = false;

// Variáveis para armazenar as URLs do computador que estiver online
const char* currentGpuEndpointUrl = "";
const char* currentCpuEndpointUrl = "";

// Chaves para extrair os dados do JSON (assumindo que a API da CPU retorne {"temperatura_cpu": valor})
const char* gpuJsonKey = "temperatura_gpu";
const char* cpuJsonKey = "temperatura_cpu";

// --- FUNÇÃO AUXILIAR PARA OBTER TEMPERATURA ---
// Esta função se conecta a uma URL, faz a requisição e analisa o JSON.
// Retorna a temperatura em caso de sucesso ou -999.0 em caso de erro.
float obterTemperatura(const char* url, const char* jsonKey) {
  WiFiClient client;
  HTTPClient http;
  float temperatura = -999.0; // Valor de erro padrão

  Serial.printf("\n[HTTP] Iniciando requisição para: %s\n", url);
  if (http.begin(client, url)) {
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      // Serial.print("[HTTP] Resposta: ");
      // Serial.println(payload);

      StaticJsonDocument<128> doc;
      DeserializationError error = deserializeJson(doc, payload);

      if (error) {
        Serial.print("Falha ao analisar o JSON: ");
        Serial.println(error.c_str());
      } else {
        if (doc.containsKey(jsonKey)) {
           temperatura = doc[jsonKey]; // Extrai o valor usando a chave fornecida
        } else {
           Serial.printf("Erro: A chave '%s' não foi encontrada no JSON.\n", jsonKey);
        }
      }
    } else {
      Serial.printf("[HTTP] Falha na requisição, código de erro: %d\n", httpCode);
    }
    http.end(); // Libera os recursos
  } else {
    Serial.printf("[HTTP] Não foi possível iniciar a conexão para %s\n", url);
  }
  
  return temperatura;
}

// Verifica qual pc está online
bool pingHost(IPAddress ipAddress) {
  Serial.printf("Tentando pingar %s... ", ipAddress.toString().c_str());
  // O segundo parâmetro é o número de tentativas de ping. 1 é suficiente para uma verificação rápida.
  bool success = Ping.ping(ipAddress, 3);
  if (success) {
    Serial.println("Online!");
  } else {
    Serial.println("Offline.");
  }
  return success;
}

void setup() {
  // Inicia a comunicação Serial para depuração
  Serial.begin(115200);
  delay(10);

  // setup de display
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
  display.display();
  delay(2000); // Pause for 2 seconds
  display.clearDisplay();
  display.drawPixel(10, 10, SSD1306_WHITE); // Draw a single pixel in white
  display.display();
  delay(1000);


  Serial.println();
  Serial.print(F("Conectando-se à rede Wi-Fi: "));
  Serial.print(ssid);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(F("Conectando-se à rede Wi-Fi: "));
  display.println();
  display.print(ssid);
  display.display(); 
  delay(2000);

  display.clearDisplay();
  display.setCursor(0, 0);
  display.print(F("Conectando"));
  display.display(); 

  // Inicia a conexão Wi-Fi
  WiFi.begin(ssid, password);

  // Espera a conexão ser estabelecida
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    display.print(".");
    display.display();
  }

  // Se conectou, imprime o endereço IP atribuído
  Serial.println();
  Serial.println(F("Wi-Fi conectado!"));
  Serial.print(F("Endereço IP: "));
  Serial.println(WiFi.localIP());
  display.clearDisplay();
  display.setCursor(10, 0);
  display.println(F("Conectado!"));
  display.println(F("Endereco IP: "));
  display.println(WiFi.localIP());
  display.display(); 

  pc01Online = pingHost(pc01IP);
  pc02Online = pingHost(pc02IP);

  if (pc01Online) {
    currentGpuEndpointUrl = gpuEndpointUrlPc01;
    currentCpuEndpointUrl = cpuEndpointUrlPc01;
    display.println("PC01 esta online!");
    display.display(); 
  } else if (pc02Online) {
    currentGpuEndpointUrl = gpuEndpointUrlPc02;
    currentCpuEndpointUrl = cpuEndpointUrlPc02;
    display.println("PC02 esta online!");
    display.display(); 
  } else {
    Serial.println("Nenhum computador online.");
    display.println("Nenhum computador online.");
    display.display(); 
  }
  Serial.println(currentGpuEndpointUrl);
  Serial.println(currentCpuEndpointUrl);
  delay(1000);
}

void loop() {
  if (WiFi.status() == WL_CONNECTED && (pc01Online || pc02Online)) {
  // --- PASSO 2: Obter temperatura da GPU ---
    temperaturaGpu = obterTemperatura(currentGpuEndpointUrl, gpuJsonKey);
    if(temperaturaGpu != -999.0){
        Serial.print(">>> Temperatura da GPU atualizada: ");
        Serial.print(temperaturaGpu);
        Serial.println(" °C");
    } else {
        Serial.println(">>> Falha ao obter a temperatura da GPU.");
    }

    delay(500); // Pequena pausa entre as requisições

    // --- PASSO 3: Obter temperatura da CPU ---
    temperaturaCpu = obterTemperatura(currentCpuEndpointUrl, cpuJsonKey);
    if(temperaturaCpu != -999.0){
        Serial.print(">>> Temperatura da CPU atualizada: ");
        Serial.print(temperaturaCpu);
        Serial.println(" °C");
    } else {
        Serial.println(">>> Falha ao obter a temperatura da CPU.");
    }

    } else {
      Serial.println("Erro na conexão com os dispositivos!");
      display.print("Erro na conexão com os dispositivos!");
      display.display(); 
    }

    Serial.println("\nAguardando 1 segundo para a próxima consulta...");
    Serial.println("---------------------------------");
    writeTextOnDisplay(temperaturaCpu, temperaturaGpu);
    delay(1000);
}


void writeTextOnDisplay(const float temperaturaCpu, const float temperaturaGpu) {
  display.clearDisplay();
  display.setTextSize(2); // Draw 2X-scale text
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(F("CPU:"));
  display.print(temperaturaCpu);
  display.print(F("C"));
  display.setCursor(0, 40);
  display.print(F("GPU:"));
  display.print(temperaturaGpu);
  display.print(F("C"));

  display.display();      // Show initial text
  delay(100);
}
