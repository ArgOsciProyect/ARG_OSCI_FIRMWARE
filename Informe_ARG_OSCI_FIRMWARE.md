# Technical Analysis: ESP32 Oscilloscope Main Application

## Overview of 

main.c



The 

main.c

 file implements the core initialization sequence for an ESP32-based oscilloscope. It serves as the primary entry point for the application and orchestrates the startup of all necessary subsystems required for oscilloscope functionality.

## Initialization Flow

The application follows a structured initialization process:

1. **NVS (Non-Volatile Storage) Initialization**
   - Initializes the flash memory storage for configuration persistence
   - Implements error recovery by erasing and reinitializing if needed

2. **Network Stack Setup**
   - Initializes ESP32's network interface (esp_netif)
   - Creates the default event loop for network event handling

3. **Cryptography Subsystem**
   - Initializes crypto functions with `init_crypto()`
   - Creates a task for RSA key-pair generation with appropriate stack size
   - Uses a semaphore to ensure key generation completes before proceeding

4. **Watchdog Configuration**
   - Disables any previous watchdog configuration
   - Configures a task watchdog with a long timeout (1000 seconds) for intensive operations
   - Sets up to monitor all cores without causing panic on timeout

5. **Signal Generator Initialization**
   - Creates a task for DAC sine wave generation for calibration and testing
   - Initializes PWM for trigger level control
   - Sets up a 1KHz square wave output for calibration purposes

6. **ADC Configuration**
   - Uses conditional compilation based on `USE_EXTERNAL_ADC` definition
   - With external ADC: initializes SPI interface, MCPWM trigger and pulse counter
   - Creates necessary synchronization primitives (SPI mutex)

7. **Hardware Timer Configuration**
   - Sets up a precise hardware timer for synchronization

8. **GPIO Configuration**
   - Configures pins for trigger input
   - Sets up status LED GPIO

9. **Network Services Startup**
   - Initializes WiFi in AP+STA (Access Point + Station) mode
   - Starts the primary HTTP server on port 81
   - Initializes data transmission subsystem

10. **Task Creation**
    - Creates the main socket handling task pinned to core 1
    - Allocates different stack sizes depending on ADC configuration
    - Sets the LED to indicate the system is ready

## Key Components

### Task Management
The application creates multiple FreeRTOS tasks to handle different functions:
- `generate_key_pair_task`: Creates RSA keys asynchronously
- `dac_sine_wave_task`: Generates sine wave for calibration
- `socket_task`: Handles socket communication, pinned to core 1

### Hardware Interfaces
The application configures various ESP32 hardware peripherals:
- SPI for external ADC communication
- GPIO for trigger inputs and status indication
- DAC and PWM for signal generation
- MCPWM for precise trigger timing
- Pulse counter for edge detection

### Networking
The oscilloscope provides multiple interfaces:
- WiFi in AP+STA mode to create its own network and potentially connect to existing ones
- HTTP server on port 81 for web-based configuration
- Socket server for high-speed data transmission

## Globals.h Analysis

The 

globals.h

 header defines constants, configurations, and global variables shared across the application modules:

### Configuration Groups
1. **Hardware Selection**
   - `USE_EXTERNAL_ADC` controls whether to use external SPI-based ADC or ESP32's internal ADC

2. **WiFi Configuration**
   - Access point SSID, password, and connection limits
   - Network port definitions

3. **Crypto Configuration**
   - RSA key size parameters (3072 bits)

4. **Timer and ADC Settings**
   - Timer dividers and scaling factors
   - ADC channel, resolution, and sample rate (600 kHz)

5. **GPIO Pin Assignments**
   - Detailed pin mappings for all peripherals
   - Includes SPI pins, trigger pins, and signal generation pins

6. **Signal Generation Parameters**
   - Square wave frequency (1 kHz)
   - PWM frequency and timer configurations

7. **Buffer Configurations**
   - Different buffer sizes depending on the ADC configuration

### Global Variables
The header declares numerous external variables that are shared between modules:
- ADC handles and configuration parameters
- SPI device handles and configuration matrices
- MCPWM handles for precise timing
- Synchronization primitives (semaphores)
- Task handles
- Cryptographic key storage

### SPI Configuration Matrix
Defines a complex matrix of SPI configurations for different sampling rates, enabling the oscilloscope to operate at various time scales and resolutions.

## System Architecture Insights

1. **Dual-Core Utilization**
   - The main socket task is pinned to core 1, allowing core 0 for other system functions
   - Task priorities are carefully assigned (socket task at priority 5)

2. **Memory Management**
   - Task stack sizes are adjusted based on ADC configuration
   - Optional memory monitoring task is provided but commented out

3. **Synchronization**
   - Uses semaphores for synchronizing between tasks
   - Employs atomic variables for thread-safe access to shared state

4. **Security Features**
   - Implements RSA cryptography for secure communication

5. **Diagnostics**
   - Comprehensive logging using ESP-IDF's logging facility
   - Clear error handling and reporting

This ESP32 oscilloscope implementation demonstrates sophisticated embedded systems design, leveraging the ESP32's capabilities to create a full-featured test instrument with network connectivity.

# Technical Analysis: Cryptography Implementation in ESP32 Oscilloscope

## Overview of 

crypto.c

 and 

crypto.h



The 

crypto.c

 and 

crypto.h

 files implement the cryptographic security layer for the ESP32 oscilloscope. This module provides RSA encryption capabilities for secure communication between the oscilloscope and client applications, protecting the device's command and control channels from unauthorized access.

## Core Functionality

### Cryptographic Key Management

1. **Key Generation**
   - Implements RSA key pair generation with a substantial key size (3072 bits)
   - Runs as a separate FreeRTOS task to prevent blocking the main execution flow
   - Uses a binary semaphore to signal completion to the main application

2. **Key Storage**
   - Stores keys in PEM format in global buffers accessible across the application
   - Provides accessor functions for both public and private keys
   - Key sizes are defined in globals.h for consistent application-wide reference

3. **Secure Communication**
   - Offers functions to decrypt messages sent to the device
   - Supports Base64-encoded encrypted messages for web interface compatibility

## Detailed Function Analysis

### init_crypto()

This initialization function:
- Creates a binary semaphore for synchronizing key generation completion
- Returns ESP_OK on successful initialization or ESP_FAIL if the semaphore creation fails
- Is called early in the application startup sequence from main.c

### generate_key_pair_task()

This function runs as a dedicated FreeRTOS task and:
1. Initializes mbedTLS contexts:
   - `mbedtls_pk_context` for the public key cryptography operations
   - `mbedtls_entropy_context` for random number generation entropy source
   - `mbedtls_ctr_drbg_context` for the deterministic random bit generator

2. Seeds the random number generator using system entropy
   - Uses the string "gen_key_pair" as personalization data for the seeding process

3. Configures the PK context for RSA operations

4. Generates the RSA key pair:
   - Uses the specified key size (3072 bits)
   - Uses a public exponent of 65537 (a common and secure choice)
   - Logs progress as this is a computationally intensive operation

5. Converts and stores the keys:
   - Writes the public key in PEM format to the global `public_key` buffer
   - Writes the private key in PEM format to the global `private_key` buffer
   - Ensures proper error handling at each step

6. Performs cleanup:
   - Frees all mbedTLS resources
   - Signals completion by giving the semaphore
   - Self-terminates the task with vTaskDelete(NULL)

### decrypt_with_private_key()

This function:
1. Decrypts data using the device's private key
2. Handles all necessary mbedTLS context setup and cleanup
3. Returns the decrypted data and its length through output parameters
4. Uses robust error handling with detailed logging

The function follows best practices for RSA operations:
- Re-initializes mbedTLS contexts for each operation
- Uses a separate random number generator instance for security
- Properly parses the private key from the stored PEM format
- Handles buffer sizing correctly to prevent overflow

### decrypt_base64_message()

