# ESP32 ARG_OSCI Firmware

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

ESP32 ARG_OSCI Firmware transforms your ESP32 into a versatile digital oscilloscope capable of capturing and transmitting analog signals in real-time. It pairs with the ARG_OSCI desktop and mobile application to create a complete signal visualization solution.

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
