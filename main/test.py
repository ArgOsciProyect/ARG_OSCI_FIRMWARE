import socket
import time
import timeit
import matplotlib.pyplot as plt
from collections import deque
import numpy as np
from scipy.interpolate import interp1d

# Configuraciones del socket
HOST = '192.168.4.1'  # Dirección IP del ESP32 (en modo AP suele ser esta)
PORT = 8080           # Puerto configurado en el código C

# Tamaño del buffer que coincide con BUFFER_SIZE del ESP32
BUFFER_SIZE = 2048 * 2  # Ajustado para datos de 8 bits (1 byte por muestra)

# Número máximo de muestras a graficar
MAX_SAMPLES = 50000

# Período entre muestras en segundos (0.5 microsegundos)
PERIOD = 0.5e-6

# Conectar al socket
with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    print("Conectando al servidor...")
    s.connect((HOST, PORT))
    print("Conectado.")

    total_bytes = 0
    total_recv_time = 0
    recv_count = 0
    start_time = timeit.default_timer()
    data_list = deque(maxlen=MAX_SAMPLES)  # Cola para almacenar las últimas muestras recibidas

    try:
        while True:
            # Medir el tiempo de recepción de datos
            recv_start_time = timeit.default_timer()
            data = s.recv(BUFFER_SIZE)
            recv_end_time = timeit.default_timer()

            if not data:
                break

            total_bytes += len(data)
            total_recv_time += (recv_end_time - recv_start_time)
            recv_count += 1

            # Almacenar los datos recibidos
            data_list.extend(data)

            # Medir el tiempo cada 10 segundos
            elapsed_time = timeit.default_timer() - start_time
            if elapsed_time >= 10:
                mb_received = total_bytes / (1024 * 1024)  # Convertir a MB
                speed = mb_received / elapsed_time  # Velocidad en MB/s
                avg_recv_time = total_recv_time / recv_count  # Tiempo promedio de recepción por bloque
                avg_speed_based_on_block_time = (BUFFER_SIZE / avg_recv_time) / (1024 * 1024)

                print(f"Velocidad de transmisión: {speed:.2f} MB/s")
                print(f"Tiempo promedio de recepción por bloque: {avg_recv_time:.6f} segundos")
                print(f"Velocidad promedio de recepción por bloque: {avg_speed_based_on_block_time:.6f} MB/s")

                # Desempaquetar los datos recibidos
                data_bytes = bytes(data_list)  # Convertir deque a bytes
                int_data = np.frombuffer(data_bytes, dtype=np.uint16)
                unpacked_data = [(val & 0x0FFF) for val in int_data]  # Extraer los primeros 12 bits

                # Graficar los datos recibidos con interpolación
                y = np.array(unpacked_data)
                x = np.arange(len(unpacked_data)) * PERIOD  # Convertir a tiempo
                f = interp1d(x, y, kind='linear')
                x_new = np.linspace(0, len(unpacked_data) * PERIOD - PERIOD, num=len(unpacked_data) * 10)  # Ajustar el rango de x_new
                y_new = f(x_new)

                plt.figure()
                plt.plot(x_new, y_new)
                plt.title('Últimas muestras recibidas en los últimos 10 segundos (Interpolación)')
                plt.xlabel('Tiempo (s)')
                plt.ylabel('Valor')
                plt.show()

                # Reiniciar los contadores para la siguiente medición
                total_bytes = 0
                total_recv_time = 0
                recv_count = 0
                data_list.clear()  # Reiniciar la lista de datos
                start_time = timeit.default_timer()

    except KeyboardInterrupt:
        print("Conexión interrumpida.")