This utility function:
1. Decodes Base64-encoded input data
2. Calls `decrypt_with_private_key()` with the decoded binary data
3. Ensures the output is null-terminated for string operations
4. Returns an ESP error code based on operation success

This function is particularly useful for web interface integration, where encrypted commands may be sent as Base64-encoded strings.

### Accessor Functions

The module provides three accessor functions:
- `get_public_key()`: Returns the buffer containing the public key
- `get_private_key()`: Returns the buffer containing the private key
- `get_key_gen_semaphore()`: Returns the handle to the key generation semaphore

These allow controlled access to the module's resources from other parts of the application.

## Security Considerations

1. **Key Size and Security**
   - The implementation uses 3072-bit RSA keys, offering strong security suitable for this application
   - Key size is defined as a constant in globals.h, making it configurable across builds

2. **Resource Management**
   - The implementation carefully frees all cryptographic contexts to prevent memory leaks
   - Error handling is comprehensive, with detailed logging at each step

3. **Task Management**
   - Key generation runs as a dedicated task to prevent blocking the system
   - Uses appropriate stack size allocation (defined in main.c) for this intensive operation
   - Uses a binary semaphore to coordinate with the main application flow

4. **API Design**
   - Provides a clean interface for encryption/decryption operations
   - Encapsulates cryptographic complexity behind simple function calls
   - Uses ESP-IDF's error code conventions for consistency

## Integration with Main Application

The crypto module integrates with the main application via:

1. The initialization sequence in main.c:
   ```c
   ESP_ERROR_CHECK(init_crypto());
   xTaskCreate(generate_key_pair_task, "generate_key_pair_task", 8192, NULL, 5, NULL);
   ```

2. Synchronization with the key generation process:
   ```c
   if (xSemaphoreTake(get_key_gen_semaphore(), portMAX_DELAY) != pdTRUE) {
       ESP_LOGE(TAG, "Failed to wait for key generation");
       return;
   }
   ```

3. Global variables declared in globals.h and defined in crypto.c:
   ```c
   unsigned char public_key[KEYSIZE];
   unsigned char private_key[KEYSIZE];
   SemaphoreHandle_t key_gen_semaphore = NULL;
   ```

This integration ensures that RSA keys are generated early in the startup process and are available before network interfaces become active, maintaining security throughout the device operation.

## mbedTLS Dependency

The implementation relies heavily on mbedTLS, a robust cryptography library well-suited for embedded systems:
- Uses the PK (Public Key) module for RSA operations
- Uses the CTR-DRBG module for secure random number generation
- Uses the Entropy module for gathering system entropy
- Uses the Base64 module for encoding/decoding operations

The dependency is clearly defined in the header file with all required include statements, ensuring proper compilation and linking.

This cryptography implementation provides the ESP32 oscilloscope with essential security features, allowing secure remote control while protecting against unauthorized access to the device's functionality.

# Technical Analysis: Data Acquisition System for ESP32 Oscilloscope

## Overview of 

acquisition.c

 and 

acquisition.h



The acquisition module forms the core data collection system of the ESP32-based oscilloscope. It provides a comprehensive framework for configuring and managing analog signal acquisition through either the ESP32's internal ADC or an external ADC connected via SPI. The module implements hardware peripherals configuration, signal generation for calibration, and precise timing control required for oscilloscope functionality.

## Design Architecture

The acquisition system is designed with a dual-path architecture that allows the oscilloscope to operate with either:
1. The ESP32's internal ADC (less precise but simpler implementation)
2. An external ADC connected via SPI (higher precision but more complex)

This flexibility is implemented through conditional compilation using the `USE_EXTERNAL_ADC` directive, allowing the oscilloscope firmware to be optimized for different hardware configurations.

## Key Components and Functionality

### Voltage Scale Management

The module defines a set of voltage scales for the oscilloscope's vertical display:

```c
static const voltage_scale_t voltage_scales[] = {
    {400.0, "200V, -200V"}, {120.0, "60V, -60V"}, {24.0, "12V, -12V"}, 
    {6.0, "3V, -3V"}, {1.0, "500mV, -500mV"}
};
```

Each scale has a base range value (peak-to-peak voltage) and a human-readable display name. These scales allow the oscilloscope to display signals of varying amplitudes appropriately, from millivolt to hundreds of volts range.

### External ADC Configuration (when USE_EXTERNAL_ADC is defined)

#### SPI Interface Initialization

The `spi_master_init()` function configures the SPI interface for communicating with an external ADC:

1. Configures the MISO pin as input with pull-down
2. Sets up the SPI bus with appropriate flags:
   - `SPICOMMON_BUSFLAG_MASTER`: ESP32 acts as SPI master
   - `SPICOMMON_BUSFLAG_MISO`: Only MISO line is used (data coming from ADC)
   - `SPICOMMON_BUSFLAG_IOMUX_PINS`: Uses direct IO matrix pins for speed
3. Initializes SPI bus and adds the ADC device with specific timing parameters
4. Uses a configuration matrix (`spi_matrix`) that contains timing parameters for different sampling rates

The SPI interface is configured in half-duplex mode (receiving only) with precisely controlled timing for CS (chip select) assert/deassert and input delay.

#### MCPWM Trigger Implementation

The `init_mcpwm_trigger()` function configures the Motor Control PWM peripheral to generate precise timing signals for external ADC triggering:

1. Sets up a GPIO pin for synchronization input
2. Configures an MCPWM timer with high resolution (80MHz base clock)
3. Configures a GPIO synchronization source for external trigger events
4. Sets up the timer to reset on synchronization events (for trigger-synchronized acquisition)
5. Configures a comparator and generator to produce precise CS timing signals
6. Programs appropriate action points to generate the pulse pattern needed by the external ADC

This implementation allows for precisely timed acquisition cycles synchronized with external events.

#### Pulse Counter for Edge Detection

The `init_pulse_counter()` function configures the ESP32's pulse counter peripheral:

1. Sets up a counter unit with high and low limits
2. Configures a channel connected to the trigger input pin
3. Sets up a glitch filter to ignore noise (filtering pulses shorter than 1μs)
4. Configures the counter to increment on positive edges initially
5. Enables the counter unit

This setup allows the oscilloscope to detect and count signal edges for triggering acquisition at specific events.

### Internal ADC Configuration (when USE_EXTERNAL_ADC is not defined)

The module provides three primary functions for managing the internal ADC:

#### Starting ADC Sampling

The `start_adc_sampling()` function:

1. Uses atomic flags to prevent concurrent initialization attempts
2. Checks if ADC is already running to avoid redundant operations
3. Configures the ADC pattern with 12dB attenuation and the specified bit width
4. Sets up a continuous ADC handle with appropriate buffer sizes
5. Configures the ADC with the specified sampling frequency and conversion mode
6. Starts continuous ADC sampling
7. Updates atomic flags to indicate the ADC is running

#### Stopping ADC Sampling

The `stop_adc_sampling()` function:

1. Checks if the ADC is currently initializing before attempting to stop
2. Verifies the ADC is actually running before trying to stop it
3. Stops ADC continuous conversion
4. Adds a delay for hardware stability
5. Deinitializes the ADC handle to release resources
6. Updates the atomic flag to indicate the ADC is stopped

#### Reconfiguring ADC Sampling Rate

The `config_adc_sampling()` function:

1. Stops and deinitializes current ADC configuration
2. Creates a new ADC handle with appropriate buffer sizes
3. Configures the ADC with updated sampling frequency (SAMPLE_RATE_HZ / adc_divider)
4. Updates the wait conversion time based on the new divider
5. Restarts the ADC with the new configuration

This function allows for dynamic adjustment of the sampling rate while the oscilloscope is operating.

### Common Hardware Configuration (both modes)

#### GPIO Configuration

The `configure_gpio()` function sets up GPIO pins for trigger detection:

1. Configures the specified GPIO pin as input
2. Enables pull-down to prevent floating inputs
3. Disables interrupts (polling is used instead)

#### Hardware Timer Configuration

The `my_timer_init()` function configures a hardware timer for precise timing:

