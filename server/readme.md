
# Monitor de Temperatura

Este projeto consiste em um sistema de monitoramento de temperatura de CPU e GPU de computadores remotos. Ele utiliza um ESP8266 com display OLED para exibir as temperaturas em tempo real, conectando-se a uma API web hospedada nos PCs monitorados.

## Componentes do Projeto

- **Script Python (API)**: `.\main.py` - API web REST que fornece as temperaturas (executar como administrador).
- **Inicializador**: `.\python_code.bat` - Eleva via UAC, configura o firewall e inicia o `main.py`.
- **Configuração de firewall**: `.\firewall_setup.bat` e `.\firewall_setup.ps1` - Liberam a porta 5000 e o ping (ICMP).
- **Cliente Arduino (ESP8266)**: o sketch em uso fica no projeto PlatformIO `arduino-proj-1` (`src/main.cpp`).
- **Versões antigas**: `.\legado\` - sketch `.ino` antigo (com a lib `ESP8266Ping`) e o `script_temperatura.py`, mantidos apenas como referência.

## Funcionalidades

- Conexão Wi-Fi para comunicação com a rede.
- Verificação de conectividade via ping para múltiplos PCs.
- Requisições HTTP para obter temperaturas de CPU e GPU via API REST.
- Exibição das temperaturas em um display OLED SSD1306.
- Suporte para monitoramento de até dois PCs simultaneamente.

## Instalação e Configuração

### 1. Parte Python (Servidor API)

1. Instale as dependências Python:
   ```
   pip install Flask pythonnet wmi
   ```
   > O `main.py` usa a DLL do LibreHardwareMonitor (já incluída em `vendor/`)
   > via `pythonnet` (módulo `clr`). O `wmi` é um fallback opcional para a CPU.
   > A leitura da **temperatura da CPU exige execução como administrador**.

2. Execute o script como administrador:
   - Use `python_code.bat` ou execute diretamente: `python main.py`
   - Certifique-se de que a porta 5000 está liberada no firewall.

3. A API estará disponível em `http://localhost:5000` com os endpoints:
   - `GET /cpu-temp`: Retorna a temperatura da CPU em JSON.
   - `GET /gpu-temp`: Retorna a temperatura da GPU em JSON.

### 2. Parte Arduino (Cliente)

1. Instale as bibliotecas no Arduino IDE:
   - ESP8266WiFi
   - ESP8266HTTPClient
   - WiFiClient
   - ArduinoJson
   - ESP8266Ping (incluído no projeto)
   - SPI
   - Wire
   - Adafruit_GFX
   - Adafruit_SSD1306

2. Configure as credenciais no código do sketch (`arduino-proj-1/src/main.cpp`):
   - `ssid`: Nome da rede Wi-Fi.
   - `password`: Senha da rede Wi-Fi.
   - `cpuEndpointUrlPc01` e `gpuEndpointUrlPc01`: URLs da API para o PC 1.
   - `cpuEndpointUrlPc02` e `gpuEndpointUrlPc02`: URLs da API para o PC 2.
   - `pc01IP` e `pc02IP`: Endereços IP dos PCs.

3. Faça upload do código para o ESP8266.

4. Conecte o display OLED SSD1306 aos pinos apropriados (I2C: SDA, SCL).

## Como Usar

1. Inicie o servidor Python em cada PC a ser monitorado.
2. Configure os IPs e URLs no código Arduino.
3. Faça upload e ligue o ESP8266.
4. O display mostrará as temperaturas dos PCs online.

## Notas

- O código suporta ESP8266; para ESP32, ajuste as bibliotecas (WiFi.h, HTTPClient.h).
- Certifique-se de que o isolamento de AP no roteador está desativado para permitir ping.
- Em caso de erro, verifique os logs no Serial Monitor do Arduino IDE.
- A pasta `legado/` contém versões antigas (sketch `.ino` e `script_temperatura.py`) que **não são mais usadas** — ficam apenas como referência.

## Dependências

### Python
- Flask
- pythonnet (fornece o módulo `clr`)
- wmi (fallback opcional para a leitura da CPU)

### Arduino
- ESP8266WiFi
- ESP8266HTTPClient
- WiFiClient
- ArduinoJson
- ESP8266Ping
- SPI
- Wire
- Adafruit_GFX
- Adafruit_SSD1306