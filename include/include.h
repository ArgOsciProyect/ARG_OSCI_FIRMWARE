#ifndef INCLUDE_H
#define INCLUDE_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <lwip/sockets.h>
#include <esp_http_server.h>
#include <cJSON.h>
#include <esp_netif.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define WIFI_SSID "ESP32_AP"
#define WIFI_PASSWORD "password123"
#define MAX_STA_CONN 4
#define PORT 8080

/**
 * @brief Initialize Wi-Fi in APSTA mode.
 */
void wifi_init(void);

/**
 * @brief Task to handle socket communication.
 * 
 * @param pvParameters Parameters for the task.
 */
void socket_task(void *pvParameters);

/**
 * @brief HTTP handler to scan for available Wi-Fi networks.
 * 
 * @param req HTTP request.
 * @return esp_err_t ESP_OK on success, ESP_FAIL on error.
 */
esp_err_t scan_wifi_handler(httpd_req_t *req);

/**
 * @brief Add unique SSID to the JSON array.
 * 
 * @param root JSON array.
 * @param ap_record Access point record.
 */
void add_unique_ssid(cJSON *root, wifi_ap_record_t *ap_record);

/**
 * @brief Scan and get access point records.
 * 
 * @param num_networks Pointer to store the number of networks found.
 * @return cJSON* JSON array of access point records.
 */
cJSON* scan_and_get_ap_records(uint16_t *num_networks);

/**
 * @brief HTTP handler for test endpoint.
 * 
 * @param req HTTP request.
 * @return esp_err_t ESP_OK on success, ESP_FAIL on error.
 */
esp_err_t test_handler(httpd_req_t *req);

/**
 * @brief Start the second web server.
 * 
 * @return httpd_handle_t Handle to the second web server.
 */
httpd_handle_t start_second_webserver(void);

/**
 * @brief Parse Wi-Fi credentials from HTTP request.
 * 
 * @param req HTTP request.
 * @param wifi_config Wi-Fi configuration to be filled.
 * @return esp_err_t ESP_OK on success, ESP_FAIL on error.
 */
esp_err_t parse_wifi_credentials(httpd_req_t *req, wifi_config_t *wifi_config);

/**
 * @brief Wait for IP address to be assigned.
 * 
 * @param ip_info IP information to be filled.
 * @return esp_err_t ESP_OK on success, ESP_FAIL on error.
 */
esp_err_t wait_for_ip(esp_netif_ip_info_t *ip_info);

/**
 * @brief Create and bind a socket.
 * 
 * @param ip_info IP information for binding.
 * @return esp_err_t ESP_OK on success, ESP_FAIL on error.
 */
esp_err_t create_socket_and_bind(esp_netif_ip_info_t *ip_info);

/**
 * @brief HTTP handler to connect to a Wi-Fi network.
 * 
 * @param req HTTP request.
 * @return esp_err_t ESP_OK on success, ESP_FAIL on error.
 */
esp_err_t connect_wifi_handler(httpd_req_t *req);

/**
 * @brief Get IP information of the access point.
 * 
 * @param ip_info IP information to be filled.
 * @return esp_err_t ESP_OK on success, ESP_FAIL on error.
 */
esp_err_t get_ap_ip_info(esp_netif_ip_info_t *ip_info);

/**
 * @brief Create and bind a socket for internal mode.
 * 
 * @param ip_info IP information for binding.
 * @return esp_err_t ESP_OK on success, ESP_FAIL on error.
 */
esp_err_t create_and_bind_socket(esp_netif_ip_info_t *ip_info);

/**
 * @brief Get socket IP and port for internal mode.
 * 
 * @param ip_str Buffer to store IP string.
 * @param new_port Pointer to store the port number.
 * @return esp_err_t ESP_OK on success, ESP_FAIL on error.
 */
esp_err_t get_socket_ip_and_port(char *ip_str, int *new_port);

/**
 * @brief Send internal mode response.
 * 
 * @param req HTTP request.
 * @param ip_str IP string.
 * @param new_port Port number.
 * @return esp_err_t ESP_OK on success, ESP_FAIL on error.
 */
esp_err_t send_internal_mode_response(httpd_req_t *req, const char *ip_str, int new_port);

/**
 * @brief HTTP handler to switch to internal mode.
 * 
 * @param req HTTP request.
 * @return esp_err_t ESP_OK on success, ESP_FAIL on error.
 */
esp_err_t internal_mode_handler(httpd_req_t *req);

/**
 * @brief Start the main web server.
 * 
 * @return httpd_handle_t Handle to the main web server.
 */
httpd_handle_t start_webserver(void);

/**
 * @brief Main application entry point.
 */
void app_main(void);

#endif // INCLUDE_H