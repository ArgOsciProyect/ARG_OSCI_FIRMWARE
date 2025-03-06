# Contributing to ESP32 ARG_OSCI Firmware

We welcome contributions to the ESP32 ARG_OSCI Firmware! Whether you're fixing a bug, adding a new feature, or improving documentation, your help is greatly appreciated. Please read this guide to understand how to contribute effectively.

## Code of Conduct

Please note that this project is released with a [Contributor Covenant Code of Conduct](CODE_OF_CONDUCT.md). By participating in this project, you agree to abide by its terms.

## How to Contribute

1. **Fork the Repository:** Start by forking the ESP32 ARG_OSCI Firmware repository to your GitHub account.

2. **Clone the Fork:** Clone your forked repository to your local machine:
   ```bash
   git clone https://github.com/ArgOsciProyect/ARG_OSCI_FIRMWARE.git
   cd ARG_OSCI_FIRMWARE
   ```

3. **Create a Branch:** Create a new branch for your feature or bug fix:
   ```bash
   git checkout -b feature/your-feature-name
   ```
   or
   ```bash
   git checkout -b fix/your-bug-fix
   ```

4. **Make Changes:** Implement your changes, adhering to the project's coding standards and guidelines.

5. **Test Your Changes:** Ensure your changes are thoroughly tested and do not introduce new issues. Test with both internal and external ADC configurations when applicable:
   ```bash
   idf.py set-target esp32
   idf.py menuconfig  # Set appropriate configuration
   idf.py build
   idf.py -p [PORT] flash monitor
   ```

6. **Check Code Style:** Follow ESP-IDF coding style conventions. Consider running static analysis tools such as cppcheck:
   ```bash
   cppcheck --enable=all --suppress=missingIncludeSystem .
   ```

7. **Commit Your Changes:** Commit your changes with a clear and concise message:
   ```bash
   git commit -m "Add: your feature description"
   ```
   or
   ```bash
   git commit -m "Fix: your bug fix description"
   ```

8. **Push to GitHub:** Push your branch to your forked repository:
   ```bash
   git push origin feature/your-feature-name
   ```
   or
   ```bash
   git push origin fix/your-bug-fix
   ```

9. **Create a Pull Request:** Submit a pull request (PR) from your branch to the main repository's `main` branch. Provide a detailed description of your changes and reference any related issues.

## General Description

This ESP32-based oscilloscope firmware communicates with the ARG_OSCI desktop/mobile application via **Wi-Fi**. It allows the acquisition of analog signals in real-time, offering flexible configuration options and secure data transmission.

## Project Overview

The firmware is structured around several key components:

* **Data Acquisition:** Responsible for capturing analog signals using either the ESP32's internal ADC or an external ADC via SPI. Provides configurable sampling rates and trigger modes.
* **Network Communication:** Implements WiFi connectivity (in both AP and STA modes) and handles data streaming through TCP sockets.
* **Security:** Implements RSA encryption for secure credential exchange and configuration.
* **Web Interface:** Provides a simple HTTP server for device configuration and control.
* **Signal Generation:** Generates calibration signals (square wave, sine wave) for testing and verification.

## Project Structure

The project follows a modular design with clear separation of concerns:

### Key Components

* **main.c:** Entry point and initialization sequence
* **acquisition.c:** Handles signal sampling from internal/external ADC
* **network.c:** Manages WiFi connectivity and socket operations
* **crypto.c:** Provides encryption and security features
* **webservers.c:** Implements HTTP servers and request handlers
* **data_transmission.c:** Manages data streaming to connected clients
* **globals.h:** Defines global variables and configuration parameters

## Requirements

* **REQ-HW-01: Dual ADC support:** Support for both internal ESP32 ADC and external SPI-connected ADC.
* **REQ-HW-02: Multiple acquisition modes:** Implement both continuous streaming and single-trigger capture modes.
* **REQ-HW-03: Configurable sampling rates:** Allow adjustable sampling frequencies based on hardware capabilities.
* **REQ-HW-04: Secure communication:** Implement RSA encryption for sensitive data exchange.
* **REQ-HW-05: Flexible connectivity:** Support both standalone access point mode and connection to existing WiFi networks.
* **REQ-HW-06: Calibration signals:** Generate reference signals for testing and calibration.
* **REQ-HW-07: Trigger control:** Implement adjustable trigger levels and edge selection.
* **REQ-HW-08: Real-time data streaming:** Efficient capture and transmission of signal data with minimal loss.

## Development Environment

1. **Setup ESP-IDF:**
   * Follow the [official ESP-IDF installation guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html)
   * Ensure you have ESP-IDF v5.3.1 or newer

2. **Required Hardware:**
   * ESP32 development board
   * For external ADC testing: SPI ADC module
   * Signal source for testing (function generator or similar)
   * USB cable for programming and power

## Reporting Bugs

If you find a bug, please submit an issue on GitHub with a clear description of the problem, steps to reproduce it, and any relevant error messages or logs.

## Suggesting Enhancements

If you have an idea for a new feature or enhancement, please submit an issue on GitHub with a detailed description of your proposal.

## Getting Help

If you need help with contributing or have any questions, feel free to reach out to the project maintainers or other contributors via GitHub issues or discussions.

Thank you for your contributions!