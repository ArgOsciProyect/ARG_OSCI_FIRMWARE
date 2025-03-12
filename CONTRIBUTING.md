# Contributing to ARG_OSCI_FIRMWARE

## Introduction

Thank you for considering contributing to ARG_OSCI_FIRMWARE! Your help is greatly appreciated whether you're fixing a bug, adding a new feature, or improving documentation.

Following these guidelines helps to communicate that you respect the time of the developers managing this open source project. In return, they will reciprocate that respect when addressing your issue, assessing changes, and helping you finalize your pull requests.

We welcome various types of contributions including:

- Bug fixes
- New features aligned with our requirements
- Documentation improvements
- Test coverage enhancements
- Performance optimizations
- Compatibility with additional ADC models
- Support for other ESP32 variants
- Derivative projects compatible with the companion APP but using alternative development boards

## Code of Conduct

Please note that this project is released with a [Contributor Covenant Code of Conduct](CODE_OF_CONDUCT.md). By participating in this project, you agree to abide by its terms.

## Ground Rules

### Expectations for Contributors

- Write and run tests for new code
- Follow ESP-IDF coding style conventions
- Document new code based on the project's documentation standards
- Create issues for any major changes and enhancements before implementation
- Be respectful and constructive in all project interactions

## Your First Contribution

Not sure where to begin? Here are some ways to get started:

- Fix a bug: Look for issues labeled with "bug" or "good first issue"
- Improve documentation: Clear documentation is crucial for our project
- Add tests: Help improve our test coverage

If you've never contributed to open source before, we recommend checking out [How to Contribute to an Open Source Project on GitHub](https://egghead.io/series/how-to-contribute-to-an-open-source-project-on-github).

## How to Contribute

1. **Fork the Repository:** Start by forking the ARG_OSCI_FIRMWARE repository to your GitHub account.

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

- **Data Acquisition:** Responsible for capturing analog signals using either the ESP32's internal ADC or an external ADC via SPI. Provides configurable sampling rates and trigger modes.
- **Network Communication:** Implements WiFi connectivity (in both AP and STA modes) and handles data streaming through TCP sockets.
- **Security:** Implements RSA encryption for secure credential exchange and configuration.
- **Web Interface:** Provides a simple HTTP server for device configuration and control.
- **Signal Generation:** Generates calibration signals (square wave, sine wave) for testing and verification.

## Project Structure

The project follows a modular design with clear separation of concerns:

### Key Components

- **main.c:** Entry point and initialization sequence
- **acquisition.c:** Handles signal sampling from internal/external ADC
- **network.c:** Manages WiFi connectivity and socket operations
- **crypto.c:** Provides encryption and security features
- **webservers.c:** Implements HTTP servers and request handlers
- **data_transmission.c:** Manages data streaming to connected clients
- **globals.h:** Defines global variables and configuration parameters

## Requirements

- **REQ-HW-01: Dual ADC support:** Support for both internal ESP32 ADC and external SPI-connected ADC.
- **REQ-HW-02: Multiple acquisition modes:** Implement both continuous streaming and single-trigger capture modes.
- **REQ-HW-03: Configurable sampling rates:** Allow adjustable sampling frequencies based on hardware capabilities.
- **REQ-HW-04: Secure communication:** Implement RSA encryption for sensitive data exchange.
- **REQ-HW-05: Flexible connectivity:** Support both standalone access point mode and connection to existing WiFi networks.
- **REQ-HW-06: Calibration signals:** Generate reference signals for testing and calibration.
- **REQ-HW-07: Trigger control:** Implement adjustable trigger levels and edge selection.
- **REQ-HW-08: Real-time data streaming:** Efficient capture and transmission of signal data with minimal loss.

## Development Environment

1. **Setup ESP-IDF:**
   - Follow the [official ESP-IDF installation guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html)
   - Ensure you have ESP-IDF v5.3.1 or newer

2. **Required Hardware:**
   - ESP32 development board
   - For external ADC testing: SPI ADC module
   - Signal source for testing (function generator or similar)
   - USB cable for programming and power

## Reporting Bugs

If you find a security vulnerability, please do NOT open an issue. Email the project maintainers directly instead.

For non-security bugs, please submit an issue on GitHub with:

1. A clear description of the problem
2. Steps to reproduce it
3. Expected behavior
4. Actual behavior
5. Screenshots if applicable
6. Information about your environment (OS, ESP-IDF version, etc.)

## Suggesting Enhancements

If you have an idea for a new feature or enhancement:

1. Check if it aligns with our project requirements (listed above)
2. Submit an issue on GitHub with a detailed description of your proposal
3. Explain why this feature would be beneficial to the project
4. If possible, outline how it might be implemented

## Code Review Process

The core team reviews pull requests as time allows. As this is a side project maintained by volunteers in their spare time, we don't expect immediate responses from contributors, and likewise, contributors should understand that reviews may take some time. We all work at our own pace and appreciate mutual patience and understanding. We value quality over speed, and we're grateful for your contributions regardless of timeline.

## Getting Help

If you need help with contributing or have any questions, feel free to reach out to the project maintainers or other contributors via GitHub issues or discussions.

Thank you for your contributions!
