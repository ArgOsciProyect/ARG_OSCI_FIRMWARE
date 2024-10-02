import socket
import numpy as np
import struct
import matplotlib.pyplot as plt
import threading
import time
from queue import Queue

# Parámetros de conexión
TCP_IP = '192.168.4.1'  # Dirección IP del ESP32 en modo AP o IP del servidor
TCP_PORT = 8080
BUFFER_SIZE = 1440  # Tamaño del segmento TCP

# Parámetros de los datos
DATA_SIZE = 960  # Número de muestras empaquetadas en 1440 bytes
ADC_MAX_VALUE = 4095

# Cola para pasar los datos entre los hilos
data_queue = Queue()

# Función para descomprimir datos de 12 bits empaquetados en 3 bytes
def unpack_12bit_data(data):
    unpacked_data = []
    for i in range(0, len(data), 3):
        # Tomar 3 bytes y desempaquetarlos en dos muestras de 12 bits
        byte1 = data[i]
        byte2 = data[i + 1]
        byte3 = data[i + 2]
        
        sample1 = ((byte1 << 4) | (byte2 >> 4)) & 0x0FFF
        sample2 = ((byte2 & 0x0F) << 8) | byte3
        unpacked_data.append(sample1)
        unpacked_data.append(sample2)
    
    return np.array(unpacked_data)

# Función de recepción de datos (hilo 1)
def receive_data():
    global total_bytes_received
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((TCP_IP, TCP_PORT))
    start_time = time.time()
    total_bytes_received = 0

    try:
        while True:
            # Recibir datos comprimidos
            data = sock.recv(BUFFER_SIZE)
            if not data:
                break

            # Aumentar el contador de bytes recibidos
            total_bytes_received += len(data)

            # Descomprimir los datos recibidos
            unpacked_data = unpack_12bit_data(data)

            # Colocar los datos descomprimidos en la cola
            data_queue.put(unpacked_data)

    except KeyboardInterrupt:
        print("Conexión terminada por el usuario.")

    finally:
        sock.close()

# Función de graficación (hilo 2)
def plot_data():
    plt.ion()  # Activar modo interactivo de matplotlib
    fig, ax = plt.subplots()
    line, = ax.plot([], [], 'b-')
    ax.set_ylim(0, ADC_MAX_VALUE)
    ax.set_xlim(0, DATA_SIZE)
    plt.title("Señal recibida")
    plt.xlabel("Muestras")
    plt.ylabel("Valor ADC")

    try:
        while True:
            # Si hay datos disponibles en la cola, graficarlos
            if not data_queue.empty():
                unpacked_data = data_queue.get()
                line.set_xdata(np.arange(len(unpacked_data)))
                line.set_ydata(unpacked_data)
                plt.draw()
                plt.pause(0.01)

            # Mostrar velocidad de transmisión (bytes por segundo)
            elapsed_time = time.time() - start_time
            if elapsed_time > 0:
                speed = total_bytes_received / elapsed_time / 1024  # en KB/s
                print(f"Velocidad de transmisión: {speed:.2f} KB/s")

    except KeyboardInterrupt:
        print("Graficación terminada por el usuario.")

    finally:
        plt.ioff()  # Desactivar modo interactivo
        plt.show()

# Crear hilos para la recepción y la graficación
receive_thread = threading.Thread(target=receive_data)
plot_thread = threading.Thread(target=plot_data)

# Iniciar ambos hilos
receive_thread.start()
plot_thread.start()

# Esperar a que ambos hilos terminen (opcional)
receive_thread.join()
plot_thread.join()