1. Sets up a timer with the specified divider
2. Configures it in count-down mode
3. Calculates an appropriate wait time based on buffer size and sampling frequency
4. Sets the initial counter value

The companion function `timer_wait()` provides a blocking wait using this timer:
1. Starts the timer
2. Polls the counter value until it reaches zero
3. Resets the counter for the next use

This provides precise timing intervals for sampling operations.

### Signal Generation for Calibration

The module includes several functions to generate reference signals for calibration:

#### Square Wave Generation

The `init_square_wave()` function configures the LEDC (LED Controller) peripheral to generate a 1kHz square wave with 50% duty cycle for calibration purposes.

#### Trigger PWM Signal

The `init_trigger_pwm()` function sets up another LEDC channel to generate a PWM signal for trigger level control, with accompanying `set_trigger_level()` function that translates a percentage (0-100%) to the appropriate duty cycle.

#### Sine Wave Generation

The `init_sine_wave()` function configures the ESP32's DAC to generate a 20kHz sine wave, wrapped in a task (`dac_sine_wave_task()`) that initializes the wave and then self-terminates.

### Configuration Information Functions

The module provides numerous helper functions that return hardware-specific configuration values:

1. **Sampling information:**
   - `get_sampling_frequency()`: Returns the base sampling frequency (2.5MHz for external ADC, ~496kHz for internal)
   - `dividing_factor()`: Returns hardware-specific divider values

2. **Data format information:**
   - `get_bits_per_packet()`: Returns the bit width of data packets (16 bits)
   - `get_data_mask()` and `get_channel_mask()`: Return bit masks for extracting data and channel information
   - `get_useful_bits()`: Returns the effective ADC resolution (10 bits for external, configured width for internal)

3. **Buffer management:**
   - `get_discard_head()` and `get_discard_trailer()`: Return the number of samples to discard from buffer ends
   - `get_samples_per_packet()`: Returns the effective number of samples per acquisition

4. **Signal reference values:**
   - `get_max_bits()`: Returns the maximum possible ADC reading (1023 for 10-bit conversion)
   - `get_mid_bits()`: Returns a reference mid-point value for signal display

## Synchronization and Thread Safety

The module employs several mechanisms to ensure thread safety:

1. **Atomic Variables:**
   - `atomic_int adc_modify_freq` and `atomic_int adc_divider`: For thread-safe sampling rate adjustments
   - `atomic_bool adc_is_running`: To track ADC operational state
   - `atomic_bool adc_initializing`: To prevent concurrent initialization attempts

2. **Semaphores:**
   - `SemaphoreHandle_t spi_mutex`: For protecting access to the SPI interface in multi-threaded contexts

## Hardware-Specific Configurations

The module demonstrates sophisticated hardware configuration through:

1. **SPI Timing Matrix:**
   The `spi_matrix` array contains precisely calculated timing parameters for different sampling rates:
   ```c
   const uint32_t spi_matrix[MATRIX_SPI_ROWS][MATRIX_SPI_COLS] = MATRIX_SPI_FREQ;
   ```
   This matrix defines SPI clock frequency, CS timing parameters, MCPWM period ticks, and compare values for each supported frequency.

2. **MCPWM Configuration:**
   Precise configuration of MCPWM parameters for generating accurately timed trigger signals.

3. **ADC Pattern Configuration:**
   Detailed setup of ADC sampling patterns, attenuation, and conversion modes.

## Performance Considerations

The implementation shows careful attention to performance requirements:

1. **Buffer Sizes:**
   Different buffer sizes are used depending on ADC configuration:
   ```c
   #ifdef USE_EXTERNAL_ADC
   #define BUF_SIZE 17280 * 4
   #else
   #define BUF_SIZE 1440*30
   #endif
   ```

2. **Timing Precision:**
   Hardware timers and MCPWM peripherals are used for precise timing control.

3. **Error Handling:**
   Comprehensive error checking and logging throughout the code.

4. **Resource Management:**
   Careful initialization and deinitialization of hardware resources to prevent leaks or conflicts.

## Integration with Other Subsystems

The acquisition module integrates with other oscilloscope subsystems through:

1. The data transmission module that sends acquired samples to clients
2. The trigger detection system for single-shot or continuous acquisition
3. The web interface for configuration of acquisition parameters

## Technical Insights

1. **PWM-Based Trigger Level:**
   The implementation uses PWM to generate an analog reference voltage for trigger comparison, allowing for a continuously adjustable trigger level without requiring a DAC.

2. **Multi-Rate Sampling:**
   The SPI matrix configuration enables multiple sampling rates with precisely timed signals.

3. **Calibration Signal Generation:**
   Built-in signal generators (square wave, sine wave) provide reference signals for calibration without external equipment.

4. **Voltage Scale System:**
   The voltage scale system supports multiple ranges, allowing proper display of signals with different amplitudes.

This acquisition module demonstrates sophisticated use of ESP32 peripherals to implement professional oscilloscope functionality, balancing between flexibility, precision, and resource constraints of the embedded platform.

# Technical Analysis: Network and WiFi Implementation in ESP32 Oscilloscope

## Overview of 

network.c

 and 

network.h



The network module implements the wireless connectivity features for the ESP32-based oscilloscope. It provides comprehensive WiFi initialization, socket management, and network scanning functionality that enables the oscilloscope to operate as both an access point (AP) and a station (STA), allowing flexible deployment scenarios. The implementation balances robustness with efficiency, ensuring reliable network operations in various environments.

## Core Functionality

### WiFi Initialization and Configuration

The `wifi_init()` function establishes the dual-mode WiFi capability, configuring the ESP32 to simultaneously act as:

1. **Access Point (AP)**: Creates a WiFi network with configurable SSID and password
2. **Station (STA)**: Allows connections to existing WiFi networks

The initialization process follows a structured approach:

```c
void wifi_init(void)
{
    // Create network interfaces if they don't exist
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif == NULL) {
        ap_netif = esp_netif_create_default_wifi_ap();
    }
    
    esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta_netif == NULL) {
        sta_netif = esp_netif_create_default_wifi_sta();
    }
    
    // Initialize WiFi with default configuration
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // Configure access point settings
    wifi_config_t wifi_config = {
        .ap = {.ssid = WIFI_SSID,
               .ssid_len = strlen(WIFI_SSID),
               .password = WIFI_PASSWORD,
               .max_connection = MAX_STA_CONN,
               .authmode = WIFI_AUTH_WPA_WPA2_PSK},
    };
    
    // Set WiFi mode and start
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}
```

Key aspects of this implementation include:

1. **Interface Reuse**: The code checks for existing network interfaces to prevent duplication
2. **Security Configuration**: Implements WPA2-PSK authentication by default, with fallback to open authentication when no password is provided
3. **Connection Limits**: Restricts the number of simultaneous client connections to the value defined by `MAX_STA_CONN`

### Socket Management

The module provides sophisticated socket handling with the `safe_close()` and `create_socket_and_bind()` functions:

#### Safe Socket Closure

The `safe_close()` function implements a graceful socket shutdown mechanism:

1. First attempts a graceful shutdown with a 30-second linger timeout:
   ```c
   struct linger so_linger;
   so_linger.l_onoff = 1;
   so_linger.l_linger = 30;
   setsockopt(sock, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger));
   ```

2. Issues a bidirectional shutdown command:
   ```c
   shutdown(sock, SHUT_RDWR);
   ```

3. Falls back to immediate closure if graceful shutdown fails:
   ```c
   so_linger.l_onoff = 1;
   so_linger.l_linger = 0;
   setsockopt(sock, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger));
   ```

This approach maximizes the likelihood of clean connection termination while ensuring resources are ultimately released even in problematic scenarios.

#### Socket Creation and Configuration

The `create_socket_and_bind()` function establishes a TCP server socket:

1. Creates a TCP/IP socket:
   ```c
   int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
   ```

