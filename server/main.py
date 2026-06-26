"""
API REST para monitoramento de temperatura da CPU e GPU.

Dependencias:
- Flask
- WinTmp

O pacote WinTmp traz a DLL do LibreHardwareMonitor, mas sua inicializacao
padrao habilita sensores extras que podem exigir DLLs ausentes. Aqui usamos a
DLL diretamente e habilitamos apenas CPU/GPU, que sao os dados usados pelo
ESP8266.
"""

import os
import platform
import threading

from flask import Flask, jsonify

app = Flask(__name__)


def _load_hardware_monitor():
    import clr

    script_dir = os.path.dirname(os.path.abspath(__file__))
    vendor_dir = os.path.join(script_dir, "vendor", "LibreHardwareMonitor")
    dll_path = os.path.join(vendor_dir, "LibreHardwareMonitorLib.dll")

    if not os.path.exists(dll_path):
        raise RuntimeError(
            "LibreHardwareMonitorLib.dll nao encontrada. "
            f"Esperado em: {dll_path}"
        )

    os.add_dll_directory(vendor_dir)
    os.chdir(vendor_dir)
    clr.AddReference(dll_path)

    from LibreHardwareMonitor import Hardware

    computer = Hardware.Computer()
    computer.IsCpuEnabled = True
    computer.IsGpuEnabled = True
    computer.Open()

    return Hardware, computer


Hardware, hw = _load_hardware_monitor()

# Serializa o acesso ao hardware: o objeto Computer e compartilhado e o
# LibreHardwareMonitor nao e thread-safe. Como o servidor Flask atende
# requisicoes em multiplas threads, chamadas simultaneas a Update() poderiam
# corromper a leitura ou travar.
_hw_lock = threading.Lock()

GPU_SENSORS = (
    Hardware.HardwareType.GpuNvidia,
    Hardware.HardwareType.GpuAmd,
    Hardware.HardwareType.GpuIntel,
)


def _update_hardware(hardware):
    hardware.Update()

    for sub_hardware in hardware.SubHardware:
        sub_hardware.Update()


def _temperature_sensors(hardware):
    _update_hardware(hardware)

    for sensor in hardware.Sensors:
        if sensor.SensorType == Hardware.SensorType.Temperature and sensor.Value is not None:
            yield sensor

    for sub_hardware in hardware.SubHardware:
        for sensor in sub_hardware.Sensors:
            if sensor.SensorType == Hardware.SensorType.Temperature and sensor.Value is not None:
                yield sensor


def _sensor_value(sensor):
    if sensor.Value is None:
        return None

    value = float(sensor.Value)
    if value <= 0.0:
        return None

    return value


def _wmi_temperatura_cpu():
    try:
        import wmi
    except Exception:
        return None

    namespaces = ("root\\LibreHardwareMonitor", "root\\OpenHardwareMonitor")
    preferred_names = ("package", "tctl", "tdie", "cpu")

    for namespace in namespaces:
        try:
            connection = wmi.WMI(namespace=namespace)
            sensors = connection.Sensor()
        except Exception:
            continue

        candidates = []
        for sensor in sensors:
            if getattr(sensor, "SensorType", None) != "Temperature":
                continue

            name = str(getattr(sensor, "Name", ""))
            value = getattr(sensor, "Value", None)

            try:
                value = float(value)
            except (TypeError, ValueError):
                continue

            if value <= 0.0:
                continue

            lowered_name = name.lower()
            if any(token in lowered_name for token in preferred_names):
                candidates.append(value)

        if candidates:
            return candidates[0]

    return None


def _acpi_temperatura_cpu():
    """Fallback que le a temperatura via ACPI (namespace WMI root\\WMI).

    Nao depende do driver ring0 do LibreHardwareMonitor, entao funciona mesmo
    com a Integridade de Memoria (HVCI) ativa. Exige privilegios de admin e
    nem todo notebook expoe uma zona termica util (pode ser da placa, nao do
    nucleo da CPU).
    """
    try:
        import wmi
    except Exception:
        return None

    try:
        connection = wmi.WMI(namespace="root\\WMI")
        zonas = connection.MSAcpi_ThermalZoneTemperature()
    except Exception:
        return None

    valores = []
    for zona in zonas:
        bruto = getattr(zona, "CurrentTemperature", None)
        if bruto is None:
            continue

        try:
            # CurrentTemperature vem em decimos de Kelvin.
            celsius = float(bruto) / 10.0 - 273.15
        except (TypeError, ValueError):
            continue

        if 0.0 < celsius < 125.0:
            valores.append(celsius)

    if valores:
        return max(valores)

    return None


