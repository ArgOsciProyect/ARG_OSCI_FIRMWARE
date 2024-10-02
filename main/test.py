import socket
import time

# Configuraciones del socket
HOST = '192.168.4.1'  # Dirección IP del ESP32 (en modo AP suele ser esta)
PORT = 3333           # Puerto configurado en el código C

# Tamaño del buffer que coincide con BUFFER_SIZE del ESP32
BUFFER_SIZE = 1440*2*6  # 1024 muestras de 16 bits (2 bytes por muestra)

# Conectar al socket
with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    print("Conectando al servidor...")
    s.connect((HOST, PORT))
    print("Conectado.")

    total_bytes = 0
    start_time = time.time()

    try:
        while True:
            data = s.recv(BUFFER_SIZE)
            if not data:
                break

            total_bytes += len(data)

            # Medir el tiempo cada 10 segundos
            elapsed_time = time.time() - start_time
            if elapsed_time >= 10:
                mb_received = total_bytes / (1024 * 1024)  # Convertir a MB
                speed = mb_received / elapsed_time  # Velocidad en MB/s

                print(f"Velocidad de transmisión: {speed:.2f} MB/s")

                # Reiniciar los contadores para la siguiente medición
                total_bytes = 0
                start_time = time.time()

    except KeyboardInterrupt:
        print("Conexión interrumpida.")