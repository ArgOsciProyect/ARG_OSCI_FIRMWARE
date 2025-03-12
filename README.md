# ESP32 ARG_OSCI Firmware

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

ESP32 ARG_OSCI Firmware transforms your ESP32 into a versatile digital oscilloscope capable of capturing and transmitting analog signals in real-time. It pairs with the ARG_OSCI desktop and mobile application to create a complete signal visualization solution.

## For Users

This firmware converts your ESP32 into a fully functional digital oscilloscope with dual acquisition modes (internal or external ADC), configurable sampling rates (up to 2.5 MHz with external ADC), signal generation capabilities for testing, adjustable trigger settings, and secure communication with RSA encryption.

### Hardware Setup

**Requirements:**

- ESP32 development board
- Optional: External ADC for higher sampling rates (via SPI)
- Input signal conditioning circuit (recommended for protecting ESP32 inputs)
- Micro USB cable for programming and power

**Connection Diagram:**

For internal ADC mode:

- Connect signal input to GPIO pin defined in `SINGLE_INPUT_PIN` (default: GPIO19)
- 1kHz square wave calibration signal available on `SQUARE_WAVE_GPIO` (default: GPIO18)
- Trigger level output on `TRIGGER_PWM_GPIO` (default: GPIO26)

For external ADC mode:

- Connect external ADC to SPI pins:
  - MISO: GPIO12
  - SCLK: GPIO14
  - CS: GPIO15

### How to Get Started

1. **Clone the repository:**

   ```bash
   git clone https://github.com/ArgOsciProyect/ARG_OSCI_FIRMWARE
   cd ARG_OSCI_FIRMWARE
   ```

2. **Configure the firmware:**
   - To select internal or external ADC mode, modify the `globals.h` file
   - Comment out the line `#define USE_EXTERNAL_ADC` for internal ADC mode
   - Keep or add this definition to use an external ADC

3. **Build and flash:**

   ```bash
   idf.py build
   idf.py -p [PORT] flash monitor
   ```

4. **Connect to the oscilloscope:**
   - The ESP32 creates a WiFi access point named "ESP32_AP" with password "password123"
   - Use the [ARG_OSCI mobile or desktop application](https://github.com/ArgOsciProyect/ARG_OSCI_APP/releases) to connect and visualize signals

### Usage Instructions

1. **Initial Connection:**
   - Connect to the "ESP32_AP" WiFi network
   - Open the ARG_OSCI application and follow the connection prompts
   - The firmware handles the secure key exchange automatically

2. **Acquisition Modes:**
   - Normal mode: Continuous data streaming
   - Single mode: Captures one frame when trigger conditions are met

3. **Adjusting Trigger Settings:**
   - Set trigger level as percentage (0-100%)
   - Select trigger edge (positive/negative)
   - In single mode, the system waits for the specified trigger event

4. **Sampling Frequency Control:**
   - Use the frequency controls in the app to adjust sampling rate
   - Higher sampling rates provide better resolution for high-frequency signals

### Customization

The firmware can be customized by modifying parameters in `globals.h`:

- `ADC_CHANNEL`: GPIO input channel for internal ADC
- `SAMPLE_RATE_HZ`: Base sampling rate for internal ADC (default: 600 kHz)
- `BUF_SIZE`: Buffer size for data acquisition
- `SQUARE_WAVE_FREQ`: Frequency of the calibration square wave (default: 1 kHz)

For external ADC configuration, modify the SPI parameters in the `spi_matrix` definition.

### Troubleshooting

If you encounter connection issues:

- Ensure the ESP32 is powered and operational (The LED should be on)
- Verify you're connected to the "ESP32_AP" WiFi network
- Check serial output for error messages using `idf.py monitor`

For acquisition problems:

- Make sure input signal is within the acceptable voltage range
- Try adjusting the trigger level if using single mode
- Reduce or increase sampling rate for more stable operation
- Disable/enable the hysteresis or the 50Khz filter applied to the trigger

## For Contributors

Thank you for considering contributing to the ESP32 ARG_OSCI Firmware! This project aims to provide a robust and flexible oscilloscope firmware for ESP32 devices.

The codebase follows a modular design with clear separation of concerns. Key areas for contribution include:

- **Acquisition Module:** Improving the sampling algorithms and adding support for different ADC configurations
- **Network Module:** Enhancing WiFi connectivity and data transmission efficiency
- **Security Features:** Strengthening the encryption and authentication mechanisms
- **Signal Processing:** Developing on-device processing capabilities to reduce data bandwidth
- **Hardware Support:** Adding compatibility with additional ESP32 variants and external components

To get started, familiarize yourself with the project structure. Core modules include:

- `acquisition.c`: Handles signal sampling and buffer management
- `network.c`: Manages WiFi connectivity and client communications
- `data_transmission.c`: Handles the formatting and sending of data packets
- `webservers.c`: Implements the configuration web interface
- `crypto.c`: Provides encryption and security features
- `main.c`: Entry point and initialization

For more detailed information on how to contribute, please see the [CONTRIBUTING.md](CONTRIBUTING.md) file.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Acknowledgements

This project is part of the ARG_OSCI oscilloscope visualization tool suite. For the companion application, visit the [ARG_OSCI App repository](https://github.com/ArgOsciProyect/ARG_OSCI_APP).