2. Binds the socket to the specified IP address with dynamic port assignment:
   ```c
   addr.sin_family = AF_INET;
   addr.sin_addr.s_addr = ip_info->ip.addr;
   addr.sin_port = htons(0); // OS-assigned port
   bind(sock, (struct sockaddr *)&addr, sizeof(addr));
   ```

3. Configures the socket for listening:
   ```c
   listen(sock, 1);
   ```

4. Retrieves and logs the assigned port number:
   ```c
   getsockname(sock, (struct sockaddr *)&bound_addr, &addr_len);
   ```

The function assigns the socket to the global `new_sock` variable, making it available to other parts of the application.

### IP Address Management

The module provides two functions for managing IP addresses:

#### Access Point IP Information

The `get_ap_ip_info()` function retrieves the IP configuration of the access point interface:

1. Calls `esp_netif_get_ip_info()` with the AP interface handle
2. Validates that a valid IP address was obtained
3. Converts the IP address to string format for logging

This function is essential for providing connection information to users, enabling them to connect to the oscilloscope's web interface.

#### Station IP Waiting

The `wait_for_ip()` function implements a polling mechanism to wait for IP address assignment in station mode:

1. Makes up to 10 attempts with 1-second intervals:
   ```c
   for (int i = 0; i < 10; i++) {
       vTaskDelay(1000 / portTICK_PERIOD_MS);
       
       if (esp_netif_get_ip_info(...) == ESP_OK && ip_info->ip.addr != 0) {
           // IP address obtained
           return ESP_OK;
       }
   }
   ```

2. Returns failure status after timeout

This approach allows the oscilloscope to connect to an existing network, providing a reasonable balance between responsiveness and reliability.

### WiFi Network Scanning

The module implements comprehensive WiFi scanning capabilities with two interconnected functions:

#### Network Scanning

The `scan_and_get_ap_records()` function performs a WiFi scan and processes the results:

1. Configures and initiates a WiFi scan:
   ```c
   wifi_scan_config_t scan_config = {.ssid = NULL, .bssid = NULL, .channel = 0, .show_hidden = true};
   ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));
   ```

2. Retrieves the scan results and number of networks found:
   ```c
   ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(num_networks));
   ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(num_networks, ap_records));
   ```

3. Processes the results to create a JSON array of unique SSIDs:
   ```c
   cJSON *root = cJSON_CreateArray();
   for (int i = 0; i < *num_networks; i++) {
       add_unique_ssid(root, &ap_records[i]);
   }
   ```

#### SSID Processing

The `add_unique_ssid()` function ensures unique SSID entries in the JSON array:

1. Searches the JSON array for an existing entry with the same SSID
2. Adds the SSID to the array only if it's unique and non-empty

This approach minimizes duplicate entries that could occur when multiple access points share the same SSID (common in enterprise deployments).

### Status Indication

The `configure_led_gpio()` function sets up a GPIO pin for status indication:

1. Configures a specific GPIO pin (`LED_GPIO`) as output:
   ```c
   gpio_config_t io_conf = {
       .pin_bit_mask = (1ULL << LED_GPIO),
       .mode = GPIO_MODE_OUTPUT,
       .pull_up_en = GPIO_PULLUP_DISABLE,
       .pull_down_en = GPIO_PULLDOWN_DISABLE,
       .intr_type = GPIO_INTR_DISABLE,
   };
   ```

2. Initializes the LED in off state:
   ```c
   ESP_ERROR_CHECK(gpio_set_level(LED_GPIO, 0));
   ```

From the main.c file, we see this LED is later activated to indicate when the socket server is ready for connections:

```c
// Activate LED to indicate socket is ready for connections
gpio_set_level(LED_GPIO, 1);
```

## Integration with Overall System

The network module integrates with the oscilloscope system in several key ways:

### Dual Network Modes

By implementing the AP+STA mode, the oscilloscope can:
1. Operate as a standalone device with its own WiFi network (AP mode)
2. Connect to existing infrastructure for integration with broader networks (STA mode)

This flexibility is particularly valuable for field deployments and lab settings.

### Web Interface Enablement

The network module supports the HTTP server functionality by:
1. Enabling WiFi connectivity for client devices
2. Managing the socket interface for data transmission

From main.c, we see the HTTP server initialization follows WiFi setup:

```c
// Initialize WiFi in AP+STA mode
wifi_init();
// Start the primary HTTP server
httpd_handle_t server = start_webserver();
```

### LED Status Indication

The LED configuration provides a visual indicator of system readiness, improving user experience by clearly signaling when the oscilloscope is available for connections.

## Error Handling and Robustness

The module implements thorough error handling throughout:

1. **WiFi Initialization**: Uses `ESP_ERROR_CHECK` to validate each step of the WiFi configuration process

2. **Socket Operations**: Implements fallback mechanisms for socket closure and comprehensive error checking for socket creation operations:
   ```c
   if (sock < 0) {
       ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
       return ESP_FAIL;
   }
   ```

3. **Memory Management**: Properly frees allocated resources when handling WiFi scan results:
   ```c
   wifi_ap_record_t *ap_records = malloc(sizeof(wifi_ap_record_t) * (*num_networks));
   // ...
   free(ap_records);
   ```

4. **IP Address Validation**: Verifies that valid IP addresses were obtained:
   ```c
   if (ret != ESP_OK || ip_info->ip.addr == 0) {
       ESP_LOGE(TAG, "Failed to get IP address of AP");
       return ESP_FAIL;
   }
   ```

## Technical Insights

### Socket Linger Implementation

The implementation uses the `SO_LINGER` socket option with two distinct configurations:

1. **Graceful Close**: Sets a 30-second linger time to allow in-flight data to be processed
2. **Forced Close**: Uses zero linger time as a fallback for immediate socket termination

This approach balances data integrity with resource management, preventing socket leaks while attempting to preserve data when possible.

### Dynamic Port Assignment

The socket binding uses port 0 to request dynamic port assignment from the operating system:
```c
addr.sin_port = htons(0); // Let the OS assign a port
```

This technique avoids hard-coding port numbers that might conflict with other services, increasing system flexibility and robustness.

### Unique SSID Collection

The implementation efficiently identifies unique SSIDs through JSON object traversal, demonstrating a practical approach to eliminating duplicates without more complex data structures:
```c
cJSON_ArrayForEach(item, root) {
    cJSON *ssid = cJSON_GetObjectItem(item, "SSID");
    if (ssid && strcmp(ssid->valuestring, (char *)ap_record->ssid) == 0) {
        ssid_exists = true;
        break;
    }
}
```

This approach simplifies client-side processing by providing a clean list of available networks.

### Defensive Programming

The implementation consistently checks for existing resources before creation and validates return values, showing a defensive programming approach that improves system reliability in embedded contexts.

The network module exemplifies a well-structured approach to wireless connectivity in embedded systems, combining flexible configuration options with robust error handling to support the oscilloscope's core functionality reliably across different deployment scenarios.

# Technical Analysis: Web Server Infrastructure in ESP32 Oscilloscope

## Overview of 

webservers.c

 and 

webservers.h



The webservers module implements the HTTP-based user interface and control system for the ESP32 oscilloscope. This sophisticated component establishes dual web servers that serve both configuration interfaces and API endpoints, enabling remote control of the oscilloscope's functions, secure network configuration, and real-time parameter adjustments. The implementation uses ESP-IDF's HTTP server component combined with JSON processing to create a complete web-based management system.

## Server Architecture

### Dual-Server Design

The oscilloscope implements two independent HTTP servers:

1. **Primary Server (Port 81)**
   - Initialized in `start_webserver()`
   - Always available via the ESP32's access point
   - Serves the main configuration interface
   - Runs on core 0 to avoid interference with data acquisition

2. **Secondary Server (Port 80)**
   - Initialized in `start_second_webserver()`
   - Only available when connected to an external WiFi network
   - Provides the same control capabilities but on the standard HTTP port
   - Dynamically started/stopped based on WiFi connection status

This dual-server approach ensures the oscilloscope remains accessible even when network configurations change, providing operational redundancy.

