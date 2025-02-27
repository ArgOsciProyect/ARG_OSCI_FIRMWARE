import serial
import csv
import time

def save_buffer_to_csv():
    # Configure serial port - adjust port name as needed
    ser = serial.Serial(
        port='/dev/ttyUSB0',  # Change this to match your system
        baudrate=115200,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
        bytesize=serial.EIGHTBITS,
        timeout=1
    )

    print("Waiting for data...")
    
    buffer_data = []
    recording = False
    
    try:
        while True:
            if ser.in_waiting:
                line = ser.readline().decode('utf-8').strip()
                
                if line == "START_BUFFER":
                    print("Started receiving buffer data...")
                    recording = True
                    continue
                    
                if line == "END_BUFFER":
                    print("Finished receiving buffer data")
                    break
                    
                if recording:
                    try:
                        value = int(line)
                        buffer_data.append(value)
                    except ValueError:
                        print(f"Skipping invalid value: {line}")
                        
        # Save to CSV
        if buffer_data:
            filename = f"adc_buffer_{time.strftime('%Y%m%d_%H%M%S')}.csv"
            with open(filename, 'w', newline='') as file:
                writer = csv.writer(file)
                writer.writerow(['Sample', 'Value'])  # Header
                for i, value in enumerate(buffer_data):
                    writer.writerow([i, value])
            print(f"Data saved to {filename}")
        else:
            print("No data received")
            
    except Exception as e:
        print(f"Error: {e}")
        
    finally:
        ser.close()
        print("Serial port closed")

if __name__ == "__main__":
    save_buffer_to_csv()