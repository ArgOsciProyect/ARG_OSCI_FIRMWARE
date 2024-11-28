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
 * @brief HTTP handler to connect to a Wi-Fi network.
 * 
 * @param req HTTP request.
 * @return esp_err_t ESP_OK on success, ESP_FAIL on error.
 */
esp_err_t connect_wifi_handler(httpd_req_t *req);

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