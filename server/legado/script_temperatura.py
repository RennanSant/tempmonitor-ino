"""
API Web REST para monitoramento de temperatura da CPU.

Este script utiliza Flask para criar um servidor web e WinTmp
para obter a temperatura da CPU do sistema hospedeiro.

Dependências:
- Flask: pip install Flask
- psutil: pip install WinTmp

Endpoints:
- GET /cpu-temp: Retorna a temperatura atual da CPU.
  Exemplo de acesso: http://127.0.0.1:5000/cpu-temp
- GET /gpu-temp: Retorna a temperatura atual da GPU.
  Exemplo de acesso: http://127.0.0.1:5000/gpu-temp
"""

from flask import Flask, jsonify
import WinTmp
import platform

# Inicializa a aplicação Flask
app = Flask(__name__)

def obter_temperatura_cpu():
    return WinTmp.CPU_Temp

def obter_temperatura_gpu():
    return WinTmp.GPU_Temp

@app.route('/cpu-temp', methods=['GET'])
def api_temperatura_cpu():
    """
    Endpoint da API para obter a temperatura da CPU.
    """
    temperatura = obter_temperatura_cpu()
    
    if temperatura is not None:
        return jsonify({
            'unidade': 'Celsius',
            'temperatura_cpu': round(temperatura, 2)
        })
    else:
        # Mensagem de erro caso a temperatura não possa ser lida
        sistema = platform.system()
        mensagem_erro = (
            f"Não foi possível obter a temperatura da CPU no sistema operacional '{sistema}'. "
            "Isso pode acontecer por falta de sensores compatíveis, falta de permissões "
            "(tente executar como administrador/root) ou o sistema operacional não suporta esta funcionalidade via psutil."
        )
        return jsonify({'erro': mensagem_erro}), 501 # 501 Not Implemented

# Rota raiz para fornecer uma mensagem de boas-vindas
@app.route('/', methods=['GET'])
def index():
    return "<h1>API de Monitoramento de Temperatura</h1><p>Use o endpoint /temperatura-cpu para obter a temperatura da CPU.</p>"

if __name__ == '__main__':
    # Executa a aplicação
    # O host '0.0.0.0' torna a API acessível na rede local
    app.run(host='0.0.0.0', port=5000, debug=True)
