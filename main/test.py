import socket
import threading
import numpy as np
import matplotlib.pyplot as plt
import time

# Configuración de conexión
ESP32_IP = "192.168.4.1"  # Dirección IP de la ESP32 (modo AP)
ESP32_PORT = 8080  # Puerto de la ESP32

DATA_SIZE = 1440  # Tamaño de datos esperado
SAMPLES = DATA_SIZE // 2  # Número de muestras (2 bytes por muestra)

# Variables globales para almacenamiento y control
data_buffer = np.zeros(SAMPLES, dtype=np.uint16)
raw_data_buffer = []
total_bytes_received = 0  # Para el cálculo de velocidad
transmission_speed = 0
is_compressed = False
transmission_active = True
buffer_lock = threading.Lock()

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

# Hilo para recibir los datos desde la ESP32
def receive_data(sock):
    global raw_data_buffer, total_bytes_received, transmission_active

    while transmission_active:
        try:
            # Recepción de datos desde la ESP32
            raw_data = sock.recv(DATA_SIZE * 3 // 2 if is_compressed else DATA_SIZE * 2)
            if not raw_data:
                break  # Desconexión o error en la recepción

            # Almacenar los datos crudos en el buffer
            with buffer_lock:
                raw_data_buffer.append(raw_data)

            total_bytes_received += len(raw_data)

        except socket.error as e:
            print(f"Error receiving data: {e}")
            transmission_active = False
            break

# Hilo para descomprimir los datos
def decompress_data():
    global raw_data_buffer, data_buffer, transmission_active

    while transmission_active:
        if raw_data_buffer:
            with buffer_lock:
                raw_data = raw_data_buffer.pop(0)

            if is_compressed:
                decompressed_data = unpack_12bit_data(np.frombuffer(raw_data, dtype=np.uint8))
            else:
                decompressed_data = np.frombuffer(raw_data, dtype=np.uint16)

            with buffer_lock:
                data_buffer[:] = decompressed_data

        time.sleep(0.01)  # Ajustar según la necesidad de procesamiento

# Hilo para calcular la velocidad de transmisión
def calculate_speed():
    global total_bytes_received, transmission_speed, transmission_active

    previous_bytes = 0
    while transmission_active:
        time.sleep(1)  # Calcular cada segundo
        with buffer_lock:
            transmission_speed = (total_bytes_received - previous_bytes) / 1024  # En KB/s
            previous_bytes = total_bytes_received
        print(f"Velocidad de transmisión: {transmission_speed:.2f} KB/s")

# Función para graficar los datos
def plot_data():
    global data_buffer, transmission_active

    plt.ion()  # Modo interactivo de matplotlib
    fig, ax = plt.subplots()
    line, = ax.plot(data_buffer)

    while transmission_active:
        with buffer_lock:
            line.set_ydata(data_buffer)
            ax.relim()
            ax.autoscale_view()

        plt.draw()
        plt.pause(0.05)  # Ajustar si es necesario

    plt.ioff()
    plt.show()

# Función principal para conectar, recibir y graficar
def main():
    global is_compressed, transmission_active

    # Preguntar si los datos están comprimidos
    compress_choice = input("¿Están los datos comprimidos? (s/n): ").lower()
    is_compressed = compress_choice == "s"

    # Crear socket y conectar a ESP32
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((ESP32_IP, ESP32_PORT))
    print(f"Conectado a la ESP32 en {ESP32_IP}:{ESP32_PORT}")

    # Crear y comenzar el hilo de recepción con prioridad alta
    receiver_thread = threading.Thread(target=receive_data, args=(sock,))
    receiver_thread.start()

    # Crear y comenzar el hilo para descomprimir los datos
    decompression_thread = threading.Thread(target=decompress_data, daemon=True)
    decompression_thread.start()

    # Crear y comenzar el hilo para calcular la velocidad
    speed_thread = threading.Thread(target=calculate_speed, daemon=True)
    speed_thread.start()

    # Iniciar el gráfico en el hilo principal
    try:
        plot_data()
    except KeyboardInterrupt:
        print("Deteniendo transmisión...")
        transmission_active = False
        receiver_thread.join()
        decompression_thread.join()
        speed_thread.join()
        sock.close()

if __name__ == "__main__":
    main()