def _fallback_temperatura_cpu():
    valor = _wmi_temperatura_cpu()
    if valor is not None:
        return valor

    return _acpi_temperatura_cpu()


def obter_temperatura_cpu():
    for hardware in hw.Hardware:
        if hardware.HardwareType != Hardware.HardwareType.Cpu:
            continue

        sensors = list(_temperature_sensors(hardware))
        if not sensors:
            return _fallback_temperatura_cpu()

        for sensor in sensors:
            value = _sensor_value(sensor)
            if value is not None and "package" in sensor.Name.lower():
                return value

        for sensor in sensors:
            value = _sensor_value(sensor)
            if value is not None:
                return value

        return _fallback_temperatura_cpu()

    return _fallback_temperatura_cpu()


def obter_temperatura_gpu():
    for hardware in hw.Hardware:
        if hardware.HardwareType not in GPU_SENSORS:
            continue

        sensors = list(_temperature_sensors(hardware))
        if not sensors:
            continue

        for sensor in sensors:
            value = _sensor_value(sensor)
            if value is not None and ("gpu core" in sensor.Name.lower() or "core" in sensor.Name.lower()):
                return value

        for sensor in sensors:
            value = _sensor_value(sensor)
            if value is not None:
                return value

    return None


def listar_sensores():
    sensores = []

    for hardware in hw.Hardware:
        _update_hardware(hardware)
        sensores.extend(_serializar_sensores_hardware(hardware))

        for sub_hardware in hardware.SubHardware:
            sensores.extend(_serializar_sensores_hardware(sub_hardware, hardware.Name))

    return sensores


def _serializar_sensores_hardware(hardware, parent_name=None):
    sensores = []

    for sensor in hardware.Sensors:
        value = None if sensor.Value is None else float(sensor.Value)
        sensores.append(
            {
                "parent": parent_name,
                "hardware": str(hardware.Name),
                "hardware_type": str(hardware.HardwareType),
                "sensor": str(sensor.Name),
                "sensor_type": str(sensor.SensorType),
                "value": value,
            }
        )

    return sensores


def _erro_temperatura(tipo):
    sistema = platform.system()
    mensagem_erro = (
        f"Nao foi possivel obter a temperatura da {tipo} no sistema operacional '{sistema}'. "
        "Isso pode acontecer por falta de sensores compativeis, falta de permissoes "
        "ou ausencia de leitura exposta pelo hardware."
    )
    return jsonify({"erro": mensagem_erro}), 501


@app.route("/cpu-temp", methods=["GET"])
def api_temperatura_cpu():
    with _hw_lock:
        temperatura_cpu = obter_temperatura_cpu()

    if temperatura_cpu is None:
        return _erro_temperatura("CPU")

    print(f"Temperatura CPU atual: {temperatura_cpu:.2f} C")
    return jsonify(
        {
            "unidade": "Celsius",
            "temperatura_cpu": round(temperatura_cpu, 2),
        }
    )


@app.route("/gpu-temp", methods=["GET"])
def api_temperatura_gpu():
    with _hw_lock:
        temperatura_gpu = obter_temperatura_gpu()

    if temperatura_gpu is None:
        return _erro_temperatura("GPU")

    print(f"Temperatura GPU atual: {temperatura_gpu:.2f} C")
    return jsonify(
        {
            "unidade": "Celsius",
            "temperatura_gpu": round(temperatura_gpu, 2),
        }
    )


@app.route("/sensors", methods=["GET"])
def api_sensores():
    with _hw_lock:
        sensores = listar_sensores()
    return jsonify({"sensores": sensores})


@app.route("/", methods=["GET"])
def index():
    return (
        "<h1>API de Monitoramento de Temperatura</h1>"
        "<p>Use /cpu-temp para obter a temperatura da CPU.</p>"
        "<p>Use /gpu-temp para obter a temperatura da GPU.</p>"
    )


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=False)
