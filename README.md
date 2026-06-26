# TempMonitor-ino

Sistema de monitoramento de temperatura de CPU/GPU de computadores, exibido em
um display OLED conectado a um ESP8266. As duas metades do sistema vivem neste
mesmo projeto porque uma depende diretamente da outra:

- **`firmware/`** — código do **ESP8266** (Wemos D1 mini, PlatformIO). Lê as
  temperaturas via HTTP e mostra no display OLED SSD1306.
- **`server/`** — **servidor Python (Flask)** que roda em cada PC monitorado e
  expõe as temperaturas em `GET /cpu-temp` e `GET /gpu-temp`.

## Como funciona (visão geral)

1. Em cada PC a ser monitorado, roda-se o servidor (`server/python_code.bat`,
   como administrador). Ele lê os sensores via LibreHardwareMonitor e publica os
   dados numa API REST na porta 5000.
2. O ESP8266 conecta no Wi-Fi, consulta a API do PC e exibe CPU/GPU no display.

> A leitura da **CPU exige privilégios de administrador** e, em CPUs AMD/Ryzen,
> o driver **PawnIO** instalado no sistema (driver de kernel assinado e
> **compatível com a Integridade de Memória**, usado pelo LibreHardwareMonitor
> 0.9.6+). Sem o PawnIO, o `Tctl/Tdie` da CPU lê **0**. Instale uma vez com
> `winget install namazso.PawnIO` (ou em pawnio.eu). A GPU não depende disso.
> Detalhes em `server/readme.md`.

## Estrutura

```
tempmonitor-ino/
├── firmware/    Projeto PlatformIO do ESP8266 (display, web server, instalador)
├── server/      Servidor Python (Flask + LibreHardwareMonitor)
└── README.md
```

### Versão 1 (tag `v1`)

A primeira versão funcional (firmware com lista de dispositivos fixa no código)
está preservada na tag git **`v1`** — veja com `git checkout v1`.

## Versão atual (v2 — implementada)

- **Loop não-bloqueante** + **tratamento de erros** no display (OFFLINE / N/D /
  timeout, em vez de valores inválidos como -999).
- **Web server no ESP8266** (porta 80, `http://tempmonitor.local`) para
  **adicionar/remover dispositivos por IP** e **selecionar** o ativo. Registro
  persistente em LittleFS.
- **Hospedagem do instalador**: o ESP serve `setup.ps1` + `main.py`; a tela web
  mostra o comando pronto pra colar no PC novo. O `setup.ps1` instala Python +
  dependências + **PawnIO**, baixa o `main.py` do ESP e as DLLs do
  LibreHardwareMonitor (release oficial v0.9.6) e configura o firewall. Início do
  servidor **manual**.

## Como gravar (firmware + arquivos hospedados)

> Antes da 1ª gravação: copie `firmware/src/secrets.h.example` para
> `firmware/src/secrets.h` e preencha o SSID/senha do seu Wi-Fi (esse arquivo
> fica fora do git).

No diretório `firmware/`:

```
pio run -t upload      # grava o firmware (codigo)
pio run -t uploadfs    # grava os arquivos hospedados em data/ (setup.ps1, main.py)
```

> ⚠️ O `uploadfs` **apaga toda a LittleFS**, incluindo a lista de dispositivos
> (`devices.json`). Rode-o quando atualizar `data/`; depois recadastre os
> dispositivos pela tela web. O `upload` (firmware) **não** apaga a lista.
>
> Ao alterar `server/main.py`, copie-o para `firmware/data/main.py` e rode
> `uploadfs` de novo, para o ESP servir a versão nova aos PCs.