### Server Configuration

Both servers are configured with careful attention to resource allocation:

```c
httpd_config_t config = HTTPD_DEFAULT_CONFIG();
config.core_id = 0;            // Run on core 0
config.server_port = 81;       // Non-standard port for primary server
config.ctrl_port = 32767;      // Control channel port
config.stack_size = 4096 * 4;  // Larger stack for complex operations
config.max_uri_handlers = 11;  // Support for numerous endpoints
config.max_resp_headers = 8;   // Allow for CORS and content-type headers
config.lru_purge_enable = true; // Automatic resource cleanup
```

The configuration demonstrates careful resource management, with each server tuned differently based on its role and expected traffic patterns.

## API Endpoints and Handlers

### Configuration and Status Endpoints

#### Device Configuration (`/config`)

The `config_handler()` function provides comprehensive information about the oscilloscope's capabilities:

1. Creates a JSON object with sampling parameters:
   - Sampling frequency
   - Bit depth
   - Data formatting masks
   - Buffer sizes
   - Available voltage scales

2. Returns a complete configuration that enables client applications to correctly interpret oscilloscope data

This endpoint is crucial for initial setup of client applications, ensuring proper data interpretation.

#### Public Key Distribution (`/get_public_key`)

The `get_public_key_handler()` function enables secure communication:

1. Implements CORS headers to support web applications from any origin
2. Handles OPTIONS requests for CORS preflight checks
3. Returns the device's RSA public key in PEM format
4. Enables encrypted communication with the oscilloscope

This security infrastructure allows sensitive information like WiFi credentials to be transmitted securely.

### Oscilloscope Control Endpoints

#### Trigger Configuration (`/trigger`)

The `trigger_handler()` function configures the oscilloscope's triggering:

1. Processes JSON requests containing:
   - `trigger_edge`: Sets positive or negative edge triggering
   - `trigger_percentage`: Sets the voltage level for triggering (0-100%)

2. For external ADC mode, reconfigures the pulse counter for edge detection:
   ```c
   #ifdef USE_EXTERNAL_ADC
   if (mode == 1) {
       if (trigger_edge == 1) {
           // Configure for positive edge detection
           ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                                        PCNT_CHANNEL_EDGE_ACTION_HOLD));
       } else {
           // Configure for negative edge detection
           ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan, PCNT_CHANNEL_EDGE_ACTION_HOLD,
                                                        PCNT_CHANNEL_EDGE_ACTION_INCREASE));
       }
   }
   #endif
   ```

3. Sets the trigger level via PWM output when in single-shot mode

#### Acquisition Mode Control (`/single` and `/normal`)

Two complementary handlers manage the oscilloscope's acquisition modes:

1. `single_handler()`: Switches to single-shot capture mode:
   - Triggers data acquisition only when the signal meets specified conditions
   - Returns simple JSON confirmation: `{"mode":"Single"}`

2. `normal_handler()`: Switches to continuous capture mode:
   - Enables continuous data streaming
   - Returns simple JSON confirmation: `{"mode":"Normal"}`

#### Sampling Frequency Control (`/freq`)

The `freq_handler()` function provides dynamic control of sampling rates:

1. Accepts `action` parameter ("more" or "less") to adjust frequency
2. Implements different behaviors based on ADC configuration:

   **For external ADC:**
   ```c
   #ifdef USE_EXTERNAL_ADC
   // Adjust SPI index for frequency selection from matrix
   if (strcmp(action->valuestring, "less") == 0 && spi_index != 6) {
       spi_index++;
   }
   if (strcmp(action->valuestring, "more") == 0 && spi_index != 0) {
       spi_index--;
   }
   
   // Apply new SPI and MCPWM configuration from matrix
   ESP_ERROR_CHECK(spi_bus_remove_device(spi));
   // [SPI reconfiguration with new timing parameters]
   ESP_ERROR_CHECK(mcpwm_timer_set_period(timer, spi_matrix[spi_index][3]));
   ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(comparator, spi_matrix[spi_index][4]));
   #else
   ```

   **For internal ADC:**
   ```c
   // Adjust divider for internal ADC
   if (strcmp(action->valuestring, "less") == 0 && adc_divider != 16) {
       adc_divider *= 2;
   }
   if (strcmp(action->valuestring, "more") == 0 && adc_divider != 1) {
       adc_divider /= 2;
   }
   adc_modify_freq = 1;
   #endif
   ```

This endpoint enables dynamic time-base adjustment, essential for oscilloscope functionality.

### Network Management Endpoints

#### WiFi Network Scanning (`/scan_wifi`)

The `scan_wifi_handler()` function provides network discovery capabilities:

1. Calls the network module's `scan_and_get_ap_records()` function
2. Returns a JSON array of available WiFi networks
3. Formats the response for display in the web interface

#### WiFi Connection Management (`/connect_wifi`)

The `connect_wifi_handler()` function establishes connections to external networks:

1. Receives encrypted SSID and password:
   ```c
   if (parse_wifi_credentials(req, &wifi_config) != ESP_OK) {
       return send_wifi_response(req, "", 0, false);
   }
   ```

2. Coordinates with the socket task to pause ADC operations during WiFi reconfiguration:
   ```c
   #ifndef USE_EXTERNAL_ADC
   // Request socket task to pause ADC operations
   atomic_store(&wifi_operation_requested, 1);
   // Wait for acknowledgment
   while (!atomic_load(&wifi_operation_acknowledged) && timeout_count < 500) {
       vTaskDelay(pdMS_TO_TICKS(10));
       timeout_count++;
   }
   #endif
   ```

3. Configures ESP32 to connect to the specified network:
   ```c
   ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
   ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
   esp_err_t err = esp_wifi_connect();
   ```

4. Establishes a new data socket on the station interface:
   ```c
   if (create_socket_and_bind(&ip_info) != ESP_OK) {
       return send_wifi_response(req, "", 0, false);
   }
   ```

5. Starts the secondary HTTP server on port 80:
   ```c
   second_server = start_second_webserver();
   ```

This sophisticated connection process maintains security while ensuring continuous oscilloscope operation during network changes.

#### Socket Management (`/reset` and `/internal_mode`)

Two handlers manage the data transmission sockets:

1. `reset_socket_handler()`: Resets the data streaming socket:
   - Determines the appropriate interface (AP or STA) based on request origin
   - Safely closes any existing socket
   - Creates and binds a new socket with dynamic port assignment
   - Returns socket details (IP and port) to the client

2. `internal_mode_handler()`: Sets up AP mode data transmission:
   - Specifically configures the socket on the AP interface
   - Used when operating in standalone mode
   - Synchronizes with the socket task to ensure clean socket transitions

### Testing and Validation Endpoints

#### Encryption Test (`/test`)

The `test_handler()` function verifies secure communication:

1. Receives an encrypted message:
   ```c
   cJSON *encrypted_msg = cJSON_GetObjectItem(root, "word");
   char *encrypted_copy = strdup(encrypted_msg->valuestring);
   ```

2. Decrypts it using the device's private key:
   ```c
   char decrypted[256];
   if (decrypt_base64_message(encrypted_copy, decrypted, sizeof(decrypted)) != ESP_OK) {
       ESP_LOGI(TAG, "Failed to decrypt message");
       free(encrypted_copy);
       httpd_resp_send_500(req);
       return ESP_FAIL;
   }
   ```

3. Returns the decrypted message to verify end-to-end encryption works correctly

#### Connection Test (`/testConnect`)

The `test_connect_handler()` function provides a simple alive check:
```c
esp_err_t test_connect_handler(httpd_req_t *req)
{
    return httpd_resp_send(req, "1", 1);
}
```

This minimalist response allows clients to verify server availability with minimal overhead.

## Security Implementation

### Encrypted Communication

The module implements robust security measures:

1. **Public Key Distribution**:
   - Provides the oscilloscope's public key via a dedicated endpoint
   - Enables asymmetric encryption for secure data exchange

