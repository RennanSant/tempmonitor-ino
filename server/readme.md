# Servidor TempMonitor (Python)

API REST que roda em cada PC monitorado e expõe a temperatura de CPU e GPU. O
ESP8266 (firmware em `../firmware`) consulta esta API e mostra os valores no
display. Para a visão geral do sistema, veja o [README do projeto](../README.md).

## O que tem nesta pasta

| Item | Função |
|---|---|
| `main.py` | O servidor Flask (a API). |
| `python_code.bat` | Lançador: eleva via UAC, configura o firewall e inicia o `main.py`. |
| `firewall_setup.ps1` | Cria as regras de firewall (porta 5000 + ping ICMP). |
| `vendor/` | Biblioteca **LibreHardwareMonitor** (DLLs .NET) que o `main.py` carrega para ler os sensores. |

## Requisitos

- **Python 3** com as dependências:
  ```
  pip install Flask pythonnet wmi
  ```
- **`vendor/LibreHardwareMonitor`** — a biblioteca que lê o hardware (já incluída).
- **PawnIO** — driver de kernel (assinado, compatível com a Integridade de
  Memória) que o LibreHardwareMonitor usa para acessar a CPU. Instale uma vez:
  ```
  winget install namazso.PawnIO
  ```
  > Sem o PawnIO, a temperatura da **CPU** lê 0 (a GPU funciona sem ele). O
  > PawnIO (driver/acesso) e o `vendor/` (biblioteca/lógica) são complementares —
  > precisa dos dois.

## Como executar

Como **administrador** (necessário para ler a CPU e configurar o firewall):

- Dê um duplo-clique em `python_code.bat` (ele eleva sozinho via UAC), ou
- Rode direto num terminal admin: `python main.py`

A API sobe em `http://0.0.0.0:5000`.

## Endpoints

| Método | Rota | Retorno |
|---|---|---|
| GET | `/cpu-temp` | `{"temperatura_cpu": <°C>, "unidade": "Celsius"}` (HTTP 501 se indisponível) |
| GET | `/gpu-temp` | `{"temperatura_gpu": <°C>, "unidade": "Celsius"}` |
| GET | `/sensors` | Lista completa de sensores detectados (diagnóstico) |
| GET | `/` | Página de boas-vindas |

## Instalar num PC novo

Não precisa copiar esta pasta manualmente: o painel (ESP) hospeda um instalador.
No PC a monitorar, num PowerShell **administrador**, rode o comando que aparece
na tela web do ESP:

```
irm http://<ip-do-painel>/download/setup.ps1 | iex
```

Ele instala tudo (Python, dependências, PawnIO, LibreHardwareMonitor) e configura
o firewall. Início do servidor é **manual** (`python main.py` como admin).
