import socket
import threading
import queue
import numpy as np
import matplotlib.pyplot as plt
import time

# Configuración de conexión
ESP32_IP = "192.168.4.1"
ESP32_PORT = 8080
DATA_SIZE = 1440  # Tamaño de datos esperado
SAMPLES = DATA_SIZE // 2  # Número de muestras (2 bytes por muestra)

# Variables globales
is_compressed = False
transmission_active = True
transmission_speed = 0
received_bytes = 0
lock = threading.Lock()

# Definir las colas
recv_queue = queue.Queue(maxsize=10)  # Cola para la recepción
comp_queue = queue.Queue(maxsize=10)  # Cola para la compresión (si aplica)
graph_queue = queue.Queue(maxsize=10)  # Cola para la graficación

# Función para desempaquetar datos comprimidos de 12 bits
def unpack_12bit_data(input_data):
    output_data = np.zeros(SAMPLES, dtype=np.uint16)
    input_len = len(input_data)
    bit_buffer = 0
    bit_count = 0
    output_index = 0

    for i in range(input_len):
        bit_buffer |= input_data[i] << bit_count
        bit_count += 8

        if bit_count >= 12 and output_index < len(output_data):
            output_data[output_index] = bit_buffer & 0xFFF
            output_index += 1
            bit_buffer >>= 12
            bit_count -= 12

    return output_data

# Thread para recibir los datos desde la ESP32 y colocarlos en recv_queue
def receive_data(sock):
    global transmission_speed, transmission_active, received_bytes

    time_start = time.time()
    while transmission_active:
        try:
            raw_data = sock.recv(DATA_SIZE * 3 // 2 if is_compressed else DATA_SIZE * 2)
            if not raw_data:
                break  # Desconexión o error en la recepción

            recv_queue.put(raw_data)  # Colocar los datos recibidos en la cola
            received_bytes += len(raw_data)

            # Actualizar la velocidad de transmisión de forma atomica
            elapsed_time = time.time() - time_start
            if elapsed_time >= 10:  # Calcular cada 10 segundos
                with lock:
                    transmission_speed = received_bytes / elapsed_time
                received_bytes = 0
                time_start = time.time()

        except socket.error as e:
            print(f"Error receiving data: {e}")
            transmission_active = False
            break

# Thread para descomprimir (si es necesario) y pasar los datos a la graph_queue
def compression_module():
    while transmission_active:
        raw_data = recv_queue.get()  # Obtener datos de la cola de recepción

        if is_compressed:
            decompressed_data = unpack_12bit_data(np.frombuffer(raw_data, dtype=np.uint8))
            comp_queue.put(decompressed_data)  # Colocar en la cola de compresión
        else:
            comp_queue.put(np.frombuffer(raw_data, dtype=np.uint16))

# Thread para graficar los datos
def plot_data():
    plt.ion()
    fig, ax = plt.subplots()
    line, = ax.plot(np.zeros(SAMPLES))

    while transmission_active:
        data_to_plot = comp_queue.get()  # Obtener datos de la cola de compresión
        line.set_ydata(data_to_plot)
        ax.relim()
        ax.autoscale_view()
        plt.draw()
        plt.pause(0.05)

    plt.ioff()
    plt.show()

# Thread de baja prioridad para imprimir la velocidad de transmisión
def print_speed():
    while transmission_active:
        time.sleep(10)
        with lock:
            print(f"Velocidad de transmisión: {transmission_speed / 1024:.2f} KB/s")

# Función principal para conectar, recibir y gestionar threads
def main():
    global is_compressed, transmission_active

    compress_choice = input("¿Están los datos comprimidos? (s/n): ").lower()
    is_compressed = compress_choice == "s"

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((ESP32_IP, ESP32_PORT))
    print(f"Conectado a la ESP32 en {ESP32_IP}:{ESP32_PORT}")

    # Crear y comenzar los threads
    receiver_thread = threading.Thread(target=receive_data, args=(sock,))
    compression_thread = threading.Thread(target=compression_module)
    plot_thread = threading.Thread(target=plot_data)
    speed_thread = threading.Thread(target=print_speed)

    # Priorizar los threads críticos
    receiver_thread.start()
    compression_thread.start()
    plot_thread.start()
    speed_thread.start()

    try:
        receiver_thread.join()
        compression_thread.join()
        plot_thread.join()
    except KeyboardInterrupt:
        print("Deteniendo transmisión...")
        transmission_active = False
        sock.close()

if __name__ == "__main__":
    main()