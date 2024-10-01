import socket
import time
import select
import threading
import matplotlib.pyplot as plt
from queue import Queue
from scipy.interpolate import interp1d
import numpy as np
import struct
from scipy.signal import find_peaks

# Configuración del servidor
HOST = '192.168.4.1'  # IP del AP de la ESP32
PORT = 8080           # Puerto del servidor
BUFFER_SIZE = 4096    # Tamaño del buffer para TCP
BUFFER_SIZE_UDP = 1024 * 32  # Tamaño del buffer para UDP
USE_TCP = True        # Cambia a False para usar UDP

data_queue = Queue(maxsize=1000)  # Cola para almacenar los datos recibidos

def receive_data():
    if USE_TCP:
        sock_type = socket.SOCK_STREAM
        protocol = "TCP"
    else:
        sock_type = socket.SOCK_DGRAM
        protocol = "UDP"

    with socket.socket(socket.AF_INET, sock_type) as s:
        server_address = (HOST, PORT)
        
        if USE_TCP:
            s.connect(server_address)
            print(f"Conectado a {HOST}:{PORT} usando {protocol}")

            # Enviar mensaje de handshake
            handshake_message = "HANDSHAKE"
            s.sendall(handshake_message.encode())
            print("Mensaje de handshake enviado")
        else:
            print(f"Usando {protocol} para enviar datos a {HOST}:{PORT}")
            s.sendto(b"DATA_REQUEST", server_address)

        total_bytes = 0
        start_time = time.time()
        last_packet_time = start_time
        max_packet_interval = 0

        try:
            while True:
                ready = select.select([s], [], [], 1.0)
                if ready[0]:
                    packet_start_time = time.time()
                    if USE_TCP:
                        data = s.recv(BUFFER_SIZE)
                    else:
                        data, _ = s.recvfrom(BUFFER_SIZE_UDP)
                    
                    if not data:
                        break
                    total_bytes += len(data)

                    # Calcular el intervalo entre paquetes
                    packet_interval = packet_start_time - last_packet_time
                    last_packet_time = packet_start_time

                    # Actualizar el tiempo mínimo registrado
                    if packet_interval > max_packet_interval:
                        max_packet_interval = packet_interval

                    # Añadir datos a la cola
                    for i in range(0, len(data), 2):
                        if i + 1 < len(data):
                            value = struct.unpack('H', data[i:i+2])[0]
                            data_queue.put(value)

                    print(f"Tiempo mínimo entre paquetes: {max_packet_interval:.6f} segundos", end='\r')
        except KeyboardInterrupt:
            pass

        end_time = time.time()
        duration = end_time - start_time
        speed = total_bytes / duration / 1024 / 1024  # Convertir a MB/s

        print(f"\nTotal de datos recibidos: {total_bytes} bytes")
        print(f"Duración: {duration:.2f} segundos")
        print(f"Velocidad de transmisión promedio: {speed:.2f} MB/s")

        # Verificar si la velocidad es suficiente para transmitir la señal
        required_speed = 4  # MB/s
        if speed >= required_speed:
            print("La velocidad es suficiente para transmitir una señal de 16 bits a 2 MHz.")
        else:
            print("La velocidad NO es suficiente para transmitir una señal de 16 bits a 2 MHz.")
        
        if not USE_TCP:
            s.sendto(b"SHUTDOWN", server_address)

def plot_data():
    plt.ion()
    fig, ax = plt.subplots()
    x_data = []
    y_data = []
    line, = ax.plot(x_data, y_data)
    ax.set_ylim(0, 65535)  # Rango de 16 bits

    sample_interval = 0.5e-6  # Intervalo de muestreo en segundos (0.5 microsegundos)

    while True:
        while not data_queue.empty():
            y_data.append(data_queue.get())
            x_data.append(len(x_data))

            # Interpolación
            if len(x_data) > 1:
                f = interp1d(x_data, y_data, kind='linear')
                x_interp = np.linspace(min(x_data), max(x_data), num=len(x_data)*10)
                y_interp = f(x_interp)
                line.set_xdata(x_interp)
                line.set_ydata(y_interp)
            else:
                line.set_xdata(x_data)
                line.set_ydata(y_data)

            ax.relim()
            ax.autoscale_view()
            plt.draw()
            plt.pause(0.01)

            # Calcular la frecuencia de la señal
            if len(y_data) > 1:
                peaks, _ = find_peaks(y_data)
                if len(peaks) > 1:
                    peak_intervals = np.diff(peaks) * sample_interval
                    avg_peak_interval = np.mean(peak_intervals)
                    frequency = 1 / avg_peak_interval if avg_peak_interval > 0 else 0
                    print(f"Frecuencia de la señal: {frequency:.2f} Hz", end='\r')

def main():
    receive_thread = threading.Thread(target=receive_data)
    receive_thread.start()

    plot_data()  # Run plot_data in the main thread

    receive_thread.join()

if __name__ == "__main__":
    main()