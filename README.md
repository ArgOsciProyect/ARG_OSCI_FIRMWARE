# Proyecto de Osciloscopio con ESP32

Este proyecto consiste en el desarrollo de un osciloscopio utilizando un ESP32. En esta fase, se implementará la parte de comunicación BLE que se utilizará solo para la configuración inicial.

## Flujo de Trabajo del Firmware

1. **Conexión BLE Inicial**:
    - El ESP32 se conecta al dispositivo externo mediante BLE.
    - Se intercambia un mensaje de reconocimiento (simil ACK) para confirmar la conexión.

2. **Intercambio de Claves**:
    - El ESP32 envía una clave pública al dispositivo externo para encriptar la clave AES.
    - El dispositivo externo envía la clave AES cifrada con la clave pública.
    - El ESP32 descifra la clave AES y a partir de ahora toda la información se cifra con AES.

3. **Configuración de Red**:
    - El dispositivo externo decide si el ESP32 actuará como punto de acceso (AP) o se conectará a un AP existente.
    - La decisión se comunica al ESP32 mediante un mensaje JSON con los campos de interés.

4. **Conexión a un AP Existente**:
    - Si se decide usar un AP existente, el ESP32 escanea y comunica las redes WiFi detectadas al dispositivo externo.
    - El dispositivo externo devuelve el SSID y la contraseña de la red a la que el ESP32 debe conectarse.
    - El ESP32 se conecta a la red con las credenciales proporcionadas y actualiza el campo `APCONN` a `true`.

5. **Configuración como AP**:
    - Si se decide usar el ESP32 como AP, este genera aleatoriamente el SSID y la contraseña y los comunica al dispositivo externo.
    - El dispositivo externo se conecta al AP del ESP32 y actualiza el campo `APCONN` a `true`.

6. **Levantamiento de Sockets**:
    - Una vez en la misma red, el ESP32 levanta dos sockets para comunicaciones no relacionadas.
    - El ESP32 envía la información necesaria para conectarse a estos dos sockets al dispositivo externo.

## Ejemplo de JSON de Configuración

```json
{
    "use_esp_ap": null,
    "available_networks": [],
    "selected_ssid": "",
    "password": "",
    "ap_conn": false,
    "socket1_port": 12345,
    "socket2_port": 12346
}