# ESP32 ARG_OSCI Firmware

Firmware for the ESP32-based digital oscilloscope that pairs with the ARG_OSCI desktop and mobile application to create a complete signal visualization solution.

## Overview

This firmware transforms your ESP32 into a versatile digital oscilloscope capable of capturing and transmitting analog signals in real-time. It supports both internal and external ADC operation, configurable sampling rates, and multiple trigger modes.

Key features:
- Dual acquisition modes (continuous and trigger-based)
- Configurable sampling frequency (up to 2.5 MHz with external ADC)
- Multiple signal generation capabilities for testing (square wave, sine wave)
- Adjustable trigger level and edge detection
- Secure communication with RSA encryption

## Hardware Setup

### Requirements
- ESP32 development board
- Optional: External ADC for higher sampling rates
- Input signal conditioning circuit (recommended for protecting ESP32 inputs)
- Micro USB cable for programming and power

### Connection Diagram

For internal ADC mode:
- Connect signal input to GPIO pin defined in `SINGLE_INPUT_PIN` (default: GPIO19)
- 1kHz square wave calibration signal available on `SQUARE_WAVE_GPIO` (default: GPIO18)
- Trigger level output on `TRIGGER_PWM_GPIO` (default: GPIO26)

For external ADC mode:
- Connect external ADC to SPI pins as defined in configuration:
  - MISO: GPIO12
  - SCLK: GPIO14
  - CS: GPIO15

## Getting Started

1. **Clone the repository**
   ```
   git clone https://github.com/ArgOsciProyect/ARG_OSCI_FIRMWARE
   cd ARG_OSCI_FIRMWARE
   ```

2. **Configure the firmware**
   - Use `menuconfig` to select internal or external ADC mode
   ```
   idf.py menuconfig
   ```
   - Navigate to "Component config" -> "ARG_OSCI Configuration" and choose your preferred ADC mode

3. **Build and flash**
   ```
   idf.py build
   idf.py -p [PORT] flash monitor
   ```

4. **Connect to the oscilloscope**
   - The ESP32 creates a WiFi access point named "ESP32_AP" with password "password123"
   - Use the [ARG_OSCI mobile or desktop application](https://github.com/ArgOsciProyect/ARG_OSCI_APP/releases) to connect and visualize signals

## Usage Instructions

1. **Initial Connection**
   - Connect to the "ESP32_AP" WiFi network
   - Open the ARG_OSCI application and follow the connection prompts
   - The firmware handles the secure key exchange automatically

2. **Acquisition Modes**
   - Normal mode: Continuous data streaming
   - Single mode: Captures one frame when trigger conditions are met

3. **Adjusting Trigger Settings**
   - Set trigger level as percentage (0-100%)
   - Select trigger edge (positive/negative)
   - In single mode, the system waits for the specified trigger event

4. **Sampling Frequency Control**
   - Use the frequency controls in the app to adjust sampling rate
   - Higher sampling rates provide better resolution for high-frequency signals

## Customization

The firmware can be customized by modifying the following parameters in `globals.h`:

- `ADC_CHANNEL`: GPIO input channel for internal ADC
- `SAMPLE_RATE_HZ`: Base sampling rate for internal ADC
- `BUF_SIZE`: Buffer size for data acquisition
- `SQUARE_WAVE_FREQ`: Frequency of the calibration square wave

For external ADC configuration, modify the SPI parameters in the `spi_matrix` definition.

## Troubleshooting

If you encounter connection issues:
- Ensure the ESP32 is powered and operational (blue LED should be on)
- Verify you're connected to the "ESP32_AP" WiFi network
- Check serial output for error messages using `idf.py monitor`

For acquisition problems:
- Make sure input signal is within the acceptable voltage range (0-3.3V)
- Try adjusting the trigger level if using single mode
- Reduce sampling rate for more stable operation

## Development

For developers interested in extending the firmware:
- The codebase follows a modular design with clear separation of concerns
- Core modules: `acquisition.c`, `network.c`, `data_transmission.c`, `webservers.c`, `crypto.c`
- Entry point and initialization in `main.c`

## Contributing

Contributions to improve the firmware are welcome. Please feel free to submit pull requests or open issues on the GitHub repository.

## License

This project is licensed under the MIT License - see the LICENSE file for details.

## Acknowledgements

This project is part of the ARG_OSCI oscilloscope visualization tool suite. For the companion application, visit the [ARG_OSCI App repository](https://github.com/ArgOsciProyect/ARG_OSCI_APP).
