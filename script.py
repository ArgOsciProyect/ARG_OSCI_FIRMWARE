import re
import csv

def parse_log_to_csv(input_file, output_file):
    # Regex pattern for ADC readings
    pattern = r'ESP32_AP: ADC Reading \[(\d+)\]: (\d+)'
    
    readings = []
    
    # Read and parse log file
    with open(input_file, 'r') as f:
        for line in f:
            match = re.search(pattern, line)
            if match:
                index = int(match.group(1))
                value = int(match.group(2))
                readings.append([index, value])
    
    # Write to CSV
    with open(output_file, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['Index', 'ADC_Value'])
        writer.writerows(readings)

# Usage
parse_log_to_csv('log.txt', 'adc_readings.csv')