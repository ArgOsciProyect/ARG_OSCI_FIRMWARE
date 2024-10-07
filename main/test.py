import socket
import time
import timeit
import matplotlib.pyplot as plt
from collections import deque
import numpy as np
from scipy.interpolate import interp1d
import threading

# Configuraciones del socket
HOST = '192.168.4.1'  # Dirección IP del ESP32 (en modo AP suele ser esta)
PORT = 8080           # Puerto configurado en el código C

# Tamaño del buffer que coincide con BUFFER_SIZE del ESP32
BUFFER_SIZE = 4096  # Ajustado para datos de 8 bits (1 byte por muestra)

# Número máximo de muestras a graficar
MAX_SAMPLES = 50000

# Período entre muestras en segundos (0.5 microsegundos)
PERIOD = 0.5e-6

# Cola para almacenar las últimas muestras recibidas
data_queue = deque(maxlen=MAX_SAMPLES)
queue_lock = threading.Lock()

def receive_data():
    total_bytes = 0
    total_recv_time = 0
    recv_count = 0
    start_time = timeit.default_timer()

    # Conectar al socket
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        print("Conectando al servidor...")
        s.connect((HOST, PORT))
        print("Conectado.")

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

                # Almacenar los datos recibidos de forma thread-safe
                with queue_lock:
                    data_queue.extend(data)

                # Medir el tiempo cada 10 segundos
                elapsed_time = timeit.default_timer() - start_time
                if elapsed_time >= 10:
                    speed = (total_bytes/(1024 * 1024)) / elapsed_time  # Velocidad en MB/s
                    print(f"Velocidad de transmisión: {speed:.2f} MB/s")
                    # Reiniciar los contadores para la siguiente medición
                    total_bytes = 0
                    total_recv_time = 0
                    recv_count = 0
                    start_time = timeit.default_timer()

        except KeyboardInterrupt:
            print("Conexión interrumpida.")

# Iniciar el thread de recepción de datos
recv_thread = threading.Thread(target=receive_data, daemon=True)
recv_thread.start()

# Graficar los datos en el hilo principal
plt.ion()  # Activar modo interactivo de matplotlib
fig, ax = plt.subplots()
line, = ax.plot([], [])

while True:
    with queue_lock:
        if len(data_queue) > 0:
            # Convertir la cola de datos en un array numpy
            data_bytes = bytes(data_queue)
            int_data = np.frombuffer(data_bytes, dtype=np.uint16)
            unpacked_data = [(val & 0x0FFF) for val in int_data]  # Extraer los primeros 12 bits

            # Graficar los datos recibidos con interpolación
            y = np.array(unpacked_data)
            x = np.arange(len(unpacked_data)) * PERIOD  # Convertir a tiempo
            f = interp1d(x, y, kind='linear')
            x_new = np.linspace(0, len(unpacked_data) * PERIOD - PERIOD, num=len(unpacked_data) * 10)
            y_new = f(x_new)

            line.set_data(x_new, y_new)
            ax.relim()
            ax.autoscale_view()

            plt.draw()
            plt.pause(0.01)  # Pequeña pausa para permitir el graficado en vivo
