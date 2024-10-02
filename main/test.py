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

        if bit_count >= 12:
            output_data[output_index] = bit_buffer & 0xFFF
            output_index += 1
            bit_buffer >>= 12
            bit_count -= 12

    return output_data

# Hilo para recibir los datos desde la ESP32
def receive_data(sock):
    global data_buffer, transmission_speed, transmission_active

    time_start = time.time()
    total_bytes = 0  # Contador para medir la velocidad
    while transmission_active:
        try:
            # Recepción de datos desde la ESP32
            raw_data = sock.recv(DATA_SIZE * 3 // 2 if is_compressed else DATA_SIZE * 2)
            if not raw_data:
                break  # Desconexión o error en la recepción

            # Descomprimir o procesar los datos
            with buffer_lock:
                if is_compressed:
                    data_buffer = unpack_12bit_data(np.frombuffer(raw_data, dtype=np.uint8))
                else:
                    data_buffer = np.frombuffer(raw_data, dtype=np.uint16)

            total_bytes += len(raw_data)
            # Actualizar la velocidad de transmisión
            elapsed_time = time.time() - time_start
            if elapsed_time > 1:  # Actualizar cada segundo
                transmission_speed = total_bytes / elapsed_time
                time_start = time.time()
                total_bytes = 0

        except socket.error as e:
            print(f"Error receiving data: {e}")
            transmission_active = False
            break

# Hilo para calcular la velocidad de transmisión
def calculate_speed():
    global transmission_speed, transmission_active

    while transmission_active:
        time.sleep(1)  # Calcula la velocidad cada segundo
        print(f"Velocidad de transmisión: {transmission_speed / 1024:.2f} KB/s")

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
        sock.close()

if __name__ == "__main__":
    main()