2. **Credential Protection**:
   - WiFi credentials are encrypted with RSA before transmission:
     ```c
     char ssid_decrypted[512];
     if (decrypt_base64_message(ssid_encrypted->valuestring, ssid_decrypted, sizeof(ssid_decrypted)) != ESP_OK) {
         httpd_resp_send_500(req);
         cJSON_Delete(root);
         return ESP_FAIL;
     }
     ```

3. **CORS Support**:
   - Enables secure web applications to interact with the oscilloscope:
     ```c
     httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
     httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET,POST,OPTIONS");
     httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
     ```

### Safe Task Synchronization

The module implements careful synchronization with data acquisition tasks:

```c
#ifndef USE_EXTERNAL_ADC
// Request socket task to pause ADC operations
atomic_store(&wifi_operation_requested, 1);

// Wait for acknowledgment with timeout
int timeout_count = 0;
while (!atomic_load(&wifi_operation_acknowledged) && timeout_count < 500) {
    vTaskDelay(pdMS_TO_TICKS(10));
    timeout_count++;
}
#endif
```

This approach prevents race conditions and ensures clean handoffs between network configuration and data acquisition.

## JSON Response Handling

The module demonstrates sophisticated JSON processing throughout its implementation:

1. **Response Creation Pattern**:
   ```c
   cJSON *response = cJSON_CreateObject();
   cJSON_AddStringToObject(response, "IP", ip_str);
   cJSON_AddNumberToObject(response, "Port", new_port);
   
   const char *json_response = cJSON_Print(response);
   httpd_resp_set_type(req, "application/json");
   esp_err_t ret = httpd_resp_send(req, json_response, strlen(json_response));
   
   cJSON_Delete(response);
   free((void *)json_response);
   ```

2. **Complex Nested Structures**:
   ```c
   // Create the voltage scales array
   cJSON *voltage_scales_array = cJSON_CreateArray();
   if (voltage_scales_array != NULL) {
       const voltage_scale_t *scales = get_voltage_scales();
       int count = get_voltage_scales_count();
       
       // Add each scale to the array
       for (int i = 0; i < count; i++) {
           cJSON *scale = cJSON_CreateObject();
           if (scale != NULL) {
               cJSON_AddNumberToObject(scale, "baseRange", scales[i].baseRange);
               cJSON_AddStringToObject(scale, "displayName", scales[i].displayName);
               cJSON_AddItemToArray(voltage_scales_array, scale);
           }
       }
       
       cJSON_AddItemToObject(config, "voltage_scales", voltage_scales_array);
   }
   ```

The implementation consistently follows proper resource management patterns, preventing memory leaks even with complex JSON structures.

## Integration with Other Modules

The webservers module serves as a central integration point for many oscilloscope subsystems:

1. **Acquisition Module**:
   - Retrieves configuration information: `get_sampling_frequency()`, `get_bits_per_packet()`, etc.
   - Controls trigger levels: `set_trigger_level(percentage)`
   - Sets acquisition modes: `set_single_trigger_mode()`, `set_continuous_mode()`

2. **Network Module**:
   - Uses WiFi scanning: `scan_and_get_ap_records()`
   - Manages socket operations: `create_socket_and_bind()`, `safe_close()`
   - Retrieves network interfaces: `get_ap_ip_info()`

3. **Crypto Module**:
   - Accesses public key: `get_public_key()`
   - Decrypts messages: `decrypt_base64_message()`

4. **Data Transmission Module**:
   - Updates socket information for streaming data to clients

This central position makes the webservers module critical to the oscilloscope's operation, serving as the bridge between user interface and hardware functionality.

## Error Handling and Robustness

The implementation demonstrates thorough error handling throughout:

1. **Memory Management**:
   - Consistent allocation and release of resources:
     ```c
     const char *response = cJSON_Print(config);
     esp_err_t ret = httpd_resp_send(req, response, strlen(response));
     
     free((void *)response);
     cJSON_Delete(config);
     ```

2. **JSON Parsing Validation**:
   ```c
   cJSON *root = cJSON_Parse(content);
   if (!root) {
       ESP_LOGI(TAG, "Failed to parse JSON");
       httpd_resp_send_500(req);
       return ESP_FAIL;
   }
   ```

3. **Response Construction Checks**:
   ```c
   cJSON *response = cJSON_CreateObject();
   if (!response) {
       ESP_LOGI(TAG, "Failed to create response object");
       httpd_resp_send_500(req);
       return ESP_FAIL;
   }
   ```

4. **Socket Operation Validation**:
   ```c
   if (bind(new_sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
       ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
       safe_close(sock);
       return ESP_FAIL;
   }
   ```

These patterns ensure the web interface remains stable even under unexpected conditions or malformed requests.

## Technical Insights

### Dynamic Server Configuration

The implementation dynamically adjusts server parameters based on its role:

1. **Primary Server (Port 81)**:
   - Larger stack size: `config.stack_size = 4096 * 4;`
   - More URI handlers: `config.max_uri_handlers = 11;`
   - Custom control port: `config.ctrl_port = 32767;`

2. **Secondary Server (Port 80)**:
   - Smaller stack: `config.stack_size = 4096 * 1.5;`
   - Fewer handlers: `config.max_uri_handlers = 10;`
   - Default control port

This differentiation allows efficient resource allocation based on expected usage patterns.

### Conditional Compilation

The implementation uses conditional compilation to handle different hardware configurations:

```c
#ifdef USE_EXTERNAL_ADC
// External ADC-specific code for SPI frequency control
// ...
#else
// Internal ADC-specific code for divider adjustment
// ...
#endif
```

This approach maintains a single codebase while supporting different hardware variants of the oscilloscope.

### Task Synchronization

The code demonstrates sophisticated inter-task synchronization using atomic operations:

```c
#ifndef USE_EXTERNAL_ADC
// Request socket task to pause ADC operations
atomic_store(&wifi_operation_requested, 1);

// Wait for acknowledgment with timeout
int timeout_count = 0;
while (!atomic_load(&wifi_operation_acknowledged) && timeout_count < 500) {
    vTaskDelay(pdMS_TO_TICKS(10));
    timeout_count++;
}
#endif
```

This pattern prevents race conditions when multiple tasks need to coordinate access to shared hardware resources.

### Socket Management

The implementation shows careful socket handling with proper cleanup:

```c
// Close existing socket if there is one
if (new_sock != -1) {
    safe_close(new_sock);
    new_sock = -1;
}
```

This approach prevents resource leaks that could eventually lead to system instability.

---

The webservers module represents a sophisticated HTTP-based control interface for the ESP32 oscilloscope, seamlessly integrating with other system components to provide a complete remote management solution. Its dual-server architecture, comprehensive API endpoint set, and robust security implementation demonstrate professional embedded web server design principles.

# Technical Analysis: Data Transmission System in ESP32 Oscilloscope

## Overview of 

data_transmission.c

 and 

data_transmission.h



The data transmission module forms the core communication layer of the ESP32 oscilloscope, responsible for streaming acquired signal data to client applications in real-time. It implements a flexible architecture that supports both continuous data streaming and triggered acquisition modes while maintaining robust socket communication and synchronization with other system components. This module serves as the critical bridge between the hardware acquisition subsystems and the end-user visualization interface.

## Key Components and Architecture

### State Management

The module maintains several atomic variables to track the oscilloscope's operating state across multiple tasks:

```c
atomic_int mode = ATOMIC_VAR_INIT(0);           // Acquisition mode (0: continuous, 1: single trigger)
atomic_int last_state = ATOMIC_VAR_INIT(0);     // Previous state of trigger input
atomic_int current_state = ATOMIC_VAR_INIT(0);  // Current state of trigger input
atomic_int trigger_edge = ATOMIC_VAR_INIT(1);   // Trigger edge type (1: positive edge, 0: negative edge)
```

For internal ADC operation, additional synchronization variables manage interactions with WiFi operations:

