import socket
import threading
import matplotlib.pyplot as plt
from collections import deque
import time



def extract_12bit_values(data):
    """Extraer valores de 12 bits de un flujo de bytes correctamente."""
    values = []
    
    for i in range(0, len(data) - 1, 3):  # Procesar cada 3 bytes
        # Primer valor: toma los 8 bits del primer byte y los 4 más significativos del segundo byte
        value1 = (data[i] << 4) | (data[i + 1] >> 4)
        # Segundo valor: toma los 4 bits menos significativos del segundo byte y los 8 bits del tercer byte
        value2 = ((data[i + 1] & 0x0F) << 8) | data[i + 2]

        values.extend([value1, value2])

    return values

# Cola para almacenar datos crudos recibidos
data_queue = deque()

# Función para recibir datos
def receive_data(sock):
    global data_queue
    while True:
        data = sock.recv(1440)  # Leer 1440 bytes
        if not data:
            break
        data_queue.append(data)  # Agregar los datos crudos a la cola para que sean procesados en otro thread

# Función para descomprimir los datos en un thread aparte
def decompress_data():
    global data_queue
    while True:
        if len(data_queue) > 0:
            raw_data = data_queue.popleft()
            decompressed_values = extract_12bit_values(raw_data)
            # Aquí podrías hacer más procesamiento o graficar en otro thread
            plot_data(decompressed_values)

# Función para graficar los datos
def plot_data(data):
    plt.clf()
    plt.plot(data)
    plt.pause(0.001)

# Función principal
def main():
    # Conectar al servidor
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect(('192.168.4.1', 8080))  # Dirección IP del ESP32

    # Iniciar el hilo de recepción
    receive_thread = threading.Thread(target=receive_data, args=(sock,))
    receive_thread.daemon = True
    receive_thread.start()

    # Iniciar el hilo de descompresión
    decompress_thread = threading.Thread(target=decompress_data)
    decompress_thread.daemon = True
    decompress_thread.start()

    # Configurar Matplotlib
    plt.ion()  # Modo interactivo
    plt.show()

    # Mantener el programa corriendo
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        sock.close()

if __name__ == "__main__":
    main()
