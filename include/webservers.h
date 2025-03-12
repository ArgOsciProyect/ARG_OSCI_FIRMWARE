/**
 * @file webservers.h
 * @brief HTTP server and handlers for ESP32 oscilloscope
 *
 * Implements the web servers (primary and secondary) that handle user interface
 * interactions, configuration, and control of the oscilloscope functionality.
 */

#ifndef WEBSERVERS_H // Change from WEBSERVER_H to WEBSERVERS_H for consistency
#define WEBSERVERS_H

#include <cJSON.h>
#include <errno.h>
#include <esp_err.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <lwip/sockets.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Start the primary HTTP server on port 81
 *
 * Initializes and starts the main HTTP server that serves the
 * configuration interface when connecting to the ESP32 access point.
 *
 * @return Server handle on success, NULL on failure
 */
httpd_handle_t start_webserver(void);

/**
 * @brief Start the secondary HTTP server on port 80
 *
 * Initializes and starts the secondary server that becomes available
 * when the ESP32 connects to an external WiFi network in station mode.
 *
 * @return Server handle on success, NULL on failure
 */
httpd_handle_t start_second_webserver(void);

/**
 * @brief Handler for device configuration requests
 *
 * Returns JSON with device configuration parameters like sampling rate,
 * bit depth, and buffer sizes.
 *
 * @param req HTTP request structure
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t config_handler(httpd_req_t *req);

/**
 * @brief Handler for WiFi network scanning
 *
 * Scans for available WiFi networks and returns them as JSON.
 *
 * @param req HTTP request structure
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t scan_wifi_handler(httpd_req_t *req);

/**
 * @brief Handler for testing encrypted communication
 *
 * Receives an encrypted message, decrypts it using the device's private key,
 * and returns the decrypted content as JSON. This endpoint is used to verify
 * the secure communication channel is working properly.
 *
 * @param req HTTP request structure
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t test_handler(httpd_req_t *req);

/**
 * @brief Handler for controlling trigger level and edge
 *
 * Sets trigger parameters (edge type and voltage level).
 *
 * @param req HTTP request structure
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t trigger_handler(httpd_req_t *req);

/**
 * @brief Handler to switch to single-shot acquisition mode
 *
 * Changes the oscilloscope to single trigger capture mode.
 *
 * @param req HTTP request structure
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t single_handler(httpd_req_t *req);

/**
 * @brief Handler to change sampling frequency
 *
 * Adjusts the ADC or SPI sampling frequency.
 *
 * @param req HTTP request structure
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t freq_handler(httpd_req_t *req);

/**
 * @brief Handler to reset data socket
 *
 * Creates a new socket for streaming data to clients.
 *
 * @param req HTTP request structure
 * @return ESP_OK on success, error code otherwise
 * @note Returns JSON with format: {"IP": "xxx.xxx.xxx.xxx", "Port": xxxx}
 */
esp_err_t reset_socket_handler(httpd_req_t *req);

/**
 * @brief Handler to switch to continuous acquisition mode
 *
 * Changes the oscilloscope to normal continuous capture mode.
 *
 * @param req HTTP request structure
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t normal_handler(httpd_req_t *req);

/**
 * @brief Handler for connecting to external WiFi networks
 *
 * Processes credentials and connects to specified WiFi network.
 *
 * @param req HTTP request structure
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t connect_wifi_handler(httpd_req_t *req);

/**
 * @brief Handler to provide RSA public key for secure communication
 *
 * Returns the device's RSA public key in PEM format for encrypting messages
 * to the device. Includes CORS headers to allow cross-origin requests.
 *
 * @param req HTTP request structure
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t get_public_key_handler(httpd_req_t *req);

/**
 * @brief Handler to enable internal mode
 *
 * Sets up data streaming in access point mode.
 *
 * @param req HTTP request structure
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t internal_mode_handler(httpd_req_t *req);

/**
 * @brief Handler to test connection is alive
 *
 * Simple endpoint that returns "1" to verify connection.
 *
 * @param req HTTP request structure
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t test_connect_handler(httpd_req_t *req);

/**
 * @brief Parse encrypted WiFi credentials from request
 *
 * Extracts and decrypts SSID and password from request body.
 *
 * @param req HTTP request structure
 * @param wifi_config Config structure to populate
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t parse_wifi_credentials(httpd_req_t *req, wifi_config_t *wifi_config);

/**
 * @brief Format and send response with socket information
 *
 * Creates JSON response with IP address and port for data socket.
 *
 * @param req HTTP request structure
 * @param ip_str IP address as string
 * @param new_port Port number
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t send_internal_mode_response(httpd_req_t *req, const char *ip_str, int new_port);

/**
 * @brief Send WiFi connection result
 *
 * Formats and sends JSON response with connection status.
 *
 * @param req HTTP request structure
 * @param ip IP address as string
 * @param port Port number
 * @param success Whether connection was successful
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t send_wifi_response(httpd_req_t *req, const char *ip, int port, bool success);

#endif /* WEBSERVERS_H */