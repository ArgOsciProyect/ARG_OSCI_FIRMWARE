import socket
import time

# Configuraciones del socket
HOST = '192.168.4.1'  # Dirección IP del ESP32 (en modo AP suele ser esta)
PORT = 3333           # Puerto configurado en el código C

# Tamaño del buffer que coincide con BUFFER_SIZE del ESP32
BUFFER_SIZE = 1440 * 2 * 6  # 1024 muestras de 16 bits (2 bytes por muestra)
COMPRESSED_BUFFER_SIZE = (BUFFER_SIZE * 3) // 4  # Tamaño del buffer comprimido

def unpack_12bit_data(compressed_data):
    num_samples = (len(compressed_data) * 2) // 3
    decompressed_data = []

    for i in range(0, len(compressed_data), 3):
        # Desempaquetar tres bytes en dos muestras de 12 bits
        byte1 = compressed_data[i]
        byte2 = compressed_data[i + 1]
        byte3 = compressed_data[i + 2]

        sample1 = (byte1 << 4) | (byte2 >> 4)
        sample2 = ((byte2 & 0x0F) << 8) | byte3

        decompressed_data.append(sample1)
        decompressed_data.append(sample2)

    return decompressed_data

# Conectar al socket
with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    print("Conectando al servidor...")
    s.connect((HOST, PORT))
    print("Conectado.")

    total_bytes = 0
    start_time = time.time()

    try:
        while True:
            compressed_data = s.recv(COMPRESSED_BUFFER_SIZE)
            if not compressed_data:
                break

            decompressed_data = unpack_12bit_data(compressed_data)
            total_bytes += len(decompressed_data) * 2  # Cada muestra es de 2 bytes

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