```c
#ifndef USE_EXTERNAL_ADC
atomic_int wifi_operation_requested = ATOMIC_VAR_INIT(0);
atomic_int wifi_operation_acknowledged = ATOMIC_VAR_INIT(0);
#endif
```

These atomic variables ensure thread-safe state transitions, preventing race conditions between the socket task and web server handlers.

### Operating Modes

The module implements two distinct operating modes that reflect typical oscilloscope functionality:

#### 1. Continuous Mode (Normal Operation)

In continuous mode (`mode = 0`), the oscilloscope continuously:
- Acquires data samples at the configured sampling rate
- Transmits them to connected clients without waiting for trigger events
- Maintains a steady stream of waveform data

This mode is activated through the `set_continuous_mode()` function:

```c
esp_err_t set_continuous_mode(void)
{
    ESP_LOGI(TAG, "Entering continuous mode");
    mode = 0;                       // Set to continuous mode
    esp_err_t ret = set_trigger_level(0); // Reset trigger level
    
    #ifdef USE_EXTERNAL_ADC
    ESP_ERROR_CHECK(pcnt_unit_stop(pcnt_unit));
    #endif
    
    return ESP_OK;
}
```

#### 2. Single Trigger Mode

In single trigger mode (`mode = 1`), the oscilloscope:
- Waits for a specific trigger event (rising or falling edge) to occur
- Captures and transmits a single buffer of data when triggered
- Continues monitoring for new triggers

This mode is activated by `set_single_trigger_mode()`:

```c
esp_err_t set_single_trigger_mode(void)
{
    ESP_LOGI(TAG, "Entering single trigger mode");
    mode = 1; // Set to single trigger mode

    #ifdef USE_EXTERNAL_ADC
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));
    
    // Configure edge detection based on trigger_edge
    if (trigger_edge == 1) {
        // Configure for positive edge detection
        ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan, 
                         PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD));
    } else {
        // Configure for negative edge detection
        ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan, 
                         PCNT_CHANNEL_EDGE_ACTION_HOLD, PCNT_CHANNEL_EDGE_ACTION_INCREASE));
    }
    
    // Get initial state
    int temp_last_state;
    pcnt_unit_get_count(pcnt_unit, &temp_last_state);
    last_state = temp_last_state;
    #else
    // Sample the current GPIO state
    last_state = gpio_get_level(SINGLE_INPUT_PIN);
    #endif
    
    return ESP_OK;
}
```

The implementation cleverly uses hardware-specific techniques for edge detection:
- For external ADC, it configures the pulse counter (PCNT) unit to detect edges
- For internal ADC, it directly samples GPIO levels and compares current and previous values

### Data Acquisition

The module provides a unified `acquire_data()` function that abstracts hardware-specific acquisition methods:

```c
esp_err_t acquire_data(uint8_t *buffer, size_t buffer_size, uint32_t *bytes_read)
{
    esp_err_t ret = ESP_OK;

    #ifdef USE_EXTERNAL_ADC
    // Configure SPI transaction
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 0;
    t.rxlength = buffer_size * 8; // in bits
    t.rx_buffer = buffer;
    t.flags = 0;

    // Take semaphore for SPI access
    if (xSemaphoreTake(spi_mutex, portMAX_DELAY) == pdTRUE) {
        // Perform SPI transaction
        ret = spi_device_polling_transmit(spi, &t);
        xSemaphoreGive(spi_mutex);

        if (ret == ESP_OK) {
            *bytes_read = buffer_size;
        } else {
            ESP_LOGE(TAG, "SPI transaction failed: %s", esp_err_to_name(ret));
            *bytes_read = 0;
        }
    } else {
        ESP_LOGE(TAG, "Failed to take SPI mutex");
        ret = ESP_FAIL;
        *bytes_read = 0;
    }
    #else
    // Wait for ADC conversion
    vTaskDelay(pdMS_TO_TICKS(wait_convertion_time));

    // Read from ADC
    ret = adc_continuous_read(adc_handle, buffer, buffer_size, bytes_read, 1000 / portTICK_PERIOD_MS);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC read failed: %s", esp_err_to_name(ret));
    }
    #endif

    return ret;
}
```

This abstraction allows the rest of the module to work with either acquisition method without code duplication.

### Network Communication

#### Socket Task

The `socket_task()` function serves as the heart of the data transmission system, implementing a state machine that:

1. Manages socket lifecycle:
   - Detects socket configuration changes
   - Accepts client connections
   - Handles disconnections
   - Deals with socket errors

2. Implements acquisition logic:
   - Configures acquisition based on current mode
   - Detects trigger events in single-trigger mode
   - Processes acquired data

3. Transmits data to clients:
   - Sends acquisition buffers with appropriate timing
   - Handles transmission errors
   - Implements non-blocking sends to prevent blocking acquisition

4. Synchronizes with other tasks:
   - Pauses for WiFi operations when needed
   - Reconfigures sampling frequency when requested

The task's main loop structure implements these layers of functionality:

```c
void socket_task(void *pvParameters)
{
    // [Variables initialization]
    
    while (1) {
        // Layer 1: WiFi operation synchronization
        #ifndef USE_EXTERNAL_ADC
        if (atomic_load(&wifi_operation_requested)) {
            // Pause ADC operations and acknowledge request
            // ...
        }
        #endif

        // Layer 2: Socket management
        if (new_sock != current_sock) {
            // Handle socket change
            // ...
        }
        
        if (new_sock == -1) {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        
        // Wait for client connection with socket change detection
        // ...
        
        // Layer 3: Acquisition and transmission mode selection
        #ifndef USE_EXTERNAL_ADC
        start_adc_sampling();
        #endif
        
        while (1) {
            // Handle frequency changes and synchronization
            #ifndef USE_EXTERNAL_ADC
            if (atomic_load(&wifi_operation_requested)) {
                break;
            }
            
            if (adc_modify_freq) {
                config_adc_sampling();
                adc_modify_freq = 0;
            }
            #endif
            
            // Layer 4: Mode-specific acquisition logic
            if (mode == 1) {
                // Single-trigger mode implementation
                // ...
            } else {
                // Continuous mode implementation
                // ...
            }
        }
        
        // Clean up when inner loop exits
        #ifndef USE_EXTERNAL_ADC
        stop_adc_sampling();
        #endif
        
        safe_close(client_sock);
        // ...
    }
}
```

This layered approach creates a robust system that can handle external events while maintaining continuous data acquisition and transmission.

#### Non-Blocking Send Implementation

A particularly sophisticated feature is the `non_blocking_send()` function, which implements a stateful non-blocking transmission mechanism:

```c
esp_err_t non_blocking_send(int client_sock, void *buffer, size_t len, int flags)
{
    static int socket_at_start = -1;  // Track socket changes during sending

    // Initialize or reset sending state
    if (!send_in_progress) {
        pending_send_buffer = buffer;
        pending_send_size = len;
        pending_send_offset = 0;
        send_in_progress = true;
        socket_at_start = new_sock;  // Store current socket value
        
        // Make socket non-blocking for this operation
        int sock_flags = fcntl(client_sock, F_GETFL, 0);
        fcntl(client_sock, F_SETFL, sock_flags | O_NONBLOCK);
    }
    
    // Send remaining data in chunks as socket buffer permits
    while (pending_send_offset < pending_send_size) {
        // Check for interruption conditions
        #ifndef USE_EXTERNAL_ADC
        if (atomic_load(&wifi_operation_requested)) {
            // Reset socket to blocking mode
            int sock_flags = fcntl(client_sock, F_GETFL, 0);
            fcntl(client_sock, F_SETFL, sock_flags & ~O_NONBLOCK);
            send_in_progress = false;
            return ESP_ERR_TIMEOUT;  // Signal need to handle WiFi operation
        }
        #else
        // In external ADC mode, check if socket has changed
        if (new_sock != socket_at_start) {
            // Reset and abort
            // ...
        }
        #endif
        
        // Try to send a chunk
        ssize_t sent = send(client_sock, pending_send_buffer + pending_send_offset,
                           pending_send_size - pending_send_offset, flags);
                           
        if (sent > 0) {
            pending_send_offset += sent;
        } else if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Socket buffer is full, wait a bit
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            
            // Actual error
            ESP_LOGE(TAG, "Send error: errno %d", errno);
            // ...
            return ESP_FAIL;
        }
    }
    
    // Reset socket to blocking mode
    int sock_flags = fcntl(client_sock, F_GETFL, 0);
    fcntl(client_sock, F_SETFL, sock_flags & ~O_NONBLOCK);
    send_in_progress = false;
    return ESP_OK;
}
```

