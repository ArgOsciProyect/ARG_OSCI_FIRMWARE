import serial
import csv
import time

def save_buffer_to_csv():
    # Configure serial port - adjust port name as needed
    ser = serial.Serial(
        port='COM3',  # Change this to match your system
        baudrate=115200,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
        bytesize=serial.EIGHTBITS,
        timeout=1
    )

    print("Waiting for data...")
    
    buffer_data = []
    recording = False
    temp_byte = None
    
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
                        if temp_byte is None:
                            temp_byte = value
                        else:
                            # Combine bytes into 16-bit value
                            full_value = (value << 8) | temp_byte
                            # Apply mask 0x0FFF
                            masked_value = full_value & 0x0FFF
                            buffer_data.append(masked_value)
                            temp_byte = None
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
            print(f"First few values: {buffer_data[:5]}")  # Print first 5 values for debugging
        else:
            print("No data received")
            
    except Exception as e:
        print(f"Error: {e}")
        
    finally:
        ser.close()
        print("Serial port closed")

if __name__ == "__main__":
    save_buffer_to_csv()