This implementation:
1. Makes the socket non-blocking only during transmission
2. Maintains state across multiple calls for partial sends
3. Handles interruptions from WiFi operations or socket changes
4. Efficiently uses socket buffer capacity without blocking acquisition

### Conditional Compilation

The module uses conditional compilation extensively to support both internal ADC and external SPI-based ADC configurations:

```c
#ifdef USE_EXTERNAL_ADC
// External ADC-specific implementation
// ...
#else
// Internal ADC-specific implementation
// ...
#endif
```

This approach ensures:
1. Efficient code size by including only relevant implementation
2. Optimized performance for each hardware configuration
3. Clear separation of hardware-specific logic

## Advanced Features

### Trigger Detection

The module implements edge-based triggering with configurable edge direction:

```c
bool is_triggered(int current, int last)
{
    // Check for the specific edge type set in trigger_edge
    if (trigger_edge == 1) {
        // Positive edge detection
        return (current > last);
    } else {
        // Negative edge detection
        return (current < last);
    }
}
```

Different hardware implementations are used for edge detection:
- External ADC: Uses the ESP32's pulse counter (PCNT) peripheral
- Internal ADC: Uses direct GPIO sampling and comparison

### WiFi Operation Coordination

For the internal ADC configuration, the module implements a sophisticated coordination mechanism to temporarily pause data acquisition during WiFi operations:

```c
#ifndef USE_EXTERNAL_ADC
// Only for internal ADC: WiFi operations check
if (atomic_load(&wifi_operation_requested)) {
    ESP_LOGI(TAG, "WiFi operation requested, pausing ADC operations");

    if (atomic_load(&adc_is_running)) {
        stop_adc_sampling();
    }

    atomic_store(&wifi_operation_acknowledged, 1);

    while (atomic_load(&wifi_operation_requested)) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    atomic_store(&wifi_operation_acknowledged, 0);

    ESP_LOGI(TAG, "Resuming ADC operations after WiFi change");
}
#endif
```

This mechanism:
1. Detects requests from the web server to pause for WiFi reconfiguration
2. Safely stops ADC operations
3. Signals acknowledgment to the requesting task
4. Waits for the operation to complete
5. Resumes normal operation

### Socket Change Detection

The module implements robust socket change detection that gracefully handles transitions when the web server reconfigures the communication socket:

```c
// Detect if the socket has changed
if (new_sock != current_sock) {
    ESP_LOGI(TAG, "Detected socket change: previous=%d, new=%d", current_sock, new_sock);
    current_sock = new_sock;
    // If we were connected with a client, close that connection
    if (client_sock >= 0) {
        safe_close(client_sock);
        client_sock = -1;
        ESP_LOGI(TAG, "Closed previous client connection due to socket change");
    }
}
```

This allows the oscilloscope to switch between WiFi interfaces (AP and STA) or handle IP address changes without disrupting the overall system.

### Error Detection and Recovery

The module implements robust error detection with a miss counter system:

```c
read_miss_count++;
ESP_LOGW(TAG, "Missed ADC readings! Count: %d", read_miss_count);
if (read_miss_count >= 10) {
    ESP_LOGE(TAG, "Critical ADC or SPI data loss detected.");
    read_miss_count = 0;
}
```

This tracks consecutive acquisition failures and provides appropriate logging for both intermittent issues and critical failures.

## Performance Optimizations

### Socket Buffer Management

The implementation uses the `MSG_MORE` flag in send operations to optimize TCP packet usage:

```c
int flags = MSG_MORE;
```

This flag instructs the TCP stack to delay sending until more data is available or the buffer is full, reducing overhead for small packets and improving throughput.

### Non-Blocking Socket Operations

The module dynamically switches between blocking and non-blocking socket modes as needed:

```c
// Make socket non-blocking
int sock_flags = fcntl(client_sock, F_GETFL, 0);
fcntl(client_sock, F_SETFL, sock_flags | O_NONBLOCK);

// ... operations ...

// Reset to blocking mode
sock_flags = fcntl(client_sock, F_GETFL, 0);
fcntl(client_sock, F_SETFL, sock_flags & ~O_NONBLOCK);
```

This approach:
1. Uses non-blocking mode during transmission to avoid stalling acquisition
2. Uses blocking mode during connection acceptance for simpler code
3. Properly restores socket state after operations

### Buffer Management

The implementation optimizes memory usage by directly transmitting from acquisition buffers after applying appropriate offsets:

```c
void *send_buffer = buffer + (get_discard_head() * sample_size);
size_t send_len = get_samples_per_packet() * sample_size;
```

This approach avoids unnecessary memory copies, preserving CPU time and reducing memory fragmentation.

## Integration with Other Modules

The data transmission module interacts with several other oscilloscope subsystems:

### Acquisition Module

- Calls `start_adc_sampling()` and `stop_adc_sampling()` to control the ADC
- Uses `acquire_data()` to obtain samples from either ADC implementation
- Configures sampling rates via `config_adc_sampling()`

### Network Module

- Uses `safe_close()` to properly terminate socket connections
- Monitors `new_sock` global variable for socket configuration changes
- Implements proper socket lifecycle management in coordination with the server

### Webserver Module

- Supports mode changes requested through HTTP endpoints
- Implements coordination for WiFi operations through atomic flags
- Handles client connections initiated through the web interface

## Technical Insights

### Thread Safety Approach

The module implements thread safety through several complementary approaches:

1. **Atomic Variables** for shared state:
   ```c
   atomic_int mode = ATOMIC_VAR_INIT(0);
   ```

2. **Semaphores** for hardware resource contention:
   ```c
   if (xSemaphoreTake(spi_mutex, portMAX_DELAY) == pdTRUE) {
       // Protected SPI access
       xSemaphoreGive(spi_mutex);
   }
   ```

3. **Task Coordination** via atomic flags and acknowledgment patterns:
   ```c
   atomic_store(&wifi_operation_acknowledged, 1);
   while (atomic_load(&wifi_operation_requested)) {
       vTaskDelay(pdMS_TO_TICKS(10));
   }
   ```

This multi-layered approach ensures safety even with complex interactions between tasks.

### TCP Socket Reuse

The implementation carefully manages socket transitions:

```c
if (new_sock != current_sock) {
    ESP_LOGI(TAG, "Detected socket change: previous=%d, new=%d", current_sock, new_sock);
    current_sock = new_sock;
    // Close existing client connection
    if (client_sock >= 0) {
        safe_close(client_sock);
        client_sock = -1;
    }
}
```

This ensures that:
1. Socket changes are detected promptly
2. Existing connections are properly closed
3. Resources are not leaked during transitions

### Adaptive Timing

For triggered acquisition, the implementation includes adaptive timing to ensure accurate capture:

```c
TickType_t xLastWakeTime = xTaskGetTickCount();
// ... trigger detection logic ...
TickType_t xCurrentTime = xTaskGetTickCount();
vTaskDelay(pdMS_TO_TICKS(wait_convertion_time / 2) - (xCurrentTime - xLastWakeTime));
```

This technique compensates for the processing time of trigger detection to ensure consistent sample timing.

---

The data transmission module forms a critical bridge between the oscilloscope's acquisition hardware and client visualization software. Its sophisticated implementation balances performance, flexibility, and robustness while supporting multiple hardware configurations and operating modes. The careful attention to thread safety, error handling, and resource management demonstrates professional embedded systems design practices essential for reliable instrumentation applications.
